#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "MinimapCaptureTypes.h"
#include "MinimapFunctionLibrary.h"
#include "MinimapTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MinimapCaptureTestUtils
{
	static constexpr float Tolerance = 1.e-3f;

	/**
	 * Independent model of where a world point lands in the RENDERED IMAGE, derived from
	 * the camera basis rather than from the projection formula.
	 *
	 * With Pitch = -90 and Roll = 0:
	 *     Up    = R * (0,0,1) = ( cos Yaw, sin Yaw, 0)
	 *     Right = R * (0,1,0) = (-sin Yaw, cos Yaw, 0)
	 *
	 * OrthoWidth spans the image horizontally over 2*EffectiveExtent.X, and the render
	 * target aspect covers 2*EffectiveExtent.Y vertically. UV V grows downward, hence the
	 * negation on the Up component.
	 *
	 * If this agrees with ProjectWorldToNormalized for every convention, then the
	 * background image and the markers are aligned by construction.
	 */
	static bool ComputeImageCoords(
		const FMinimapCalibration& Calibration,
		const FVector& WorldLocation,
		FVector2D& OutImageCoords)
	{
		float CaptureYaw = 0.0f;
		FString Reason;
		if (!UMinimapFunctionLibrary::ComputeCaptureYaw(Calibration, CaptureYaw, Reason))
		{
			return false;
		}

		const double Theta = FMath::DegreesToRadians(static_cast<double>(CaptureYaw));
		const FVector2D Up(FMath::Cos(Theta), FMath::Sin(Theta));
		const FVector2D Right(-FMath::Sin(Theta), FMath::Cos(Theta));

		const FVector2D Rel = FVector2D(WorldLocation.X, WorldLocation.Y) - Calibration.WorldCenter;
		const FVector2D Extent = Calibration.GetZoomedExtent();

		OutImageCoords = FVector2D(
			 (Rel.X * Right.X + Rel.Y * Right.Y) / Extent.X,
			-(Rel.X * Up.X    + Rel.Y * Up.Y)    / Extent.Y);
		return true;
	}

	static FMinimapCalibration MakeCalibration(bool bSwapUV, bool bInvertU, bool bInvertV,
		float MapYaw, bool bPreserveAspect, const FVector2D& Extent)
	{
		FMinimapCalibration Calibration;
		Calibration.WorldCenter = FVector2D(250.0, -400.0);
		Calibration.WorldExtent = Extent;
		Calibration.MapYaw = MapYaw;
		Calibration.Zoom = 1.0f;
		Calibration.MinZ = 0.0f;
		Calibration.MaxZ = 1000.0f;
		Calibration.bPreserveAspectRatio = bPreserveAspect;
		Calibration.bSwapUV = bSwapUV;
		Calibration.bInvertU = bInvertU;
		Calibration.bInvertV = bInvertV;
		return Calibration;
	}
}

// ---------------------------------------------------------------------------
// Background and markers agree, for every supported axis convention
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapCaptureAlignmentTest,
	"Minimap.Capture.BackgroundMarkerAlignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapCaptureAlignmentTest::RunTest(const FString& Parameters)
{
	using namespace MinimapCaptureTestUtils;

	// Centre, the four edge midpoints, the four corners, and intermediate positions -
	// exactly the agreement the feature requires.
	const FVector2D Offsets[] = {
		FVector2D(0, 0),
		FVector2D(700, 0), FVector2D(0, 700), FVector2D(-700, 0), FVector2D(0, -700),
		FVector2D(1000, 1000), FVector2D(-1000, 1000), FVector2D(1000, -1000), FVector2D(-1000, -1000),
		FVector2D(500, 300), FVector2D(-250, 640)
	};
	const float MapYaws[] = { 0.0f, 37.0f, 90.0f, -125.0f, 180.0f, 270.0f };
	const FVector2D Extents[] = { FVector2D(1000, 1000), FVector2D(1000, 1400), FVector2D(1400, 1000) };

	int32 Comparisons = 0;

	for (const bool bSwapUV : { false, true })
	{
		// Only bInvertU == bInvertV is representable by a camera; the mirrored cases are
		// covered by the rejection test below.
		for (const bool bInvert : { false, true })
		{
			for (const float MapYaw : MapYaws)
			{
				for (const bool bPreserveAspect : { true, false })
				{
					for (const FVector2D& Extent : Extents)
					{
						const FMinimapCalibration Calibration =
							MakeCalibration(bSwapUV, bInvert, bInvert, MapYaw, bPreserveAspect, Extent);

						for (const FVector2D& Offset : Offsets)
						{
							const FVector World(
								Calibration.WorldCenter.X + Offset.X,
								Calibration.WorldCenter.Y + Offset.Y,
								0.0);

							// Marker path.
							const FVector2D Normalized = UMinimapFunctionLibrary::ProjectWorldToNormalized(
								Calibration, World, Calibration.WorldCenter, 0.0f);

							// Background path, derived independently from the camera basis.
							FVector2D ImageCoords;
							if (!ComputeImageCoords(Calibration, World, ImageCoords))
							{
								AddError(TEXT("ComputeCaptureYaw rejected a supported convention."));
								return false;
							}

							++Comparisons;

							if (FMath::Abs(Normalized.X - ImageCoords.X) > Tolerance ||
							    FMath::Abs(Normalized.Y - ImageCoords.Y) > Tolerance)
							{
								AddError(FString::Printf(
									TEXT("Background/marker mismatch. swap=%d invert=%d MapYaw=%.1f "
									     "preserve=%d extent=(%.0f,%.0f) offset=(%.0f,%.0f): "
									     "marker=(%.4f,%.4f) image=(%.4f,%.4f)"),
									bSwapUV ? 1 : 0, bInvert ? 1 : 0, MapYaw, bPreserveAspect ? 1 : 0,
									Extent.X, Extent.Y, Offset.X, Offset.Y,
									Normalized.X, Normalized.Y, ImageCoords.X, ImageCoords.Y));
								return false;
							}
						}
					}
				}
			}
		}
	}

	TestTrue(TEXT("Alignment was actually exercised"), Comparisons > 400);
	return true;
}

// ---------------------------------------------------------------------------
// Mirrored conventions are rejected rather than silently misaligned
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapCaptureMirrorRejectionTest,
	"Minimap.Capture.MirroredConventionRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapCaptureMirrorRejectionTest::RunTest(const FString& Parameters)
{
	using namespace MinimapCaptureTestUtils;

	// Exactly one invert flag set is a reflection. No camera orientation can produce it,
	// so capture must refuse rather than emit a flipped map.
	for (const bool bSwapUV : { false, true })
	{
		for (const float MapYaw : { 0.0f, 45.0f })
		{
			for (int32 Case = 0; Case < 2; ++Case)
			{
				const bool bInvertU = (Case == 0);
				const bool bInvertV = (Case == 1);

				const FMinimapCalibration Calibration =
					MakeCalibration(bSwapUV, bInvertU, bInvertV, MapYaw, true, FVector2D(1000, 1000));

				float CaptureYaw = -1.0f;
				FString Reason;
				const bool bSupported = UMinimapFunctionLibrary::ComputeCaptureYaw(Calibration, CaptureYaw, Reason);

				TestFalse(TEXT("Mirrored convention must be rejected"), bSupported);
				TestFalse(TEXT("Rejection must explain itself"), Reason.IsEmpty());
				TestEqual(TEXT("Rejected yaw is left at zero"), CaptureYaw, 0.0f);
			}
		}
	}

	// And the supported cases must still pass.
	for (const bool bInvert : { false, true })
	{
		FString Reason;
		const FMinimapCalibration Calibration =
			MakeCalibration(false, bInvert, bInvert, 0.0f, true, FVector2D(1000, 1000));
		TestTrue(TEXT("Matching invert flags are supported"),
			UMinimapFunctionLibrary::IsCaptureAlignmentSupported(Calibration, Reason));
	}

	return true;
}

// ---------------------------------------------------------------------------
// Coverage: ortho width, resolution aspect, non-square bounds
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapCaptureCoverageTest,
	"Minimap.Capture.CoverageAndResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapCaptureCoverageTest::RunTest(const FString& Parameters)
{
	using namespace MinimapCaptureTestUtils;

	// --- Ortho width covers the full effective extent ---------------------
	{
		const FMinimapCalibration Square =
			MakeCalibration(false, false, false, 0.0f, true, FVector2D(1000, 1000));
		TestEqual(TEXT("Ortho width spans the full extent"),
			UMinimapFunctionLibrary::GetCaptureOrthoWidth(Square), 2000.0f, Tolerance);

		// Aspect preservation pads to the LARGER axis - the documented policy: never crop
		// the level, never stretch the image.
		const FMinimapCalibration Padded =
			MakeCalibration(false, false, false, 0.0f, true, FVector2D(1000, 1400));
		TestEqual(TEXT("Padded ortho width uses the larger axis"),
			UMinimapFunctionLibrary::GetCaptureOrthoWidth(Padded), 2800.0f, Tolerance);

		// Zoom scales coverage.
		FMinimapCalibration Zoomed = Square;
		Zoomed.Zoom = 2.0f;
		TestEqual(TEXT("Zoom halves the covered width"),
			UMinimapFunctionLibrary::GetCaptureOrthoWidth(Zoomed), 1000.0f, Tolerance);
	}

	// --- Resolution matches the world aspect ------------------------------
	{
		const FMinimapCalibration SquareCal =
			MakeCalibration(false, false, false, 0.0f, true, FVector2D(1000, 1000));
		const FIntPoint SquareRes = UMinimapFunctionLibrary::ComputeCaptureResolution(SquareCal, 1024);
		TestEqual(TEXT("Square bounds give a square target"), SquareRes.X, SquareRes.Y);
		TestEqual(TEXT("Longest edge honours the requested resolution"), SquareRes.X, 1024);

		// Non-square WITHOUT aspect preservation: the render target must match the world
		// aspect, otherwise the image would stretch relative to the markers.
		const FMinimapCalibration TallCal =
			MakeCalibration(false, false, false, 0.0f, false, FVector2D(1000, 1400));
		const FIntPoint TallRes = UMinimapFunctionLibrary::ComputeCaptureResolution(TallCal, 1024);
		TestEqual(TEXT("Taller bounds give a taller target"), TallRes.Y, 1024);
		TestTrue(TEXT("Taller bounds narrow the width"), TallRes.X < TallRes.Y);

		const float WorldAspect  = 1000.0f / 1400.0f;
		const float TargetAspect = static_cast<float>(TallRes.X) / static_cast<float>(TallRes.Y);
		TestEqual(TEXT("Target aspect matches the world aspect"), TargetAspect, WorldAspect, 0.01f);

		const FMinimapCalibration WideCal =
			MakeCalibration(false, false, false, 0.0f, false, FVector2D(1400, 1000));
		const FIntPoint WideRes = UMinimapFunctionLibrary::ComputeCaptureResolution(WideCal, 1024);
		TestEqual(TEXT("Wider bounds give a wider target"), WideRes.X, 1024);
		TestTrue(TEXT("Wider bounds shorten the height"), WideRes.Y < WideRes.X);

		// Every dimension must stay legal whatever is requested.
		for (const int32 Requested : { 1, 16, 64, 1024, 4096, 100000 })
		{
			const FIntPoint Res = UMinimapFunctionLibrary::ComputeCaptureResolution(TallCal, Requested);
			TestTrue(TEXT("Resolution stays within limits"),
				Res.X >= 16 && Res.Y >= 16 && Res.X <= 8192 && Res.Y <= 8192);
			TestTrue(TEXT("Resolution dimensions are even"), Res.X % 2 == 0 && Res.Y % 2 == 0);
		}
	}

	// --- Degenerate calibrations never produce a garbage target -----------
	{
		FMinimapCalibration Degenerate =
			MakeCalibration(false, false, false, 0.0f, true, FVector2D(0, 0));
		const FIntPoint Res = UMinimapFunctionLibrary::ComputeCaptureResolution(Degenerate, 1024);
		TestTrue(TEXT("Degenerate extent still yields a legal target"), Res.X >= 16 && Res.Y >= 16);

		FString Reason;
		TestFalse(TEXT("Degenerate calibration is not capture-alignable"),
			UMinimapFunctionLibrary::IsCaptureAlignmentSupported(Degenerate, Reason));
		TestFalse(TEXT("Degenerate rejection explains itself"), Reason.IsEmpty());
	}

	return true;
}

// ---------------------------------------------------------------------------
// Settings clamping
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapCaptureSettingsTest,
	"Minimap.Capture.SettingsSanitization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapCaptureSettingsTest::RunTest(const FString& Parameters)
{
	// Defaults must preserve the existing static-image workflow exactly.
	{
		const FMinimapCaptureSettings Defaults;
		TestTrue(TEXT("Default source is the legacy static texture"),
			Defaults.BackgroundSource == EMinimapBackgroundSource::StaticTexture);
		TestTrue(TEXT("Capture Every Frame is not a policy option by default"),
			Defaults.RefreshPolicy == EMinimapRefreshPolicy::CaptureOnceWhenReady);
		TestFalse(TEXT("Show-only mode is off by default"), Defaults.bUseShowOnlyList);
		TestTrue(TEXT("Player pawn is hidden from the capture by default"), Defaults.bHideLocalPlayerPawn);
	}

	// Hostile values must be clamped, not propagated into GPU resource creation.
	{
		FMinimapCaptureSettings Settings;
		Settings.CaptureResolution       = 100000;
		Settings.AutoHeightMargin        = -500.0f;
		Settings.RelativeHeightAlpha     = 5.0f;
		Settings.CaptureDepth            = -10.0f;
		Settings.RefreshCoalesceSeconds  = -1.0f;
		Settings.PeriodicRefreshInterval = 0.0f;
		Settings.InitialCaptureDelay     = 999.0f;
		Settings.ExposureBias            = 100.0f;

		Settings.SanitizeInPlace();

		TestEqual(TEXT("Resolution clamped"), Settings.CaptureResolution, 4096);
		TestEqual(TEXT("Height margin clamped"), Settings.AutoHeightMargin, 0.0f);
		TestEqual(TEXT("Relative alpha clamped"), Settings.RelativeHeightAlpha, 1.0f);
		TestEqual(TEXT("Capture depth clamped"), Settings.CaptureDepth, 0.0f);
		TestEqual(TEXT("Coalesce window clamped"), Settings.RefreshCoalesceSeconds, 0.0f);
		TestEqual(TEXT("Periodic interval floored"), Settings.PeriodicRefreshInterval, 0.5f);
		TestEqual(TEXT("Initial delay clamped"), Settings.InitialCaptureDelay, 10.0f);
		TestEqual(TEXT("Exposure bias clamped"), Settings.ExposureBias, 8.0f);

		Settings.CaptureResolution = 1;
		Settings.SanitizeInPlace();
		TestEqual(TEXT("Resolution floored"), Settings.CaptureResolution, 64);
	}

	// The validation report must classify and count correctly.
	{
		FMinimapValidationReport Report;
		TestTrue(TEXT("Empty report is valid"), Report.IsValid());

		Report.Add(false, TEXT("a warning"));
		TestTrue(TEXT("A warning alone is still valid"), Report.IsValid());
		TestEqual(TEXT("Warning counted"), Report.WarningCount, 1);

		Report.Add(true, TEXT("an error"));
		TestFalse(TEXT("An error invalidates the report"), Report.IsValid());
		TestEqual(TEXT("Error counted"), Report.ErrorCount, 1);
		TestTrue(TEXT("Display string names the error"), Report.ToDisplayString().Contains(TEXT("an error")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
