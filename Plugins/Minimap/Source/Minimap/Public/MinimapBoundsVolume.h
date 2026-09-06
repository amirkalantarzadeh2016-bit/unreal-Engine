#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MinimapTypes.h"
#include "MinimapBoundsVolume.generated.h"

class UBillboardComponent;
class UBoxComponent;

/**
 * Drop one of these in the level, scale its box to cover the playable area, and the minimap
 * calibrates itself on BeginPlay. This is the actor the original Blueprint reached for
 * (BP-OverlapActor) but never actually used - the ranges were hardcoded instead.
 *
 * The derived calibration is:
 *   WorldCenter = box world location (XY)
 *   WorldExtent = box scaled extent   (XY)
 *   MapYaw      = actor yaw           (so a rotated volume rotates the map with it)
 *   MinZ / MaxZ = box world Z +/- scaled extent Z
 *
 * No hardcoded world ranges appear anywhere: every number comes from the transform.
 */
UCLASS(Blueprintable, HideCategories = (Input, Replication, Collision, LOD, Cooking),
	meta = (DisplayName = "Minimap Bounds Volume"))
class MINIMAP_API AMinimapBoundsVolume : public AActor
{
	GENERATED_BODY()

public:
	AMinimapBoundsVolume();

	/** Scale this to cover the playable area. Rotate the actor to tilt the map. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Minimap")
	TObjectPtr<UBoxComponent> BoundsBox;

#if WITH_EDITORONLY_DATA
	/** Editor-only sprite so the volume is easy to find in a busy level. */
	UPROPERTY()
	TObjectPtr<UBillboardComponent> EditorSprite;
#endif

	// ---------------------------------------------------------------------
	// Behaviour
	// ---------------------------------------------------------------------

	/** Push this volume's calibration to the subsystem on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration")
	bool bApplyOnBeginPlay = true;

	/**
	 * Ignore the box entirely and publish ManualCalibration verbatim.
	 * Use when the map image was authored against known coordinates.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration")
	bool bManualOverride = false;

	/** Used instead of the box transform when bManualOverride is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (EditCondition = "bManualOverride"))
	FMinimapCalibration ManualCalibration;

	// --- Options folded into the derived calibration ----------------------

	/** Take MapYaw from this actor's yaw. Turn off to pin the map to world north. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (EditCondition = "!bManualOverride"))
	bool bUseActorYawAsMapYaw = true;

	/** Extra yaw added on top, for a map texture whose "up" is not the volume's forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (EditCondition = "!bManualOverride", Units = "deg"))
	float AdditionalMapYaw = 0.0f;

	/** Square the effective extent so world distances are undistorted on the map. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (EditCondition = "!bManualOverride"))
	bool bPreserveAspectRatio = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (EditCondition = "!bManualOverride"))
	bool bCircularMap = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (EditCondition = "!bManualOverride", ClampMin = "0.01"))
	float Zoom = 1.0f;

	// --- Axis convention --------------------------------------------------

	/**
	 * Match the legacy M_Minimap: world X -> U, world Y -> V.
	 * Leave OFF for the standard convention (world +Y -> map right, world +X -> map up).
	 * This is an axis SWAP and cannot be expressed by the invert flags below.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Axis",
		meta = (EditCondition = "!bManualOverride"))
	bool bSwapUV = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Axis",
		meta = (EditCondition = "!bManualOverride"))
	bool bInvertU = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Axis",
		meta = (EditCondition = "!bManualOverride"))
	bool bInvertV = false;

	// ---------------------------------------------------------------------
	// API
	// ---------------------------------------------------------------------

	/** Build the calibration this volume represents. Does not touch the subsystem. */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	FMinimapCalibration BuildCalibration() const;

	/** Build and publish to the subsystem. Returns false if invalid or unavailable. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Minimap")
	bool ApplyCalibration();

#if WITH_EDITOR
	/**
	 * Editor-only convenience: resize the box to encompass every actor in the level.
	 *
	 * This DOES iterate all actors, which is exactly what the performance rules forbid
	 * at runtime. It is safe here because it is a button an author presses in the editor,
	 * never part of the update path, and it is compiled out of packaged builds entirely.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Minimap")
	void FitToLevelBounds();

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;
};
