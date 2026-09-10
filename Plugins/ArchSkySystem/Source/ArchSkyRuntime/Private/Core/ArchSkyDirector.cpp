// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/ArchSkyDirector.h"

#include "Components/ArrowComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Core/ArchSkySubsystem.h"
#include "Data/ArchTimeCalendar.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "HAL/IConsoleManager.h"
#include "Misc/EngineVersionComparison.h"
#include "Net/UnrealNetwork.h"
#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "ArchSkyDirector"

/**
 * Scalability override for the sky-light recapture threshold.
 *
 * Declared here rather than in ArchSkyConsole.cpp because that file is compiled out of
 * Shipping and this is a performance knob a shipped VR build genuinely needs. Negative
 * means "use the Project Settings value", so the CVar is inert until someone sets it.
 */
static TAutoConsoleVariable<float> CVarArchSkyRecaptureThreshold(
	TEXT("ArchSky.Perf.SkyRecaptureThreshold"),
	-1.f,
	TEXT("Overrides the sun-altitude change, in degrees, that forces a sky-light recapture.\n")
	TEXT("Negative (default) leaves the Project Settings value alone. Larger is cheaper.\n")
	TEXT("See PERFORMANCE.md."),
	ECVF_Scalability);

namespace ArchSkyDirectorConstants
{
	/**
	 * AActor::SetNetUpdateFrequency() replaced direct assignment to the (now private)
	 * NetUpdateFrequency member partway through the 5.x line. Selecting on the engine
	 * version keeps the plugin warning-free on 5.4 and on every later release.
	 */
	template <typename ActorType>
	void SetNetUpdateFrequencyCompat(ActorType& Actor, float Hz)
	{
#if UE_VERSION_NEWER_THAN(5, 5, 0)
		Actor.SetNetUpdateFrequency(Hz);
#else
		Actor.NetUpdateFrequency = Hz;
#endif
	}

	/**
	 * Parameter names written to MPC_ArchSky. These MUST match MATERIALS.md exactly; the
	 * document is the contract between this file and every material an artist writes.
	 */
	namespace Mpc
	{
		static const FName SunAltitude01(TEXT("SunAltitude01"));
		static const FName SunAltitudeDegrees(TEXT("SunAltitudeDegrees"));
		static const FName MoonIllumination(TEXT("MoonIllumination"));
		static const FName MoonAltitude01(TEXT("MoonAltitude01"));
		static const FName Wetness(TEXT("Wetness"));
		static const FName SnowCoverage(TEXT("SnowCoverage"));
		static const FName RainIntensity(TEXT("RainIntensity"));
		static const FName DustIntensity(TEXT("DustIntensity"));
		static const FName SeasonBlend(TEXT("SeasonBlend"));
		static const FName TimeOfDay01(TEXT("TimeOfDay01"));
		static const FName FogDensity(TEXT("FogDensity"));
		static const FName CloudCoverage(TEXT("CloudCoverage"));
		static const FName WindStrength(TEXT("WindStrength"));
		static const FName WindTurbulence(TEXT("WindTurbulence"));
		static const FName Turbidity(TEXT("Turbidity"));

		static const FName SunDirection(TEXT("SunDirection"));
		static const FName MoonDirection(TEXT("MoonDirection"));
		static const FName SunLightColor(TEXT("SunLightColor"));
		static const FName WindVector(TEXT("WindVector"));
		static const FName FogInscatteringColor(TEXT("FogInscatteringColor"));
	}

	/** Unreal world units per metre. */
	static constexpr float CentimetresPerMetre = 100.f;

	/** Unreal world units per kilometre. */
	static constexpr float CentimetresPerKilometre = 100000.f;
}

// ---------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------

AArchSkyDirector::AArchSkyDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// Tick after the subsystem's FTickableGameObject so we act on this frame's state, not
	// last frame's. TG_PostPhysics also keeps us behind any Blueprint that drives the sky.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	// ARCH NOTE: replication is decided here, at CDO construction, rather than later.
	// AActor::SetReplicates() is only legal on an authority, and a level-placed actor on a
	// client must already have bReplicates set to accept property updates - so deferring it
	// to BeginPlay would silently break client reception. Reading the settings CDO forces
	// its config load, which is safe at this point because ArchSkyRuntime loads PreDefault.
	{
		const UArchSkySettings* Settings = UArchSkySettings::Get();
		const bool bReplicationEnabled = Settings && Settings->bEnableReplication;

		bReplicates = bReplicationEnabled;
		bAlwaysRelevant = bReplicationEnabled;   // One actor; every client needs it.

		const float UpdateHz = Settings ? FMath::Clamp(Settings->ReplicationUpdateFrequencyHz, 0.5f, 30.f) : 2.f;
		ArchSkyDirectorConstants::SetNetUpdateFrequencyCompat(*this, UpdateHz);
	}

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SunLightComponent = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunLight"));
	SunLightComponent->SetupAttachment(SceneRoot);
	SunLightComponent->SetMobility(EComponentMobility::Movable);
	SunLightComponent->bAtmosphereSunLight = true;
	SunLightComponent->AtmosphereSunLightIndex = 0;
	SunLightComponent->LightSourceAngle = 0.545f;
	SunLightComponent->Intensity = 100000.f;
	SunLightComponent->bUseTemperature = false;   // We compute the colour ourselves.
	SunLightComponent->CastShadows = true;
	SunLightComponent->bCastVolumetricShadow = true;
	SunLightComponent->DynamicShadowDistanceMovableLight = 20000.f;

	MoonLightComponent = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("MoonLight"));
	MoonLightComponent->SetupAttachment(SceneRoot);
	MoonLightComponent->SetMobility(EComponentMobility::Movable);
	MoonLightComponent->bAtmosphereSunLight = true;
	MoonLightComponent->AtmosphereSunLightIndex = 1;
	MoonLightComponent->LightSourceAngle = 0.545f;
	MoonLightComponent->Intensity = 0.4f;
	MoonLightComponent->CastShadows = true;
	MoonLightComponent->bCastVolumetricShadow = false;   // Moonlight shafts are not worth the cost.

	SkyLightComponent = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLightComponent->SetupAttachment(SceneRoot);
	SkyLightComponent->SetMobility(EComponentMobility::Movable);
	SkyLightComponent->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	SkyLightComponent->bRealTimeCapture = true;
	SkyLightComponent->bLowerHemisphereIsBlack = false;
	SkyLightComponent->Intensity = 1.f;

	SkyAtmosphereComponent = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	SkyAtmosphereComponent->SetupAttachment(SceneRoot);
	SkyAtmosphereComponent->TransformMode = ESkyAtmosphereTransformMode::PlanetTopAtAbsoluteWorldOrigin;

	VolumetricCloudComponent = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("VolumetricCloud"));
	VolumetricCloudComponent->SetupAttachment(SceneRoot);

	HeightFogComponent = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	HeightFogComponent->SetupAttachment(SceneRoot);
	HeightFogComponent->bEnableVolumetricFog = true;

	SkySphereComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkySphere"));
	SkySphereComponent->SetupAttachment(SceneRoot);
	SkySphereComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkySphereComponent->SetCastShadow(false);
	SkySphereComponent->bVisibleInRayTracing = false;
	SkySphereComponent->SetVisibility(false);   // Path B only; enabled when configured.

#if WITH_EDITORONLY_DATA
	NorthIndicatorComponent = CreateDefaultSubobject<UArrowComponent>(TEXT("TrueNorthIndicator"));
	if (NorthIndicatorComponent)
	{
		NorthIndicatorComponent->SetupAttachment(SceneRoot);
		NorthIndicatorComponent->ArrowColor = FColor(255, 64, 64);   // Red = true north.
		NorthIndicatorComponent->ArrowSize = 4.f;
		NorthIndicatorComponent->bIsScreenSizeScaled = true;
		NorthIndicatorComponent->SetHiddenInGame(true);
	}

	PlanNorthIndicatorComponent = CreateDefaultSubobject<UArrowComponent>(TEXT("PlanNorthIndicator"));
	if (PlanNorthIndicatorComponent)
	{
		PlanNorthIndicatorComponent->SetupAttachment(SceneRoot);
		PlanNorthIndicatorComponent->ArrowColor = FColor(64, 160, 255);  // Blue = plan north.
		PlanNorthIndicatorComponent->ArrowSize = 3.f;
		PlanNorthIndicatorComponent->bIsScreenSizeScaled = true;
		PlanNorthIndicatorComponent->SetHiddenInGame(true);
	}
#endif

	InitialiseDefaultCurves();
}

void AArchSkyDirector::InitialiseDefaultCurves()
{
	// ARCH NOTE: curves are populated in code rather than shipped as .uasset curve assets.
	// A code-defined default means the plugin behaves correctly the instant it is enabled,
	// with no content dependency; a project that wants art-directed falloff assigns a
	// UCurveFloat in Project Settings, which overrides these at BeginPlay.

	// --- Sun illuminance, in lux, against altitude in degrees ---
	// 120000 lux is the standard clear-sky figure for an overhead sun. The knee between
	// -6 and +10 degrees is what makes sunrise read as a sunrise rather than a dimmer.
	if (FRichCurve* Curve = SunIntensityCurve.GetRichCurve())
	{
		Curve->Reset();
		Curve->AddKey(-18.f, 0.f);
		Curve->AddKey(-6.f, 0.f);
		Curve->AddKey(-2.f, 80.f);
		Curve->AddKey(0.f, 600.f);
		Curve->AddKey(2.f, 5000.f);
		Curve->AddKey(5.f, 18000.f);
		Curve->AddKey(10.f, 40000.f);
		Curve->AddKey(20.f, 68000.f);
		Curve->AddKey(40.f, 95000.f);
		Curve->AddKey(60.f, 110000.f);
		Curve->AddKey(90.f, 120000.f);

		for (auto KeyIt = Curve->GetKeyHandleIterator(); KeyIt; ++KeyIt)
		{
			Curve->SetKeyInterpMode(*KeyIt, RCIM_Cubic);
			Curve->SetKeyTangentMode(*KeyIt, RCTM_Auto);
		}
	}

	// --- Correlated colour temperature, in Kelvin, against altitude in degrees ---
	// 1800 K at the horizon is a deep orange sunrise; 6500 K is the D65 white point.
	if (FRichCurve* Curve = SunColorTemperatureCurve.GetRichCurve())
	{
		Curve->Reset();
		Curve->AddKey(-6.f, 1600.f);
		Curve->AddKey(0.f, 1800.f);
		Curve->AddKey(2.f, 2400.f);
		Curve->AddKey(5.f, 3200.f);
		Curve->AddKey(10.f, 4300.f);
		Curve->AddKey(20.f, 5200.f);
		Curve->AddKey(40.f, 6000.f);
		Curve->AddKey(90.f, 6500.f);

		for (auto KeyIt = Curve->GetKeyHandleIterator(); KeyIt; ++KeyIt)
		{
			Curve->SetKeyInterpMode(*KeyIt, RCIM_Cubic);
			Curve->SetKeyTangentMode(*KeyIt, RCTM_Auto);
		}
	}

	// --- Sky-light scale against altitude in degrees ---
	// Never reaches zero: even at -18 degrees there is airglow and starlight, and a scene
	// with a literally black ambient term looks broken rather than dark.
	if (FRichCurve* Curve = SkyLightIntensityCurve.GetRichCurve())
	{
		Curve->Reset();
		Curve->AddKey(-18.f, 0.02f);
		Curve->AddKey(-12.f, 0.05f);
		Curve->AddKey(-6.f, 0.18f);
		Curve->AddKey(-2.f, 0.42f);
		Curve->AddKey(0.f, 0.55f);
		Curve->AddKey(5.f, 0.78f);
		Curve->AddKey(15.f, 0.92f);
		Curve->AddKey(45.f, 1.f);
		Curve->AddKey(90.f, 1.f);

		for (auto KeyIt = Curve->GetKeyHandleIterator(); KeyIt; ++KeyIt)
		{
			Curve->SetKeyInterpMode(*KeyIt, RCIM_Cubic);
			Curve->SetKeyTangentMode(*KeyIt, RCTM_Auto);
		}
	}
}

void AArchSkyDirector::ApplySettingsDefaults()
{
	const UArchSkySettings* Settings = UArchSkySettings::Get();
	if (!Settings)
	{
		return;
	}

	ResolvedCloudMode = Settings->CloudMode;

	SkyRecaptureAltitudeThreshold = Settings->SkyRecaptureAltitudeThreshold;
	SkyRecaptureWeatherThreshold = Settings->SkyRecaptureWeatherThreshold;
	MinFramesBetweenSkyRecaptures = Settings->MinFramesBetweenSkyRecaptures;
	CloudUpdateInterval = Settings->CloudUpdateInterval;
	bWriteMaterialParameterCollection = Settings->bWriteMaterialParameterCollection;
	MoonPeakIlluminanceLux = Settings->MoonPeakIlluminanceLux;
	bEnableMoonLight = bEnableMoonLight && Settings->bEnableMoonLight;

	if (SunLightComponent)
	{
		SunLightComponent->LightSourceAngle = Settings->SunLightSourceAngleDegrees;
	}
	if (MoonLightComponent)
	{
		MoonLightComponent->LightSourceAngle = Settings->MoonLightSourceAngleDegrees;
	}

	// Optional curve assets replace the code-defined defaults wholesale.
	if (!Settings->DefaultSunIntensityCurve.IsNull())
	{
		SunIntensityCurve.ExternalCurve = Settings->DefaultSunIntensityCurve.LoadSynchronous();
	}
	if (!Settings->DefaultSunColorTemperatureCurve.IsNull())
	{
		SunColorTemperatureCurve.ExternalCurve = Settings->DefaultSunColorTemperatureCurve.LoadSynchronous();
	}

	ArchSkyDirectorConstants::SetNetUpdateFrequencyCompat(*this, FMath::Clamp(Settings->ReplicationUpdateFrequencyHz, 0.5f, 30.f));
}

void AArchSkyDirector::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Replication is configured in the constructor (see the note there). Nothing to do
	// here beyond the base implementation; the override is kept because component
	// adoption in a Blueprint subclass is expected to hook it.
}

void AArchSkyDirector::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

#if WITH_EDITORONLY_DATA
	// Keep the two compass arrows pointing where they claim to. The plan-north arrow is
	// simply scene +X; the true-north arrow is +X rotated by the state's north offset.
	if (PlanNorthIndicatorComponent)
	{
		PlanNorthIndicatorComponent->SetRelativeRotation(FRotator::ZeroRotator);
	}

	if (NorthIndicatorComponent)
	{
		float NorthOffset = 0.f;
		if (const UArchSkySubsystem* Subsystem = GetSkySubsystem())
		{
			NorthOffset = Subsystem->GetSkyState().NorthOffsetDegrees;
		}
		NorthIndicatorComponent->SetRelativeRotation(FRotator(0.f, NorthOffset, 0.f));
	}
#endif
}

// ---------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------

void AArchSkyDirector::BeginPlay()
{
	Super::BeginPlay();

	ValidateSingleInstance();
	ApplySettingsDefaults();

	if (bUseExistingSceneActors)
	{
		DiscoverExistingSceneActors();
	}

	// Path B does not need a volumetric cloud component at all, and leaving one enabled
	// costs a full-screen raymarch for nothing.
	if (VolumetricCloudComponent)
	{
		const bool bWantVolumetric = (ResolvedCloudMode == EArchCloudMode::Volumetric) && !AdoptedVolumetricCloud.IsValid();
		VolumetricCloudComponent->SetVisibility(bWantVolumetric);
		VolumetricCloudComponent->SetComponentTickEnabled(bWantVolumetric);
	}

	if (SkySphereComponent)
	{
		const bool bWantSkySphere = (ResolvedCloudMode == EArchCloudMode::SkySphere) && SkySphereComponent->GetStaticMesh() != nullptr;
		SkySphereComponent->SetVisibility(bWantSkySphere);

		if (ResolvedCloudMode == EArchCloudMode::SkySphere && !SkySphereComponent->GetStaticMesh())
		{
			UE_LOG(LogArchSky, Warning,
				TEXT("Cloud mode is SkySphere (Path B) but no mesh is assigned to SkySphereComponent. ")
				TEXT("Clouds will not render. See MATERIALS.md, 'Path B'."));
		}
	}

	// Real-time capture is not available on every feature level. When it is off we fall
	// back to explicit, frame-throttled RecaptureSky calls instead.
	if (USkyLightComponent* SkyLight = AdoptedSkyLight.IsValid() ? AdoptedSkyLight.Get() : SkyLightComponent.Get())
	{
		bUsingRealTimeSkyCapture = SkyLight->bRealTimeCapture;
	}

	BindToSubsystem();
	MarkAllDirty();
}

void AArchSkyDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindFromSubsystem();
	Super::EndPlay(EndPlayReason);
}

void AArchSkyDirector::BindToSubsystem()
{
	if (bBoundToSubsystem)
	{
		return;
	}

	UArchSkySubsystem* Subsystem = UArchSkySubsystem::Get(this);
	if (!Subsystem)
	{
		ARCHSKY_LOG_ONCE(Warning,
			TEXT("AArchSkyDirector could not find an ArchSky subsystem for this world. The sky will not update."));
		return;
	}

	SkySubsystem = Subsystem;
	Subsystem->OnSkyStateChanged.AddDynamic(this, &AArchSkyDirector::HandleSkyStateChanged);
	Subsystem->NotifyDirectorRegistered(true);

	bBoundToSubsystem = true;
}

void AArchSkyDirector::UnbindFromSubsystem()
{
	if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
	{
		Subsystem->OnSkyStateChanged.RemoveDynamic(this, &AArchSkyDirector::HandleSkyStateChanged);
		Subsystem->NotifyDirectorRegistered(false);
	}

	SkySubsystem.Reset();
	bBoundToSubsystem = false;
}

void AArchSkyDirector::ValidateSingleInstance() const
{
	const UArchSkySettings* Settings = UArchSkySettings::Get();
	if (!Settings || !Settings->bWarnOnDuplicateDirectors)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// ARCH NOTE: TActorIterator here is acceptable because this runs exactly once, at
	// BeginPlay. The requirement is "no GetAllActorsOfClass at RUNTIME"; a one-shot
	// validation sweep on level start is not a hot path, and the alternative - a static
	// registry - introduces a lifetime problem across PIE sessions for no benefit.
	int32 DirectorCount = 0;
	for (TActorIterator<AArchSkyDirector> It(const_cast<UWorld*>(World)); It; ++It)
	{
		++DirectorCount;
	}

	if (DirectorCount > 1)
	{
		UE_LOG(LogArchSky, Warning,
			TEXT("%d AArchSkyDirector actors found in '%s'. They will fight over the same lights. ")
			TEXT("Delete all but one."),
			DirectorCount, *World->GetName());
	}
}

void AArchSkyDirector::DiscoverExistingSceneActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// One sweep, at BeginPlay only. See the note in ValidateSingleInstance.
	int32 DirectionalLightCount = 0;
	TArray<UDirectionalLightComponent*> FoundDirectionalLights;

	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		ADirectionalLight* Light = *It;
		if (!IsValid(Light) || Light->IsOwnedBy(this))
		{
			continue;
		}

		if (UDirectionalLightComponent* Component = Cast<UDirectionalLightComponent>(Light->GetLightComponent()))
		{
			FoundDirectionalLights.Add(Component);
			++DirectionalLightCount;
		}
	}

	// Assign by AtmosphereSunLightIndex where the level author has set it, otherwise by
	// order found. Index 0 is the sun, index 1 the moon - that is an engine convention, not
	// ours, and a level that gets it wrong renders a black sky.
	for (UDirectionalLightComponent* Component : FoundDirectionalLights)
	{
		if (Component->AtmosphereSunLightIndex == 1 && !AdoptedMoonLight.IsValid())
		{
			AdoptedMoonLight = Component;
		}
		else if (!AdoptedSunLight.IsValid())
		{
			AdoptedSunLight = Component;
		}
		else if (!AdoptedMoonLight.IsValid())
		{
			AdoptedMoonLight = Component;
		}
	}

	if (DirectionalLightCount > 2)
	{
		UE_LOG(LogArchSky, Warning,
			TEXT("Adoption mode found %d directional lights; only two (sun and moon) will be driven. ")
			TEXT("Set AtmosphereSunLightIndex explicitly to choose which."),
			DirectionalLightCount);
	}

	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		if (IsValid(*It) && !It->IsOwnedBy(this))
		{
			if (AdoptedSkyLight.IsValid())
			{
				UE_LOG(LogArchSky, Warning, TEXT("Adoption mode found more than one SkyLight; using the first."));
				break;
			}
			AdoptedSkyLight = It->GetLightComponent();
		}
	}

	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		if (IsValid(*It) && !It->IsOwnedBy(this))
		{
			if (AdoptedHeightFog.IsValid())
			{
				UE_LOG(LogArchSky, Warning, TEXT("Adoption mode found more than one ExponentialHeightFog; using the first."));
				break;
			}
			AdoptedHeightFog = It->GetComponent();
		}
	}

	// SkyAtmosphere and VolumetricCloud have no dedicated AActor subclass exposed to
	// gameplay code in every engine version, so find them component-first.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor == this)
		{
			continue;
		}

		if (!AdoptedSkyAtmosphere.IsValid())
		{
			if (USkyAtmosphereComponent* Component = Actor->FindComponentByClass<USkyAtmosphereComponent>())
			{
				AdoptedSkyAtmosphere = Component;
			}
		}

		if (!AdoptedVolumetricCloud.IsValid())
		{
			if (UVolumetricCloudComponent* Component = Actor->FindComponentByClass<UVolumetricCloudComponent>())
			{
				AdoptedVolumetricCloud = Component;
			}
		}
	}

	// Hide our own components wherever we adopted a replacement, and complain once about
	// anything we expected but did not find.
	auto ReportAdoption = [](const TCHAR* Name, bool bFound)
	{
		if (bFound)
		{
			UE_LOG(LogArchSky, Log, TEXT("Adoption mode: driving the level's existing %s."), Name);
		}
		else
		{
			UE_LOG(LogArchSky, Warning,
				TEXT("Adoption mode: no existing %s found in this level. The Director's own component will be used instead."),
				Name);
		}
	};

	ReportAdoption(TEXT("directional light (sun)"), AdoptedSunLight.IsValid());
	ReportAdoption(TEXT("directional light (moon)"), AdoptedMoonLight.IsValid());
	ReportAdoption(TEXT("sky light"), AdoptedSkyLight.IsValid());
	ReportAdoption(TEXT("sky atmosphere"), AdoptedSkyAtmosphere.IsValid());
	ReportAdoption(TEXT("exponential height fog"), AdoptedHeightFog.IsValid());

	auto HideIfAdopted = [](USceneComponent* Own, bool bAdopted)
	{
		if (Own && bAdopted)
		{
			Own->SetVisibility(false);
			Own->SetComponentTickEnabled(false);
		}
	};

	HideIfAdopted(SunLightComponent, AdoptedSunLight.IsValid());
	HideIfAdopted(MoonLightComponent, AdoptedMoonLight.IsValid());
	HideIfAdopted(SkyLightComponent, AdoptedSkyLight.IsValid());
	HideIfAdopted(SkyAtmosphereComponent, AdoptedSkyAtmosphere.IsValid());
	HideIfAdopted(HeightFogComponent, AdoptedHeightFog.IsValid());
	HideIfAdopted(VolumetricCloudComponent, AdoptedVolumetricCloud.IsValid());

	// A movable light is non-negotiable: a static or stationary directional light cannot
	// be rotated at runtime, so a shadow study would silently show the wrong shadows.
	auto WarnIfNotMovable = [](const UDirectionalLightComponent* Component, const TCHAR* Name)
	{
		if (Component && Component->Mobility != EComponentMobility::Movable)
		{
			UE_LOG(LogArchSky, Error,
				TEXT("Adopted %s is not Movable. It cannot be rotated at runtime and the shadow study will be wrong. ")
				TEXT("Set its Mobility to Movable."),
				Name);
		}
	};

	WarnIfNotMovable(AdoptedSunLight.Get(), TEXT("sun light"));
	WarnIfNotMovable(AdoptedMoonLight.Get(), TEXT("moon light"));
}

// ---------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------

bool AArchSkyDirector::NeedsWeatherReapply(const FArchSkyState& State) const
{
	return !bHasAppliedWeather
		|| State.WeatherPresetA != LastAppliedWeatherA
		|| State.WeatherPresetB != LastAppliedWeatherB
		|| !FMath::IsNearlyEqual(State.WeatherBlendAlpha, LastAppliedWeatherAlpha, UE_KINDA_SMALL_NUMBER)
		|| State.Location != LastAppliedLocation;
}

void AArchSkyDirector::RecordAppliedWeather(const FArchSkyState& State)
{
	LastAppliedWeatherA = State.WeatherPresetA;
	LastAppliedWeatherB = State.WeatherPresetB;
	LastAppliedWeatherAlpha = State.WeatherBlendAlpha;
	LastAppliedLocation = State.Location;
	bHasAppliedWeather = true;
}

void AArchSkyDirector::HandleSkyStateChanged(const FArchSkyState& NewState)
{
	// ARCH NOTE: this handler does NOT apply anything. It only records what became stale.
	// The actual application happens once per tick, so a frame in which the state changes
	// five times (a slider drag plus a console command plus replication) still costs one
	// apply. This is the whole reason the Director ticks at all.
	DirtyFlags |= EArchSkyDirtyFlags::Lights;
	DirtyFlags |= EArchSkyDirtyFlags::MaterialParams;
	DirtyFlags |= EArchSkyDirtyFlags::Clouds;

	// Atmosphere and fog are expensive enough to be worth gating, but gating them on
	// "a transition is running" would miss an instant weather change and the final frame
	// of a timed one. Compare against what was actually last applied instead.
	if (NeedsWeatherReapply(NewState))
	{
		DirtyFlags |= EArchSkyDirtyFlags::Atmosphere;
		DirtyFlags |= EArchSkyDirtyFlags::Fog;
		DirtyFlags |= EArchSkyDirtyFlags::SkyLight;
	}
}

void AArchSkyDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	SCOPE_CYCLE_COUNTER(STAT_ArchSky_DirectorTick);

	UArchSkySubsystem* Subsystem = SkySubsystem.Get();
	if (!Subsystem)
	{
		// The subsystem may not have existed at BeginPlay in an unusual world setup.
		BindToSubsystem();
		Subsystem = SkySubsystem.Get();
		if (!Subsystem)
		{
			return;
		}
	}

	const bool bTimeFlowing = !Subsystem->IsTimePaused();
	const bool bTransitionActive = Subsystem->IsWeatherTransitionActive();

	// --- The mandatory early-out ---
	// Paused, no transition, nothing marked dirty: do nothing at all. On a static
	// architectural shot this brings the Director's cost to a branch per frame.
	if (!bTimeFlowing && !bTransitionActive && DirtyFlags == EArchSkyDirtyFlags::None)
	{
		return;
	}

	const FArchSkyState& State = Subsystem->GetSkyState();
	const FArchSolarPosition& Sun = Subsystem->GetSolarPosition();
	const FArchLunarPosition& Moon = Subsystem->GetLunarPosition();
	const FArchWeatherParams& Weather = Subsystem->GetCurrentWeatherBlended();

	// --- Lights: cheap, every frame while anything is moving ---
	if (EnumHasAnyFlags(DirtyFlags, EArchSkyDirtyFlags::Lights) || bTimeFlowing)
	{
		ApplyLights(State, Sun, Moon);
		EnumRemoveFlags(DirtyFlags, EArchSkyDirtyFlags::Lights);
	}

	// --- Atmosphere and fog: weather and location only, never time ---
	if (EnumHasAnyFlags(DirtyFlags, EArchSkyDirtyFlags::Atmosphere) || bTransitionActive)
	{
		ApplyAtmosphere(Weather);
		EnumRemoveFlags(DirtyFlags, EArchSkyDirtyFlags::Atmosphere);
	}

	if (EnumHasAnyFlags(DirtyFlags, EArchSkyDirtyFlags::Fog) || bTransitionActive)
	{
		ApplyFog(Weather);
		EnumRemoveFlags(DirtyFlags, EArchSkyDirtyFlags::Fog);
	}

	// Both weather-driven subsystems are now in sync with this state.
	RecordAppliedWeather(State);

	// --- Clouds: throttled to CloudUpdateInterval ---
	SecondsSinceCloudUpdate += DeltaSeconds;
	const bool bCloudIntervalElapsed = SecondsSinceCloudUpdate >= CloudUpdateInterval;
	if (bCloudIntervalElapsed && (EnumHasAnyFlags(DirtyFlags, EArchSkyDirtyFlags::Clouds) || bTransitionActive))
	{
		ApplyClouds(Weather, State);
		SecondsSinceCloudUpdate = 0.f;
		EnumRemoveFlags(DirtyFlags, EArchSkyDirtyFlags::Clouds);
	}

	// --- Sky light: the expensive one. Two thresholds AND a hard frame floor. ---
	ApplySkyLight(Weather, Sun);

	// --- MPC: one batched write per frame, never per material ---
	if (EnumHasAnyFlags(DirtyFlags, EArchSkyDirtyFlags::MaterialParams) || bTimeFlowing || bTransitionActive)
	{
		ApplyMaterialParameters(State, Sun, Moon, Weather);
		EnumRemoveFlags(DirtyFlags, EArchSkyDirtyFlags::MaterialParams);
	}

	// --- Replication push, server only, at the configured rate ---
	if (HasAuthority() && GetIsReplicated())
	{
		const UArchSkySettings* Settings = UArchSkySettings::Get();
		const float PushInterval = (Settings && Settings->ReplicationUpdateFrequencyHz > 0.f)
			? 1.f / Settings->ReplicationUpdateFrequencyHz
			: 0.5f;

		SecondsSinceReplicationPush += DeltaSeconds;
		if (SecondsSinceReplicationPush >= PushInterval)
		{
			SecondsSinceReplicationPush = 0.f;

			const FArchSkyReplicatedState NewReplicated = Subsystem->MakeReplicatedState();
			if (NewReplicated != ReplicatedSkyState)
			{
				ReplicatedSkyState = NewReplicated;
				// Nudge the actor so the property change is picked up promptly rather than
				// waiting for the next natural net update window.
				ForceNetUpdate();
			}
		}
	}
}

void AArchSkyDirector::MarkAllDirty()
{
	DirtyFlags = EArchSkyDirtyFlags::All;
	SecondsSinceCloudUpdate = CloudUpdateInterval;   // Let clouds update on the very next tick.
}

// ---------------------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------------------

float AArchSkyDirector::EvaluateCurve(const FRuntimeFloatCurve& Curve, float Time, float Fallback)
{
	// GetRichCurveConst() returns the external asset's curve when one is assigned and the
	// embedded curve otherwise, so this one call covers both configurations.
	if (const FRichCurve* RichCurve = Curve.GetRichCurveConst())
	{
		if (RichCurve->GetNumKeys() > 0)
		{
			return RichCurve->Eval(Time);
		}
	}

	return Fallback;
}

float AArchSkyDirector::ComputeMoonWeight(double SunAltitudeDegrees) const
{
	const UArchSkySettings* Settings = UArchSkySettings::Get();
	const float FadeStart = Settings ? Settings->MoonFadeOutStartSunAltitude : -6.f;
	const float FadeEnd = Settings ? Settings->MoonFadeOutEndSunAltitude : 0.f;

	if (FMath::IsNearlyEqual(FadeStart, FadeEnd))
	{
		// Degenerate configuration: fall back to a hard switch rather than dividing by zero.
		return (SunAltitudeDegrees < FadeEnd) ? 1.f : 0.f;
	}

	// SmoothStep, not a linear ramp: a linear cross-fade of two lights whose intensities
	// differ by five orders of magnitude still shows a visible seam at the join.
	const float Alpha = FMath::Clamp(
		(static_cast<float>(SunAltitudeDegrees) - FadeStart) / (FadeEnd - FadeStart), 0.f, 1.f);

	return 1.f - FMath::SmoothStep(0.f, 1.f, Alpha);
}

void AArchSkyDirector::ApplyLights(const FArchSkyState& State, const FArchSolarPosition& Sun, const FArchLunarPosition& Moon)
{
	// Deliberately a value, not a reference: the ternary below would otherwise bind a
	// const reference to a temporary in one branch and to subsystem-owned storage in the
	// other, which is legal but needlessly subtle.
	static const FArchWeatherParams DefaultWeather;
	const UArchSkySubsystem* Subsystem = SkySubsystem.Get();
	const FArchWeatherParams& Weather = Subsystem ? Subsystem->GetCurrentWeatherBlended() : DefaultWeather;

	// --- Sun ---
	// Geometric altitude, not apparent: the shadow is cast along the true direction.
	CurrentSunRotation = ArchSolarMath::SolarToUnrealLightRotation(
		Sun.AzimuthDegrees, Sun.TrueAltitudeDegrees, State.NorthOffsetDegrees);

	UDirectionalLightComponent* SunLight = AdoptedSunLight.IsValid() ? AdoptedSunLight.Get() : SunLightComponent.Get();
	if (IsValid(SunLight))
	{
		SunLight->SetWorldRotation(CurrentSunRotation);

		const float BaseLux = EvaluateCurve(SunIntensityCurve, static_cast<float>(Sun.TrueAltitudeDegrees), 0.f);
		const float Lux = FMath::Max(0.f, BaseLux * Weather.SunIntensityMultiplier);

		// ARCH NOTE: a UDirectionalLightComponent's Intensity is ALREADY in lux - the
		// engine defines it that way, and IntensityUnits exists only on
		// ULocalLightComponent (point and spot), where candelas/lumens are ambiguous.
		// So the curve's output unit and the component's unit already agree, and setting
		// an IntensityUnits here would not compile. 120000 really is a clear midday sun
		// for a correctly exposed physical camera.
		SunLight->SetIntensity(Lux);

		const float Kelvin = EvaluateCurve(SunColorTemperatureCurve, static_cast<float>(Sun.TrueAltitudeDegrees), 6500.f);
		FLinearColor SunColor = FLinearColor::MakeFromColorTemperature(Kelvin);

		// The weather tint pulls the sun towards the sky's own colour - which is what
		// actually happens in a dust storm, where the direct beam is filtered red.
		if (Weather.SunLightColorTintStrength > 0.f)
		{
			SunColor = FMath::Lerp(SunColor, SunColor * Weather.SkyLuminanceTint, Weather.SunLightColorTintStrength);
		}

		SunLight->SetLightColor(SunColor);

		// Softening the disc softens the penumbra. Clamped so a preset cannot produce a
		// source angle the shadow projection cannot represent.
		const UArchSkySettings* Settings = UArchSkySettings::Get();
		const float BaseAngle = Settings ? Settings->SunLightSourceAngleDegrees : 0.545f;
		SunLight->LightSourceAngle = FMath::Clamp(BaseAngle * Weather.ShadowSoftnessMultiplier, 0.01f, 20.f);

		// Below the horizon the sun contributes nothing but still costs a shadow pass.
		const bool bSunVisible = Lux > UE_KINDA_SMALL_NUMBER;
		SunLight->SetVisibility(bSunVisible);
		SunLight->SetCastShadows(bSunVisible);
		SunLight->MarkRenderStateDirty();
	}

	// --- Moon ---
	UDirectionalLightComponent* MoonLight = AdoptedMoonLight.IsValid() ? AdoptedMoonLight.Get() : MoonLightComponent.Get();
	if (IsValid(MoonLight))
	{
		const float MoonWeight = ComputeMoonWeight(Sun.TrueAltitudeDegrees);

		// Illuminance falls off with the sine of altitude (the cosine-law projection onto
		// the ground) and scales linearly with the lit fraction of the disc. A crescent
		// really is roughly fifty times dimmer than a full moon, and looks it.
		const float AltitudeFactor = FMath::Clamp(
			static_cast<float>(FMath::Sin(FMath::DegreesToRadians(FMath::Max(0.0, Moon.TrueAltitudeDegrees)))), 0.f, 1.f);

		const float Lux = MoonPeakIlluminanceLux
			* static_cast<float>(Moon.IlluminatedFraction)
			* AltitudeFactor
			* MoonWeight
			* Weather.SunIntensityMultiplier;

		const bool bMoonVisible = bEnableMoonLight && Moon.bIsAboveHorizon && Lux > UE_SMALL_NUMBER;

		if (bMoonVisible)
		{
			MoonLight->SetWorldRotation(ArchSolarMath::SolarToUnrealLightRotation(
				Moon.AzimuthDegrees, Moon.TrueAltitudeDegrees, State.NorthOffsetDegrees));

			// Also lux; see the note on the sun above.
			MoonLight->SetIntensity(Lux);
			MoonLight->SetLightColor(MoonLightColor);
		}

		MoonLight->SetVisibility(bMoonVisible);
		MoonLight->SetCastShadows(bMoonVisible);
		MoonLight->MarkRenderStateDirty();
	}

#if WITH_EDITORONLY_DATA
	if (NorthIndicatorComponent)
	{
		NorthIndicatorComponent->SetRelativeRotation(FRotator(0.f, State.NorthOffsetDegrees, 0.f));
	}
#endif
}

void AArchSkyDirector::ApplySkyLight(const FArchWeatherParams& Weather, const FArchSolarPosition& Sun)
{
	USkyLightComponent* SkyLight = AdoptedSkyLight.IsValid() ? AdoptedSkyLight.Get() : SkyLightComponent.Get();
	if (!IsValid(SkyLight))
	{
		return;
	}

	// Intensity is cheap and can track every frame; only the CAPTURE is throttled.
	const float AltitudeScale = EvaluateCurve(SkyLightIntensityCurve, static_cast<float>(Sun.TrueAltitudeDegrees), 1.f);
	SkyLight->SetIntensity(FMath::Max(0.f, AltitudeScale * Weather.SkyLightIntensityMultiplier));

	if (bUsingRealTimeSkyCapture)
	{
		// Real-time capture updates itself on the render thread; calling RecaptureSky on
		// top of it would do the work twice.
		return;
	}

	const double AltitudeDelta = FMath::Abs(Sun.TrueAltitudeDegrees - AltitudeAtLastRecapture);
	const float WeatherDelta = Weather.GetSignificantDelta(WeatherAtLastRecapture);

	// A non-negative CVar overrides the per-instance threshold, so a device profile can
	// dial sky-light cost down without touching the level.
	const float CVarThreshold = CVarArchSkyRecaptureThreshold.GetValueOnGameThread();
	const float EffectiveAltitudeThreshold = (CVarThreshold >= 0.f) ? CVarThreshold : SkyRecaptureAltitudeThreshold;

	const bool bAltitudeTriggered = AltitudeDelta >= static_cast<double>(EffectiveAltitudeThreshold);
	const bool bWeatherTriggered = WeatherDelta >= SkyRecaptureWeatherThreshold;
	const bool bForced = EnumHasAnyFlags(DirtyFlags, EArchSkyDirtyFlags::SkyLight);

	if (!bAltitudeTriggered && !bWeatherTriggered && !bForced)
	{
		return;
	}

	// The hard floor. Whatever the thresholds say, a cubemap capture never runs more often
	// than this. Without it, a fast time-lapse would request one every single frame.
	const UWorld* World = GetWorld();
	const uint64 CurrentFrame = World ? static_cast<uint64>(GFrameCounter) : 0;
	if (CurrentFrame < FrameOfLastRecapture + static_cast<uint64>(FMath::Max(1, MinFramesBetweenSkyRecaptures)))
	{
		return;
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_ArchSky_SkyRecapture);
		SkyLight->RecaptureSky();
	}

	AltitudeAtLastRecapture = Sun.TrueAltitudeDegrees;
	WeatherAtLastRecapture = Weather;
	FrameOfLastRecapture = CurrentFrame;
	EnumRemoveFlags(DirtyFlags, EArchSkyDirtyFlags::SkyLight);
}

void AArchSkyDirector::ApplyAtmosphere(const FArchWeatherParams& Weather)
{
	SCOPE_CYCLE_COUNTER(STAT_ArchSky_WeatherApply);

	USkyAtmosphereComponent* Atmosphere = AdoptedSkyAtmosphere.IsValid()
		? AdoptedSkyAtmosphere.Get()
		: SkyAtmosphereComponent.Get();

	if (!IsValid(Atmosphere))
	{
		ARCHSKY_LOG_ONCE(Warning, TEXT("No SkyAtmosphere component available; atmospheric scattering will not respond to weather."));
		return;
	}

	Atmosphere->SetRayleighScatteringScale(FMath::Max(0.f, Weather.RayleighScatteringScale));

	// ARCH NOTE: turbidity is folded into the Mie scale rather than exposed as its own
	// engine parameter, because USkyAtmosphereComponent has no turbidity input. Linke
	// turbidity 2 is a pristine sky and 12 a dust storm; dividing by 2.5 puts the clean-air
	// case at ~1.0, which is the engine's own default, so a "Clear" preset is a no-op.
	const float TurbidityScale = FMath::Max(0.f, Weather.AerosolTurbidity / 2.5f);
	Atmosphere->SetMieScatteringScale(FMath::Max(0.f, Weather.MieScatteringScale * TurbidityScale));
	Atmosphere->SetMieAnisotropy(FMath::Clamp(Weather.MieAnisotropy, 0.f, 0.999f));

	// Absorption rises with aerosol load; this is what turns a dust storm brown rather
	// than merely bright.
	Atmosphere->SetMieAbsorptionScale(FMath::Max(0.f, TurbidityScale * 0.5f));

	Atmosphere->SetSkyLuminanceFactor(Weather.SkyLuminanceTint);
	Atmosphere->MarkRenderStateDirty();
}

void AArchSkyDirector::ApplyFog(const FArchWeatherParams& Weather)
{
	SCOPE_CYCLE_COUNTER(STAT_ArchSky_WeatherApply);

	UExponentialHeightFogComponent* Fog = AdoptedHeightFog.IsValid() ? AdoptedHeightFog.Get() : HeightFogComponent.Get();
	if (!IsValid(Fog))
	{
		ARCHSKY_LOG_ONCE(Warning, TEXT("No ExponentialHeightFog component available; fog will not respond to weather."));
		return;
	}

	Fog->SetFogDensity(FMath::Max(0.f, Weather.FogDensity));
	Fog->SetFogHeightFalloff(FMath::Max(0.001f, Weather.FogHeightFalloff));
	Fog->SetFogInscatteringColor(Weather.FogInscatteringColor);
	Fog->SetStartDistance(FMath::Max(0.f, Weather.FogStartDistance));
	Fog->SetVolumetricFogExtinctionScale(FMath::Max(0.f, Weather.VolumetricFogExtinctionScale));
	Fog->MarkRenderStateDirty();
}

void AArchSkyDirector::ApplyClouds(const FArchWeatherParams& Weather, const FArchSkyState& State)
{
	SCOPE_CYCLE_COUNTER(STAT_ArchSky_WeatherApply);

	if (ResolvedCloudMode == EArchCloudMode::Disabled)
	{
		return;
	}

	if (ResolvedCloudMode == EArchCloudMode::SkySphere)
	{
		// Path B carries everything through the MPC; there is no component to configure.
		// ApplyMaterialParameters writes CloudCoverage, WindVector and the rest, which the
		// sky-sphere material consumes. Nothing to do here, and saying so explicitly is
		// better than an empty branch that looks like an oversight.
		return;
	}

	UVolumetricCloudComponent* Clouds = AdoptedVolumetricCloud.IsValid()
		? AdoptedVolumetricCloud.Get()
		: VolumetricCloudComponent.Get();

	if (!IsValid(Clouds))
	{
		ARCHSKY_LOG_ONCE(Warning, TEXT("Cloud mode is Volumetric but no VolumetricCloud component is available."));
		return;
	}

	// The component exposes only layer geometry; coverage, density and erosion live in the
	// cloud material and reach it through the MPC. See MATERIALS.md, "Path A".
	Clouds->SetLayerBottomAltitude(FMath::Max(0.1f, Weather.CloudAltitudeKm));
	Clouds->SetLayerHeight(FMath::Max(0.1f, Weather.CloudLayerThicknessKm));

	// Tracing distance scaled with layer thickness: a 9 km thunderstorm anvil needs a
	// longer march than a 1.5 km fair-weather deck, and a fixed value wastes either
	// quality or milliseconds.
	Clouds->SetTracingMaxDistance(FMath::Clamp(Weather.CloudLayerThicknessKm * 6.f, 20.f, 120.f));

	Clouds->MarkRenderStateDirty();
	(void)State;
}

UMaterialParameterCollection* AArchSkyDirector::GetParameterCollection()
{
	if (CachedParameterCollection)
	{
		return CachedParameterCollection;
	}

	const UArchSkySettings* Settings = UArchSkySettings::Get();
	if (!Settings || Settings->SkyParameterCollection.IsNull())
	{
		return nullptr;
	}

	CachedParameterCollection = Settings->SkyParameterCollection.LoadSynchronous();

	if (!CachedParameterCollection)
	{
		ARCHSKY_LOG_ONCE(Warning,
			TEXT("The configured MaterialParameterCollection could not be loaded; material parameters will not be written."));
	}

	return CachedParameterCollection;
}

void AArchSkyDirector::ApplyMaterialParameters(const FArchSkyState& State, const FArchSolarPosition& Sun,
	const FArchLunarPosition& Moon, const FArchWeatherParams& Weather)
{
	if (!bWriteMaterialParameterCollection)
	{
		return;
	}

	UMaterialParameterCollection* Collection = GetParameterCollection();
	if (!Collection)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_ArchSky_MpcWrite);

	using namespace ArchSkyDirectorConstants;

	// ARCH NOTE: this is the ONE place in the plugin that writes material parameters, and
	// it runs at most once per frame. Every material in the project reads from the shared
	// collection instead of holding a dynamic material instance the Director would have to
	// push to individually - that would be O(materials) per frame instead of O(1).

	const FVector DirectionToSun = ArchSolarMath::SolarToUnrealDirectionToBody(
		Sun.AzimuthDegrees, Sun.TrueAltitudeDegrees, State.NorthOffsetDegrees);

	const FVector DirectionToMoon = ArchSolarMath::SolarToUnrealDirectionToBody(
		Moon.AzimuthDegrees, Moon.TrueAltitudeDegrees, State.NorthOffsetDegrees);

	// Wind is a compass bearing in the preset; convert it into a scene-space vector using
	// exactly the same north correction the sun uses, so wind and shadows agree.
	const float WindBearing = Weather.CloudWindDirectionDeg + State.NorthOffsetDegrees;
	const float WindRadians = FMath::DegreesToRadians(WindBearing);
	const FVector WindVector(
		FMath::Cos(WindRadians) * Weather.CloudWindSpeed,
		FMath::Sin(WindRadians) * Weather.CloudWindSpeed,
		0.f);

	// Normalised altitude: 0 at the horizon, 1 at the zenith, negative below.
	const float SunAltitude01 = FMath::Clamp(static_cast<float>(Sun.TrueAltitudeDegrees) / 90.f, -1.f, 1.f);
	const float MoonAltitude01 = FMath::Clamp(static_cast<float>(Moon.TrueAltitudeDegrees) / 90.f, -1.f, 1.f);

	const float Kelvin = EvaluateCurve(SunColorTemperatureCurve, static_cast<float>(Sun.TrueAltitudeDegrees), 6500.f);
	const FLinearColor SunColor = FLinearColor::MakeFromColorTemperature(Kelvin);

	const float SeasonBlend = SkySubsystem.IsValid() ? SkySubsystem->GetSeasonBlend01() : 0.5f;

	// Precipitation is split by type so a material does not have to branch on an enum it
	// cannot see: rain and dust get their own scalars and only one is ever non-zero.
	const float RainIntensity = (Weather.PrecipType == EArchPrecipType::Rain || Weather.PrecipType == EArchPrecipType::Hail)
		? Weather.PrecipIntensity : 0.f;
	const float DustIntensity = (Weather.PrecipType == EArchPrecipType::Dust) ? Weather.PrecipIntensity : 0.f;

	auto SetScalar = [Collection, World](FName Name, float Value)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, Name, Value);
	};

	auto SetVector = [Collection, World](FName Name, const FLinearColor& Value)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, Name, Value);
	};

	SetScalar(Mpc::SunAltitude01, SunAltitude01);
	SetScalar(Mpc::SunAltitudeDegrees, static_cast<float>(Sun.TrueAltitudeDegrees));
	SetScalar(Mpc::MoonIllumination, static_cast<float>(Moon.IlluminatedFraction));
	SetScalar(Mpc::MoonAltitude01, MoonAltitude01);
	SetScalar(Mpc::Wetness, Weather.PuddleWetness);
	SetScalar(Mpc::SnowCoverage, Weather.SurfaceSnowCoverage);
	SetScalar(Mpc::RainIntensity, RainIntensity);
	SetScalar(Mpc::DustIntensity, DustIntensity);
	SetScalar(Mpc::SeasonBlend, SeasonBlend);
	SetScalar(Mpc::TimeOfDay01, State.TimeOfDayHours / 24.f);
	SetScalar(Mpc::FogDensity, Weather.FogDensity);
	SetScalar(Mpc::CloudCoverage, Weather.CloudCoverage);
	SetScalar(Mpc::WindStrength, Weather.WindStrength);
	SetScalar(Mpc::WindTurbulence, Weather.WindTurbulence);
	SetScalar(Mpc::Turbidity, Weather.AerosolTurbidity);

	// Vectors: W carries a useful scalar rather than being wasted on padding.
	SetVector(Mpc::SunDirection, FLinearColor(
		static_cast<float>(DirectionToSun.X),
		static_cast<float>(DirectionToSun.Y),
		static_cast<float>(DirectionToSun.Z),
		SunAltitude01));

	SetVector(Mpc::MoonDirection, FLinearColor(
		static_cast<float>(DirectionToMoon.X),
		static_cast<float>(DirectionToMoon.Y),
		static_cast<float>(DirectionToMoon.Z),
		static_cast<float>(Moon.IlluminatedFraction)));

	SetVector(Mpc::SunLightColor, SunColor);

	SetVector(Mpc::WindVector, FLinearColor(
		static_cast<float>(WindVector.X),
		static_cast<float>(WindVector.Y),
		static_cast<float>(WindVector.Z),
		Weather.CloudErosion));

	SetVector(Mpc::FogInscatteringColor, Weather.FogInscatteringColor);
}

// ---------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------

UArchSkySubsystem* AArchSkyDirector::GetSkySubsystem() const
{
	if (UArchSkySubsystem* Cached = SkySubsystem.Get())
	{
		return Cached;
	}

	return UArchSkySubsystem::Get(this);
}

FRotator AArchSkyDirector::GetSunLightRotation() const
{
	return CurrentSunRotation;
}

FVector AArchSkyDirector::GetDirectionToSun() const
{
	// The light's forward vector is the direction light travels; the direction TO the sun
	// is its negation. Deriving it here rather than storing a second vector keeps the two
	// from ever disagreeing.
	return -CurrentSunRotation.Vector();
}

// ---------------------------------------------------------------------------------------
// Networking
// ---------------------------------------------------------------------------------------

void AArchSkyDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AArchSkyDirector, ReplicatedSkyState);
}

void AArchSkyDirector::OnRep_SkyState()
{
	if (UArchSkySubsystem* Subsystem = GetSkySubsystem())
	{
		Subsystem->ApplyReplicatedState(ReplicatedSkyState);
	}

	MarkAllDirty();
}

namespace ArchSkyNetDetail
{
	/** Shared gate for every client request RPC. */
	bool IsClientControlAllowed()
	{
		const UArchSkySettings* Settings = UArchSkySettings::Get();
		return Settings && Settings->bAllowClientTimeControl;
	}
}

bool AArchSkyDirector::Server_RequestSetTimeOfDay_Validate(float Hours)
{
	// Reject NaN and absurd values at the network boundary rather than clamping them
	// silently: a client sending those is malfunctioning or malicious, and either way the
	// server should not be asked to reason about it.
	return FMath::IsFinite(Hours) && Hours >= -48.f && Hours <= 48.f;
}

void AArchSkyDirector::Server_RequestSetTimeOfDay_Implementation(float Hours)
{
	if (!ArchSkyNetDetail::IsClientControlAllowed())
	{
		UE_LOG(LogArchSky, Warning, TEXT("A client requested a time change but bAllowClientTimeControl is false. Ignoring."));
		return;
	}

	if (UArchSkySubsystem* Subsystem = GetSkySubsystem())
	{
		Subsystem->SetTimeOfDay(Hours);
	}
}

bool AArchSkyDirector::Server_RequestSetDayOfYear_Validate(int32 Day)
{
	return Day >= 1 && Day <= 366;
}

void AArchSkyDirector::Server_RequestSetDayOfYear_Implementation(int32 Day)
{
	if (!ArchSkyNetDetail::IsClientControlAllowed())
	{
		UE_LOG(LogArchSky, Warning, TEXT("A client requested a date change but bAllowClientTimeControl is false. Ignoring."));
		return;
	}

	if (UArchSkySubsystem* Subsystem = GetSkySubsystem())
	{
		Subsystem->SetDayOfYear(Day);
	}
}

bool AArchSkyDirector::Server_RequestSetWeather_Validate(FName PresetId, float TransitionSeconds)
{
	return !PresetId.IsNone() && FMath::IsFinite(TransitionSeconds) && TransitionSeconds >= 0.f && TransitionSeconds <= 600.f;
}

void AArchSkyDirector::Server_RequestSetWeather_Implementation(FName PresetId, float TransitionSeconds)
{
	if (!ArchSkyNetDetail::IsClientControlAllowed())
	{
		UE_LOG(LogArchSky, Warning, TEXT("A client requested a weather change but bAllowClientTimeControl is false. Ignoring."));
		return;
	}

	if (UArchSkySubsystem* Subsystem = GetSkySubsystem())
	{
		// The subsystem itself validates the preset id and warns on an unknown one, so an
		// invalid name from a client is handled exactly like an invalid name from the UI.
		Subsystem->SetWeatherPreset(PresetId, TransitionSeconds);
	}
}

bool AArchSkyDirector::Server_RequestSetTimeFlowRate_Validate(float HoursPerSecond)
{
	return FMath::IsFinite(HoursPerSecond) && FMath::Abs(HoursPerSecond) <= 600.f;
}

void AArchSkyDirector::Server_RequestSetTimeFlowRate_Implementation(float HoursPerSecond)
{
	if (!ArchSkyNetDetail::IsClientControlAllowed())
	{
		UE_LOG(LogArchSky, Warning, TEXT("A client requested a time-flow change but bAllowClientTimeControl is false. Ignoring."));
		return;
	}

	if (UArchSkySubsystem* Subsystem = GetSkySubsystem())
	{
		Subsystem->SetTimeFlowRate(HoursPerSecond);
	}
}

// ---------------------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------------------

#if WITH_EDITOR

void AArchSkyDirector::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshEditorPreview();
}

void AArchSkyDirector::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);

	// Chain edits are what a nested struct property (a curve key, a location field) sends.
	// Handling them separately is what makes dragging a slider inside FArchGeoLocation
	// update the viewport on every mouse-move rather than only on release.
	RefreshEditorPreview();
}

void AArchSkyDirector::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Moving the Director does not change the sky, but its north arrows are attached to it.
	if (bFinished)
	{
		RefreshEditorPreview();
	}
}

void AArchSkyDirector::RefreshEditorPreview()
{
	if (!bPreviewInEditor)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || World->IsGameWorld())
	{
		// In PIE the normal tick path already runs; doing it twice would be waste.
		return;
	}

	UArchSkySubsystem* Subsystem = GetSkySubsystem();
	if (!Subsystem)
	{
		return;
	}

	MarkAllDirty();

	const FArchSkyState& State = Subsystem->GetSkyState();
	const FArchWeatherParams& Weather = Subsystem->GetCurrentWeatherBlended();

	ApplyLights(State, Subsystem->GetSolarPosition(), Subsystem->GetLunarPosition());
	ApplyAtmosphere(Weather);
	ApplyFog(Weather);
	ApplyClouds(Weather, State);
	ApplySkyLight(Weather, Subsystem->GetSolarPosition());
	ApplyMaterialParameters(State, Subsystem->GetSolarPosition(), Subsystem->GetLunarPosition(), Weather);

	RecordAppliedWeather(State);
	DirtyFlags = EArchSkyDirtyFlags::None;
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
