// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/EngineTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "Templates/SubclassOf.h"

#include "ArchOpeningTypes.generated.h"

class USoundBase;
class USoundAttenuation;
class UCurveFloat;

// ---------------------------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EArchOpeningMotionType : uint8
{
	/** Leaf rotates about a hinge axis. Side hinged doors/windows, top or bottom hinged windows. */
	Hinged		UMETA(DisplayName = "Hinged"),
	/** Leaf translates along a straight local direction. Single sliding panel. */
	Sliding		UMETA(DisplayName = "Sliding")
};

/**
 * Handing, defined from the reference "outside" side of the opening.
 * Stand on the outside, look at the opening: Left means the hinge is on your left.
 * This is a local-space definition, so rotating the opening in the level does not change it.
 */
UENUM(BlueprintType)
enum class EArchOpeningHingeSide : uint8
{
	Left	UMETA(DisplayName = "Left hinged (viewed from outside)"),
	Right	UMETA(DisplayName = "Right hinged (viewed from outside)")
};

UENUM(BlueprintType)
enum class EArchOpeningSwingDirection : uint8
{
	/** Free edge travels away from the reference outside direction. */
	Inward		UMETA(DisplayName = "Opens inward"),
	/** Free edge travels toward the reference outside direction. */
	Outward		UMETA(DisplayName = "Opens outward")
};

UENUM(BlueprintType)
enum class EArchOpeningHingeAxisPreset : uint8
{
	/** Vertical hinge on the left or right jamb. Doors and casement windows. */
	VerticalSide	UMETA(DisplayName = "Vertical side hinge"),
	/** Horizontal hinge at the head. Awning window. */
	HorizontalTop	UMETA(DisplayName = "Horizontal top hinge (awning)"),
	/** Horizontal hinge at the sill. Hopper window. */
	HorizontalBottom UMETA(DisplayName = "Horizontal bottom hinge (hopper)"),
	/** Explicit local axis supplied by the user. */
	Custom			UMETA(DisplayName = "Custom local axis")
};

UENUM(BlueprintType)
enum class EArchOpeningState : uint8
{
	Closed				UMETA(DisplayName = "Closed"),
	OpeningDelay		UMETA(DisplayName = "Opening Delay"),
	HandlePreparation	UMETA(DisplayName = "Handle Preparation"),
	Opening				UMETA(DisplayName = "Opening"),
	Open				UMETA(DisplayName = "Open"),
	ClosingDelay		UMETA(DisplayName = "Closing Delay"),
	Closing				UMETA(DisplayName = "Closing"),
	Obstructed			UMETA(DisplayName = "Obstructed")
};

UENUM(BlueprintType)
enum class EArchOpeningTimingMode : uint8
{
	/** Author seconds for a full open and a full close. Default. */
	Duration	UMETA(DisplayName = "Duration (seconds)"),
	/** Author deg/s (hinged) or cm/s (sliding). Converted to a nominal full-travel duration. */
	Speed		UMETA(DisplayName = "Speed (deg/s or cm/s)")
};

UENUM(BlueprintType)
enum class EArchOpeningEasing : uint8
{
	Linear		UMETA(DisplayName = "Linear"),
	SmoothStep	UMETA(DisplayName = "Smooth Step"),
	EaseIn		UMETA(DisplayName = "Ease In (quadratic)"),
	EaseOut		UMETA(DisplayName = "Ease Out (quadratic)"),
	EaseInOut	UMETA(DisplayName = "Ease In/Out (cubic)"),
	/** Uses the assigned float curve. Falls back to Smooth Step if the curve fails validation. */
	Custom		UMETA(DisplayName = "Custom Curve")
};

UENUM(BlueprintType)
enum class EArchOpeningHandleReturn : uint8
{
	/** Lever returns to rest once the leaf starts moving. Typical interior room door. */
	ReturnAfterActuation	UMETA(DisplayName = "Return after actuation"),
	/** Handle stays actuated for as long as the opening is not closed. Typical PVC window handle. */
	RemainActuatedWhileOpen	UMETA(DisplayName = "Remain actuated while open")
};

UENUM(BlueprintType)
enum class EArchOpeningInteractionMode : uint8
{
	Disabled			UMETA(DisplayName = "Disabled"),
	ClickOnly			UMETA(DisplayName = "Click only"),
	ProximityOnly		UMETA(DisplayName = "Proximity only"),
	ClickAndProximity	UMETA(DisplayName = "Click and proximity")
};

UENUM(BlueprintType)
enum class EArchOpeningObstructionPolicy : uint8
{
	/** Obstruction checks are not performed. */
	Ignore	UMETA(DisplayName = "Ignore (no checks)"),
	/** Hold the current pose until the obstruction clears, then continue closing. */
	Stop	UMETA(DisplayName = "Stop and wait"),
	/** Back off by a configured amount of openness, wait, then retry. */
	Reopen	UMETA(DisplayName = "Reopen by amount")
};

/** Which command last drove the opening. Used to arbitrate proximity against manual control. */
UENUM(BlueprintType)
enum class EArchOpeningCommandSource : uint8
{
	Script		UMETA(DisplayName = "Script / Blueprint"),
	Click		UMETA(DisplayName = "Click interaction"),
	Proximity	UMETA(DisplayName = "Proximity volume"),
	AutoClose	UMETA(DisplayName = "Auto close"),
	Preview		UMETA(DisplayName = "Editor preview")
};

UENUM(BlueprintType)
enum class EArchOpeningPartRole : uint8
{
	Stationary	UMETA(DisplayName = "Stationary"),
	Leaf		UMETA(DisplayName = "Movable leaf"),
	Handle		UMETA(DisplayName = "Handle")
};

/** Severity used by the validation report. */
UENUM(BlueprintType)
enum class EArchOpeningIssueSeverity : uint8
{
	Info	UMETA(DisplayName = "Info"),
	Warning	UMETA(DisplayName = "Warning"),
	Error	UMETA(DisplayName = "Error")
};

// ---------------------------------------------------------------------------------------------
// Part references
// ---------------------------------------------------------------------------------------------

/**
 * A reference to one scene component the opening drives (or deliberately leaves alone), together
 * with the calibrated rest pose captured for it.
 *
 * The rest pose is stored RELATIVE TO THE CALIBRATION FRAME (see FArchOpeningCalibration), not in
 * world space, so moving or rotating the opening actor in the level carries the whole setup with
 * it without recalibration.
 *
 * The component pointer is a plain reflected pointer. Cross-actor component references inside one
 * level serialize correctly and are nulled by the garbage collector when the owning actor is
 * deleted, which is what makes deletion of an assigned part non-fatal here.
 */
USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningPartRef
{
	GENERATED_BODY()

	/** The driven component. Usually a UStaticMeshComponent, but any USceneComponent works. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assigned Parts")
	TObjectPtr<USceneComponent> Component = nullptr;

	/** Rest ("closed") transform of the component, expressed in the opening's calibration frame. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calibration")
	FTransform RestRelative = FTransform::Identity;

	/** True once RestRelative has been captured by a calibration pass. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calibration")
	bool bRestCaptured = false;

	/** Mobility the component had before the plugin promoted it to Movable, for restoration. */
	UPROPERTY()
	TEnumAsByte<EComponentMobility::Type> OriginalMobility = EComponentMobility::Static;

	/** True if this plugin changed the component's mobility. */
	UPROPERTY()
	bool bMobilityPromoted = false;

	/**
	 * Name of the component's owning actor at the moment it was assigned.
	 *
	 * Used only by duplicate detection: when an opening is duplicated on its own, the engine leaves
	 * these references pointing at the original actors, whose names still match what was recorded.
	 * When the opening is duplicated together with its meshes the engine remaps the references to
	 * the copies, which always carry new unique names. Comparing the two distinguishes the cases.
	 */
	UPROPERTY()
	FName OwnerActorNameAtAssign;

	bool IsValidPart() const { return ::IsValid(Component); }

	friend bool operator==(const FArchOpeningPartRef& A, const FArchOpeningPartRef& B)
	{
		return A.Component == B.Component;
	}
};

// ---------------------------------------------------------------------------------------------
// Calibration
// ---------------------------------------------------------------------------------------------

/**
 * The local coordinate space every motion setting is authored in.
 *
 * The calibration frame is the opening component's world rotation and location, with a uniform
 * scale applied only when the component's world scale is uniform and positive. Distances the user
 * types (hinge offsets, travel distance) are therefore in unrotated centimetres of that frame.
 */
USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningCalibration
{
	GENERATED_BODY()

	/**
	 * Local direction pointing to the reference "outside" of the opening. Handing (left/right) and
	 * inward/outward are defined against this, so they stay meaningful at any world rotation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration",
		meta = (ToolTip = "Local direction pointing to the reference outside face. Left/right handing and inward/outward are measured from here."))
	FVector OutsideDirection = FVector(1.0f, 0.0f, 0.0f);

	/** Local up direction of the opening. Used for vertical hinge presets and for handing maths. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration")
	FVector UpDirection = FVector(0.0f, 0.0f, 1.0f);

	/** True once a closed pose has been captured for every assigned part. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calibration")
	bool bCalibrated = false;

	/** Uniform scale baked into the calibration frame when the component scale was uniform. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calibration")
	float CapturedUniformScale = 1.0f;

	/**
	 * World transform of the calibration frame at the moment the rest poses were captured.
	 *
	 * Rest poses are stored relative to this frame, so if the opening component is later moved the
	 * frame no longer matches and the calibration is stale. The plugin either transports the parts
	 * with the frame (editor, opt-in) or reports the mismatch as a validation warning.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calibration")
	FTransform CapturedFrame = FTransform::Identity;
};

// ---------------------------------------------------------------------------------------------
// Motion settings
// ---------------------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningHingedSettings
{
	GENERATED_BODY()

	/** Hinge position in the opening's calibration frame, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinged Motion",
		meta = (ToolTip = "Hinge position in the opening's local frame (cm). Use 'Snap Hinge To Leaf Edge' in the setup panel to place it from the leaf bounds."))
	FVector HingeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinged Motion")
	EArchOpeningHingeAxisPreset AxisPreset = EArchOpeningHingeAxisPreset::VerticalSide;

	/** Only meaningful for the vertical side preset; ignored for top/bottom and custom axes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinged Motion",
		meta = (EditCondition = "AxisPreset == EArchOpeningHingeAxisPreset::VerticalSide", EditConditionHides))
	EArchOpeningHingeSide HingeSide = EArchOpeningHingeSide::Left;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinged Motion",
		meta = (EditCondition = "AxisPreset != EArchOpeningHingeAxisPreset::Custom", EditConditionHides))
	EArchOpeningSwingDirection SwingDirection = EArchOpeningSwingDirection::Inward;

	/** Explicit hinge axis in the calibration frame. Normalised on use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinged Motion",
		meta = (EditCondition = "AxisPreset == EArchOpeningHingeAxisPreset::Custom", EditConditionHides))
	FVector CustomAxis = FVector(0.0f, 0.0f, 1.0f);

	/** Angle at fully open, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinged Motion",
		meta = (Units = "deg", ClampMin = "0.0", ClampMax = "179.0", UIMin = "0.0", UIMax = "170.0"))
	float OpenAngle = 90.0f;

	/** Advanced: flip the resolved rotation direction without changing the presets. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = "Hinged Motion")
	bool bInvertDirection = false;
};

USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningSlidingSettings
{
	GENERATED_BODY()

	/**
	 * Slide direction in the opening's calibration frame. The default is the observer's left when
	 * standing outside, i.e. Up x Outside.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sliding Motion")
	FVector SlideDirection = FVector(0.0f, 1.0f, 0.0f);

	/** Travel of the leaf between fully closed and fully open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sliding Motion",
		meta = (Units = "cm", ClampMin = "0.0", UIMin = "0.0", UIMax = "400.0"))
	float TravelDistance = 90.0f;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = "Sliding Motion")
	bool bInvertDirection = false;
};

// ---------------------------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningTimingSettings
{
	GENERATED_BODY()

	/** Exactly one of Duration / Speed is authoritative. Speed is converted to a duration on use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing")
	EArchOpeningTimingMode TimingMode = EArchOpeningTimingMode::Duration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing",
		meta = (Units = "s", ClampMin = "0.0", UIMin = "0.05", UIMax = "10.0",
			EditCondition = "TimingMode == EArchOpeningTimingMode::Duration", EditConditionHides))
	float OpenDuration = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing",
		meta = (Units = "s", ClampMin = "0.0", UIMin = "0.05", UIMax = "10.0",
			EditCondition = "TimingMode == EArchOpeningTimingMode::Duration", EditConditionHides))
	float CloseDuration = 1.6f;

	/**
	 * Nominal full-travel speed while opening: deg/s for hinged, cm/s for sliding.
	 * With easing enabled this defines the total time for a full travel, not the instantaneous
	 * velocity at any given moment.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing",
		meta = (ClampMin = "0.0", UIMin = "1.0", UIMax = "360.0",
			EditCondition = "TimingMode == EArchOpeningTimingMode::Speed", EditConditionHides))
	float OpenSpeed = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing",
		meta = (ClampMin = "0.0", UIMin = "1.0", UIMax = "360.0",
			EditCondition = "TimingMode == EArchOpeningTimingMode::Speed", EditConditionHides))
	float CloseSpeed = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing",
		meta = (Units = "s", ClampMin = "0.0", UIMax = "10.0"))
	float DelayBeforeOpening = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing",
		meta = (Units = "s", ClampMin = "0.0", UIMax = "10.0"))
	float DelayBeforeClosing = 0.0f;

	/** Openness the opening rests at when play begins, 0 closed to 1 fully open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float InitialOpenness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing")
	bool bAutoClose = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing",
		meta = (Units = "s", ClampMin = "0.0", UIMax = "60.0",
			EditCondition = "bAutoClose", EditConditionHides))
	float AutoCloseDelay = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing")
	EArchOpeningEasing OpeningEasing = EArchOpeningEasing::EaseInOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing")
	EArchOpeningEasing ClosingEasing = EArchOpeningEasing::EaseInOut;

	/** Must map 0->0 and 1->1 and be non-decreasing. Rejected curves fall back to Smooth Step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing",
		meta = (EditCondition = "OpeningEasing == EArchOpeningEasing::Custom", EditConditionHides))
	TObjectPtr<UCurveFloat> OpeningCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing",
		meta = (EditCondition = "ClosingEasing == EArchOpeningEasing::Custom", EditConditionHides))
	TObjectPtr<UCurveFloat> ClosingCurve = nullptr;
};

// ---------------------------------------------------------------------------------------------
// Handles
// ---------------------------------------------------------------------------------------------

/**
 * One group of hardware meshes that rotate together about a shared pivot.
 *
 * A group inherits the leaf motion (the leaf delta is applied after the handle delta), and adds a
 * local rotation of its own. Meshes listed here must NOT also be listed as leaf parts; that would
 * apply the leaf motion twice. Validation reports that case as an error.
 */
USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningHandleGroup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles")
	FName GroupName = TEXT("Handle");

	/** Meshes that rotate rigidly together in this group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles")
	TArray<FArchOpeningPartRef> Parts;

	/** Pivot position in the opening's calibration frame, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles", meta = (Units = "cm"))
	FVector PivotLocation = FVector::ZeroVector;

	/** Rotation axis in the opening's calibration frame. Normalised on use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles")
	FVector RotationAxis = FVector(1.0f, 0.0f, 0.0f);

	/** Actuated angle. Sign selects the rotation direction, so mirrored inner/outer levers differ. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles",
		meta = (Units = "deg", ClampMin = "-180.0", ClampMax = "180.0"))
	float RotationAngle = -40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles",
		meta = (Units = "s", ClampMin = "0.0", UIMax = "3.0"))
	float ActuationDuration = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles",
		meta = (Units = "s", ClampMin = "0.0", UIMax = "3.0"))
	float ReturnDuration = 0.30f;

	/**
	 * Time the leaf waits after this handle starts actuating. The opening's Handle Preparation
	 * state lasts for the longest such delay across every group that actuates for the transition.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles",
		meta = (Units = "s", ClampMin = "0.0", UIMax = "3.0"))
	float DelayBeforeLeafMovement = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles")
	EArchOpeningHandleReturn ReturnBehavior = EArchOpeningHandleReturn::ReturnAfterActuation;

	/** Whether this group also actuates at the start of a closing transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles")
	bool bActuateOnClosing = true;

	/** Current actuation alpha, 0 rest to 1 fully actuated. Transient animation state. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Diagnostics")
	float CurrentAlpha = 0.0f;

	/** Alpha the group is animating toward. Transient animation state. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Diagnostics")
	float TargetAlpha = 0.0f;
};

// ---------------------------------------------------------------------------------------------
// Interaction / proximity / obstruction / audio
// ---------------------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningInteractionSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	EArchOpeningInteractionMode Mode = EArchOpeningInteractionMode::ClickOnly;

	/** Maximum distance from the interactor's view point to the hit point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction",
		meta = (Units = "cm", ClampMin = "0.0", UIMin = "50.0", UIMax = "1000.0"))
	float MaxInteractionDistance = 250.0f;

	/** Channel the interactor traces on. Assigned meshes must block it to be clickable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	TEnumAsByte<ECollisionChannel> InteractionTraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bLeafMeshesClickable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bHandleMeshesClickable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bStationaryMeshesClickable = false;

	/**
	 * Optional extra components that resolve to this opening when clicked, for example an invisible
	 * collision proxy in front of a leaf whose glass does not block the trace channel.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = "Interaction")
	TArray<TObjectPtr<USceneComponent>> InteractionProxies;

	/** Master switch checked by every interaction entry point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bInteractionEnabled = true;
};

USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningProximitySettings
{
	GENERATED_BODY()

	/** Half-extent of the box trigger, in the opening's calibration frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity", meta = (Units = "cm"))
	FVector BoxExtent = FVector(150.0f, 120.0f, 110.0f);

	/** Offset of the trigger centre from the opening origin, in the calibration frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity", meta = (Units = "cm"))
	FVector BoxOffset = FVector(0.0f, 0.0f, 100.0f);

	/** Only actors of these classes count as occupants. Empty means "any APawn". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity")
	TArray<TSubclassOf<AActor>> AllowedOccupantClasses;

	/** Restrict occupancy to pawns currently possessed by a player controller. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity")
	bool bRequirePlayerControlledPawn = true;

	/** Object types the trigger generates overlaps against. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = "Proximity")
	TArray<TEnumAsByte<EObjectTypeQuery>> OverlapObjectTypes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity")
	bool bCloseOnExit = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity",
		meta = (Units = "s", ClampMin = "0.0", UIMax = "30.0", EditCondition = "bCloseOnExit"))
	float CloseDelay = 2.0f;
};

USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningObstructionSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstruction")
	EArchOpeningObstructionPolicy Policy = EArchOpeningObstructionPolicy::Stop;

	/** Also check while opening, not only while closing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstruction")
	bool bCheckWhileOpening = false;

	/** Only pawns count as obstructions. Turn off to treat any blocking actor as an obstruction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstruction")
	bool bOnlyPawnsObstruct = true;

	/** Channel used for the leaf sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstruction")
	TEnumAsByte<ECollisionChannel> ObstructionChannel = ECC_Pawn;

	/** Seconds between obstruction queries while moving. 0 means every animated frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstruction",
		meta = (Units = "s", ClampMin = "0.0", UIMax = "0.5"))
	float CheckInterval = 0.05f;

	/**
	 * How far ahead of the current pose the swept boxes are placed, in openness units. Larger
	 * values detect earlier at the cost of stopping further from the obstruction.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = "Obstruction",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float LookaheadOpenness = 0.05f;

	/**
	 * Number of boxes the leaf volume is split into along the hinge-to-free-edge direction. More
	 * slices approximate a swept rotation better. 1 uses a single oriented box.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = "Obstruction",
		meta = (ClampMin = "1", ClampMax = "12"))
	int32 SweepSliceCount = 4;

	/** Openness the leaf backs off by under the Reopen policy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstruction",
		meta = (ClampMin = "0.0", ClampMax = "1.0",
			EditCondition = "Policy == EArchOpeningObstructionPolicy::Reopen", EditConditionHides))
	float ReopenAmount = 0.35f;

	/** Seconds the opening waits, obstruction-free, before resuming its interrupted transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstruction",
		meta = (Units = "s", ClampMin = "0.0", UIMax = "10.0"))
	float RetryDelay = 1.0f;
};

USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningAudioSettings
{
	GENERATED_BODY()

	/** Every sound is optional. The plugin ships no audio assets and works with all of them unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> HandleActuationSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> OpenStartSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> CloseStartSound = nullptr;

	/** Looping sound played only while the leaf is actually moving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> MovementLoopSound = nullptr;

	/** Played only when the leaf actually reaches fully closed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> LatchSound = nullptr;

	/** Played when the leaf reaches fully open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> EndStopSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundAttenuation> Attenuation = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio",
		meta = (ClampMin = "0.0", UIMax = "4.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio",
		meta = (ClampMin = "0.1", UIMax = "4.0"))
	float PitchMultiplier = 1.0f;

	/** Seconds used to fade the movement loop out when motion stops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio",
		meta = (Units = "s", ClampMin = "0.0", UIMax = "2.0"))
	float LoopFadeOutTime = 0.15f;

	/**
	 * Sound origin. When true, sounds follow the first assigned leaf part (so a swinging door is
	 * audible where the leaf is). When false they play at the opening component.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bAttachToLeaf = true;

	/** Editor preview stays silent unless this is explicitly enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bPlaySoundsInEditorPreview = false;
};

// ---------------------------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningIssue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Diagnostics")
	EArchOpeningIssueSeverity Severity = EArchOpeningIssueSeverity::Info;

	/** Stable key, used to de-duplicate repeated log output. */
	UPROPERTY(BlueprintReadOnly, Category = "Diagnostics")
	FName Key;

	UPROPERTY(BlueprintReadOnly, Category = "Diagnostics")
	FText Message;

	FArchOpeningIssue() = default;
	FArchOpeningIssue(EArchOpeningIssueSeverity InSeverity, FName InKey, const FText& InMessage)
		: Severity(InSeverity), Key(InKey), Message(InMessage) {}
};

USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningValidationReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Diagnostics")
	TArray<FArchOpeningIssue> Issues;

	int32 CountOf(EArchOpeningIssueSeverity Severity) const
	{
		int32 Count = 0;
		for (const FArchOpeningIssue& Issue : Issues)
		{
			if (Issue.Severity == Severity)
			{
				++Count;
			}
		}
		return Count;
	}

	bool HasErrors() const { return CountOf(EArchOpeningIssueSeverity::Error) > 0; }

	void Add(EArchOpeningIssueSeverity Severity, FName Key, const FText& Message)
	{
		Issues.Emplace(Severity, Key, Message);
	}
};
