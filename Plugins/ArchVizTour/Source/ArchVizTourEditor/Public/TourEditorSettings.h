// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"

#include "TourEditorSettings.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * Project Settings > Plugins > ArchViz Tour.
 *
 * Authoring defaults only. Nothing here is read by a packaged client: runtime behaviour lives
 * in UTourRuntimeSettings, which is in the runtime module precisely so a shipped build can
 * reach it.
 */
UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig, meta = (DisplayName = "ArchViz Tour"))
class ARCHVIZTOUREDITOR_API UTourEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTourEditorSettings();

	virtual FName GetCategoryName() const override;

	// ---------------------------------------------------------------------
	// Path defaults
	// ---------------------------------------------------------------------

	/** Speed given to a newly created path, in centimetres per second. */
	UPROPERTY(EditAnywhere, Config, Category = "Path Defaults", meta = (ClampMin = "0.01", Units = "cm/s"))
	float DefaultPathSpeed = 200.0f;

	/** Radius pre-filled into the generation panel, in centimetres. */
	UPROPERTY(EditAnywhere, Config, Category = "Path Defaults", meta = (ClampMin = "1.0", Units = "cm"))
	float DefaultArcRadius = 500.0f;

	/** Point count pre-filled into the generation panel. */
	UPROPERTY(EditAnywhere, Config, Category = "Path Defaults", meta = (ClampMin = "2", ClampMax = "64"))
	int32 DefaultArcPointCount = 5;

	/** Focal length given to newly created points, in millimetres. */
	UPROPERTY(EditAnywhere, Config, Category = "Path Defaults", meta = (ClampMin = "1.0"))
	float DefaultFocalLength = 35.0f;

	/** Eye height used by Snap Points To Floor, in centimetres. */
	UPROPERTY(EditAnywhere, Config, Category = "Path Defaults", meta = (Units = "cm"))
	float DefaultSnapHeightOffset = 160.0f;

	/** Mesh assigned to a new path's rail. Optional; without it the rail simply does not draw. */
	UPROPERTY(EditAnywhere, Config, Category = "Path Defaults")
	TSoftObjectPtr<UStaticMesh> DefaultRailMesh;

	/** Material assigned to a new path's rail. */
	UPROPERTY(EditAnywhere, Config, Category = "Path Defaults")
	TSoftObjectPtr<UMaterialInterface> DefaultRailMaterial;

	// ---------------------------------------------------------------------
	// Viewport visualization
	// ---------------------------------------------------------------------

	/** Draw numbered handles, tangents and labels for the selected path. */
	UPROPERTY(EditAnywhere, Config, Category = "Viewport")
	bool bDrawPointLabels = true;

	/** Draw an arrow at each point showing the direction of travel. */
	UPROPERTY(EditAnywhere, Config, Category = "Viewport")
	bool bDrawDirectionArrows = true;

	/** Draw the tangent handles, and allow dragging them. */
	UPROPERTY(EditAnywhere, Config, Category = "Viewport")
	bool bDrawTangentHandles = true;

	/** Radius of a point handle in the viewport, in centimetres. */
	UPROPERTY(EditAnywhere, Config, Category = "Viewport", meta = (ClampMin = "1.0", Units = "cm"))
	float PointHandleSize = 12.0f;

	/** Colour of an unselected point handle. */
	UPROPERTY(EditAnywhere, Config, Category = "Viewport")
	FLinearColor PointColor = FLinearColor(0.1f, 0.6f, 1.0f);

	/** Colour of the selected point handle. */
	UPROPERTY(EditAnywhere, Config, Category = "Viewport")
	FLinearColor SelectedPointColor = FLinearColor(1.0f, 0.7f, 0.1f);

	/** Colour of the tangent handles and their lines. */
	UPROPERTY(EditAnywhere, Config, Category = "Viewport")
	FLinearColor TangentColor = FLinearColor(0.9f, 0.3f, 0.9f);

	// ---------------------------------------------------------------------
	// Render defaults
	// ---------------------------------------------------------------------

	/** Directory pre-filled into a new render job. Relative paths resolve under Saved/. */
	UPROPERTY(EditAnywhere, Config, Category = "Render Defaults")
	FString DefaultOutputDirectory = TEXT("TourRenders");

	/** Resolution pre-filled into a new render job. */
	UPROPERTY(EditAnywhere, Config, Category = "Render Defaults")
	FIntPoint DefaultRenderResolution = FIntPoint(1920, 1080);

	/** Frame rate pre-filled into a new render job. */
	UPROPERTY(EditAnywhere, Config, Category = "Render Defaults", meta = (ClampMin = "1.0", ClampMax = "240.0"))
	float DefaultFrameRate = 30.0f;

	/**
	 * Path to an ffmpeg executable used by the Scene Capture backend.
	 *
	 * Empty means "look for ffmpeg on PATH". The Movie Render Pipeline backend uses its own
	 * encoder settings under Project Settings > Plugins > Movie Pipeline CLI Encoder instead.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Render Defaults", meta = (FilePathFilter = "*"))
	FString FFmpegExecutablePath;

	/** Directory new Level Sequence bakes are written into. */
	UPROPERTY(EditAnywhere, Config, Category = "Render Defaults", meta = (ContentDir))
	FDirectoryPath DefaultSequenceBakeDirectory;

	/** Convenience accessor; never null. */
	static const UTourEditorSettings& Get();
};
