// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "BPFormatterSettings.generated.h"

/** What a connected subgraph is mostly doing, which drives its comment title and colour. */
UENUM()
enum class EBPClusterType : uint8
{
	/** Contains an event node; this is where execution enters. */
	Event UMETA(DisplayName = "Event"),

	/** Every node is pure, so the whole cluster is a value computation. */
	Pure UMETA(DisplayName = "Pure"),

	/** Contains a branch, switch or loop. */
	ControlFlow UMETA(DisplayName = "Control Flow"),

	/** Contains nodes whose names read as error handling or validation. */
	ErrorHandling UMETA(DisplayName = "Error Handling"),

	/** Contains a variable write or a function result. */
	Output UMETA(DisplayName = "Output"),

	/** Anything else. */
	Logic UMETA(DisplayName = "Logic")
};

/**
 * Project settings for the Blueprint AI Bridge, shown under Project Settings > Plugins >
 * Blueprint Formatter.
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "Blueprint Formatter"))
class BLUEPRINTAIBRIDGE_API UBPAIBridgeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBPAIBridgeSettings();

	//~ Begin UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;
	//~ End UDeveloperSettings interface

	/**
	 * Horizontal distance between the left edges of two adjacent layers.
	 * Widened automatically when a layer holds a node wider than this.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Layout", meta = (ClampMin = "40.0", UIMin = "80.0", UIMax = "800.0"))
	float HorizontalPadding;

	/**
	 * Vertical distance between the top edges of two nodes in the same layer.
	 * Widened automatically when a node is taller than this.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Layout", meta = (ClampMin = "40.0", UIMin = "60.0", UIMax = "600.0"))
	float VerticalPadding;

	/** Vertical gap left between one cluster's bounding box and the next. */
	UPROPERTY(EditAnywhere, config, Category = "Layout", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "800.0"))
	float ClusterSpacing;

	/** Run the formatter automatically after a successful "Apply Selected Changes". */
	UPROPERTY(EditAnywhere, config, Category = "Layout")
	bool bAutoFormatAfterApply;

	/** Draw a comment box around each detected cluster. */
	UPROPERTY(EditAnywhere, config, Category = "Annotation")
	bool bAnnotateClusters;

	/** Space left between a cluster's bounding box and the comment box drawn around it. */
	UPROPERTY(EditAnywhere, config, Category = "Annotation", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "200.0"))
	float CommentPadding;

	/** Skip the comment box for clusters smaller than this, which are usually noise. */
	UPROPERTY(EditAnywhere, config, Category = "Annotation", meta = (ClampMin = "1", UIMin = "1", UIMax = "10"))
	int32 MinClusterSizeToAnnotate;

	/** Comment box colour per cluster type. */
	UPROPERTY(EditAnywhere, config, Category = "Annotation")
	TMap<EBPClusterType, FLinearColor> ClusterColors;

	/** Colour for a cluster type, falling back to a neutral grey if the map has no entry. */
	FLinearColor GetClusterColor(EBPClusterType ClusterType) const;

	/** Display name for a cluster type; this is what a generated comment box is titled. */
	static FString GetClusterTypeName(EBPClusterType ClusterType);
};
