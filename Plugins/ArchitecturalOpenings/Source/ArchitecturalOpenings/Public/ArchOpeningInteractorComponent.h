// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"

#include "ArchOpeningInteractorComponent.generated.h"

class APlayerController;
class UArchOpeningComponent;

UENUM(BlueprintType)
enum class EArchInteractorTraceMode : uint8
{
	/** First-person walkthrough: trace forward from the player view point. */
	CameraCenter	UMETA(DisplayName = "Centre of screen"),
	/** Presentation style with a visible cursor: trace under the mouse. */
	MouseCursor		UMETA(DisplayName = "Under mouse cursor"),
	/** Try the cursor first, fall back to the camera centre if the cursor is not shown. */
	Auto			UMETA(DisplayName = "Auto (cursor if shown, else centre)")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FArchOpeningFocusChanged, UArchOpeningComponent*, FocusedOpening);

/**
 * Optional helper for player pawns.
 *
 * Entirely opt-in: it does not replace the project's player controller, does not add input
 * mappings and does not require Enhanced Input. Bind TryInteract() to whatever input system the
 * project already uses - a legacy action binding, an Enhanced Input action handler, a UMG button,
 * or a Blueprint event. Openings work without this component; it just packages the two tracing
 * styles archviz projects usually need.
 */
UCLASS(ClassGroup = (ArchitecturalOpenings), meta = (BlueprintSpawnableComponent, DisplayName = "Architectural Opening Interactor"))
class ARCHITECTURALOPENINGS_API UArchOpeningInteractorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UArchOpeningInteractorComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactor")
	EArchInteractorTraceMode TraceMode = EArchInteractorTraceMode::Auto;

	/**
	 * Fallback reach used when no opening was hit yet. Each opening then applies its own
	 * MaxInteractionDistance, which is the value that actually gates acceptance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactor",
		meta = (Units = "cm", ClampMin = "1.0", UIMax = "2000.0"))
	float TraceLength = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactor")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactor")
	bool bTraceComplex = false;

	/** Poll the focused opening every frame so UI can show a prompt. Off by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactor")
	bool bTrackFocusEveryFrame = false;

	UPROPERTY(BlueprintAssignable, Category = "Interactor")
	FArchOpeningFocusChanged OnFocusChanged;

	/** Traces once and interacts with whatever opening was hit. Returns true if one accepted. */
	UFUNCTION(BlueprintCallable, Category = "Interactor")
	bool TryInteract();

	/** Traces once and returns the opening currently under the crosshair or cursor, if any. */
	UFUNCTION(BlueprintCallable, Category = "Interactor")
	UArchOpeningComponent* FindFocusedOpening(float& OutDistance) const;

	UFUNCTION(BlueprintPure, Category = "Interactor")
	UArchOpeningComponent* GetFocusedOpening() const { return FocusedOpening.Get(); }

	//~ Begin UActorComponent Interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void BeginPlay() override;
	//~ End UActorComponent Interface

protected:
	APlayerController* ResolvePlayerController() const;

	/** Performs the configured trace. Returns false if no controller or nothing was hit. */
	bool PerformTrace(FHitResult& OutHit, FVector& OutViewLocation) const;

	TWeakObjectPtr<UArchOpeningComponent> FocusedOpening;
};
