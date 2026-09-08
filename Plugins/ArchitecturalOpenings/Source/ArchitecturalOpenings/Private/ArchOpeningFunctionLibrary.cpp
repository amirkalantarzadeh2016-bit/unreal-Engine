// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningFunctionLibrary.h"

#include "ArchOpeningComponent.h"
#include "ArchOpeningSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "ArchOpenings"

UArchOpeningComponent* UArchOpeningFunctionLibrary::ResolveOpeningFromComponent(const UObject* WorldContextObject, USceneComponent* Component)
{
	if (Component == nullptr)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	if (World == nullptr)
	{
		World = Component->GetWorld();
	}

	if (World != nullptr)
	{
		if (UArchOpeningSubsystem* Subsystem = World->GetSubsystem<UArchOpeningSubsystem>())
		{
			if (UArchOpeningComponent* Found = Subsystem->FindOpeningForComponent(Component))
			{
				return Found;
			}
		}
	}

	// Fallback for openings whose parts all live on the same actor as the opening component, and for
	// any opening whose BeginPlay has not run yet.
	if (AActor* OwnerActor = Component->GetOwner())
	{
		TArray<UArchOpeningComponent*> Openings;
		OwnerActor->GetComponents(Openings);

		for (UArchOpeningComponent* Opening : Openings)
		{
			if (Opening->IsComponentClickable(Component))
			{
				return Opening;
			}
		}

		if (Openings.Num() == 1)
		{
			return Openings[0];
		}
	}

	return nullptr;
}

UArchOpeningComponent* UArchOpeningFunctionLibrary::ResolveOpeningFromHit(const UObject* WorldContextObject, const FHitResult& Hit)
{
	USceneComponent* HitComponent = Hit.GetComponent();
	if (HitComponent == nullptr)
	{
		return nullptr;
	}

	UArchOpeningComponent* Opening = ResolveOpeningFromComponent(WorldContextObject, HitComponent);
	if (Opening == nullptr)
	{
		return nullptr;
	}

	// The registry knows which components belong to the opening; the clickable flags decide whether
	// this particular one counts as an interaction surface.
	return Opening->IsComponentClickable(HitComponent) ? Opening : nullptr;
}

TArray<UArchOpeningComponent*> UArchOpeningFunctionLibrary::GetOpeningsOnActor(AActor* Actor)
{
	TArray<UArchOpeningComponent*> Openings;
	if (::IsValid(Actor))
	{
		Actor->GetComponents(Openings);
	}
	return Openings;
}

FText UArchOpeningFunctionLibrary::SummarizeValidation(const FArchOpeningValidationReport& Report)
{
	const int32 Errors = Report.CountOf(EArchOpeningIssueSeverity::Error);
	const int32 Warnings = Report.CountOf(EArchOpeningIssueSeverity::Warning);

	if (Errors == 0 && Warnings == 0)
	{
		return LOCTEXT("ValidationOk", "Ready. No problems found.");
	}

	return FText::Format(
		LOCTEXT("ValidationSummaryFmt", "{0} error(s), {1} warning(s)."),
		FText::AsNumber(Errors), FText::AsNumber(Warnings));
}

#undef LOCTEXT_NAMESPACE
