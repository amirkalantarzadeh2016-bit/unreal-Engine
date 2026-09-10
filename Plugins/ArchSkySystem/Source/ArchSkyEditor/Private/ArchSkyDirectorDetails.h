// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class AArchSkyDirector;
class IDetailLayoutBuilder;
class UArchSkySubsystem;

/**
 * Details-panel customisation for AArchSkyDirector.
 *
 * Adds a "Sun Study" category at the top of the panel with live sliders for the values an
 * architect actually manipulates - time, date, north offset, weather - none of which live
 * on the Director at all: they live on the subsystem. Exposing them here is what lets the
 * sky be scrubbed from the details panel WITHOUT entering PIE, which is how a shadow study
 * is actually produced.
 *
 * ARCH NOTE: the sliders write straight to the subsystem and then ask the Director to
 * re-apply. They do NOT go through a UPROPERTY on the Director, because that would mean
 * duplicating the state and inventing a synchronisation rule between the copy and the
 * subsystem's authoritative one. There is exactly one source of truth in this plugin and
 * the details panel is not allowed to become a second.
 */
class FArchSkyDirectorDetails : public IDetailCustomization
{
public:
	/** Factory, registered with the property editor module. */
	static TSharedRef<IDetailCustomization> MakeInstance();

	//~ Begin IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization

private:
	/** The Director being edited, or null on a multi-selection. */
	TWeakObjectPtr<AArchSkyDirector> EditedDirector;

	/** Its subsystem, cached for the slider callbacks. */
	UArchSkySubsystem* GetSubsystem() const;

	/** Pushes a change into the subsystem and repaints the editor viewport. */
	void ApplyAndRefresh() const;

	// --- Slider accessors. Slate wants getter/setter pairs, not properties. ---

	float GetTimeOfDay() const;
	void SetTimeOfDay(float NewValue);

	int32 GetDayOfYear() const;
	void SetDayOfYear(int32 NewValue);

	float GetNorthOffset() const;
	void SetNorthOffset(float NewValue);

	/** The live readout under the sliders. */
	FText GetSummaryText() const;

	/** The sunrise/noon/sunset line. */
	FText GetSunTimesText() const;

	/** Jump-button handlers. */
	FReply OnJumpToSunrise();
	FReply OnJumpToSolarNoon();
	FReply OnJumpToSunset();
	FReply OnSetSolstice(int32 PresetIndex);
	FReply OnValidateScene();
};
