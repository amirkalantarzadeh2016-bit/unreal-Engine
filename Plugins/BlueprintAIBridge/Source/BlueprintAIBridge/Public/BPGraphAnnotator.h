// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BPGraphClusterDetector.h"
#include "CoreMinimal.h"

class UBPAIBridgeSettings;
class UEdGraph;
class UEdGraphNode_Comment;

/** What one annotation pass did. */
struct FBPAnnotationResult
{
	int32 Created = 0;
	int32 Updated = 0;
	int32 Skipped = 0;
	TArray<FString> Warnings;
};

/**
 * Draws one comment box per cluster, titled with the cluster type and coloured from settings.
 *
 * Re-running is idempotent: each cluster is matched against the comment boxes this module has
 * already produced (tracked per graph, and recognised again after a restart by title plus the
 * nodes they enclose), and a matched box is resized in place rather than duplicated.
 */
class BLUEPRINTAIBRIDGE_API FBPGraphAnnotator
{
public:
	/**
	 * Creates or updates a comment box around each cluster.
	 *
	 * Expects the caller to have opened a transaction; every mutation is marked for undo.
	 * Writes the resulting comment node back into each cluster's CommentNode.
	 */
	static FBPAnnotationResult AnnotateClusters(
		UEdGraph* Graph,
		TArray<FBPGraphCluster>& InOutClusters,
		const UBPAIBridgeSettings* Settings);

	/** True if the comment box looks like one this module produced. */
	static bool IsFormatterComment(const UEdGraphNode_Comment* Comment);

	/** Forgets the tracked comment boxes for a graph, or for every graph when Graph is null. */
	static void ForgetGraph(UEdGraph* Graph);

private:
	/** Spawns a comment node into the graph, fully initialised but not yet positioned. */
	static UEdGraphNode_Comment* CreateComment(UEdGraph* Graph);

	/**
	 * Picks the best existing box for a cluster: the unclaimed formatter comment enclosing the
	 * most of the cluster's nodes. Returns nullptr when nothing overlaps.
	 */
	static UEdGraphNode_Comment* FindExistingComment(
		const FBPGraphCluster& Cluster,
		const TArray<UEdGraphNode_Comment*>& Candidates,
		TSet<UEdGraphNode_Comment*>& InOutClaimed);

	/** Writes title, colour, geometry and contents onto a comment box. */
	static void ApplyComment(
		UEdGraphNode_Comment* Comment,
		const FBPGraphCluster& Cluster,
		const UBPAIBridgeSettings* Settings);

	/** Every formatter comment in the graph: the tracked ones plus anything matching by title. */
	static TArray<UEdGraphNode_Comment*> GatherCandidates(UEdGraph* Graph);

	/**
	 * Comment boxes this module has created, per graph.
	 *
	 * Weak so a deleted or undone comment drops out on its own. Session-scoped; GatherCandidates
	 * falls back to recognising boxes by title for anything created before an editor restart.
	 */
	static TMap<TWeakObjectPtr<UEdGraph>, TArray<TWeakObjectPtr<UEdGraphNode_Comment>>> TrackedComments;
};
