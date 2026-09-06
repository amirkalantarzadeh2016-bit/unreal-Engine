#include "MinimapMarkerWidget.h"

#include "Components/Image.h"
#include "Engine/Texture2D.h"
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

	// Prefer a dedicated off-map icon (typically an arrow) when one is authored.
	UTexture2D* DesiredTexture = (Snapshot.bOutOfBounds && Style.OutOfBoundsIcon)
		? Style.OutOfBoundsIcon
		: Style.Icon;

	if (DesiredTexture && IconImage->GetBrush().GetResourceObject() != DesiredTexture)
	{
		// Only touch the brush when the texture actually changed: SetBrushFromTexture
		// invalidates the widget's layout, so calling it every update is wasteful.
		IconImage->SetBrushFromTexture(DesiredTexture, /*bMatchSize=*/false);
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
