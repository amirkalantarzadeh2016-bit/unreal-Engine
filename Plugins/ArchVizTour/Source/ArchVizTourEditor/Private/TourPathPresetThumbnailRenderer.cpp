// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourPathPresetThumbnailRenderer.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Texture2D.h"
#include "TourGeometryLibrary.h"
#include "TourPathPreset.h"

namespace ArchVizTourEditor::ThumbnailPrivate
{
	/** Samples taken along each spline segment when tracing the curve. */
	static constexpr int32 SamplesPerSegment = 12;

	/** Fraction of the tile left empty around the curve. */
	static constexpr float MarginFraction = 0.12f;

	/** Colour of the traced curve. */
	static const FLinearColor CurveColor(0.15f, 0.62f, 0.95f);

	/** Colour of the start marker. */
	static const FLinearColor StartColor(0.25f, 0.9f, 0.35f);

	/** Colour of the end marker. */
	static const FLinearColor EndColor(0.95f, 0.35f, 0.25f);

	/** Tile background. */
	static const FLinearColor BackgroundColor(0.05f, 0.06f, 0.08f);
}

bool UTourPathPresetThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	const UTourPathPreset* Preset = Cast<UTourPathPreset>(Object);
	return Preset != nullptr && Preset->PathData.Points.Num() >= 2;
}

void UTourPathPresetThumbnailRenderer::Draw(
	UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
	FRenderTarget* Viewport, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	using namespace ArchVizTourEditor::ThumbnailPrivate;

	const UTourPathPreset* Preset = Cast<UTourPathPreset>(Object);
	if (Preset == nullptr || Preset->PathData.Points.Num() < 2 || Width == 0 || Height == 0)
	{
		return;
	}

	const FTourPathData& PathData = Preset->PathData;

	Canvas->DrawTile(
		static_cast<float>(X), static_cast<float>(Y),
		static_cast<float>(Width), static_cast<float>(Height),
		0.0f, 0.0f, 1.0f, 1.0f, BackgroundColor);

	// Trace the curve once into world-space XY, then fit that to the tile. Fitting the control
	// points instead would clip wherever the curve bulges outside its own hull.
	const int32 SegmentCount = PathData.GetSegmentCount();
	const int32 SampleCount = SegmentCount * SamplesPerSegment + 1;

	TArray<FVector2D> Samples;
	Samples.Reserve(SampleCount);

	FBox2D Bounds(ForceInit);
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const float Key = static_cast<float>(SegmentCount) * static_cast<float>(Index) / static_cast<float>(SampleCount - 1);
		const FVector Position = UTourGeometryLibrary::EvaluatePositionAtKey(PathData, Key);

		const FVector2D Sample(Position.X, Position.Y);
		Samples.Add(Sample);
		Bounds += Sample;
	}

	const FVector2D Extent = Bounds.GetExtent();
	// A perfectly straight path has zero extent on one axis; guarding here keeps the scale finite.
	const double LargestExtent = FMath::Max3(Extent.X, Extent.Y, 1.0);

	const float TileSize = static_cast<float>(FMath::Min(Width, Height));
	const float DrawableSize = TileSize * (1.0f - 2.0f * MarginFraction);
	const float Scale = DrawableSize / static_cast<float>(LargestExtent * 2.0);

	const FVector2D Center = Bounds.GetCenter();
	const FVector2D TileCenter(X + Width * 0.5f, Y + Height * 0.5f);

	auto ToScreen = [&](const FVector2D& WorldPoint)
	{
		// Y is flipped: world +Y runs right in a top-down view but screen +Y runs down, and
		// without the flip every path renders mirrored against what the viewport shows.
		return FVector2D(
			TileCenter.X + static_cast<float>(WorldPoint.X - Center.X) * Scale,
			TileCenter.Y - static_cast<float>(WorldPoint.Y - Center.Y) * Scale);
	};

	for (int32 Index = 1; Index < Samples.Num(); ++Index)
	{
		FCanvasLineItem Line(ToScreen(Samples[Index - 1]), ToScreen(Samples[Index]));
		Line.SetColor(CurveColor);
		Line.LineThickness = 2.0f;
		Canvas->DrawItem(Line);
	}

	// Start and end markers, so the direction of travel is readable from the tile.
	const float MarkerSize = FMath::Max(TileSize * 0.04f, 2.0f);

	const FVector2D StartScreen = ToScreen(Samples[0]);
	Canvas->DrawTile(
		StartScreen.X - MarkerSize, StartScreen.Y - MarkerSize,
		MarkerSize * 2.0f, MarkerSize * 2.0f, 0.0f, 0.0f, 1.0f, 1.0f, StartColor);

	if (!PathData.bClosedLoop)
	{
		const FVector2D EndScreen = ToScreen(Samples.Last());
		Canvas->DrawTile(
			EndScreen.X - MarkerSize, EndScreen.Y - MarkerSize,
			MarkerSize * 2.0f, MarkerSize * 2.0f, 0.0f, 0.0f, 1.0f, 1.0f, EndColor);
	}
}
