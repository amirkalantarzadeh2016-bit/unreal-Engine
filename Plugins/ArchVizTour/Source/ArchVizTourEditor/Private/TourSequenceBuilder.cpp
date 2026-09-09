// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourSequenceBuilder.h"

#include "ArchVizTourLog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "IAssetTools.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneTimeHelpers.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "TourCameraRig.h"
#include "TourEditorSettings.h"
#include "TourGeometryLibrary.h"
#include "TourPath.h"
#include "TourSequencePreset.h"
#include "TourSubsystem.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "ArchVizTourEditor"

namespace ArchVizTourEditor::SequenceBuilderPrivate
{
	/**
	 * Samples per second written into the baked sequence.
	 *
	 * 30 is enough for the curve editor to reproduce the tour's motion while keeping the key
	 * count workable: a five-minute tour bakes to 9,000 keys per channel at this rate, and
	 * doubling it doubles that for motion that is already smooth.
	 */
	static constexpr float BakeSampleRate = 30.0f;

	/** Tick resolution of the generated movie scene. Matches Sequencer's own default. */
	static const FFrameRate TickResolution(24000, 1);

	/** Add a cubic key with tangent weights derived from an ease pair. */
	static void AddDoubleKey(FMovieSceneDoubleChannel& Channel, FFrameNumber Frame, double Value, float EaseIn, float EaseOut)
	{
		FMovieSceneDoubleValue KeyValue(Value);
		KeyValue.InterpMode = RCIM_Cubic;

		if (EaseIn > 0.0f || EaseOut > 0.0f)
		{
			// Weighted tangents are how the curve editor represents the same cubic-Bezier ease
			// UTourGeometryLibrary::EvaluateEase applies, so the baked curve accelerates exactly
			// like procedural playback rather than merely looking similar.
			KeyValue.TangentMode = RCTM_User;
			KeyValue.Tangent.TangentWeightMode = RCTWM_WeightedBoth;
			KeyValue.Tangent.ArriveTangentWeight = EaseIn;
			KeyValue.Tangent.LeaveTangentWeight  = EaseOut;
		}
		else
		{
			KeyValue.TangentMode = RCTM_Auto;
		}

		Channel.GetData().UpdateOrAddKey(Frame, KeyValue);
	}

	/** Add a cubic key to a float channel. */
	static void AddFloatKey(FMovieSceneFloatChannel& Channel, FFrameNumber Frame, float Value)
	{
		FMovieSceneFloatValue KeyValue(Value);
		KeyValue.InterpMode = RCIM_Cubic;
		KeyValue.TangentMode = RCTM_Auto;
		Channel.GetData().UpdateOrAddKey(Frame, KeyValue);
	}

	/** Content path new sequences are written into. */
	static FString ResolvePackagePath(const FString& Requested)
	{
		if (!Requested.IsEmpty())
		{
			return Requested;
		}

		const FString Configured = UTourEditorSettings::Get().DefaultSequenceBakeDirectory.Path;
		return Configured.IsEmpty() ? TEXT("/Game/Cinematics/TourSequences") : Configured;
	}

	/** The world the editor currently has open, which is where the tour's paths live. */
	static UWorld* GetEditorWorld()
	{
		return GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	}
}

// ---------------------------------------------------------------------------
// Sampling
// ---------------------------------------------------------------------------

bool UTourSequenceBuilder::SampleTour(
	const UObject* WorldContextObject,
	UTourSequencePreset* Preset,
	float SampleRate,
	TArray<FTourCameraState>& OutStates,
	TArray<float>& OutTimes)
{
	OutStates.Reset();
	OutTimes.Reset();

	if (Preset == nullptr || !Preset->IsPlayable())
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("SampleTour: the tour is missing or has no steps."));
		return false;
	}

	UTourSubsystem* Subsystem = UTourSubsystem::Get(WorldContextObject);
	if (Subsystem == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("SampleTour: no tour subsystem for the supplied world. Open the level holding the tour's paths first."));
		return false;
	}

	SampleRate = FMath::Max(SampleRate, 1.0f);

	// The tour is sampled through the subsystem rather than re-implemented here, which is the
	// only way the baked curve is guaranteed to match what playback produces: any second
	// implementation of the step/dwell/ease timeline would eventually drift from the first.
	if (!Subsystem->LoadTour(Preset))
	{
		return false;
	}

	float TotalSeconds = 0.0f;
	float ElapsedSeconds = 0.0f;
	Subsystem->GetTourTimes(ElapsedSeconds, TotalSeconds);

	if (TotalSeconds <= UE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("SampleTour: tour '%s' has no duration."), *Preset->GetName());
		Subsystem->StopTour();
		return false;
	}

	const int32 SampleCount = FMath::Max(FMath::CeilToInt(TotalSeconds * SampleRate), 2);

	OutStates.Reserve(SampleCount);
	OutTimes.Reserve(SampleCount);

	ATourCameraRig* Rig = Subsystem->GetOrSpawnCameraRig();
	if (Rig == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("SampleTour: could not create a camera rig to sample through."));
		Subsystem->StopTour();
		return false;
	}

	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(SampleCount - 1);

		// Scrubbing rather than ticking: a scrub is deterministic and independent of frame rate,
		// which is exactly what a bake needs.
		Subsystem->ScrubToAlpha(Alpha);

		OutStates.Add(Rig->GetLastAppliedState());
		OutTimes.Add(Alpha * TotalSeconds);
	}

	Subsystem->StopTour();

	return OutStates.Num() >= 2;
}

// ---------------------------------------------------------------------------
// Bake
// ---------------------------------------------------------------------------

ULevelSequence* UTourSequenceBuilder::BuildLevelSequence(UTourSequencePreset* Preset, bool bLinkToPreset, const FString& OutputPackagePath)
{
	using namespace ArchVizTourEditor::SequenceBuilderPrivate;

	if (Preset == nullptr || !Preset->IsPlayable())
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("BuildLevelSequence: the tour is missing or has no steps."));
		return nullptr;
	}

	UWorld* EditorWorld = GetEditorWorld();
	if (EditorWorld == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("BuildLevelSequence: no editor world is open."));
		return nullptr;
	}

	TArray<FTourCameraState> States;
	TArray<float> Times;
	if (!SampleTour(EditorWorld, Preset, BakeSampleRate, States, Times))
	{
		return nullptr;
	}

	// --- Create the asset ---------------------------------------------------
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	const FString PackagePath = ResolvePackagePath(OutputPackagePath);
	const FString BaseName = Preset->GetName() + TEXT("_Seq");

	FString UniquePackageName;
	FString UniqueAssetName;
	AssetTools.CreateUniqueAssetName(PackagePath / BaseName, FString(), UniquePackageName, UniqueAssetName);

	UPackage* Package = CreatePackage(*UniquePackageName);
	if (Package == nullptr)
	{
		UE_LOG(LogArchVizTour, Error, TEXT("BuildLevelSequence: could not create package '%s'."), *UniquePackageName);
		return nullptr;
	}

	ULevelSequence* Sequence = NewObject<ULevelSequence>(Package, *UniqueAssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (Sequence == nullptr)
	{
		UE_LOG(LogArchVizTour, Error, TEXT("BuildLevelSequence: could not create the level sequence."));
		return nullptr;
	}

	Sequence->Initialize();

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	check(MovieScene != nullptr);

	const FFrameRate DisplayRate(FMath::RoundToInt(BakeSampleRate), 1);
	MovieScene->SetDisplayRate(DisplayRate);
	MovieScene->SetTickResolution(TickResolution);

	const float TotalSeconds = Times.Last();
	const FFrameNumber StartFrame = 0;
	const FFrameNumber EndFrame = TickResolution.AsFrameNumber(TotalSeconds);
	MovieScene->SetPlaybackRange(TRange<FFrameNumber>(StartFrame, EndFrame + 1));

	// --- Spawnable camera ---------------------------------------------------
	// Spawnable rather than possessable: the sequence has to be renderable on its own, without
	// depending on a particular camera actor existing in whichever level it is opened in.
	ACineCameraActor* CameraTemplate = NewObject<ACineCameraActor>(GetTransientPackage(), ACineCameraActor::StaticClass(), TEXT("TourCamera"), RF_Transactional);
	check(CameraTemplate != nullptr);

	const FGuid CameraGuid = MovieScene->AddSpawnable(TEXT("TourCamera"), *CameraTemplate);
	if (!CameraGuid.IsValid())
	{
		UE_LOG(LogArchVizTour, Error, TEXT("BuildLevelSequence: could not add a spawnable camera to the movie scene."));
		return nullptr;
	}

	// --- Transform track ----------------------------------------------------
	UMovieScene3DTransformTrack* TransformTrack = MovieScene->AddTrack<UMovieScene3DTransformTrack>(CameraGuid);
	check(TransformTrack != nullptr);

	UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(TransformTrack->CreateNewSection());
	check(TransformSection != nullptr);
	TransformSection->SetRange(TRange<FFrameNumber>::All());
	TransformTrack->AddSection(*TransformSection);

	// Channels 0-2 are translation, 3-5 rotation, 6-8 scale. Scale is left unkeyed: a camera
	// with keyed unit scale is noise in the curve editor.
	TArrayView<FMovieSceneDoubleChannel*> TransformChannels =
		TransformSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();

	if (TransformChannels.Num() < 6)
	{
		UE_LOG(LogArchVizTour, Error, TEXT("BuildLevelSequence: the transform section exposed %d channels; six were expected."), TransformChannels.Num());
		return nullptr;
	}

	// --- Lens tracks --------------------------------------------------------
	// Focal length and aperture live on the camera *component*, so they need a component
	// binding of their own rather than tracks on the actor.
	UMovieSceneFloatTrack* FocalLengthTrack = nullptr;
	UMovieSceneFloatTrack* ApertureTrack = nullptr;
	UMovieSceneFloatSection* FocalLengthSection = nullptr;
	UMovieSceneFloatSection* ApertureSection = nullptr;

	if (const UCineCameraComponent* CameraComponent = CameraTemplate->GetCineCameraComponent())
	{
		const FGuid ComponentGuid = MovieScene->AddPossessable(CameraComponent->GetName(), CameraComponent->GetClass());
		if (ComponentGuid.IsValid())
		{
			MovieScene->FindPossessable(ComponentGuid)->SetParent(CameraGuid, MovieScene);
			Sequence->BindPossessableObject(ComponentGuid, *CameraComponent, CameraTemplate);

			FocalLengthTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(ComponentGuid);
			FocalLengthTrack->SetPropertyNameAndPath(TEXT("CurrentFocalLength"), TEXT("CurrentFocalLength"));
			FocalLengthSection = Cast<UMovieSceneFloatSection>(FocalLengthTrack->CreateNewSection());
			FocalLengthSection->SetRange(TRange<FFrameNumber>::All());
			FocalLengthTrack->AddSection(*FocalLengthSection);

			ApertureTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(ComponentGuid);
			ApertureTrack->SetPropertyNameAndPath(TEXT("CurrentAperture"), TEXT("CurrentAperture"));
			ApertureSection = Cast<UMovieSceneFloatSection>(ApertureTrack->CreateNewSection());
			ApertureSection->SetRange(TRange<FFrameNumber>::All());
			ApertureTrack->AddSection(*ApertureSection);
		}
		else
		{
			UE_LOG(LogArchVizTour, Warning,
				TEXT("BuildLevelSequence: could not bind the cine camera component; focal length and aperture will not be animated."));
		}
	}

	// --- Keys ---------------------------------------------------------------
	// Ease weights are taken from the step the sample falls in, so a step authored with a soft
	// start shows that softness in the curve editor rather than only in playback.
	for (int32 Index = 0; Index < States.Num(); ++Index)
	{
		const FTourCameraState& State = States[Index];
		const FFrameNumber Frame = TickResolution.AsFrameNumber(Times[Index]);

		// Only the first and last key of the whole bake carry the ease weights: intermediate
		// samples already trace the eased motion, so weighting them too would ease it twice.
		const bool bIsFirst = (Index == 0);
		const bool bIsLast  = (Index == States.Num() - 1);
		const float EaseIn  = bIsFirst ? 1.0f : 0.0f;
		const float EaseOut = bIsLast  ? 1.0f : 0.0f;

		AddDoubleKey(*TransformChannels[0], Frame, State.Location.X, EaseIn, EaseOut);
		AddDoubleKey(*TransformChannels[1], Frame, State.Location.Y, EaseIn, EaseOut);
		AddDoubleKey(*TransformChannels[2], Frame, State.Location.Z, EaseIn, EaseOut);

		AddDoubleKey(*TransformChannels[3], Frame, State.Rotation.Roll,  EaseIn, EaseOut);
		AddDoubleKey(*TransformChannels[4], Frame, State.Rotation.Pitch, EaseIn, EaseOut);
		AddDoubleKey(*TransformChannels[5], Frame, State.Rotation.Yaw,   EaseIn, EaseOut);

		if (FocalLengthSection != nullptr)
		{
			TArrayView<FMovieSceneFloatChannel*> Channels = FocalLengthSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
			if (Channels.Num() > 0)
			{
				AddFloatKey(*Channels[0], Frame, State.FocalLength);
			}
		}

		if (ApertureSection != nullptr)
		{
			TArrayView<FMovieSceneFloatChannel*> Channels = ApertureSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
			if (Channels.Num() > 0)
			{
				AddFloatKey(*Channels[0], Frame, State.Aperture);
			}
		}
	}

	// --- Camera cut ---------------------------------------------------------
	// Without a cut track the sequence animates a camera nobody is looking through, which is the
	// single most common reason a baked tour "renders black".
	if (UMovieSceneCameraCutTrack* CutTrack = Cast<UMovieSceneCameraCutTrack>(MovieScene->AddTrack(UMovieSceneCameraCutTrack::StaticClass())))
	{
		const FMovieSceneObjectBindingID BindingID = UE::MovieScene::FRelativeObjectBindingID(CameraGuid);
		if (UMovieSceneCameraCutSection* CutSection = CutTrack->AddNewCameraCut(BindingID, StartFrame))
		{
			CutSection->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame + 1));
		}
	}

	// --- Finish -------------------------------------------------------------
	FAssetRegistryModule::AssetCreated(Sequence);
	Package->MarkPackageDirty();

	if (bLinkToPreset)
	{
		Preset->Modify();
		Preset->BakedSequence = Sequence;
		Preset->MarkPackageDirty();
	}

	UE_LOG(LogArchVizTour, Log,
		TEXT("Baked tour '%s' into level sequence '%s': %.2f s, %d samples at %.0f fps."),
		*Preset->GetName(), *Sequence->GetName(), TotalSeconds, States.Num(), BakeSampleRate);

	return Sequence;
}

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

bool UTourSequenceBuilder::ImportFromLevelSequence(ULevelSequence* Sequence, UTourSequencePreset* InOutPreset)
{
	if (Sequence == nullptr || InOutPreset == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("ImportFromLevelSequence: the sequence or the target preset is null."));
		return false;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("ImportFromLevelSequence: '%s' has no movie scene."), *Sequence->GetName());
		return false;
	}

	const FFrameRate TickRate = MovieScene->GetTickResolution();
	const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();

	if (PlaybackRange.IsDegenerate() || !PlaybackRange.HasLowerBound() || !PlaybackRange.HasUpperBound())
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("ImportFromLevelSequence: '%s' has no playback range."), *Sequence->GetName());
		return false;
	}

	const double SequenceDuration = TickRate.AsSeconds(
		FFrameTime(PlaybackRange.GetUpperBoundValue() - PlaybackRange.GetLowerBoundValue()));

	// Camera cuts are the only structural boundary a sequence carries that corresponds to a
	// step. Everything else about a step - which path it traverses, its sub-range, its event tag
	// - has no representation in a sequence at all, so those fields are preserved rather than
	// invented, and this becomes a retiming operation rather than a lossy rebuild.
	TArray<TRange<FFrameNumber>> CutRanges;

	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		const UMovieSceneCameraCutTrack* CutTrack = Cast<UMovieSceneCameraCutTrack>(Track);
		if (CutTrack == nullptr)
		{
			continue;
		}

		for (const UMovieSceneSection* Section : CutTrack->GetAllSections())
		{
			if (Section != nullptr && Section->GetRange().HasLowerBound() && Section->GetRange().HasUpperBound())
			{
				CutRanges.Add(Section->GetRange());
			}
		}
	}

	CutRanges.Sort([](const TRange<FFrameNumber>& A, const TRange<FFrameNumber>& B)
	{
		return A.GetLowerBoundValue() < B.GetLowerBoundValue();
	});

	InOutPreset->Modify();

	if (CutRanges.Num() > 0 && CutRanges.Num() == InOutPreset->Steps.Num())
	{
		// Same number of cuts as steps: retime each step from its cut.
		for (int32 Index = 0; Index < CutRanges.Num(); ++Index)
		{
			const double CutSeconds = TickRate.AsSeconds(
				FFrameTime(CutRanges[Index].GetUpperBoundValue() - CutRanges[Index].GetLowerBoundValue()));

			InOutPreset->Steps[Index].Duration = static_cast<float>(CutSeconds);
		}

		UE_LOG(LogArchVizTour, Log,
			TEXT("Rebuilt tour '%s' from sequence '%s': retimed %d steps from its camera cuts."),
			*InOutPreset->GetName(), *Sequence->GetName(), CutRanges.Num());
	}
	else if (InOutPreset->Steps.Num() > 0)
	{
		// The structure diverged, so the sequence's total length is distributed across the
		// existing steps in their current proportions. Rewriting the step list from the sequence
		// would throw away every field a sequence cannot express.
		const float ExistingTotal = InOutPreset->GetTotalDuration() * FMath::Max(InOutPreset->GlobalTimeScale, UE_KINDA_SMALL_NUMBER);

		if (ExistingTotal > UE_KINDA_SMALL_NUMBER)
		{
			const float Scale = static_cast<float>(SequenceDuration) / ExistingTotal;
			for (FTourStep& Step : InOutPreset->Steps)
			{
				Step.Duration = FMath::Max(Step.Duration * Scale, 0.0f);
			}
		}

		UE_LOG(LogArchVizTour, Warning,
			TEXT("Rebuilt tour '%s' from sequence '%s': the sequence has %d camera cuts but the tour has %d steps, so the steps were scaled to the sequence's %.2f s length instead of being matched one to one."),
			*InOutPreset->GetName(), *Sequence->GetName(), CutRanges.Num(), InOutPreset->Steps.Num(), SequenceDuration);
	}
	else
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Rebuilt tour '%s' from sequence '%s': the tour has no steps to retime. A sequence cannot supply which path a step traverses, so the steps must exist first."),
			*InOutPreset->GetName(), *Sequence->GetName());
		return false;
	}

	InOutPreset->BakedSequence = Sequence;
	InOutPreset->MarkPackageDirty();

	return true;
}

#undef LOCTEXT_NAMESPACE
