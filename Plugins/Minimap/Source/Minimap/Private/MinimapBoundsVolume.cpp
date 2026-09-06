#include "MinimapBoundsVolume.h"

#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "MinimapModule.h"
#include "MinimapSubsystem.h"

#if WITH_EDITOR
#include "EngineUtils.h"
#endif

AMinimapBoundsVolume::AMinimapBoundsVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	// Never replicated: calibration is authored data, identical on every machine.
	bReplicates = false;
	SetCanBeDamaged(false);

	BoundsBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsBox"));
	RootComponent = BoundsBox;

	// A sensible starting size; authors scale this, and nothing in the runtime path
	// ever assumes these numbers.
	BoundsBox->SetBoxExtent(FVector(5000.0f, 5000.0f, 2000.0f), /*bUpdateOverlaps=*/false);
	BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsBox->SetGenerateOverlapEvents(false);
	BoundsBox->SetHiddenInGame(true);
	BoundsBox->ShapeColor = FColor(80, 200, 255);
	BoundsBox->bDrawOnlyIfSelected = false;

#if WITH_EDITORONLY_DATA
	EditorSprite = CreateDefaultSubobject<UBillboardComponent>(TEXT("EditorSprite"));
	if (EditorSprite)
	{
		EditorSprite->SetupAttachment(BoundsBox);
		EditorSprite->bIsScreenSizeScaled = true;
		EditorSprite->SetHiddenInGame(true);
	}
#endif
}

void AMinimapBoundsVolume::BeginPlay()
{
	Super::BeginPlay();

	if (bApplyOnBeginPlay)
	{
		ApplyCalibration();
	}
}

FMinimapCalibration AMinimapBoundsVolume::BuildCalibration() const
{
	if (bManualOverride)
	{
		FString Reason;
		if (!ManualCalibration.IsValidCalibration(&Reason))
		{
			UE_LOG(LogMinimap, Warning,
				TEXT("'%s': manual calibration override is invalid (%s). It will be rejected by the subsystem."),
				*GetName(), *Reason);
		}
		return ManualCalibration;
	}

	FMinimapCalibration Calibration;

	if (!IsValid(BoundsBox))
	{
		// Cannot happen via the constructor, but a Blueprint child could null the root.
		UE_LOG(LogMinimap, Error,
			TEXT("'%s' has no BoundsBox; returning a default calibration that will be rejected."), *GetName());
		Calibration.WorldExtent = FVector2D::ZeroVector;
		return Calibration;
	}

	// GetScaledBoxExtent() already folds in the component AND actor scale, so a designer
	// can size the volume with either and get the same answer.
	const FVector ScaledExtent = BoundsBox->GetScaledBoxExtent();
	const FVector WorldLocation = BoundsBox->GetComponentLocation();

	Calibration.WorldCenter = FVector2D(WorldLocation.X, WorldLocation.Y);
	Calibration.WorldExtent = FVector2D(FMath::Abs(ScaledExtent.X), FMath::Abs(ScaledExtent.Y));

	// The box rotates with the actor, so the map must rotate with it too or the projection
	// and the volume would disagree the moment the volume is not axis-aligned.
	Calibration.MapYaw = bUseActorYawAsMapYaw
		? static_cast<float>(GetActorRotation().Yaw) + AdditionalMapYaw
		: AdditionalMapYaw;

	Calibration.MinZ = static_cast<float>(WorldLocation.Z - FMath::Abs(ScaledExtent.Z));
	Calibration.MaxZ = static_cast<float>(WorldLocation.Z + FMath::Abs(ScaledExtent.Z));

	Calibration.Zoom                 = FMath::Max(Zoom, 0.01f);
	Calibration.bPreserveAspectRatio = bPreserveAspectRatio;
	Calibration.bCircularMap         = bCircularMap;
	Calibration.bSwapUV              = bSwapUV;
	Calibration.bInvertU             = bInvertU;
	Calibration.bInvertV             = bInvertV;

	// Validate here as well as in the subsystem so the author gets a message naming the
	// offending actor rather than an anonymous rejection.
	FString Reason;
	if (!Calibration.IsValidCalibration(&Reason))
	{
		UE_LOG(LogMinimap, Warning,
			TEXT("'%s' produced an invalid calibration: %s (scaled box extent was %s). "
			     "Scale the BoundsBox so its X and Y extents are greater than zero."),
			*GetName(), *Reason, *ScaledExtent.ToCompactString());
	}

	return Calibration;
}

bool AMinimapBoundsVolume::ApplyCalibration()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogMinimap, Warning, TEXT("'%s': ApplyCalibration called with no world."), *GetName());
		return false;
	}

	UMinimapSubsystem* Subsystem = World->GetSubsystem<UMinimapSubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogMinimap, Warning,
			TEXT("'%s': no UMinimapSubsystem in world '%s'; calibration not applied."),
			*GetName(), *World->GetName());
		return false;
	}

	const FMinimapCalibration Calibration = BuildCalibration();

	// SetCalibration re-validates and refuses bad input, keeping the last good values.
	const bool bApplied = Subsystem->SetCalibration(Calibration);
	if (bApplied)
	{
		UE_LOG(LogMinimap, Log, TEXT("'%s': calibration applied to the minimap subsystem."), *GetName());
	}
	return bApplied;
}

#if WITH_EDITOR

void AMinimapBoundsVolume::FitToLevelBounds()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FBox LevelBounds(ForceInit);
	int32 ConsideredActors = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor == this)
		{
			continue;
		}

		// Skip actors with no meaningful geometry; including them would balloon the volume
		// around editor-only helpers and other bounds volumes.
		if (Actor->IsA<AMinimapBoundsVolume>())
		{
			continue;
		}

		FVector Origin;
		FVector Extent;
		Actor->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);

		if (Extent.IsNearlyZero())
		{
			continue;
		}

		LevelBounds += FBox(Origin - Extent, Origin + Extent);
		++ConsideredActors;
	}

	if (ConsideredActors == 0 || !LevelBounds.IsValid)
	{
		UE_LOG(LogMinimap, Warning,
			TEXT("'%s': FitToLevelBounds found no actors with usable bounds; volume unchanged."), *GetName());
		return;
	}

	Modify();

	// Reset rotation and scale so the captured extent means exactly what it says; a
	// residual scale here would silently double the mapped area.
	SetActorRotation(FRotator::ZeroRotator);
	SetActorScale3D(FVector::OneVector);
	SetActorLocation(LevelBounds.GetCenter());

	const FVector NewExtent = LevelBounds.GetExtent();
	BoundsBox->SetBoxExtent(NewExtent, /*bUpdateOverlaps=*/false);

	UE_LOG(LogMinimap, Log,
		TEXT("'%s': fitted to %d actors. Center=%s Extent=%s"),
		*GetName(), ConsideredActors,
		*LevelBounds.GetCenter().ToCompactString(), *NewExtent.ToCompactString());
}

void AMinimapBoundsVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Live-update the calibration while editing in PIE so tweaks are visible immediately.
	// Guarded to play worlds: touching the editor world's subsystem from here would fight
	// with the level's own BeginPlay-time calibration.
	const UWorld* World = GetWorld();
	if (World && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game))
	{
		ApplyCalibration();
	}
}

#endif // WITH_EDITOR
