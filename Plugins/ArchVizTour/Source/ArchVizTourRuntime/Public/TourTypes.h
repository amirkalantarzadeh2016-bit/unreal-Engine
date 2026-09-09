// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/PlayerCameraManager.h"
#include "Components/SplineComponent.h"
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/ObjectMacros.h"

#include "TourTypes.generated.h"

/**
 * How a single tour step moves the camera.
 *
 * SplineMove and StaticCamera are both first-class: a tour is an ordered array of steps that
 * freely mixes continuous spline traversal with hard cuts / blends to placed CineCameras.
 */
UENUM(BlueprintType)
enum class ETourStepType : uint8
{
	/** Traverse a sub-range of an ATourPath spline, driving the ATourCameraRig. */
	SplineMove		UMETA(DisplayName = "Spline Move"),

	/** Blend the player's view target to a placed ACineCameraActor and hold it. */
	StaticCamera	UMETA(DisplayName = "Static Camera"),

	/** Hold the current camera state for Duration seconds without moving. */
	Dwell			UMETA(DisplayName = "Dwell"),

	/** Hold, but broadcast CustomEventTag on entry so Blueprint can drive bespoke behaviour. */
	Custom			UMETA(DisplayName = "Custom")
};

/** Authoritative playback state of UTourSubsystem. */
UENUM(BlueprintType)
enum class ETourState : uint8
{
	/** No tour running. A preset may or may not be loaded. */
	Idle		UMETA(DisplayName = "Idle"),

	/** Advancing through steps. */
	Playing		UMETA(DisplayName = "Playing"),

	/** Time accumulation suspended; camera holds its last state. */
	Paused		UMETA(DisplayName = "Paused"),

	/** Inside a view-target blend at the start of a step. Progress still advances. */
	Blending	UMETA(DisplayName = "Blending"),

	/** The last step completed and bLoopTour was false. */
	Finished	UMETA(DisplayName = "Finished")
};

/** Which engine system actually moves the camera during playback. */
UENUM(BlueprintType)
enum class ETourPlaybackBackend : uint8
{
	/**
	 * Default. UTourSubsystem evaluates the step list itself every frame. Requires no baked
	 * asset, supports scrubbing / reverse / live authoring, and is the only backend that
	 * works on a preset the user is still editing.
	 */
	Procedural	UMETA(DisplayName = "Procedural"),

	/**
	 * Delegate playback to a ULevelSequencePlayer running the preset's baked
	 * ULevelSequence. Guarantees playback and offline render use bit-identical motion.
	 * Falls back to Procedural with a warning when no baked sequence is assigned.
	 */
	Sequencer	UMETA(DisplayName = "Level Sequence")
};

/** Which analytic generator produced (or should regenerate) an FTourPathData. */
UENUM(BlueprintType)
enum class ETourGeneratorType : uint8
{
	None		UMETA(DisplayName = "Hand Authored"),
	Arc			UMETA(DisplayName = "Arc"),
	Helix		UMETA(DisplayName = "Helix"),
	Orbit		UMETA(DisplayName = "Orbit"),
	DollyLine	UMETA(DisplayName = "Dolly Line")
};

/**
 * A single authored camera keyframe on a tour path.
 *
 * Tangents are stored in Unreal's Hermite convention (the derivative of the segment with
 * respect to its 0..1 parameter), which is what USplineComponent consumes directly. See
 * UTourGeometryLibrary::GenerateArc for the conversion from Bezier handle length.
 */
USTRUCT(BlueprintType)
struct ARCHVIZTOURRUNTIME_API FTourPoint
{
	GENERATED_BODY()

	/** Local-space position of the point, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FVector Location = FVector::ZeroVector;

	/** Incoming Hermite tangent, in centimetres per unit spline key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FVector ArriveTangent = FVector::ZeroVector;

	/** Outgoing Hermite tangent, in centimetres per unit spline key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FVector LeaveTangent = FVector::ZeroVector;

	/** Explicit camera orientation, in degrees. Only consulted when bUseExplicitRotation is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FRotator Rotation = FRotator::ZeroRotator;

	/**
	 * Disambiguates the "explicit point rotation -> LookAt target -> spline tangent"
	 * resolution order. Without it a hand-authored FRotator::ZeroRotator would be
	 * indistinguishable from "no rotation authored", and every point that happens to face
	 * world +X would silently fall through to the tangent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	bool bUseExplicitRotation = false;

	/** Cine camera focal length at this point, in millimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "1.0", UIMin = "8.0", UIMax = "300.0"))
	float FocalLength = 35.0f;

	/** Cine camera aperture at this point, as an f-stop (f/N). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.7", UIMin = "1.2", UIMax = "22.0"))
	float Aperture = 2.8f;

	/** Manual focus distance in centimetres. Values <= 0 leave the camera's own focus method alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0", Units = "cm"))
	float ManualFocusDistance = 0.0f;

	/** When true, the camera aims at the actor resolved from LookAtTargetName instead of using Rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bUseLookAtTarget = false;

	/** Name or tag of an actor in the level to aim at. Resolved lazily and cached. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (EditCondition = "bUseLookAtTarget"))
	FName LookAtTargetName = NAME_None;

	/** Desired traversal speed leaving this point, in centimetres per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.0", Units = "cm/s"))
	float Speed = 200.0f;

	/** Seconds to hold still on arrival at this point before continuing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.0", Units = "s"))
	float DwellTime = 0.0f;

	/** Ease-in weight, 0..1. 0 enters at full speed, 1 is a fully damped start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EaseIn = 0.0f;

	/** Ease-out weight, 0..1. 0 leaves at full speed, 1 is a fully damped stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EaseOut = 0.0f;

	/** Interpolation mode pushed to the matching USplineComponent point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	TEnumAsByte<ESplinePointType::Type> PointType = ESplinePointType::CurveCustomTangent;

	/** Human-readable label shown in the viewport visualizer and authoring UI. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FText Label;

	bool operator==(const FTourPoint& Other) const;
	bool operator!=(const FTourPoint& Other) const { return !(*this == Other); }
};

/**
 * Parameters of the analytic generator that produced an FTourPathData.
 *
 * Kept alongside the generated points so an authored path can be regenerated in place after
 * a radius or point-count tweak without the user re-entering every field.
 */
USTRUCT(BlueprintType)
struct ARCHVIZTOURRUNTIME_API FTourArcGenerationParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	ETourGeneratorType GeneratorType = ETourGeneratorType::None;

	/** Arc / helix / orbit centre in the path's local space, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (Units = "cm"))
	FVector Center = FVector::ZeroVector;

	/** Arc radius, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "1.0", Units = "cm"))
	float Radius = 500.0f;

	/** Angle of the first point measured about UpAxis, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (Units = "deg"))
	float StartAngleDeg = 0.0f;

	/** Signed sweep, in degrees. Negative sweeps run clockwise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (Units = "deg"))
	float SweepAngleDeg = 90.0f;

	/** Number of generated points. Must be >= 2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "2", UIMax = "64"))
	int32 PointCount = 5;

	/** Rotation axis of the arc. Normalised on use; a degenerate axis falls back to +Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	FVector UpAxis = FVector::UpVector;

	/** Total rise along UpAxis across the whole sweep, in centimetres. Non-zero makes a helix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (Units = "cm"))
	float HeightDelta = 0.0f;

	/** Turns for a helix. Ignored by every other generator. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "0.01"))
	float HelixTurns = 1.0f;

	/** Start point of a dolly line, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (Units = "cm"))
	FVector DollyStart = FVector::ZeroVector;

	/** End point of a dolly line, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (Units = "cm"))
	FVector DollyEnd = FVector(1000.0f, 0.0f, 0.0f);

	/** Name of the actor an Orbit generator was centred on, so the orbit can be re-centred later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	FName OrbitTargetName = NAME_None;

	/** When true, generated points aim at Center instead of following the tangent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	bool bFaceCenter = false;
};

/**
 * Pure geometry of one camera path: the points, whether it closes, and the parameters of the
 * generator that made it. Contains no UObject references so it round-trips cleanly through
 * JSON and USaveGame.
 */
USTRUCT(BlueprintType)
struct ARCHVIZTOURRUNTIME_API FTourPathData
{
	GENERATED_BODY()

	/** Ordered camera keyframes, in the space described by SplineWorldTransform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	TArray<FTourPoint> Points;

	/** When true the last point connects back to the first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	bool bClosedLoop = false;

	/** Speed used for any point whose own Speed is <= 0, in centimetres per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path", meta = (ClampMin = "0.01", Units = "cm/s"))
	float DefaultSpeed = 200.0f;

	/** Parameters of the generator that produced Points, for in-place regeneration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	FTourArcGenerationParams ArcGenerationParams;

	/** World transform the points were authored under, so a preset can be re-placed exactly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	FTransform SplineWorldTransform = FTransform::Identity;

	/** True when the data describes a curve that can actually be traversed (>= 2 points). */
	bool IsTraversable() const { return Points.Num() >= 2; }

	/** Number of interpolated segments; a closed loop has one more segment than it has gaps. */
	int32 GetSegmentCount() const;

	bool operator==(const FTourPathData& Other) const;
	bool operator!=(const FTourPathData& Other) const { return !(*this == Other); }
};

/**
 * One entry of a tour. The atomic unit the playback UI navigates with: Next / Previous move
 * between steps, never between individual spline points.
 */
USTRUCT(BlueprintType)
struct ARCHVIZTOURRUNTIME_API FTourStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step")
	ETourStepType StepType = ETourStepType::SplineMove;

	/** Label shown in the playback UI's step list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step")
	FText Label;

	/**
	 * Length of the step in seconds, before GlobalTimeScale.
	 * Values <= 0 on a SplineMove mean "derive the duration from arc length and point speed";
	 * on every other step type they are clamped up to a single frame and warned about.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step", meta = (ClampMin = "0.0", Units = "s"))
	float Duration = 5.0f;

	/** View-target blend length entering this step, in seconds. 0 is a hard cut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (ClampMin = "0.0", Units = "s"))
	float BlendTime = 1.0f;

	/** Blend shape used by APlayerController::SetViewTargetWithBlendParams. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend")
	TEnumAsByte<EViewTargetBlendFunction> BlendFunction = VTBlend_Cubic;

	/** Exponent for VTBlend_EaseIn / EaseOut / EaseInOut. Ignored by the other functions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (ClampMin = "0.0"))
	float BlendExp = 2.0f;

	/** Freeze the outgoing view target for the duration of the blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend")
	bool bLockOutgoing = false;

	/** Name or tag of the ATourPath this step traverses. SplineMove steps only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step|Spline", meta = (EditCondition = "StepType == ETourStepType::SplineMove", EditConditionHides))
	FName SplinePathRef = NAME_None;

	/**
	 * Start of the traversed sub-range, as a spline input key (point index plus fraction).
	 * Sub-ranges are what let a single authored spline serve many steps.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step|Spline", meta = (ClampMin = "0.0", EditCondition = "StepType == ETourStepType::SplineMove", EditConditionHides))
	float StartInputKey = 0.0f;

	/**
	 * End of the traversed sub-range, as a spline input key.
	 * Values <= StartInputKey mean "run to the end of the spline".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step|Spline", meta = (ClampMin = "0.0", EditCondition = "StepType == ETourStepType::SplineMove", EditConditionHides))
	float EndInputKey = 0.0f;

	/** Name or tag of the ACineCameraActor this step cuts to. StaticCamera steps only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step|Static", meta = (EditCondition = "StepType == ETourStepType::StaticCamera", EditConditionHides))
	FName StaticCameraRef = NAME_None;

	/** Automatically pause instead of advancing when this step completes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step")
	bool bPauseAtEnd = false;

	/** Broadcast through UTourSubsystem::OnCustomEventTag when the step is entered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Step")
	FGameplayTag CustomEventTag;

	bool operator==(const FTourStep& Other) const;
	bool operator!=(const FTourStep& Other) const { return !(*this == Other); }
};

/**
 * Fully resolved camera pose and lens for a single instant of a tour.
 *
 * Produced by ATourPath::EvaluateAtDistance and consumed by ATourCameraRig::ApplyState.
 * Deliberately POD-like so it can be interpolated, cached and compared without allocation.
 */
USTRUCT(BlueprintType)
struct ARCHVIZTOURRUNTIME_API FTourCameraState
{
	GENERATED_BODY()

	/** World-space camera position, in centimetres. */
	UPROPERTY(BlueprintReadWrite, Category = "Tour")
	FVector Location = FVector::ZeroVector;

	/** World-space camera orientation, in degrees. */
	UPROPERTY(BlueprintReadWrite, Category = "Tour")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Focal length in millimetres. */
	UPROPERTY(BlueprintReadWrite, Category = "Tour")
	float FocalLength = 35.0f;

	/** Aperture as an f-stop. */
	UPROPERTY(BlueprintReadWrite, Category = "Tour")
	float Aperture = 2.8f;

	/** Manual focus distance in centimetres; <= 0 means "leave the camera's focus method alone". */
	UPROPERTY(BlueprintReadWrite, Category = "Tour")
	float FocusDistance = 0.0f;

	/** False when evaluation failed (empty path, teardown); the rig then holds its last pose. */
	UPROPERTY(BlueprintReadWrite, Category = "Tour")
	bool bValid = false;

	/** Component-wise linear blend. Rotations are blended as quaternions to avoid gimbal artefacts. */
	static FTourCameraState Blend(const FTourCameraState& A, const FTourCameraState& B, float Alpha);
};
