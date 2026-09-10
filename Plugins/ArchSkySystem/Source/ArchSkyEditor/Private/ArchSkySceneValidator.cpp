// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchSkySceneValidator.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Core/ArchSkyDirector.h"
#include "Data/ArchSkySettings.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "ArchSkySceneValidator"

const FName UArchSkySceneValidator::MessageLogName(TEXT("ArchSky"));

namespace ArchSkyValidatorDetail
{
	/** Appends one finding. */
	void Add(TArray<FArchValidationIssue>& Issues, EArchValidationSeverity Severity,
		FText Message, FText Remedy, AActor* RelatedActor = nullptr)
	{
		FArchValidationIssue Issue;
		Issue.Severity = Severity;
		Issue.Message = MoveTemp(Message);
		Issue.Remedy = MoveTemp(Remedy);
		Issue.RelatedActor = RelatedActor;
		Issues.Add(MoveTemp(Issue));
	}
}

TArray<FArchValidationIssue> UArchSkySceneValidator::ValidateWorld(UWorld* World)
{
	using namespace ArchSkyValidatorDetail;

	TArray<FArchValidationIssue> Issues;

	if (!World)
	{
		return Issues;
	}

	// --- Directors ---
	TArray<AArchSkyDirector*> Directors;
	for (TActorIterator<AArchSkyDirector> It(World); It; ++It)
	{
		Directors.Add(*It);
	}

	if (Directors.Num() == 0)
	{
		Add(Issues, EArchValidationSeverity::Error,
			LOCTEXT("NoDirector", "This level contains no ArchSky Director."),
			LOCTEXT("NoDirectorFix", "Use ArchSky > Add Sky Director to Level, or drag an ArchSky Director from the Place Actors panel."));
	}
	else if (Directors.Num() > 1)
	{
		for (AArchSkyDirector* Director : Directors)
		{
			Add(Issues, EArchValidationSeverity::Error,
				FText::Format(LOCTEXT("DuplicateDirector",
					"This level contains {0} ArchSky Directors. They will fight over the same lights every frame."),
					FText::AsNumber(Directors.Num())),
				LOCTEXT("DuplicateDirectorFix", "Delete all but one."),
				Director);
		}
	}

	AArchSkyDirector* PrimaryDirector = Directors.Num() > 0 ? Directors[0] : nullptr;
	const bool bAdoptionMode = PrimaryDirector && PrimaryDirector->bUseExistingSceneActors;

	// --- Directional lights ---
	TArray<UDirectionalLightComponent*> ExternalDirectionalLights;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		ADirectionalLight* Light = *It;
		if (!IsValid(Light))
		{
			continue;
		}

		UDirectionalLightComponent* Component = Cast<UDirectionalLightComponent>(Light->GetLightComponent());
		if (!Component)
		{
			continue;
		}

		ExternalDirectionalLights.Add(Component);

		// A non-movable directional light cannot be rotated at runtime, so the sun would
		// simply never move - the single most common setup mistake for this plugin.
		if (Component->Mobility != EComponentMobility::Movable)
		{
			Add(Issues, EArchValidationSeverity::Error,
				FText::Format(LOCTEXT("LightNotMovable",
					"Directional light '{0}' is not Movable, so ArchSky cannot rotate it and the shadow study will be wrong."),
					FText::FromString(Light->GetActorNameOrLabel())),
				LOCTEXT("LightNotMovableFix", "Set the light's Mobility to Movable."),
				Light);
		}

		if (Component->AtmosphereSunLightIndex < 0 || Component->AtmosphereSunLightIndex > 1)
		{
			Add(Issues, EArchValidationSeverity::Warning,
				FText::Format(LOCTEXT("BadSunIndex",
					"Directional light '{0}' has AtmosphereSunLightIndex {1}. The engine only supports 0 (sun) and 1 (moon)."),
					FText::FromString(Light->GetActorNameOrLabel()),
					FText::AsNumber(Component->AtmosphereSunLightIndex)),
				LOCTEXT("BadSunIndexFix", "Set it to 0 for the sun or 1 for the moon."),
				Light);
		}

		if (!Component->bAtmosphereSunLight)
		{
			Add(Issues, EArchValidationSeverity::Warning,
				FText::Format(LOCTEXT("NotAtmosphereSunLight",
					"Directional light '{0}' does not have 'Atmosphere Sun Light' enabled, so the sky will not respond to it."),
					FText::FromString(Light->GetActorNameOrLabel())),
				LOCTEXT("NotAtmosphereSunLightFix", "Enable Atmosphere Sun Light on the light component."),
				Light);
		}
	}

	// Two directional lights that both claim index 0 produce a black or double-lit sky.
	int32 IndexZeroCount = 0;
	for (const UDirectionalLightComponent* Component : ExternalDirectionalLights)
	{
		if (Component->bAtmosphereSunLight && Component->AtmosphereSunLightIndex == 0)
		{
			++IndexZeroCount;
		}
	}

	if (IndexZeroCount > 1)
	{
		Add(Issues, EArchValidationSeverity::Error,
			FText::Format(LOCTEXT("DuplicateSunIndex",
				"{0} directional lights are set as atmosphere sun light index 0. Only one can be the sun."),
				FText::AsNumber(IndexZeroCount)),
			LOCTEXT("DuplicateSunIndexFix", "Give the moon light index 1, or delete the extra light."));
	}

	if (!bAdoptionMode && ExternalDirectionalLights.Num() > 0 && PrimaryDirector)
	{
		Add(Issues, EArchValidationSeverity::Warning,
			FText::Format(LOCTEXT("ExtraDirectionalLights",
				"The level has {0} directional light actor(s) in addition to the Director's own sun and moon."),
				FText::AsNumber(ExternalDirectionalLights.Num())),
			LOCTEXT("ExtraDirectionalLightsFix",
				"Either delete them, or enable 'Use Existing Scene Actors' on the Director so it drives them instead."),
			PrimaryDirector);
	}

	// --- Sky atmosphere ---
	int32 AtmosphereCount = 0;
	int32 SkyLightCount = 0;
	int32 FogCount = 0;
	int32 CloudCount = 0;
	AActor* FirstStaticSkyLight = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		if (Actor->FindComponentByClass<USkyAtmosphereComponent>())
		{
			++AtmosphereCount;
		}
		if (Actor->FindComponentByClass<UExponentialHeightFogComponent>())
		{
			++FogCount;
		}
		if (Actor->FindComponentByClass<UVolumetricCloudComponent>())
		{
			++CloudCount;
		}

		if (USkyLightComponent* SkyLight = Actor->FindComponentByClass<USkyLightComponent>())
		{
			++SkyLightCount;

			if (SkyLight->Mobility == EComponentMobility::Static && !FirstStaticSkyLight)
			{
				FirstStaticSkyLight = Actor;
			}
		}
	}

	if (AtmosphereCount == 0)
	{
		Add(Issues, EArchValidationSeverity::Error,
			LOCTEXT("NoAtmosphere", "This level has no Sky Atmosphere, so there is no sky to light."),
			LOCTEXT("NoAtmosphereFix", "Add an ArchSky Director (it brings its own), or add a SkyAtmosphere actor."));
	}
	else if (AtmosphereCount > 1)
	{
		Add(Issues, EArchValidationSeverity::Warning,
			FText::Format(LOCTEXT("DuplicateAtmosphere",
				"This level has {0} Sky Atmosphere components. Only one contributes; the rest are wasted."),
				FText::AsNumber(AtmosphereCount)),
			LOCTEXT("DuplicateAtmosphereFix", "Delete the extras, or enable 'Use Existing Scene Actors' on the Director."));
	}

	if (SkyLightCount == 0)
	{
		Add(Issues, EArchValidationSeverity::Warning,
			LOCTEXT("NoSkyLight", "This level has no Sky Light, so interiors will have no ambient bounce."),
			LOCTEXT("NoSkyLightFix", "Add an ArchSky Director, or add a SkyLight actor."));
	}

	if (FirstStaticSkyLight)
	{
		Add(Issues, EArchValidationSeverity::Error,
			LOCTEXT("StaticSkyLight",
				"The Sky Light is Static, so it cannot follow a moving sun and the ambient term will be frozen at one time of day."),
			LOCTEXT("StaticSkyLightFix", "Set the Sky Light's Mobility to Movable and enable Real Time Capture."),
			FirstStaticSkyLight);
	}

	if (FogCount == 0)
	{
		Add(Issues, EArchValidationSeverity::Info,
			LOCTEXT("NoFog", "This level has no Exponential Height Fog, so weather presets will not produce visible haze."),
			LOCTEXT("NoFogFix", "Add an ArchSky Director, or add an ExponentialHeightFog actor."));
	}

	// --- Settings-dependent checks ---
	if (const UArchSkySettings* Settings = UArchSkySettings::Get())
	{
		if (Settings->CloudMode == EArchCloudMode::Volumetric && CloudCount == 0)
		{
			Add(Issues, EArchValidationSeverity::Warning,
				LOCTEXT("NoClouds", "Cloud mode is Volumetric but the level has no Volumetric Cloud component."),
				LOCTEXT("NoCloudsFix",
					"Add an ArchSky Director, or switch Project Settings > Plugins > ArchSky > Cloud Mode to Sky Sphere."));
		}

		if (Settings->SkyParameterCollection.IsNull())
		{
			Add(Issues, EArchValidationSeverity::Info,
				LOCTEXT("NoMpc",
					"No Material Parameter Collection is configured, so materials will not receive sun direction, wetness or snow coverage."),
				LOCTEXT("NoMpcFix",
					"Create MPC_ArchSky as described in MATERIALS.md and assign it in Project Settings > Plugins > ArchSky."));
		}

		if (Settings->bEnableReplication && !Settings->bAllowClientTimeControl)
		{
			Add(Issues, EArchValidationSeverity::Info,
				LOCTEXT("ClientControlOff",
					"Replication is on but clients cannot control time. Only the server can drive the presentation."),
				LOCTEXT("ClientControlOffFix",
					"This is usually what you want for a presentation. Enable bAllowClientTimeControl for a collaborative review."));
		}
	}

	// --- Lumen ---
	// A moving sun with no dynamic GI produces indirect light that never changes, which is
	// the most common reason a "correct" sun study still looks wrong indoors.
	if (World->GetWorldSettings())
	{
		Add(Issues, EArchValidationSeverity::Info,
			LOCTEXT("LumenReminder",
				"ArchSky assumes a dynamic global-illumination path. With Lumen off, indirect light will not follow the sun."),
			LOCTEXT("LumenReminderFix",
				"Check Project Settings > Rendering > Global Illumination is set to Lumen, and Reflections likewise."));
	}

	// Most severe first, so the message log leads with what actually breaks the scene.
	Issues.Sort([](const FArchValidationIssue& A, const FArchValidationIssue& B)
	{
		return static_cast<uint8>(A.Severity) > static_cast<uint8>(B.Severity);
	});

	return Issues;
}

void UArchSkySceneValidator::ValidateAndReport(UWorld* World)
{
	FMessageLog MessageLog(MessageLogName);
	MessageLog.NewPage(LOCTEXT("ValidationPage", "ArchSky Scene Validation"));

	const TArray<FArchValidationIssue> Issues = ValidateWorld(World);

	if (Issues.IsEmpty())
	{
		MessageLog.Info(LOCTEXT("ValidationClean", "ArchSky scene setup looks correct."));
		MessageLog.Open(EMessageSeverity::Info, /*bForce*/ true);
		return;
	}

	int32 ErrorCount = 0;
	int32 WarningCount = 0;

	for (const FArchValidationIssue& Issue : Issues)
	{
		// `auto` rather than a spelled-out type: EMessageSeverity is a namespaced enum in
		// 5.4 and an enum class from 5.5, and naming EMessageSeverity::Type breaks on the
		// latter. Deduction is correct under both.
		auto Severity = EMessageSeverity::Info;
		switch (Issue.Severity)
		{
		case EArchValidationSeverity::Error:   Severity = EMessageSeverity::Error;   ++ErrorCount; break;
		case EArchValidationSeverity::Warning: Severity = EMessageSeverity::Warning; ++WarningCount; break;
		default:                               Severity = EMessageSeverity::Info;    break;
		}

		const TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(Severity);
		Message->AddToken(FTextToken::Create(Issue.Message));

		// A clickable actor token turns "which light?" into one click.
		if (AActor* RelatedActor = Issue.RelatedActor.Get())
		{
			Message->AddToken(FUObjectToken::Create(RelatedActor));
		}

		Message->AddToken(FTextToken::Create(FText::Format(
			LOCTEXT("RemedyFormat", "Fix: {0}"), Issue.Remedy)));

		MessageLog.AddMessage(Message);
	}

	UE_LOG(LogArchSky, Log, TEXT("ArchSky scene validation: %d error(s), %d warning(s), %d note(s)."),
		ErrorCount, WarningCount, Issues.Num() - ErrorCount - WarningCount);

	MessageLog.Open(ErrorCount > 0 ? EMessageSeverity::Error : EMessageSeverity::Warning, /*bForce*/ true);
}

#undef LOCTEXT_NAMESPACE
