// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/ArchSkyState.h"
#include "Curves/CurveFloat.h"
#include "Data/ArchSkySettings.h"
#include "Data/ArchWeatherPreset.h"
#include "GameFramework/Actor.h"
#include "Math/ArchMoonMath.h"
#include "Math/ArchSolarMath.h"

#include "ArchSkyDirector.generated.h"

class UArchSkySubsystem;
class UArrowComponent;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UMaterialParameterCollection;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class UVolumetricCloudComponent;

/**
 * Bit flags describing which parts of the scene need re-applying.
 *
 * ARCH NOTE: dirty flags rather than "apply everything every frame". Rotating a light is
 * a handful of float writes; recapturing a sky light is a full cubemap render. Conflating
 * them would put a cubemap capture on every frame of a time-lapse. Every expensive
 * operation gets its own flag and its own threshold, all exposed in Project Settings.
 */
enum class EArchSkyDirtyFlags : uint32
{
	None			= 0,
	/** Light rotation, intensity and colour. Cheap; runs every frame while time flows. */
	Lights			= 1 << 0,
	/** Sky-light cubemap recapture. Expensive; altitude- and frame-throttled. */
	SkyLight		= 1 << 1,
	/** Sky atmosphere scattering coefficients. Moderate; weather and location only. */
	Atmosphere		= 1 << 2,
	/** Exponential height fog. Moderate; weather only. */
	Fog				= 1 << 3,
	/** Volumetric cloud or sky-sphere material parameters. Time-throttled. */
	Clouds			= 1 << 4,
	/** Material parameter collection. Batched into one write per frame. */
	MaterialParams	= 1 << 5,
	/** Everything. */
	All				= 0xFFFFFFFF
};

ENUM_CLASS_FLAGS(EArchSkyDirtyFlags);

/**
 * The one scene authority for the sky. Place exactly one per level.
 *
 * RESPONSIBILITIES
 * ----------------
 * The Director OWNS the scene side and nothing else. It subscribes to
 * UArchSkySubsystem::OnSkyStateChanged, converts the state into component values, and
 * pushes them. It never decides what the state should be, never advances time, and holds
 * no authority the subsystem does not give it.
 *
 * By default it creates and owns every component it needs, so dropping this actor into an
 * empty level produces a complete, correct sky. Set bUseExistingSceneActors to adopt the
 * lights and atmosphere a level already has instead.
 *
 * THREADING: game thread only.
 */
UCLASS(Blueprintable, ClassGroup = "ArchSky", meta = (DisplayName = "ArchSky Director"),
	HideCategories = (Replication, Collision, Input, LOD, Cooking))
class ARCHSKYRUNTIME_API AArchSkyDirector : public AActor
{
	GENERATED_BODY()

public:
	AArchSkyDirector();

	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostInitializeComponents() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;
	//~ End AActor

#if WITH_EDITOR
	//~ Begin UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent) override;
	virtual void PostEditMove(bool bFinished) override;
	//~ End UObject

	/**
	 * Recomputes and re-applies the whole sky from the current state, without PIE.
	 * This is what makes the details-panel sliders scrub live in the editor viewport.
	 */
	void RefreshEditorPreview();
#endif

	// -----------------------------------------------------------------------------------
	// Owned components
	// -----------------------------------------------------------------------------------

	/** Root. Positioning the Director does not move the sky; it is a pure controller. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ArchSky|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** The sun. AtmosphereSunLightIndex 0. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ArchSky|Components")
	TObjectPtr<UDirectionalLightComponent> SunLightComponent;

	/** The moon. AtmosphereSunLightIndex 1, cross-faded against the sun across twilight. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ArchSky|Components")
	TObjectPtr<UDirectionalLightComponent> MoonLightComponent;

	/** Captured-scene sky light providing the diffuse ambient term. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ArchSky|Components")
	TObjectPtr<USkyLightComponent> SkyLightComponent;

	/** Physically based atmosphere. Driven by the weather's scattering parameters. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ArchSky|Components")
	TObjectPtr<USkyAtmosphereComponent> SkyAtmosphereComponent;

	/** Volumetric clouds. Only driven when CloudMode is Volumetric. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ArchSky|Components")
	TObjectPtr<UVolumetricCloudComponent> VolumetricCloudComponent;

	/** Exponential height fog, including the volumetric fog term. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ArchSky|Components")
	TObjectPtr<UExponentialHeightFogComponent> HeightFogComponent;

	/**
	 * Optional sky-sphere mesh for the Path B cloud mode.
	 * Left without a mesh by default; assign one to use the cheap panoramic path.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ArchSky|Components")
	TObjectPtr<UStaticMeshComponent> SkySphereComponent;

#if WITH_EDITORONLY_DATA
	/** Editor-only arrow showing TRUE north. Never present in a packaged build. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ArchSky|Components")
	TObjectPtr<UArrowComponent> NorthIndicatorComponent;

	/** Editor-only arrow showing the PLAN's north, i.e. scene +X. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ArchSky|Components")
	TObjectPtr<UArrowComponent> PlanNorthIndicatorComponent;
#endif

	// -----------------------------------------------------------------------------------
	// Configuration
	// -----------------------------------------------------------------------------------

	/**
	 * When true, the Director hunts the level for existing ADirectionalLight, ASkyLight,
	 * ASkyAtmosphere, AExponentialHeightFog and AVolumetricCloud actors and drives those
	 * instead of its own components, which it then hides.
	 *
	 * Use this to retrofit ArchSky onto a level that already has a lighting setup an
	 * artist has tuned. A missing actor produces one warning and is skipped, never a crash.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ArchSky|Setup")
	bool bUseExistingSceneActors = false;

	/** When true, the Director applies the sky in the editor viewport without PIE. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ArchSky|Setup")
	bool bPreviewInEditor = true;

	/**
	 * Sun altitude (degrees) mapped to direct illuminance (lux).
	 * Default runs 0 lux at -6 degrees to ~120000 lux at the zenith, which is the
	 * physically plausible clear-sky range.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ArchSky|Sun",
		meta = (XAxisName = "Sun Altitude (deg)", YAxisName = "Illuminance (lux)"))
	FRuntimeFloatCurve SunIntensityCurve;

	/**
	 * Sun altitude (degrees) mapped to correlated colour temperature (Kelvin).
	 * Default runs ~1800 K at the horizon to ~6500 K at the zenith.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ArchSky|Sun",
		meta = (XAxisName = "Sun Altitude (deg)", YAxisName = "Colour Temperature (K)"))
	FRuntimeFloatCurve SunColorTemperatureCurve;

	/**
	 * Sun altitude (degrees) mapped to sky-light intensity scale.
	 * Keeps ambient light alive through twilight after the sun light has gone to zero.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ArchSky|Sun",
		meta = (XAxisName = "Sun Altitude (deg)", YAxisName = "Sky Light Scale"))
	FRuntimeFloatCurve SkyLightIntensityCurve;

	/** Moon illuminance (lux) at full phase and zenith, before the illumination term. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ArchSky|Moon",
		meta = (ClampMin = "0.0", ClampMax = "50.0", UIMin = "0.0", UIMax = "5.0", Units = "Lux"))
	float MoonPeakIlluminanceLux = 0.4f;

	/** Colour of moonlight. Slightly blue by convention, not by physics. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ArchSky|Moon")
	FLinearColor MoonLightColor = FLinearColor(0.62f, 0.72f, 1.f, 1.f);

	/** When false, the moon light is never enabled regardless of the sky state. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ArchSky|Moon")
	bool bEnableMoonLight = true;

	// --- Performance. Every threshold documented in PERFORMANCE.md. ---

	/** Sun-altitude change, in degrees, that forces a sky-light recapture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance",
		meta = (ClampMin = "0.05", ClampMax = "45.0", UIMin = "0.25", UIMax = "10.0", Units = "Degrees"))
	float SkyRecaptureAltitudeThreshold = 1.f;

	/** Normalised weather change that forces a sky-light recapture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance",
		meta = (ClampMin = "0.01", ClampMax = "1.0", UIMin = "0.02", UIMax = "0.5"))
	float SkyRecaptureWeatherThreshold = 0.05f;

	/** Hard floor, in frames, between two RecaptureSky() calls. Never exceeded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance",
		meta = (ClampMin = "1", ClampMax = "600", UIMin = "1", UIMax = "120"))
	int32 MinFramesBetweenSkyRecaptures = 30;

	/** Seconds between cloud parameter updates. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance",
		meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "1.0", Units = "Seconds"))
	float CloudUpdateInterval = 0.1f;

	/** When false, the Director never writes to the material parameter collection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance")
	bool bWriteMaterialParameterCollection = true;

	// -----------------------------------------------------------------------------------
	// Public queries
	// -----------------------------------------------------------------------------------

	/** The subsystem this Director is bound to. Null before BeginPlay or after teardown. */
	UFUNCTION(BlueprintPure, Category = "ArchSky")
	UArchSkySubsystem* GetSkySubsystem() const;

	/** Forces every subsystem of the scene to be re-applied on the next tick. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky")
	void MarkAllDirty();

	/** The world rotation currently applied to the sun light. */
	UFUNCTION(BlueprintPure, Category = "ArchSky")
	FRotator GetSunLightRotation() const;

	/** Unit vector from the scene towards the sun, in world space. */
	UFUNCTION(BlueprintPure, Category = "ArchSky")
	FVector GetDirectionToSun() const;

	// -----------------------------------------------------------------------------------
	// Networking
	// -----------------------------------------------------------------------------------

	/** Server RPC: a client asks for a time change. Gated by bAllowClientTimeControl. */
	UFUNCTION(Server, Reliable, WithValidation, Category = "ArchSky|Networking")
	void Server_RequestSetTimeOfDay(float Hours);

	/** Server RPC: a client asks for a date change. */
	UFUNCTION(Server, Reliable, WithValidation, Category = "ArchSky|Networking")
	void Server_RequestSetDayOfYear(int32 Day);

	/** Server RPC: a client asks for a weather change. */
	UFUNCTION(Server, Reliable, WithValidation, Category = "ArchSky|Networking")
	void Server_RequestSetWeather(FName PresetId, float TransitionSeconds);

	/** Server RPC: a client asks for a time-flow change. */
	UFUNCTION(Server, Reliable, WithValidation, Category = "ArchSky|Networking")
	void Server_RequestSetTimeFlowRate(float HoursPerSecond);

protected:
	/** Replicated, quantised sky state. Pushed at ReplicationUpdateFrequencyHz, not per frame. */
	UPROPERTY(ReplicatedUsing = OnRep_SkyState)
	FArchSkyReplicatedState ReplicatedSkyState;

	/** Applies a freshly received replicated state to the local subsystem. */
	UFUNCTION()
	void OnRep_SkyState();

private:
	/** Subscribed to UArchSkySubsystem::OnSkyStateChanged. Only sets dirty flags. */
	UFUNCTION()
	void HandleSkyStateChanged(const FArchSkyState& NewState);

	/** Binds to the subsystem and does the first full apply. */
	void BindToSubsystem();

	/** Unbinds cleanly. Safe to call twice. */
	void UnbindFromSubsystem();

	/** Populates the three curves with their physically plausible defaults. */
	void InitialiseDefaultCurves();

	/** Finds and adopts pre-existing lighting actors when bUseExistingSceneActors is set. */
	void DiscoverExistingSceneActors();

	/** Warns once if this level contains more than one Director. */
	void ValidateSingleInstance() const;

	/** Copies the project-settings throttles onto this instance's defaults. */
	void ApplySettingsDefaults();

	// --- Application steps, one per dirty flag ---

	void ApplyLights(const FArchSkyState& State, const FArchSolarPosition& Sun, const FArchLunarPosition& Moon);
	void ApplySkyLight(const FArchWeatherParams& Weather, const FArchSolarPosition& Sun);
	void ApplyAtmosphere(const FArchWeatherParams& Weather);
	void ApplyFog(const FArchWeatherParams& Weather);
	void ApplyClouds(const FArchWeatherParams& Weather, const FArchSkyState& State);
	void ApplyMaterialParameters(const FArchSkyState& State, const FArchSolarPosition& Sun,
		const FArchLunarPosition& Moon, const FArchWeatherParams& Weather);

	/** Evaluates a runtime curve, falling back to a linear default if it is empty. */
	static float EvaluateCurve(const FRuntimeFloatCurve& Curve, float Time, float Fallback);

	/** 0 at MoonFadeOutEnd, 1 at MoonFadeOutStart, smoothly interpolated between. */
	float ComputeMoonWeight(double SunAltitudeDegrees) const;

	/** Resolves and caches the material parameter collection configured in the settings. */
	UMaterialParameterCollection* GetParameterCollection();

	/** Weak handle to the subsystem. Weak, so a world teardown order surprise cannot dangle. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UArchSkySubsystem> SkySubsystem;

	/** The parameter collection resolved from settings, held so we do not re-load it. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> CachedParameterCollection;

	/** Adopted actors, when bUseExistingSceneActors is set. Weak: the level owns them. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UDirectionalLightComponent> AdoptedSunLight;

	UPROPERTY(Transient)
	TWeakObjectPtr<UDirectionalLightComponent> AdoptedMoonLight;

	UPROPERTY(Transient)
	TWeakObjectPtr<USkyLightComponent> AdoptedSkyLight;

	UPROPERTY(Transient)
	TWeakObjectPtr<USkyAtmosphereComponent> AdoptedSkyAtmosphere;

	UPROPERTY(Transient)
	TWeakObjectPtr<UExponentialHeightFogComponent> AdoptedHeightFog;

	UPROPERTY(Transient)
	TWeakObjectPtr<UVolumetricCloudComponent> AdoptedVolumetricCloud;

	/** Pending work. */
	EArchSkyDirtyFlags DirtyFlags = EArchSkyDirtyFlags::All;

	/** Sun altitude at the last sky-light recapture, for the altitude threshold. */
	double AltitudeAtLastRecapture = -1000.0;

	/** Weather at the last sky-light recapture, for the weather threshold. */
	FArchWeatherParams WeatherAtLastRecapture;

	/** Frame number of the last recapture, for the hard frame floor. */
	uint64 FrameOfLastRecapture = 0;

	/** Seconds since the last cloud parameter update. */
	float SecondsSinceCloudUpdate = 0.f;

	/** Seconds since the last replication push, on the server. */
	float SecondsSinceReplicationPush = 0.f;

	/** Cloud path resolved from settings at BeginPlay. */
	EArchCloudMode ResolvedCloudMode = EArchCloudMode::Volumetric;

	/** Rotation currently applied to the sun light, cached for GetSunLightRotation. */
	FRotator CurrentSunRotation = FRotator::ZeroRotator;

	/** True once BindToSubsystem has succeeded. */
	bool bBoundToSubsystem = false;

	/** True when real-time sky capture is available, so we never call RecaptureSky. */
	bool bUsingRealTimeSkyCapture = false;
};
