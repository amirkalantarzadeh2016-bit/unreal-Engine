// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SplineMeshComponent.h"
#include "GameFramework/Actor.h"
#include "TourTypes.h"

#include "TourPath.generated.h"

class UMaterialInterface;
class USplineMeshComponent;
class UStaticMesh;
class UTourPathPreset;
class UTourSplineComponent;

/**
 * A camera path placed in the level.
 *
 * Data ownership is deliberately one-directional: the Points array is authoritative and the
 * USplineComponent is a derived cache rebuilt from it. The reverse arrangement - deriving
 * Points from the spline - forces a two-way sync whose two halves inevitably disagree about
 * which side just changed. SyncPointsFromSpline() is the one escape hatch, for recovering a
 * path that was edited through the stock spline tools.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup = "ArchViz Tour", meta = (DisplayName = "Tour Path"))
class ARCHVIZTOURRUNTIME_API ATourPath : public AActor
{
	GENERATED_BODY()

public:
	ATourPath();

	// ---------------------------------------------------------------------
	// Components
	// ---------------------------------------------------------------------

	/** Root component and derived cache of Points. Read it, do not author through it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tour Path")
	TObjectPtr<UTourSplineComponent> Spline;

	// ---------------------------------------------------------------------
	// Path data
	// ---------------------------------------------------------------------

	/** Authoritative camera keyframes, in actor-local space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path")
	TArray<FTourPoint> Points;

	/** Close the path back on itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path")
	bool bClosedLoop = false;

	/** Speed used for points whose own Speed is not positive, in centimetres per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path", meta = (ClampMin = "0.01", Units = "cm/s"))
	float DefaultSpeed = 200.0f;

	/** Parameters of the generator that produced Points, for regeneration from the details panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path|Generation")
	FTourArcGenerationParams GenerationParams;

	/**
	 * Reparameterisation samples per spline segment.
	 *
	 * Drives USplineComponent's own distance table, which is what makes cm/s traversal
	 * physically constant. The engine default of 10 is visibly steppy on the slow moves
	 * ArchViz work is made of.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path", meta = (ClampMin = "4", ClampMax = "256"))
	int32 ReparamStepsPerSegment = 32;

	// ---------------------------------------------------------------------
	// Rail visual
	// ---------------------------------------------------------------------

	/** Draw a physical rail along the spline. Automatically hidden while rendering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path|Rail")
	bool bShowRailMesh = false;

	/** Mesh swept along the spline. Null disables the rail regardless of bShowRailMesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path|Rail")
	TObjectPtr<UStaticMesh> RailMesh;

	/** Optional material override applied to every rail segment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path|Rail")
	TObjectPtr<UMaterialInterface> RailMaterial;

	/** Which mesh axis runs along the spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path|Rail")
	TEnumAsByte<ESplineMeshAxis::Type> RailForwardAxis = ESplineMeshAxis::X;

	/** Cross-section scale of the rail, in the mesh's own units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path|Rail")
	FVector2D RailScale = FVector2D(1.0, 1.0);

	/** Spline-mesh components generated per spline segment. Higher follows curvature more closely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path|Rail", meta = (ClampMin = "1", ClampMax = "32"))
	int32 RailSegmentsPerSplineSegment = 4;

	// ---------------------------------------------------------------------
	// Preset binding
	// ---------------------------------------------------------------------

	/** Preset targeted by the SaveToPreset / LoadFromPreset buttons. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path|Preset")
	TObjectPtr<UTourPathPreset> LinkedPreset;

	// ---------------------------------------------------------------------
	// AActor
	// ---------------------------------------------------------------------

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
#endif

	// ---------------------------------------------------------------------
	// Data conversion
	// ---------------------------------------------------------------------

	/** Package this actor's authored state as plain data, with the actor transform baked in. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Path")
	FTourPathData BuildPathData() const;

	/**
	 * Replace this actor's authored state from plain data and rebuild the spline.
	 * @param InPathData     Data to apply.
	 * @param bApplyTransform  Move the actor to InPathData.SplineWorldTransform as well.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Path")
	void ApplyPathData(const FTourPathData& InPathData, bool bApplyTransform = false);

	/** Push Points into the spline component. Cheap; safe to call every construction. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Path")
	void SyncSplineFromPoints();

	/**
	 * Rebuild Points from the spline component, preserving each point's camera data by index.
	 * Escape hatch for a path that was edited with the stock spline tools.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "ArchViz Tour|Path")
	void SyncPointsFromSpline();

	// ---------------------------------------------------------------------
	// Evaluation
	// ---------------------------------------------------------------------

	/**
	 * Sample the full camera state at a distance along the path.
	 *
	 * Arc-length parameterised through the spline's reparameterisation table, so equal
	 * distance steps are equal spatial steps no matter how the points are spaced.
	 *
	 * Rotation resolution order: explicit point rotation, then LookAt target, then the spline
	 * tangent. bUseExplicitRotation is what distinguishes an authored identity rotation from an
	 * unauthored one.
	 *
	 * @param Distance  Distance from the start, in centimetres. Clamped to the path length.
	 * @param OutState  Receives the sampled state. bValid is false when the path is degenerate.
	 * @return true when OutState is usable.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Path")
	bool EvaluateAtDistance(float Distance, FTourCameraState& OutState) const;

	/** Sample at a fractional spline input key instead of a distance. @see EvaluateAtDistance */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Path")
	bool EvaluateAtInputKey(float InputKey, FTourCameraState& OutState) const;

	/** Total traversable length of the path, in centimetres. 0 for a degenerate path. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Path")
	float GetPathLength() const;

	/**
	 * Convert a spline input key into a distance along the path.
	 * @param InputKey  Fractional spline key; clamped to the valid range.
	 * @return Distance in centimetres.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Path")
	float GetDistanceAtInputKey(float InputKey) const;

	/** Speed authored at a distance along the path, in centimetres per second. Never returns 0. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Path")
	float GetSpeedAtDistance(float Distance) const;

	/** True when the path has enough points to be traversed. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Path")
	bool IsTraversable() const;

	// ---------------------------------------------------------------------
	// Authoring actions
	// ---------------------------------------------------------------------

	/** Regenerate Points from GenerationParams, replacing whatever is there. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "ArchViz Tour|Actions")
	void GenerateArcRail();

	/** Mirror the path across the actor's local YZ plane and reverse traversal order. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "ArchViz Tour|Actions")
	void MirrorPath();

	/** Reverse traversal order, preserving the curve's shape. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "ArchViz Tour|Actions")
	void ReversePath();

	/**
	 * Drop every point onto the geometry below it.
	 * Traces down from SnapTraceStartHeight above each point and offsets the hit by SnapHeightOffset.
	 * Points with no hit are left where they are and reported.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "ArchViz Tour|Actions")
	void SnapPointsToFloor();

	/** Set every point's local Z to FlattenHeight. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "ArchViz Tour|Actions")
	void FlattenToHeight();

	/**
	 * Regenerate the rail spline meshes.
	 *
	 * Reuses the existing components rather than destroying and recreating them: component
	 * churn on every property edit costs a render-state rebuild per segment and shows up
	 * immediately as editor hitching on a long path.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "ArchViz Tour|Actions")
	void RebuildRailMeshes();

	/** Write this path into LinkedPreset. Transacted and dirties the package in the editor. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "ArchViz Tour|Actions")
	void SaveToPreset();

	/** Replace this path from LinkedPreset. Transacted in the editor. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "ArchViz Tour|Actions")
	void LoadFromPreset();

	/** Write this path into an explicit preset. @return true on success. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Path")
	bool SaveToPresetAsset(UTourPathPreset* Preset);

	/** Replace this path from an explicit preset. @return true on success. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Path")
	bool LoadFromPresetAsset(const UTourPathPreset* Preset);

	// ---------------------------------------------------------------------
	// Action parameters
	// ---------------------------------------------------------------------

	/** Height above each point that SnapPointsToFloor starts its downward trace from, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path|Actions", meta = (ClampMin = "0.0", Units = "cm"))
	float SnapTraceStartHeight = 500.0f;

	/** Maximum downward trace length used by SnapPointsToFloor, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path|Actions", meta = (ClampMin = "1.0", Units = "cm"))
	float SnapTraceLength = 10000.0f;

	/** Distance kept above the traced surface, in centimetres. Typically eye height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path|Actions", meta = (Units = "cm"))
	float SnapHeightOffset = 160.0f;

	/** Local Z used by FlattenToHeight, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path|Actions", meta = (Units = "cm"))
	float FlattenHeight = 0.0f;

	/** Hide or restore the rail meshes. Used by the render pipeline around a capture. */
	void SetRailVisible(bool bVisible);

private:
	/** Pooled rail segments. Surplus components are hidden and deactivated, never destroyed. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> RailMeshComponents;

	/**
	 * LookAt targets resolved by name, cached so a per-frame evaluation never iterates actors.
	 * Weak, so a target destroyed mid-tour degrades to the tangent instead of dangling.
	 */
	mutable TMap<FName, TWeakObjectPtr<AActor>> LookAtTargetCache;

	/** Names already reported as unresolvable, so the log is not spammed once per frame. */
	mutable TSet<FName> ReportedMissingTargets;

	/** Find an actor by name or tag. Returns null and warns once per name. */
	AActor* ResolveLookAtTarget(FName TargetName) const;

	/** Blend the camera attributes of the two points bracketing a fractional spline key. */
	void SampleCameraAttributes(float InputKey, FTourCameraState& OutState) const;

	/** Clamp a fractional spline key into the valid range, wrapping when the path is closed. */
	float NormalizeInputKey(float InputKey) const;
};
