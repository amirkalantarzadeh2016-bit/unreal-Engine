#include "MinimapMarkerWidget.h"

#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "MinimapModule.h"
#include "MinimapSubsystem.h"
#include "MinimapTrackedComponent.h"

void UMinimapMarkerWidget::SetBoundMarker(UMinimapTrackedComponent* Marker)
{
	if (BoundMarker.Get() == Marker)
	{
		return;
	}

	BoundMarker = Marker;
	OnMarkerBound(Marker);
}

void UMinimapMarkerWidget::OnMarkerUpdated_Implementation(const FMinimapMarkerSnapshot& Snapshot)
{
	// The snapshot holds a pointer captured during the update; the owning actor can have
	// been destroyed between then and now, so re-validate before touching it.
	const UMinimapTrackedComponent* Marker = Snapshot.Tracked;
	if (!IsValid(Marker) || !IsValid(IconImage))
	{
		return;
	}

	const FMinimapMarkerStyle& Style = Marker->Style;

	// Prefer a dedicated off-map icon (typically an arrow) when one is authored, and use
	// the matching fallback shape when it is not.
	const bool bUseOutOfBoundsVariant = Snapshot.bOutOfBounds;

	UTexture2D* AuthoredTexture = bUseOutOfBoundsVariant
		? (Style.OutOfBoundsIcon ? Style.OutOfBoundsIcon.Get() : Style.Icon.Get())
		: Style.Icon.Get();

	const EMinimapMarkerShape FallbackShape = bUseOutOfBoundsVariant
		? Style.OutOfBoundsFallbackShape
		: Style.FallbackShape;

	UTexture2D* DesiredTexture = AuthoredTexture;

	if (!DesiredTexture && Style.bUseGeneratedIconWhenUnset)
	{
		// The whole point of this path: without it, an unset Icon left the brush exactly as
		// the widget was authored, so any placeholder sitting in that Image showed up on
		// the map as the marker.
		if (UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this))
		{
			DesiredTexture = Subsystem->ResolveMarkerIcon(nullptr, FallbackShape, GeneratedIconResolution);
		}
	}

	if (DesiredTexture)
	{
		if (IconImage->GetBrush().GetResourceObject() != DesiredTexture)
		{
			// Only touch the brush when the texture actually changed: SetBrushFromTexture
			// invalidates the widget's layout, so calling it every update is wasteful.
			IconImage->SetBrushFromTexture(DesiredTexture, /*bMatchSize=*/false);
		}
	}
	else if (!bWarnedMissingIcon)
	{
		// Only reachable when the generated fallback is deliberately switched off.
		bWarnedMissingIcon = true;
		UE_LOG(LogMinimap, Warning,
			TEXT("Marker on '%s' has no icon and Use Generated Icon When Unset is off, so the "
			     "widget keeps whatever brush it was authored with. Set Style.Icon, or re-enable "
			     "the generated fallback."),
			*GetNameSafe(Marker->GetOwner()));
	}

	IconImage->SetDesiredSizeOverride(Style.IconSize);
	IconImage->SetColorAndOpacity(Style.Tint);

	// EdgeAngle points at the target and is only meaningful once clamped; IconAngle is the
	// actor's own facing. Never both.
	const float Angle = (Snapshot.bOutOfBounds && bRotateToEdgeAngleWhenOutOfBounds)
		? Snapshot.EdgeAngle
		: Snapshot.IconAngle;

	IconImage->SetRenderTransformAngle(Angle);
}
