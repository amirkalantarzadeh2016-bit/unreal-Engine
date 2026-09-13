// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Misc/Paths.h"
#include "TourRenderSettings.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArchVizTourRenderFileNameTokenTest,
	"ArchVizTour.Render.FileNameTokenExpansion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchVizTourRenderFileNameTokenTest::RunTest(const FString& Parameters)
{
	UTourRenderSettings* Settings = NewObject<UTourRenderSettings>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("Render settings"), Settings))
	{
		return false;
	}

	const FDateTime Timestamp(2026, 4, 20, 17, 5, 9);

	Settings->FileNamePattern    = TEXT("{tour}/{date}/{tour}_{frame}");
	Settings->FrameNumberPadding = 4;
	Settings->Resolution         = FIntPoint(1920, 1080);

	// --- Frame numbering ----------------------------------------------------
	TestEqual(TEXT("Frame 0 expands zero-padded"),
		Settings->BuildRelativeFileName(TEXT("Riverside"), 0, Timestamp),
		FString(TEXT("Riverside/2026-04-20/Riverside_0000")));

	TestEqual(TEXT("Frame 137 expands zero-padded"),
		Settings->BuildRelativeFileName(TEXT("Riverside"), 137, Timestamp),
		FString(TEXT("Riverside/2026-04-20/Riverside_0137")));

	TestEqual(TEXT("Negative frame indices clamp to zero"),
		Settings->BuildRelativeFileName(TEXT("Riverside"), -5, Timestamp),
		FString(TEXT("Riverside/2026-04-20/Riverside_0000")));

	// --- ffmpeg's printf pattern -------------------------------------------
	TestEqual(TEXT("The printf token matches the padding"), Settings->GetFrameNumberPrintfToken(), FString(TEXT("%04d")));

	TestEqual(TEXT("The encoder input pattern carries the printf token, not a frame number"),
		Settings->BuildRelativeFileNameWithToken(TEXT("Riverside"), Settings->GetFrameNumberPrintfToken(), Timestamp),
		FString(TEXT("Riverside/2026-04-20/Riverside_%04d")));

	// --- The video name drops the frame token entirely -----------------------
	TestEqual(TEXT("An empty frame token leaves no trailing separator"),
		Settings->BuildRelativeFileNameWithToken(TEXT("Riverside"), FString(), Timestamp),
		FString(TEXT("Riverside/2026-04-20/Riverside")));

	// The regression this guards: substituting the *rendered* frame number and then searching the
	// finished name for it would also rewrite a tour name, a date or a resolution that happened
	// to contain the same digits.
	{
		Settings->FileNamePattern = TEXT("{tour}_{frame}");

		const FString Pattern = Settings->BuildRelativeFileNameWithToken(
			TEXT("Block 0000"), Settings->GetFrameNumberPrintfToken(), Timestamp);

		TestEqual(TEXT("A tour name containing the frame digits is left alone"),
			Pattern, FString(TEXT("Block 0000_%04d")));
	}

	// --- Filesystem-hostile names -------------------------------------------
	{
		Settings->FileNamePattern = TEXT("{tour}_{frame}");

		const FString Sanitised = Settings->BuildRelativeFileName(TEXT("Phase 1: North/South?"), 3, Timestamp);
		TestFalse(TEXT("Colons are stripped from the tour name"), Sanitised.Contains(TEXT(":")));
		TestFalse(TEXT("Question marks are stripped from the tour name"), Sanitised.Contains(TEXT("?")));
		// The pattern's own slashes still create directories; only the substituted name is scrubbed.
		TestEqual(TEXT("The tour name contributes no path separators"), Sanitised,
			FString(TEXT("Phase 1_ North_South__0003")));
	}

	// --- Extensions and resolution tokens ------------------------------------
	Settings->ImageFormat = ETourImageFormat::PNG;
	TestEqual(TEXT("PNG extension"), Settings->GetImageExtension(), FString(TEXT("png")));
	Settings->ImageFormat = ETourImageFormat::EXR;
	TestEqual(TEXT("EXR extension"), Settings->GetImageExtension(), FString(TEXT("exr")));
	Settings->ImageFormat = ETourImageFormat::JPEG;
	TestEqual(TEXT("JPEG extension"), Settings->GetImageExtension(), FString(TEXT("jpg")));

	Settings->FileNamePattern = TEXT("{width}x{height}/{frame}");
	TestEqual(TEXT("Resolution tokens expand"),
		Settings->BuildRelativeFileName(TEXT("T"), 1, Timestamp), FString(TEXT("1920x1080/0001")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArchVizTourRenderSettingsSanitizeTest,
	"ArchVizTour.Render.SettingsSanitizeAndFrameCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchVizTourRenderSettingsSanitizeTest::RunTest(const FString& Parameters)
{
	UTourRenderSettings* Settings = NewObject<UTourRenderSettings>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("Render settings"), Settings))
	{
		return false;
	}

	// --- Frame count ---------------------------------------------------------
	Settings->FrameRate      = 30.0f;
	Settings->DurationSource = ETourRenderDurationSource::TourLength;

	TestEqual(TEXT("Ten seconds at 30 fps is 300 frames"), Settings->ComputeFrameCount(10.0f), 300);
	// A render of zero frames is never what the user meant, and every later division by the
	// frame count would be undefined.
	TestEqual(TEXT("A zero-length tour still renders one frame"), Settings->ComputeFrameCount(0.0f), 1);
	TestEqual(TEXT("A negative length still renders one frame"), Settings->ComputeFrameCount(-4.0f), 1);

	Settings->DurationSource          = ETourRenderDurationSource::Explicit;
	Settings->ExplicitDurationSeconds = 2.0f;
	TestEqual(TEXT("An explicit duration overrides the tour length"), Settings->ComputeFrameCount(60.0f), 60);

	// --- Sanitize ------------------------------------------------------------
	Settings->Resolution             = FIntPoint(99999, -4);
	Settings->FrameRate              = 10000.0f;
	Settings->SpatialSampleCount     = 0;
	Settings->TemporalSampleCount    = 9999;
	Settings->EngineWarmUpFrameCount = -1;
	Settings->CompressionQuality     = 0;
	Settings->EncoderQuality         = 200;
	Settings->FrameNumberPadding     = 0;
	Settings->ExplicitDurationSeconds = -1.0f;
	Settings->VideoExtension          = TEXT(".mov");

	Settings->SanitizeInPlace();

	TestEqual(TEXT("Resolution width is clamped to the 4K ceiling"), Settings->Resolution.X, 4096);
	TestEqual(TEXT("Resolution height is clamped to the floor"), Settings->Resolution.Y, 16);
	TestEqual(TEXT("Frame rate is clamped"), Settings->FrameRate, 240.0f);
	TestEqual(TEXT("Spatial sample count is clamped up"), Settings->SpatialSampleCount, 1);
	TestEqual(TEXT("Temporal sample count is clamped down"), Settings->TemporalSampleCount, 64);
	TestEqual(TEXT("Warm-up count is clamped up"), Settings->EngineWarmUpFrameCount, 0);
	TestEqual(TEXT("Compression quality is clamped up"), Settings->CompressionQuality, 1);
	TestEqual(TEXT("Encoder quality is clamped down"), Settings->EncoderQuality, 51);
	TestEqual(TEXT("Frame padding is clamped up"), Settings->FrameNumberPadding, 1);
	TestTrue(TEXT("Explicit duration is made positive"), Settings->ExplicitDurationSeconds > 0.0f);
	TestEqual(TEXT("A leading dot is stripped from the video extension"), Settings->VideoExtension, FString(TEXT("mov")));

	// --- Output directory ----------------------------------------------------
	Settings->OutputDirectory = TEXT("TourRenders");
	const FString Resolved = Settings->GetResolvedOutputDirectory();
	TestTrue(TEXT("A relative output directory resolves under the project's Saved directory"),
		Resolved.Contains(TEXT("Saved")) && Resolved.EndsWith(TEXT("TourRenders")));
	TestFalse(TEXT("The resolved output directory is absolute"), FPaths::IsRelative(Resolved));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
