// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourCaptureComponent.h"

#include "ArchVizTourLog.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Scene.h"
#include "Engine/TextureRenderTarget2D.h"

namespace ArchVizTour::CapturePrivate
{
	/** Above this a render target costs more VRAM than most workstations have to spare. */
	static constexpr int32 MaxCaptureDimension = 4096;
}

UTourCaptureComponent::UTourCaptureComponent()
{
	// Driven explicitly by the render backend, one capture per output frame.
	PrimaryComponentTick.bCanEverTick = false;

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	check(SceneCapture != nullptr);
	SceneCapture->SetupAttachment(this);

	// Everything below is what separates an offline capture from a live one:
	//  - FinalColorLDR / FinalColorHDR is the full post-processed image, not a G-buffer slice.
	//  - bCaptureEveryFrame off means the capture happens only when CaptureFrame asks, so a
	//    dropped or duplicated game frame cannot produce a duplicated output frame.
	//  - bAlwaysPersistRenderingState keeps temporal history (TAA, motion blur, exposure)
	//    between explicit captures; without it every frame is the first frame and the output
	//    shimmers.
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->bAlwaysPersistRenderingState = true;
	SceneCapture->ProjectionType = ECameraProjectionMode::Perspective;
}

bool UTourCaptureComponent::Configure(FIntPoint Resolution, ETourImageFormat Format)
{
	using namespace ArchVizTour::CapturePrivate;

	if (!IsValid(SceneCapture))
	{
		UE_LOG(LogArchVizTour, Error, TEXT("Tour capture component has no scene capture."));
		return false;
	}

	Resolution.X = FMath::Clamp(Resolution.X, 16, MaxCaptureDimension);
	Resolution.Y = FMath::Clamp(Resolution.Y, 16, MaxCaptureDimension);

	bFloatFormat = (Format == ETourImageFormat::EXR);

	if (!IsValid(RenderTarget))
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
		check(RenderTarget != nullptr);
	}

	// EXR output wants linear half float; the 8-bit formats want RGBA8 so the readback is a
	// straight memcpy with no conversion and no colour-space guessing.
	RenderTarget->RenderTargetFormat = bFloatFormat ? RTF_RGBA16f : RTF_RGBA8;
	RenderTarget->ClearColor         = FLinearColor::Black;
	RenderTarget->bAutoGenerateMips  = false;
	// A float target is linear by definition; the 8-bit path captures the already-tonemapped
	// LDR image, which is sRGB-encoded.
	RenderTarget->bForceLinearGamma  = bFloatFormat;
	RenderTarget->TargetGamma        = bFloatFormat ? 1.0f : 0.0f;
	RenderTarget->InitAutoFormat(Resolution.X, Resolution.Y);
	RenderTarget->UpdateResourceImmediate(/*bClearRenderTarget*/ true);

	SceneCapture->CaptureSource = bFloatFormat ? ESceneCaptureSource::SCS_FinalColorHDR : ESceneCaptureSource::SCS_FinalColorLDR;
	SceneCapture->TextureTarget = RenderTarget;

	UE_LOG(LogArchVizTour, Log, TEXT("Tour capture configured at %d x %d (%s)."),
		Resolution.X, Resolution.Y, bFloatFormat ? TEXT("RGBA16f linear") : TEXT("RGBA8"));

	return true;
}

void UTourCaptureComponent::SetView(const FVector& Location, const FRotator& Rotation, float HorizontalFOV)
{
	if (!IsValid(SceneCapture))
	{
		return;
	}

	SceneCapture->SetWorldLocationAndRotation(Location, Rotation);
	SceneCapture->FOVAngle = FMath::Clamp(HorizontalFOV, 5.0f, 170.0f);
}

void UTourCaptureComponent::ApplyPostProcess(const FPostProcessSettings& Settings, float BlendWeight)
{
	if (IsValid(SceneCapture))
	{
		SceneCapture->PostProcessSettings = Settings;
		SceneCapture->PostProcessBlendWeight = FMath::Clamp(BlendWeight, 0.0f, 1.0f);
	}
}

void UTourCaptureComponent::CaptureFrame()
{
	if (IsConfigured())
	{
		SceneCapture->CaptureScene();
	}
}

bool UTourCaptureComponent::IsConfigured() const
{
	return IsValid(SceneCapture) && IsValid(RenderTarget) && RenderTarget->GetResource() != nullptr;
}

void UTourCaptureComponent::ReleaseResources()
{
	if (IsValid(SceneCapture))
	{
		SceneCapture->TextureTarget = nullptr;
	}

	if (IsValid(RenderTarget))
	{
		RenderTarget->ReleaseResource();
		RenderTarget = nullptr;
	}
}
