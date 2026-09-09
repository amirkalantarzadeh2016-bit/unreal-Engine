// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "TourRenderSettings.generated.h"

/** Which render backend a job should use. */
UENUM(BlueprintType)
enum class ETourRenderBackend : uint8
{
	/**
	 * Pick the best backend that is actually available: Movie Render Pipeline when its plugin
	 * is packaged, and the scene-capture pipeline otherwise.
	 */
	Automatic		UMETA(DisplayName = "Automatic"),

	/**
	 * Movie Render Pipeline. Far higher quality - real anti-aliasing accumulation, motion blur,
	 * warm-up frames - but only exists when the MovieRenderPipeline plugin is enabled and
	 * packaged, and it needs a baked Level Sequence to render.
	 */
	MoviePipeline	UMETA(DisplayName = "Movie Render Pipeline"),

	/**
	 * SceneCapture2D into a render target, read back asynchronously and written by
	 * ImageWriteQueue. Always available, on every platform, with no extra plugin. Quality is
	 * whatever the live renderer produces at that resolution.
	 */
	SceneCapture	UMETA(DisplayName = "Scene Capture")
};

/** File format written for each frame. */
UENUM(BlueprintType)
enum class ETourImageFormat : uint8
{
	/** 8-bit RGBA, lossless. The default, and what ffmpeg consumes most cheaply. */
	PNG		UMETA(DisplayName = "PNG"),

	/** 8-bit RGB, lossy. Smallest files; use only for previews. */
	JPEG	UMETA(DisplayName = "JPEG"),

	/** 16-bit half float, linear. For grading; the render target switches to RGBA16f. */
	EXR		UMETA(DisplayName = "EXR")
};

/** Where a render job gets its length from. */
UENUM(BlueprintType)
enum class ETourRenderDurationSource : uint8
{
	/** Use the tour's own total duration. */
	TourLength		UMETA(DisplayName = "Tour Length"),

	/** Use ExplicitDurationSeconds, for rendering an excerpt. */
	Explicit		UMETA(DisplayName = "Explicit Duration")
};

/**
 * Everything a render job needs, as an editable object rather than a long argument list.
 *
 * A plain UObject, not a data asset: render settings are usually job-scoped and built by a UI,
 * and forcing every variation through an asset is friction with no payoff. Save one as an
 * asset only if a studio wants a shared house preset.
 */
UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "Tour Render Settings"))
class ARCHVIZTOURCAPTURE_API UTourRenderSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Output resolution in pixels. Clamped to 4096 x 4096; larger targets exhaust VRAM. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (ClampMin = "16"))
	FIntPoint Resolution = FIntPoint(1920, 1080);

	/** Frames per second of the output. Also the fixed timestep the capture loop runs at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (ClampMin = "1.0", ClampMax = "240.0"))
	float FrameRate = 30.0f;

	/** Where the render's length comes from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	ETourRenderDurationSource DurationSource = ETourRenderDurationSource::TourLength;

	/** Length in seconds when DurationSource is Explicit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (ClampMin = "0.1", Units = "s", EditCondition = "DurationSource == ETourRenderDurationSource::Explicit", EditConditionHides))
	float ExplicitDurationSeconds = 10.0f;

	/**
	 * Output directory. Relative paths resolve against the project's Saved/ directory, which
	 * exists and is writable in a packaged client; the project directory itself may not be.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	FString OutputDirectory = TEXT("TourRenders");

	/**
	 * Filename pattern, without an extension.
	 *
	 * Tokens: {tour} the tour's title, {date} the local date as YYYY-MM-DD, {time} as HH-MM-SS,
	 * {frame} the zero-padded frame number, {width} and {height} the resolution. Forward slashes
	 * create subdirectories.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	FString FileNamePattern = TEXT("{tour}/{date}/{tour}_{frame}");

	/** Zero padding applied to {frame}. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (ClampMin = "1", ClampMax = "10"))
	int32 FrameNumberPadding = 4;

	/** Per-frame image format. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	ETourImageFormat ImageFormat = ETourImageFormat::PNG;

	/** JPEG quality, 1..100. Ignored by the lossless formats. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (ClampMin = "1", ClampMax = "100"))
	int32 CompressionQuality = 95;

	/** Which backend to use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Backend")
	ETourRenderBackend Backend = ETourRenderBackend::Automatic;

	// ---------------------------------------------------------------------
	// Movie Render Pipeline quality. Ignored by the scene-capture backend.
	// ---------------------------------------------------------------------

	/** Sub-pixel jitters accumulated per output frame. The main anti-aliasing control. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Backend|Movie Render Pipeline", meta = (ClampMin = "1", ClampMax = "64"))
	int32 SpatialSampleCount = 8;

	/** Sub-frame samples accumulated per output frame. Drives motion blur quality. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Backend|Movie Render Pipeline", meta = (ClampMin = "1", ClampMax = "64"))
	int32 TemporalSampleCount = 1;

	/** Frames rendered and discarded before the first output frame, to settle temporal effects. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Backend|Movie Render Pipeline", meta = (ClampMin = "0", ClampMax = "256"))
	int32 EngineWarmUpFrameCount = 32;

	/** Fully stream in grass and foliage before each frame. Slow, and the only way to avoid pop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Backend|Movie Render Pipeline")
	bool bFlushGrass = true;

	/** Apply the engine's cinematic scalability settings for the duration of the render. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Backend|Movie Render Pipeline")
	bool bUseCinematicQuality = true;

	// ---------------------------------------------------------------------
	// Encoding
	// ---------------------------------------------------------------------

	/** Encode the frame sequence to a video file when an ffmpeg binary can be found. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encoding")
	bool bEncodeVideo = true;

	/**
	 * Path to an ffmpeg executable. Empty falls back to the editor settings' path, and then to
	 * "ffmpeg" on PATH. When nothing is found the frame sequence is kept and a warning is
	 * surfaced through OnRenderCompleted rather than the job failing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encoding", meta = (EditCondition = "bEncodeVideo"))
	FString FFmpegExecutablePath;

	/**
	 * ffmpeg argument template.
	 *
	 * Tokens: {ffmpeg} the executable, {fps} the frame rate, {input} the printf-style input
	 * pattern, {output} the output file, {quality} the CRF value.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encoding", meta = (EditCondition = "bEncodeVideo"))
	FString FFmpegArgumentTemplate = TEXT("-y -r {fps} -i \"{input}\" -c:v libx264 -preset slow -crf {quality} -pix_fmt yuv420p \"{output}\"");

	/** x264 CRF. Lower is better quality and a larger file; 18 is visually lossless. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encoding", meta = (ClampMin = "0", ClampMax = "51", EditCondition = "bEncodeVideo"))
	int32 EncoderQuality = 18;

	/** Container extension of the encoded video, without the dot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encoding", meta = (EditCondition = "bEncodeVideo"))
	FString VideoExtension = TEXT("mp4");

	/** Delete the frame sequence once the encode succeeds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encoding", meta = (EditCondition = "bEncodeVideo"))
	bool bDeleteFramesAfterEncode = false;

	// ---------------------------------------------------------------------
	// Scene composition
	// ---------------------------------------------------------------------

	/**
	 * Include UMG and HUD in the captured frames.
	 *
	 * The scene-capture backend renders the scene only and cannot include UI at all; setting
	 * this there produces a warning rather than silently doing nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	bool bIncludeUI = false;

	/** Hide every ATourPath rail mesh for the duration of the render, then restore it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	bool bHideTourRailMeshes = true;

	// ---------------------------------------------------------------------
	// Helpers
	// ---------------------------------------------------------------------

	/** Resolved, absolute output directory. Relative paths resolve under the project's Saved/. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Render")
	FString GetResolvedOutputDirectory() const;

	/** File extension for ImageFormat, without the dot. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Render")
	FString GetImageExtension() const;

	/**
	 * Expand FileNamePattern for one frame.
	 * @param TourName     Value substituted for {tour}, sanitised for the filesystem.
	 * @param FrameIndex   Value substituted for {frame}, zero-padded to FrameNumberPadding.
	 * @param Timestamp    Local time substituted for {date} and {time}.
	 * @return A path relative to the output directory, without an extension.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Render")
	FString BuildRelativeFileName(const FString& TourName, int32 FrameIndex, const FDateTime& Timestamp) const;

	/** Number of frames a job with this configuration will produce. Always at least 1. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Render")
	int32 ComputeFrameCount(float TourDurationSeconds) const;

	/** Clamp every field into a workable range and log anything that had to be corrected. */
	void SanitizeInPlace();
};
