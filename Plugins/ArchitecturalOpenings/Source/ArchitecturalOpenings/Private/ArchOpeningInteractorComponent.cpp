// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningInteractorComponent.h"

#include "ArchOpeningComponent.h"
#include "ArchOpeningFunctionLibrary.h"
#include "ArchOpeningLog.h"

#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UArchOpeningInteractorComponent::UArchOpeningInteractorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UArchOpeningInteractorComponent::BeginPlay()
{
	Super::BeginPlay();

	// Focus polling is opt-in, so an interactor that only reacts to input costs nothing per frame.
	SetComponentTickEnabled(bTrackFocusEveryFrame);
}

APlayerController* UArchOpeningInteractorComponent::ResolvePlayerController() const
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr)
	{
		return nullptr;
	}

	if (APlayerController* AsController = Cast<APlayerController>(OwnerActor))
	{
		return AsController;
	}

	if (const APawn* AsPawn = Cast<APawn>(OwnerActor))
	{
		return Cast<APlayerController>(AsPawn->GetController());
	}

	// Owner is neither: fall back to the pawn's controller if the owner is attached to one.
	if (const APawn* InstigatorPawn = OwnerActor->GetInstigator())
	{
		return Cast<APlayerController>(InstigatorPawn->GetController());
	}

	return nullptr;
}

bool UArchOpeningInteractorComponent::PerformTrace(FHitResult& OutHit, FVector& OutViewLocation) const
{
	APlayerController* Controller = ResolvePlayerController();
	if (Controller == nullptr)
	{
		return false;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	OutViewLocation = ViewLocation;

	const bool bUseCursor =
		TraceMode == EArchInteractorTraceMode::MouseCursor ||
		(TraceMode == EArchInteractorTraceMode::Auto && Controller->bShowMouseCursor);

	if (bUseCursor)
	{
		const ETraceTypeQuery TraceType = UEngineTypes::ConvertToTraceType(TraceChannel);
		return Controller->GetHitResultUnderCursorByChannel(TraceType, bTraceComplex, OutHit);
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * FMath::Max(TraceLength, 1.0f);

	FCollisionQueryParams Params(FName(TEXT("ArchOpeningInteract")), bTraceComplex);
	Params.AddIgnoredActor(GetOwner());
	if (APawn* ControlledPawn = Controller->GetPawn())
	{
		Params.AddIgnoredActor(ControlledPawn);
	}

	return World->LineTraceSingleByChannel(OutHit, ViewLocation, TraceEnd, TraceChannel, Params);
}

UArchOpeningComponent* UArchOpeningInteractorComponent::FindFocusedOpening(float& OutDistance) const
{
	OutDistance = 0.0f;

	FHitResult Hit;
	FVector ViewLocation = FVector::ZeroVector;

	if (!PerformTrace(Hit, ViewLocation))
	{
		return nullptr;
	}

	UArchOpeningComponent* Opening = UArchOpeningFunctionLibrary::ResolveOpeningFromHit(this, Hit);
	if (Opening == nullptr)
	{
		return nullptr;
	}

	OutDistance = FVector::Dist(ViewLocation, Hit.ImpactPoint);
	return Opening;
}

bool UArchOpeningInteractorComponent::TryInteract()
{
	float Distance = 0.0f;
	UArchOpeningComponent* Opening = FindFocusedOpening(Distance);

	if (Opening == nullptr)
	{
		return false;
	}

	// The opening owns its own reach: two doors in one scene can have different interaction ranges.
	if (Distance > Opening->Interaction.MaxInteractionDistance)
	{
		return false;
	}

	return Opening->HandleClickInteraction(GetOwner());
}

void UArchOpeningInteractorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bTrackFocusEveryFrame)
	{
		SetComponentTickEnabled(false);
		return;
	}

	float Distance = 0.0f;
	UArchOpeningComponent* Opening = FindFocusedOpening(Distance);

	if (Opening != nullptr && Distance > Opening->Interaction.MaxInteractionDistance)
	{
		Opening = nullptr;
	}

	if (FocusedOpening.Get() != Opening)
	{
		FocusedOpening = Opening;
		OnFocusChanged.Broadcast(Opening);
	}
}
