// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BPGraphClusterDetector.h"
#include "CoreMinimal.h"
#include "Math/Box2D.h"

class UBlueprint;
class UBPAIBridgeSettings;
class UEdGraph;
class UEdGraphNode;

/** Log category for the graph formatter (layout, clustering and annotation). */
BLUEPRINTAIBRIDGE_API DECLARE_LOG_CATEGORY_EXTERN(LogBPFormatter, Log, All);

/** Spacing knobs for one layout run. Seeded from UBPAIBridgeSettings. */
struct FBPLayoutOptions
{
	/** Distance between the left edges of adjacent layers, widened for oversized nodes. */
	float HorizontalPadding = 280.0f;

	/** Distance between the top edges of adjacent nodes in a layer, widened for tall nodes. */
	float VerticalPadding = 160.0f;

	/** Vertical gap between one cluster's bounding box and the next. */
	float ClusterSpacing = 220.0f;

	/** Builds options from project settings; falls back to the defaults above if unavailable. */
	static FBPLayoutOptions FromSettings(const UBPAIBridgeSettings* Settings);
};

/** What one format run did, for the log and the panel's status line. */
struct FBPFormatResult
{
	int32 GraphCount = 0;
	int32 NodeCount = 0;
	int32 ClusterCount = 0;
	int32 CommentsCreated = 0;
	int32 CommentsUpdated = 0;

	FBox2D BoundsBefore = FBox2D(ForceInit);
	FBox2D BoundsAfter = FBox2D(ForceInit);

	TArray<FString> Warnings;
	bool bSuccess = false;

	/** One-line summary for the panel's status box. */
	FString ToString() const;
};

/**
 * Layered (Sugiyama-style) left-to-right graph layout.
 *
 * Layering follows exec flow first: nodes carrying exec pins are ranked by longest path over
 * exec edges alone, and pure/data-only nodes are then pulled to just left of whichever node
 * consumes them. Within a layer, nodes are ordered by a barycentre sweep to cut edge crossings.
 *
 * Each connected cluster is laid out independently and the clusters are stacked vertically, so
 * a cluster's bounding box never overlaps its neighbour's -- which is what makes the comment
 * boxes the annotator draws meaningful.
 */
class BLUEPRINTAIBRIDGE_API FBPGraphLayoutEngine
{
public:
	/**
	 * Full pipeline for one Blueprint: snapshot, then a single transaction covering cluster
	 * detection, layout and annotation for every graph in Graphs.
	 *
	 * This is the entry point the "Format Graph" button calls. Undo restores every node
	 * position and removes every comment box the run created.
	 */
	static FBPFormatResult FormatBlueprint(UBlueprint* Blueprint, const TArray<UEdGraph*>& Graphs);

	/**
	 * Positions the nodes of every cluster and writes the results back to the nodes.
	 *
	 * Callers must have opened a transaction and are responsible for the snapshot; FormatBlueprint
	 * does both. Updates each cluster's Bounds in place.
	 */
	static void LayoutClusters(UEdGraph* Graph, TArray<FBPGraphCluster>& InOutClusters, const FBPLayoutOptions& Options);

	/** Bounding box of every layout-eligible node in the graph. Empty box if there are none. */
	static FBox2D ComputeGraphBounds(UEdGraph* Graph);

	/** Bounding box of a specific node set. */
	static FBox2D ComputeNodesBounds(const TArray<UEdGraphNode*>& Nodes);

	/**
	 * Best-effort node footprint.
	 *
	 * A node's true size is computed by its Slate widget, which only exists while the graph is
	 * open, so NodeWidth/NodeHeight are usually zero here. Where they are set they are trusted;
	 * otherwise the size is estimated from the title length and pin count. Layout tolerates the
	 * error because padding is applied as a minimum step rather than an exact gap.
	 */
	static FVector2D EstimateNodeSize(UEdGraphNode* Node);

private:
	/** Longest-path layer assignment, exec edges first and data edges second. */
	static void AssignLayers(
		const TArray<UEdGraphNode*>& Nodes,
		const TMap<UEdGraphNode*, int32>& NodeToIndex,
		TArray<int32>& OutLayers);

	/** Barycentre sweep that orders nodes within each layer to reduce edge crossings. */
	static void OrderWithinLayers(
		const TArray<UEdGraphNode*>& Nodes,
		const TMap<UEdGraphNode*, int32>& NodeToIndex,
		TArray<TArray<int32>>& InOutLayerContents);

	/** Writes NodePosX/Y for one cluster and returns the cluster's new bounding box. */
	static FBox2D PlaceCluster(
		FBPGraphCluster& Cluster,
		const FBPLayoutOptions& Options,
		float OriginY);

	/** True if the pin participates in execution flow. */
	static bool IsExecPin(const UEdGraphPin* Pin);

	/** Moves a node, marking it for undo first. */
	static void SetNodePosition(UEdGraphNode* Node, const FVector2D& Position);
};
