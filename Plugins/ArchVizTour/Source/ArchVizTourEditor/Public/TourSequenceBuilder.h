// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TourTypes.h"
#include "UObject/Object.h"

#include "TourSequenceBuilder.generated.h"

class ULevelSequence;
class UMovieScene;
class UTourSequencePreset;

/**
 * Converts between a UTourSequencePreset and a ULevelSequence asset.
 *
 * Editor-only: creating an asset package requires the editor, and there is no reason a
 * packaged client would ever need to bake one - it plays either the procedural step list or a
 * sequence that was baked before packaging.
 *
 * The bake is what makes "the same motion for playback and for the render" true: with a baked
 * sequence, ETourPlaybackBackend::Sequencer and the Movie Render Pipeline render backend read
 * the identical keys, rather than each re-deriving the motion from the step list.
 */
UCLASS()
class ARCHVIZTOUREDITOR_API UTourSequenceBuilder : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Bake a tour into a new (or existing) ULevelSequence asset.
	 *
	 * Produces a spawnable CineCameraActor with:
	 *  - a UMovieScene3DTransformTrack sampled from the tour's own evaluation, so the baked
	 *    motion is the procedural motion rather than an approximation of it;
	 *  - UMovieSceneFloatTracks for focal length and aperture on the camera component;
	 *  - a camera cut track, so the sequence takes the view when it plays.
	 *
	 * Keys use RCIM_Cubic with tangent weights derived from each point's EaseIn / EaseOut, so
	 * the curve editor shows the same acceleration the procedural backend applies.
	 *
	 * @param Preset         Tour to bake. Must have at least one step.
	 * @param bLinkToPreset  Write the new sequence into Preset->BakedSequence.
	 * @param OutputPackagePath  Content path for the new asset. Empty uses the editor settings' default.
	 * @return The sequence, or null on failure. Failures are logged to LogArchVizTour.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Sequencer")
	static ULevelSequence* BuildLevelSequence(UTourSequencePreset* Preset, bool bLinkToPreset = true, const FString& OutputPackagePath = FString());

	/**
	 * Rebuild an editable preset from a baked sequence.
	 *
	 * Round-trips the parts of the tour a sequence can carry: overall length, frame rate and the
	 * camera cut boundaries, which become step boundaries. Anything a sequence has no concept of
	 * - which ATourPath a step traverses, its sub-range, its custom event tag - is preserved from
	 * the existing preset rather than being invented, which is why a target preset is required.
	 *
	 * @param Sequence  Baked sequence to read.
	 * @param InOutPreset  Preset updated in place.
	 * @return true when the preset was updated.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Sequencer")
	static bool ImportFromLevelSequence(ULevelSequence* Sequence, UTourSequencePreset* InOutPreset);

	/**
	 * Sample a tour into a flat list of camera states at a fixed rate.
	 *
	 * Shared by the bake and by anything else that needs the tour as a curve rather than as a
	 * step list. Requires a world, because the paths a step references are level actors.
	 *
	 * @param WorldContextObject  Any object in the world holding the tour's paths.
	 * @param Preset      Tour to sample.
	 * @param SampleRate  Samples per second.
	 * @param OutStates   Receives one state per sample.
	 * @param OutTimes    Receives the time of each sample, in seconds.
	 * @return true when at least two samples were produced.
	 */
	static bool SampleTour(
		const UObject* WorldContextObject,
		UTourSequencePreset* Preset,
		float SampleRate,
		TArray<FTourCameraState>& OutStates,
		TArray<float>& OutTimes);
};
