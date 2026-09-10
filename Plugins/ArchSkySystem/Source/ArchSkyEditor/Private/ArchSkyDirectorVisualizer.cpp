// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchSkyDirectorVisualizer.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Core/ArchSkyDirector.h"
#include "Core/ArchSkySubsystem.h"
#include "Data/ArchTimeCalendar.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "HAL/IConsoleManager.h"
#include "Math/ArchSolarMath.h"
#include "SceneManagement.h"
#include "SceneView.h"

#define LOCTEXT_NAMESPACE "ArchSkyVisualizer"

namespace ArchSkyVisualizerSettings
{
	static TAutoConsoleVariable<float> CVarSphereRadius(
		TEXT("ArchSky.Editor.SunPathRadius"),
		2000.f,
		TEXT("Radius, in centimetres, of the sun-path sphere drawn around a selected ArchSky Director."));

	static TAutoConsoleVariable<int32> CVarShowAnalemma(
		TEXT("ArchSky.Editor.ShowAnalemma"),
		0,
		TEXT("Draws the analemma (the sun's figure-eight at a fixed clock time across a year).\n")
		TEXT("  0: off (default - it costs 365 extra solar solves per draw)\n")
		TEXT("  1: on"));

	static TAutoConsoleVariable<int32> CVarShowReferenceArcs(
		TEXT("ArchSky.Editor.ShowReferenceArcs"),
		1,
		TEXT("Draws the solstice and equinox reference sun paths.\n  0: off\n  1: on (default)"));

	/** Samples per full-day arc. 15-minute resolution reads as a smooth curve. */
	static constexpr int32 ArcSampleCount = 96;

	/** Colours, chosen to survive both the light and the dark editor themes. */
	static const FLinearColor SummerArcColour(1.f, 0.78f, 0.25f, 1.f);
	static const FLinearColor EquinoxArcColour(0.85f, 0.85f, 0.85f, 1.f);
	static const FLinearColor WinterArcColour(0.45f, 0.72f, 1.f, 1.f);
	static const FLinearColor CurrentArcColour(1.f, 1.f, 1.f, 1.f);
	static const FLinearColor BelowHorizonColour(0.16f, 0.2f, 0.32f, 1.f);
	static const FLinearColor TrueNorthColour(1.f, 0.25f, 0.25f, 1.f);
	static const FLinearColor PlanNorthColour(0.25f, 0.63f, 1.f, 1.f);
	static const FLinearColor HorizonRingColour(0.4f, 0.4f, 0.45f, 1.f);
	static const FLinearColor AnalemmaColour(0.6f, 1.f, 0.6f, 1.f);
}

float FArchSkyDirectorVisualizer::GetSphereRadius()
{
	return FMath::Max(100.f, ArchSkyVisualizerSettings::CVarSphereRadius.GetValueOnGameThread());
}

const AArchSkyDirector* FArchSkyDirectorVisualizer::GetDirector(const UActorComponent* Component)
{
	if (!Component)
	{
		return nullptr;
	}

	return Cast<AArchSkyDirector>(Component->GetOwner());
}

// ---------------------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------------------

void FArchSkyDirectorVisualizer::DrawSunPathForDay(const AArchSkyDirector& Director, const UArchSkySubsystem& Subsystem,
	int32 DayOfYear, const FLinearColor& AboveHorizonColour, const FLinearColor& BelowColour,
	float Thickness, FPrimitiveDrawInterface* PDI) const
{
	using namespace ArchSkyVisualizerSettings;

	const FVector Origin = Director.GetActorLocation();
	const float Radius = GetSphereRadius();
	const float NorthOffset = Subsystem.GetSkyState().NorthOffsetDegrees;

	FVector PreviousPoint = FVector::ZeroVector;
	bool bHasPrevious = false;

	for (int32 Index = 0; Index <= ArcSampleCount; ++Index)
	{
		const float Hours = (static_cast<float>(Index) / static_cast<float>(ArcSampleCount)) * 24.f;
		const FArchSolarPosition Sample = Subsystem.GetSolarPositionAtDayAndHour(DayOfYear, Hours);

		const FVector Point = Origin + ArchSolarMath::SolarToUnrealDirectionToBody(
			Sample.AzimuthDegrees, Sample.TrueAltitudeDegrees, NorthOffset) * Radius;

		if (bHasPrevious)
		{
			// The portion of the arc below the horizon is drawn dimmer rather than
			// omitted, because seeing where the sun goes at night is what makes the
			// geometry of a low winter arc obvious.
			const bool bAbove = Sample.TrueAltitudeDegrees > 0.0;
			PDI->DrawLine(PreviousPoint, Point, bAbove ? AboveHorizonColour : BelowColour,
				SDPG_World, bAbove ? Thickness : Thickness * 0.5f);
		}

		PreviousPoint = Point;
		bHasPrevious = true;
	}
}

void FArchSkyDirectorVisualizer::DrawHourMarkers(const AArchSkyDirector& Director, const UArchSkySubsystem& Subsystem,
	FPrimitiveDrawInterface* PDI) const
{
	using namespace ArchSkyVisualizerSettings;

	const FVector Origin = Director.GetActorLocation();
	const float Radius = GetSphereRadius();
	const FArchSkyState& State = Subsystem.GetSkyState();

	PendingHourLabels.Reset();

	for (int32 Hour = 0; Hour < 24; ++Hour)
	{
		const FArchSolarPosition Sample = Subsystem.GetSolarPositionAtHour(static_cast<float>(Hour));

		// Only label the daylight hours: a ring of twenty-four labels, half of them under
		// the ground plane, is unreadable and none of the night ones mean anything.
		if (Sample.TrueAltitudeDegrees <= 0.0)
		{
			continue;
		}

		const FVector Direction = ArchSolarMath::SolarToUnrealDirectionToBody(
			Sample.AzimuthDegrees, Sample.TrueAltitudeDegrees, State.NorthOffsetDegrees);

		const FVector Point = Origin + Direction * Radius;

		// A short radial tick, so the marker reads as a position on the arc.
		PDI->DrawLine(Origin + Direction * (Radius * 0.96f), Point, CurrentArcColour, SDPG_World, 2.f);

		PendingHourLabels.Emplace(Point, FString::Printf(TEXT("%02d:00"), Hour));
	}
}

void FArchSkyDirectorVisualizer::DrawAnalemma(const AArchSkyDirector& Director, const UArchSkySubsystem& Subsystem,
	FPrimitiveDrawInterface* PDI) const
{
	using namespace ArchSkyVisualizerSettings;

	const FVector Origin = Director.GetActorLocation();
	const float Radius = GetSphereRadius();
	const FArchSkyState& State = Subsystem.GetSkyState();

	// The analemma is the locus of the sun sampled at the SAME clock time on every day of
	// the year. Its figure-eight shape is the equation of time made visible, and it is the
	// most direct way to show a client why "noon" is not when the sun is highest.
	FVector PreviousPoint = FVector::ZeroVector;
	bool bHasPrevious = false;

	const int32 DaysInYear = ArchTimeCalendar::DaysInYear(State.Year);

	// Every third day: 122 samples still draw a clean figure-eight at a third of the cost.
	for (int32 Day = 1; Day <= DaysInYear; Day += 3)
	{
		const FArchSolarPosition Sample = Subsystem.GetSolarPositionAtDayAndHour(Day, State.TimeOfDayHours);

		const FVector Point = Origin + ArchSolarMath::SolarToUnrealDirectionToBody(
			Sample.AzimuthDegrees, Sample.TrueAltitudeDegrees, State.NorthOffsetDegrees) * Radius;

		if (bHasPrevious)
		{
			PDI->DrawLine(PreviousPoint, Point, AnalemmaColour, SDPG_World, 2.f);
		}

		PreviousPoint = Point;
		bHasPrevious = true;
	}
}

void FArchSkyDirectorVisualizer::DrawCompass(const AArchSkyDirector& Director, float NorthOffsetDegrees,
	FPrimitiveDrawInterface* PDI) const
{
	using namespace ArchSkyVisualizerSettings;

	const FVector Origin = Director.GetActorLocation();
	const float Radius = GetSphereRadius();

	// Plan north is scene +X by definition; true north is +X rotated by the offset.
	const FVector PlanNorth = FVector::ForwardVector;
	const FVector TrueNorth = FRotator(0.f, NorthOffsetDegrees, 0.f).RotateVector(FVector::ForwardVector);

	auto DrawArrow = [PDI, &Origin](const FVector& Direction, float Length, const FLinearColor& Colour, float Thickness)
	{
		const FVector Tip = Origin + Direction * Length;
		PDI->DrawLine(Origin, Tip, Colour, SDPG_Foreground, Thickness);

		// Two barbs, in the horizontal plane, so the arrowhead reads from above - which is
		// the view an architect checks a north arrow from.
		const FVector Side = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
		const float BarbLength = Length * 0.12f;

		PDI->DrawLine(Tip, Tip - Direction * BarbLength + Side * BarbLength * 0.5f, Colour, SDPG_Foreground, Thickness);
		PDI->DrawLine(Tip, Tip - Direction * BarbLength - Side * BarbLength * 0.5f, Colour, SDPG_Foreground, Thickness);
	};

	// Plan north drawn shorter, so an offset of zero still shows both arrows nested rather
	// than one hiding the other completely.
	DrawArrow(PlanNorth, Radius * 0.55f, PlanNorthColour, 3.f);
	DrawArrow(TrueNorth, Radius * 0.75f, TrueNorthColour, 4.f);
}

void FArchSkyDirectorVisualizer::DrawHorizonRing(const AArchSkyDirector& Director, FPrimitiveDrawInterface* PDI) const
{
	using namespace ArchSkyVisualizerSettings;

	const FVector Origin = Director.GetActorLocation();
	const float Radius = GetSphereRadius();

	constexpr int32 RingSegments = 64;
	FVector PreviousPoint = Origin + FVector(Radius, 0.f, 0.f);

	for (int32 Index = 1; Index <= RingSegments; ++Index)
	{
		const float Angle = (static_cast<float>(Index) / static_cast<float>(RingSegments)) * 2.f * UE_PI;
		const FVector Point = Origin + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);

		PDI->DrawLine(PreviousPoint, Point, HorizonRingColour, SDPG_World, 1.f);
		PreviousPoint = Point;
	}
}

// ---------------------------------------------------------------------------------------
// FComponentVisualizer
// ---------------------------------------------------------------------------------------

void FArchSkyDirectorVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View,
	FPrimitiveDrawInterface* PDI)
{
	using namespace ArchSkyVisualizerSettings;

	const AArchSkyDirector* Director = GetDirector(Component);
	if (!Director || !PDI)
	{
		return;
	}

	// Only draw once per actor: the Director has several components and the visualiser is
	// registered against the root, but a Blueprint subclass could add more.
	if (Component != Director->GetRootComponent())
	{
		return;
	}

	const UArchSkySubsystem* Subsystem = Director->GetSkySubsystem();
	if (!Subsystem)
	{
		return;
	}

	const FArchSkyState& State = Subsystem->GetSkyState();
	const float Radius = GetSphereRadius();

	DrawHorizonRing(*Director, PDI);
	DrawCompass(*Director, State.NorthOffsetDegrees, PDI);

	// Reference arcs bound the sun's whole annual range.
	if (CVarShowReferenceArcs.GetValueOnGameThread() != 0)
	{
		const int32 SummerDay = ArchTimeCalendar::GetSolsticeDayOfYear(State.Year, EArchSolsticePreset::SummerSolstice);
		const int32 EquinoxDay = ArchTimeCalendar::GetSolsticeDayOfYear(State.Year, EArchSolsticePreset::SpringEquinox);
		const int32 WinterDay = ArchTimeCalendar::GetSolsticeDayOfYear(State.Year, EArchSolsticePreset::WinterSolstice);

		DrawSunPathForDay(*Director, *Subsystem, SummerDay, SummerArcColour, BelowHorizonColour, 2.f, PDI);
		DrawSunPathForDay(*Director, *Subsystem, EquinoxDay, EquinoxArcColour, BelowHorizonColour, 2.f, PDI);
		DrawSunPathForDay(*Director, *Subsystem, WinterDay, WinterArcColour, BelowHorizonColour, 2.f, PDI);
	}

	// The current date's arc, drawn thicker so it stands out against the references.
	DrawSunPathForDay(*Director, *Subsystem, State.DayOfYear, CurrentArcColour, BelowHorizonColour, 3.5f, PDI);
	DrawHourMarkers(*Director, *Subsystem, PDI);

	if (CVarShowAnalemma.GetValueOnGameThread() != 0)
	{
		DrawAnalemma(*Director, *Subsystem, PDI);
	}

	// The sun itself, plus the line back to the Director's origin.
	const FArchSolarPosition& Sun = Subsystem->GetSolarPosition();
	const FVector SunDirection = ArchSolarMath::SolarToUnrealDirectionToBody(
		Sun.AzimuthDegrees, Sun.TrueAltitudeDegrees, State.NorthOffsetDegrees);

	const FVector Origin = Director->GetActorLocation();
	const FVector SunPoint = Origin + SunDirection * Radius;

	DrawWireSphere(PDI, SunPoint, FLinearColor(1.f, 0.9f, 0.4f, 1.f), Radius * 0.04f, 16, SDPG_Foreground, 3.f);
	PDI->DrawLine(Origin, SunPoint, FLinearColor(1.f, 0.9f, 0.4f, 0.7f), SDPG_World, 2.f);

	// The shadow this sun would cast from a unit-height object, projected onto the ground.
	if (Sun.TrueAltitudeDegrees > 0.0)
	{
		const double ShadowMultiplier = ArchSolarMath::ShadowLengthMultiplier(Sun.TrueAltitudeDegrees, 20.0);
		const FVector ShadowDirection = FVector(-SunDirection.X, -SunDirection.Y, 0.0).GetSafeNormal();

		// Scaled to a tenth of the sphere radius as the notional object height, so the
		// indicator stays inside the drawn sphere at every sun altitude.
		const float ObjectHeight = Radius * 0.1f;
		const FVector ShadowTip = Origin + ShadowDirection * (ObjectHeight * static_cast<float>(ShadowMultiplier));

		PDI->DrawLine(Origin, Origin + FVector(0.f, 0.f, ObjectHeight), FLinearColor::Gray, SDPG_World, 2.f);
		PDI->DrawLine(Origin + FVector(0.f, 0.f, ObjectHeight), ShadowTip, FLinearColor(0.2f, 0.2f, 0.25f, 1.f), SDPG_World, 2.f);
		PDI->DrawLine(Origin, ShadowTip, FLinearColor(0.2f, 0.2f, 0.25f, 1.f), SDPG_World, 3.f);
	}
}

void FArchSkyDirectorVisualizer::DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport,
	const FSceneView* View, FCanvas* Canvas)
{
	const AArchSkyDirector* Director = GetDirector(Component);
	if (!Director || !Canvas || !View)
	{
		return;
	}

	if (Component != Director->GetRootComponent())
	{
		return;
	}

	const UArchSkySubsystem* Subsystem = Director->GetSkySubsystem();
	if (!Subsystem)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	// Hour labels, projected from the world positions gathered during DrawVisualization.
	for (const TPair<FVector, FString>& Label : PendingHourLabels)
	{
		const FVector4 Projected = View->WorldToScreen(Label.Key);
		if (Projected.W <= 0.f)
		{
			// Behind the camera.
			continue;
		}

		FVector2D ScreenPosition;
		if (!View->ScreenToPixel(Projected, ScreenPosition))
		{
			continue;
		}

		FCanvasTextItem TextItem(ScreenPosition, FText::FromString(Label.Value), Font, FLinearColor::White);
		TextItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(TextItem);
	}

	// A compact summary in the corner of the viewport, so an architect scrubbing the
	// details-panel sliders can read the numbers without opening the game UI.
	const FArchSolarPosition& Sun = Subsystem->GetSolarPosition();
	const FArchSolarDayInfo& DayInfo = Subsystem->GetSolarDayInfo();

	const FString Summary = FString::Printf(
		TEXT("ArchSky  |  %s %s  |  Sun az %.1f° alt %.1f°  |  Rise %s  Noon %s  Set %s  |  Shadow %.2f×"),
		*Subsystem->GetFormattedDateString(EArchCalendarType::Gregorian).ToString(),
		*Subsystem->GetFormattedTimeString(true).ToString(),
		Sun.AzimuthDegrees, Sun.TrueAltitudeDegrees,
		*ArchTimeCalendar::FormatTimeOfDay(DayInfo.SunriseHours, true).ToString(),
		*ArchTimeCalendar::FormatTimeOfDay(DayInfo.SolarNoonHours, true).ToString(),
		*ArchTimeCalendar::FormatTimeOfDay(DayInfo.SunsetHours, true).ToString(),
		Subsystem->GetShadowLengthMultiplier());

	FCanvasTextItem SummaryItem(FVector2D(24.f, 24.f), FText::FromString(Summary), Font, FLinearColor(1.f, 0.95f, 0.7f));
	SummaryItem.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(SummaryItem);
}

#undef LOCTEXT_NAMESPACE
