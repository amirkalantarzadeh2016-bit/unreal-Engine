// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPGraphAnnotator.h"

#include "BPFormatterSettings.h"
#include "BPGraphLayoutEngine.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"

namespace BPAnnotatorInternal
{
	/**
	 * Clearance above a comment's contents for its title bar.
	 *
	 * A comment box's NodePosY is the top of the title bar, not the top of the area it frames,
	 * so without this the first row of nodes sits underneath the title.
	 */
	constexpr float TitleBarHeight = 36.0f;

	/** Font size for generated comment titles. */
	constexpr int32 CommentFontSize = 18;

	/** Area shared by two boxes, or zero when they do not meet. */
	float IntersectionArea(const FBox2D& A, const FBox2D& B)
	{
		if (!A.bIsValid || !B.bIsValid)
		{
			return 0.0f;
		}

		const float Width = FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X);
		const float Height = FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y);

		if (Width <= 0.0f || Height <= 0.0f)
		{
			return 0.0f;
		}

		return Width * Height;
	}
}

using namespace BPAnnotatorInternal;

TMap<TWeakObjectPtr<UEdGraph>, TArray<TWeakObjectPtr<UEdGraphNode_Comment>>> FBPGraphAnnotator::TrackedComments;

bool FBPGraphAnnotator::IsFormatterComment(const UEdGraphNode_Comment* Comment)
{
	if (Comment == nullptr)
	{
		return false;
	}

	// The titles this module writes are exactly the cluster type names, so a box carrying one
	// is ours -- or close enough that updating it in place beats stacking a second box on top.
	static const EBPClusterType AllTypes[] = {
		EBPClusterType::Event,
		EBPClusterType::Pure,
		EBPClusterType::ControlFlow,
		EBPClusterType::ErrorHandling,
		EBPClusterType::Output,
		EBPClusterType::Logic
	};

	for (EBPClusterType Type : AllTypes)
	{
		if (Comment->NodeComment == UBPAIBridgeSettings::GetClusterTypeName(Type))
		{
			return true;
		}
	}

	return false;
}

void FBPGraphAnnotator::ForgetGraph(UEdGraph* Graph)
{
	if (Graph == nullptr)
	{
		TrackedComments.Reset();
		return;
	}

	TrackedComments.Remove(Graph);
}

FBox2D FBPGraphAnnotator::GetCommentBounds(const UEdGraphNode_Comment* Comment)
{
	if (Comment == nullptr)
	{
		return FBox2D(ForceInit);
	}

	const FVector2D Min(static_cast<float>(Comment->NodePosX), static_cast<float>(Comment->NodePosY));
	const FVector2D Size(static_cast<float>(Comment->NodeWidth), static_cast<float>(Comment->NodeHeight));

	FBox2D Bounds(ForceInit);
	Bounds += Min;
	Bounds += Min + Size;

	return Bounds;
}

TArray<UEdGraphNode_Comment*> FBPGraphAnnotator::GatherCandidates(UEdGraph* Graph)
{
	TArray<UEdGraphNode_Comment*> Candidates;

	if (Graph == nullptr)
	{
		return Candidates;
	}

	// Graphs that have been destroyed leave dead keys behind; clear them out while we are here
	// so a long editing session does not accumulate them.
	for (auto It = TrackedComments.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	if (TArray<TWeakObjectPtr<UEdGraphNode_Comment>>* Tracked = TrackedComments.Find(Graph))
	{
		// Drop entries the user deleted or undid away, then keep what survives.
		Tracked->RemoveAll([](const TWeakObjectPtr<UEdGraphNode_Comment>& Weak)
		{
			return !Weak.IsValid();
		});

		for (const TWeakObjectPtr<UEdGraphNode_Comment>& Weak : *Tracked)
		{
			if (UEdGraphNode_Comment* Comment = Weak.Get())
			{
				if (Comment->GetGraph() == Graph)
				{
					Candidates.AddUnique(Comment);
				}
			}
		}
	}

	// Anything created before an editor restart is no longer tracked; recover it by title.
	for (const auto& Element : Graph->Nodes)
	{
		if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(ToRawPtr(Element)))
		{
			if (IsFormatterComment(Comment))
			{
				Candidates.AddUnique(Comment);
			}
		}
	}

	return Candidates;
}

UEdGraphNode_Comment* FBPGraphAnnotator::FindExistingComment(
	const FBPGraphCluster& Cluster,
	const TArray<UEdGraphNode_Comment*>& Candidates,
	TSet<UEdGraphNode_Comment*>& InOutClaimed)
{
	if (!Cluster.PreLayoutBounds.bIsValid)
	{
		return nullptr;
	}

	UEdGraphNode_Comment* Best = nullptr;
	float BestOverlap = 0.0f;

	for (UEdGraphNode_Comment* Comment : Candidates)
	{
		if (Comment == nullptr || InOutClaimed.Contains(Comment))
		{
			continue;
		}

		// Matching on where the cluster was, not where it now is: layout has already run, but
		// comment boxes were left out of it, so an existing box is still over its old cluster.
		const float Overlap = IntersectionArea(GetCommentBounds(Comment), Cluster.PreLayoutBounds);
		if (Overlap > BestOverlap)
		{
			BestOverlap = Overlap;
			Best = Comment;
		}
	}

	if (Best != nullptr)
	{
		InOutClaimed.Add(Best);
	}

	return Best;
}

UEdGraphNode_Comment* FBPGraphAnnotator::CreateComment(UEdGraph* Graph)
{
	if (Graph == nullptr)
	{
		return nullptr;
	}

	UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(
		Graph, UEdGraphNode_Comment::StaticClass(), NAME_None, RF_Transactional);

	if (Comment == nullptr)
	{
		return nullptr;
	}

	Graph->AddNode(Comment, /*bFromUI*/ false, /*bSelectNewNode*/ false);
	Comment->CreateNewGuid();
	Comment->PostPlacedNewNode();
	Comment->AllocateDefaultPins();

	return Comment;
}

void FBPGraphAnnotator::ApplyComment(
	UEdGraphNode_Comment* Comment,
	const FBPGraphCluster& Cluster,
	const UBPAIBridgeSettings* Settings)
{
	if (Comment == nullptr || !Cluster.Bounds.bIsValid)
	{
		return;
	}

	const float Padding = Settings ? Settings->CommentPadding : 40.0f;

	Comment->Modify();

	Comment->NodeComment = UBPAIBridgeSettings::GetClusterTypeName(Cluster.Type);
	Comment->CommentColor = Settings
		? Settings->GetClusterColor(Cluster.Type)
		: FLinearColor(0.35f, 0.35f, 0.35f, 0.25f);
	Comment->FontSize = CommentFontSize;
	Comment->bCommentBubbleVisible = false;

	// Group movement is what makes the box useful: dragging it carries its cluster along. The
	// editor works out which nodes that is from the box's geometry each time it moves, so there
	// is nothing for the formatter to record here.
	Comment->MoveMode = ECommentBoxMode::GroupMovement;

	const FVector2D Min = Cluster.Bounds.Min - FVector2D(Padding, Padding + TitleBarHeight);
	const FVector2D Size = Cluster.Bounds.GetSize() + FVector2D(Padding * 2.0f, Padding * 2.0f + TitleBarHeight);

	Comment->NodePosX = FMath::RoundToInt32(Min.X);
	Comment->NodePosY = FMath::RoundToInt32(Min.Y);
	Comment->NodeWidth = FMath::RoundToInt32(Size.X);
	Comment->NodeHeight = FMath::RoundToInt32(Size.Y);
}

FBPAnnotationResult FBPGraphAnnotator::AnnotateClusters(
	UEdGraph* Graph,
	TArray<FBPGraphCluster>& InOutClusters,
	const UBPAIBridgeSettings* Settings)
{
	FBPAnnotationResult Result;

	if (Graph == nullptr)
	{
		Result.Warnings.Add(TEXT("Cannot annotate a null graph."));
		return Result;
	}

	const int32 MinClusterSize = Settings ? Settings->MinClusterSizeToAnnotate : 2;

	const TArray<UEdGraphNode_Comment*> Candidates = GatherCandidates(Graph);
	TSet<UEdGraphNode_Comment*> Claimed;

	TArray<TWeakObjectPtr<UEdGraphNode_Comment>>& Tracked = TrackedComments.FindOrAdd(Graph);

	for (FBPGraphCluster& Cluster : InOutClusters)
	{
		if (Cluster.Nodes.Num() < MinClusterSize || !Cluster.Bounds.bIsValid)
		{
			Result.Skipped++;
			continue;
		}

		UEdGraphNode_Comment* Comment = FindExistingComment(Cluster, Candidates, Claimed);
		const bool bReused = (Comment != nullptr);

		if (!bReused)
		{
			Graph->Modify();
			Comment = CreateComment(Graph);

			if (Comment == nullptr)
			{
				Result.Warnings.Add(FString::Printf(
					TEXT("Could not create a comment box for cluster %d (%s)."),
					Cluster.ClusterIndex, *Cluster.GetTypeName()));
				continue;
			}

			Claimed.Add(Comment);
		}

		ApplyComment(Comment, Cluster, Settings);

		Cluster.CommentNode = Comment;
		Tracked.AddUnique(Comment);

		(bReused ? Result.Updated : Result.Created)++;
	}

	// Boxes we recognise but no cluster wanted: the graph has changed shape since the last run.
	// Left alone rather than deleted, because the user may have kept one deliberately.
	for (UEdGraphNode_Comment* Comment : Candidates)
	{
		if (Comment != nullptr && !Claimed.Contains(Comment))
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("Comment box '%s' in '%s' no longer matches any cluster; left as it was."),
				*Comment->NodeComment, *Graph->GetName()));
		}
	}

	UE_LOG(LogBPFormatter, Log,
		TEXT("Annotated '%s': %d comment(s) created, %d updated, %d cluster(s) below the size threshold."),
		*Graph->GetName(), Result.Created, Result.Updated, Result.Skipped);

	return Result;
}
