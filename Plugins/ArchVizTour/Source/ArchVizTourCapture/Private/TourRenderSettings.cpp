// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourRenderSettings.h"

#include "ArchVizTourLog.h"
#include "Misc/Paths.h"

namespace ArchVizTour::RenderPrivate
{
	/** Above this a render target costs more VRAM than most workstations have to spare. */
	static constexpr int32 MaxRenderDimension = 4096;

	/** Strip characters that are illegal in a path on any supported platform. */
	static FString SanitizeForFileName(const FString& In)
	{
		FString Out = In;
		const TCHAR* Illegal = TEXT("\\/:*?\"<>|");
		for (const TCHAR* Cursor = Illegal; *Cursor != TEXT('\0'); ++Cursor)
		{
			Out.ReplaceCharInline(*Cursor, TEXT('_'), ESearchCase::CaseSensitive);
		}

		Out.TrimStartAndEndInline();
		return Out.IsEmpty() ? TEXT("Tour") : Out;
	}
}

FString UTourRenderSettings::GetResolvedOutputDirectory() const
{
	const FString Directory = OutputDirectory.IsEmpty() ? TEXT("TourRenders") : OutputDirectory;

	if (FPaths::IsRelative(Directory))
	{
		// Saved/ is the one directory guaranteed to exist and be writable in a packaged client;
		// the project directory beside the executable often is not.
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / Directory);
	}

	return FPaths::ConvertRelativePathToFull(Directory);
}

FString UTourRenderSettings::GetImageExtension() const
{
	switch (ImageFormat)
	{
	case ETourImageFormat::JPEG: return TEXT("jpg");
	case ETourImageFormat::EXR:  return TEXT("exr");
	case ETourImageFormat::PNG:
	default:                     return TEXT("png");
	}
}

FString UTourRenderSettings::BuildRelativeFileName(const FString& TourName, int32 FrameIndex, const FDateTime& Timestamp) const
{
	using namespace ArchVizTour::RenderPrivate;

	FString Result = FileNamePattern.IsEmpty() ? TEXT("{tour}/{date}/{tour}_{frame}") : FileNamePattern;

	const FString FrameText = FString::Printf(TEXT("%0*d"), FMath::Clamp(FrameNumberPadding, 1, 10), FMath::Max(FrameIndex, 0));

	Result.ReplaceInline(TEXT("{tour}"),   *SanitizeForFileName(TourName), ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("{date}"),   *Timestamp.ToString(TEXT("%Y-%m-%d")), ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("{time}"),   *Timestamp.ToString(TEXT("%H-%M-%S")), ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("{frame}"),  *FrameText, ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("{width}"),  *FString::FromInt(Resolution.X), ESearchCase::IgnoreCase);
	Result.ReplaceInline(TEXT("{height}"), *FString::FromInt(Resolution.Y), ESearchCase::IgnoreCase);

	return Result;
}

int32 UTourRenderSettings::ComputeFrameCount(float TourDurationSeconds) const
{
	const float Duration = (DurationSource == ETourRenderDurationSource::Explicit)
		? ExplicitDurationSeconds
		: TourDurationSeconds;

	const float Rate = FMath::Max(FrameRate, 1.0f);

	// A render of zero frames is never what the user meant, and every downstream division by
	// the frame count would be undefined.
	return FMath::Max(FMath::RoundToInt(FMath::Max(Duration, 0.0f) * Rate), 1);
}

void UTourRenderSettings::SanitizeInPlace()
{
	using namespace ArchVizTour::RenderPrivate;

	const FIntPoint RequestedResolution = Resolution;
	Resolution.X = FMath::Clamp(Resolution.X, 16, MaxRenderDimension);
	Resolution.Y = FMath::Clamp(Resolution.Y, 16, MaxRenderDimension);

	if (Resolution != RequestedResolution)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Render resolution %d x %d is outside the supported range; clamped to %d x %d."),
			RequestedResolution.X, RequestedResolution.Y, Resolution.X, Resolution.Y);
	}

	const float RequestedFrameRate = FrameRate;
	FrameRate = FMath::Clamp(FrameRate, 1.0f, 240.0f);
	if (!FMath::IsNearlyEqual(FrameRate, RequestedFrameRate))
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("Render frame rate %.2f clamped to %.2f."), RequestedFrameRate, FrameRate);
	}

	SpatialSampleCount      = FMath::Clamp(SpatialSampleCount, 1, 64);
	TemporalSampleCount     = FMath::Clamp(TemporalSampleCount, 1, 64);
	EngineWarmUpFrameCount  = FMath::Clamp(EngineWarmUpFrameCount, 0, 256);
	CompressionQuality      = FMath::Clamp(CompressionQuality, 1, 100);
	EncoderQuality          = FMath::Clamp(EncoderQuality, 0, 51);
	FrameNumberPadding      = FMath::Clamp(FrameNumberPadding, 1, 10);
	ExplicitDurationSeconds = FMath::Max(ExplicitDurationSeconds, 0.1f);

	if (VideoExtension.IsEmpty())
	{
		VideoExtension = TEXT("mp4");
	}
	VideoExtension.RemoveFromStart(TEXT("."));
}
