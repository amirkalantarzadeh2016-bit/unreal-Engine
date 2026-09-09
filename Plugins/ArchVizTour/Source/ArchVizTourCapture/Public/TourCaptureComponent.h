// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"
#include "TourRenderSettings.h"

#include "TourCaptureComponent.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * A SceneCapture2D plus the render target it writes into, wrapped so callers deal in tour
 * concepts (resolution, format, "capture this frame") rather than in capture-source flags and
 * render-target formats.
 *
 * Composition rather than inheritance: the wrapper owns the capture component's whole
 * configuration, and exposing USceneCaptureComponent2D's full property surface on top of it
 * would invite half-configured captures that silently produce black frames.
 */
UCLASS(ClassGroup = "ArchViz Tour", meta = (BlueprintSpawnableComponent, DisplayName = "Tour Capture"))
class ARCHVIZTOURCAPTURE_API UTourCaptureComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UTourCaptureComponent();

	/** The wrapped capture component. Configured entirely through this wrapper. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tour Capture")
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	/** Target the capture renders into. Created by Configure. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tour Capture")
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	/**
	 * Create or resize the render target and configure the capture for offline output.
	 *
	 * @param Resolution  Target size in pixels. Clamped to 4096 in each axis.
	 * @param Format      Output format; EXR selects an RGBA16f linear target, the 8-bit formats
	 *                    select RGBA8 so the readback needs no conversion.
	 * @return true when the target is ready.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Capture")
	bool Configure(FIntPoint Resolution, ETourImageFormat Format);

	/**
	 * Point the capture at a camera pose.
	 * @param Location   World position, in centimetres.
	 * @param Rotation   World orientation, in degrees.
	 * @param HorizontalFOV  Horizontal field of view, in degrees.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Capture")
	void SetView(const FVector& Location, const FRotator& Rotation, float HorizontalFOV);

	/**
	 * Copy a view's post-process settings onto the capture.
	 * Without them the capture misses exposure, depth of field and grading, and looks nothing
	 * like what the tour shows on screen.
	 * @param Settings     Post-process stack from the active view.
	 * @param BlendWeight  Weight the view applies its stack at, 0..1.
	 */
	void ApplyPostProcess(const struct FPostProcessSettings& Settings, float BlendWeight);

	/**
	 * Render one frame into the render target.
	 * Explicit rather than every-frame automatic, so the capture happens exactly once per
	 * output frame and the readback below can be paired with it one to one.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Capture")
	void CaptureFrame();

	/** True once Configure has produced a usable render target. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Capture")
	bool IsConfigured() const;

	/** Release the render target. Called when a job ends so the memory does not linger. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Capture")
	void ReleaseResources();

	/** True when the configured target holds linear half-float data rather than 8-bit colour. */
	bool IsFloatFormat() const { return bFloatFormat; }

private:
	/** Mirrors the render target's pixel format, so the readback picks the right path. */
	bool bFloatFormat = false;
};
