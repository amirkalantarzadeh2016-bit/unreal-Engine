// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BPFormatterSettings.h"
#include "CoreMinimal.h"
#include "Math/Box2D.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphNode_Comment;

/** One connected subgraph, plus what it is mostly doing and where it ended up. */
struct FBPGraphCluster
{
	/** Nodes in this connected component, in the graph's own node order. */
	TArray<UEdGraphNode*> Nodes;

	EBPClusterType Type = EBPClusterType::Logic;

	/** Bounding box of Nodes. Set by the detector, then overwritten by the layout engine. */
	FBox2D Bounds = FBox2D(ForceInit);

	/**
	 * Where the cluster sat before the layout engine touched it.
	 *
	 * This is how the annotator recognises the comment box it drew on a previous run: the box
	 * has not moved (comment nodes are excluded from layout), so it still overlaps the region
	 * the cluster used to occupy. Position-independent identity via the nodes a comment
	 * encloses would be neater, but the editor recomputes that set on every move, so it is not
	 * something the formatter can rely on between runs.
	 */
	FBox2D PreLayoutBounds = FBox2D(ForceInit);

	/**
	 * Comment box drawn around this cluster, if any. The annotator writes it so a second run
	 * resizes the existing box instead of stacking a new one on top.
	 */
	TWeakObjectPtr<UEdGraphNode_Comment> CommentNode;

	/** Stable-ish index within the graph, used for logging. */
	int32 ClusterIndex = INDEX_NONE;

	FString GetTypeName() const { return UBPAIBridgeSettings::GetClusterTypeName(Type); }
};

/**
 * Splits a graph into connected subgraphs with union-find over every pin connection, then
 * classifies each one by the kind of node that dominates it.
 */
class BLUEPRINTAIBRIDGE_API FBPGraphClusterDetector
{
public:
	/**
	 * Returns one cluster per connected component, largest first.
	 * Comment nodes are excluded: they connect nothing and would otherwise be dragged around
	 * by the layout engine as if they were logic.
	 */
	static TArray<FBPGraphCluster> DetectClusters(UEdGraph* Graph);

	/**
	 * Classifies a set of nodes.
	 *
	 * Checks run in a fixed precedence: Event, Pure, ControlFlow, ErrorHandling, Output, and
	 * Logic as the fallback. A cluster entered by an event is named for that however much
	 * branching it also contains, because that is how a reader looks for it.
	 */
	static EBPClusterType ClassifyCluster(const TArray<UEdGraphNode*>& Nodes);

private:
	/** True if the node is an event entry point. */
	static bool IsEventNode(UEdGraphNode* Node);

	/** True if the node is pure (no exec pins, no side effects). */
	static bool IsPureNode(UEdGraphNode* Node);

	/** True for branches, switches and the loop macros. */
	static bool IsControlFlowNode(UEdGraphNode* Node);

	/** True when the node's name reads as validation or error handling. */
	static bool IsErrorHandlingNode(UEdGraphNode* Node);

	/** True for variable writes and function results. */
	static bool IsOutputNode(UEdGraphNode* Node);

	/** True for nodes the formatter should leave where they are. */
	static bool ShouldIgnoreNode(UEdGraphNode* Node);
};
