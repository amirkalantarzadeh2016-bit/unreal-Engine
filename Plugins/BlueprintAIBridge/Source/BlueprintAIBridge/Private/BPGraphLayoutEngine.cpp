// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPGraphLayoutEngine.h"

#include "BPExporter.h"
#include "BPFormatterSettings.h"
#include "BPGraphAnnotator.h"
#include "BPGraphClusterDetector.h"
#include "BPSnapshotStore.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "BlueprintAIBridge"

DEFINE_LOG_CATEGORY(LogBPFormatter);

namespace BPLayoutInternal
{
	/** Extra clearance added on top of a node's own footprint before the next one starts. */
	constexpr float NodeMargin = 40.0f;

	/** Fallbacks for the size estimate, in the units the graph editor uses. */
	constexpr float MinNodeWidth = 160.0f;
	constexpr float MaxNodeWidth = 460.0f;
	constexpr float NodeHeaderHeight = 48.0f;
	constexpr float NodePinHeight = 26.0f;
	constexpr float NodeFooterHeight = 16.0f;

	/**
	 * Opens an editor transaction and guarantees it is closed.
	 *
	 * The formatter has several early-out paths; an unbalanced BeginTransaction would leave the
	 * editor's undo buffer open and swallow the user's next action, so the close is tied to
	 * scope rather than to remembering it on each return.
	 */
	class FScopedFormatTransaction
	{
	public:
		explicit FScopedFormatTransaction(const FText& Description)
		{
			if (GEditor != nullptr)
			{
				GEditor->BeginTransaction(Description);
				bOpen = true;
			}
		}

		~FScopedFormatTransaction()
		{
			if (bOpen && GEditor != nullptr)
			{
				GEditor->EndTransaction();
			}
		}

		FScopedFormatTransaction(const FScopedFormatTransaction&) = delete;
		FScopedFormatTransaction& operator=(const FScopedFormatTransaction&) = delete;

		bool IsOpen() const { return bOpen; }

	private:
		bool bOpen = false;
	};

	FString BoundsToString(const FBox2D& Box)
	{
		if (!Box.bIsValid)
		{
			return TEXT("<empty>");
		}

		return FString::Printf(
			TEXT("(%.0f, %.0f)-(%.0f, %.0f) [%.0f x %.0f]"),
			Box.Min.X, Box.Min.Y, Box.Max.X, Box.Max.Y,
			Box.GetSize().X, Box.GetSize().Y);
	}
}

using namespace BPLayoutInternal;

// ---------------------------------------------------------------------------------------
// Options and result
// ---------------------------------------------------------------------------------------

FBPLayoutOptions FBPLayoutOptions::FromSettings(const UBPAIBridgeSettings* Settings)
{
	FBPLayoutOptions Options;

	if (Settings != nullptr)
	{
		Options.HorizontalPadding = Settings->HorizontalPadding;
		Options.VerticalPadding = Settings->VerticalPadding;
		Options.ClusterSpacing = Settings->ClusterSpacing;
	}

	return Options;
}

FString FBPFormatResult::ToString() const
{
	FString Text;

	if (bSuccess)
	{
		Text = FString::Printf(
			TEXT("Format complete: %d graph(s), %d node(s), %d cluster(s); %d comment(s) created, %d updated."),
			GraphCount, NodeCount, ClusterCount, CommentsCreated, CommentsUpdated);
	}
	else if (GraphCount == 0 && Warnings.Num() == 0)
	{
		// Nothing went wrong; there was simply nothing in scope worth moving.
		Text = TEXT("Format: nothing to do. The selected graphs have no nodes to lay out.");
	}
	else
	{
		Text = FString::Printf(TEXT("Format failed: %d graph(s) formatted."), GraphCount);
	}

	for (const FString& Warning : Warnings)
	{
		Text += FString::Printf(TEXT("\nWARNING: %s"), *Warning);
	}

	return Text;
}

// ---------------------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------------------

FVector2D FBPGraphLayoutEngine::EstimateNodeSize(UEdGraphNode* Node)
{
	if (Node == nullptr)
	{
		return FVector2D(MinNodeWidth, NodeHeaderHeight);
	}

	// Resized nodes (comments, and anything the user has dragged out) carry a real size.
	if (Node->NodeWidth > 0 && Node->NodeHeight > 0)
	{
		return FVector2D(static_cast<float>(Node->NodeWidth), static_cast<float>(Node->NodeHeight));
	}

	int32 InputPins = 0;
	int32 OutputPins = 0;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin == nullptr || Pin->bHidden || Pin->bOrphanedPin)
		{
			continue;
		}

		(Pin->Direction == EGPD_Input ? InputPins : OutputPins)++;
	}

	const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
	const float Width = FMath::Clamp(Title.Len() * 9.0f + 80.0f, MinNodeWidth, MaxNodeWidth);
	const float Height = NodeHeaderHeight + FMath::Max(InputPins, OutputPins) * NodePinHeight + NodeFooterHeight;

	return FVector2D(Width, Height);
}

FBox2D FBPGraphLayoutEngine::ComputeNodesBounds(const TArray<UEdGraphNode*>& Nodes)
{
	FBox2D Bounds(ForceInit);

	for (UEdGraphNode* Node : Nodes)
	{
		if (Node == nullptr)
		{
			continue;
		}

		const FVector2D Min(static_cast<float>(Node->NodePosX), static_cast<float>(Node->NodePosY));
		Bounds += Min;
		Bounds += Min + EstimateNodeSize(Node);
	}

	return Bounds;
}

FBox2D FBPGraphLayoutEngine::ComputeGraphBounds(UEdGraph* Graph)
{
	TArray<UEdGraphNode*> Nodes;

	if (Graph != nullptr)
	{
		for (const auto& Element : Graph->Nodes)
		{
			if (UEdGraphNode* Node = Element)
			{
				Nodes.Add(Node);
			}
		}
	}

	return ComputeNodesBounds(Nodes);
}

bool FBPGraphLayoutEngine::IsExecPin(const UEdGraphPin* Pin)
{
	return Pin != nullptr && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
}

void FBPGraphLayoutEngine::SetNodePosition(UEdGraphNode* Node, const FVector2D& Position)
{
	if (Node == nullptr)
	{
		return;
	}

	// Marked before the write so undo restores the original coordinates.
	Node->Modify();
	Node->NodePosX = FMath::RoundToInt32(Position.X);
	Node->NodePosY = FMath::RoundToInt32(Position.Y);
}

// ---------------------------------------------------------------------------------------
// Layering
// ---------------------------------------------------------------------------------------

void FBPGraphLayoutEngine::AssignLayers(
	const TArray<UEdGraphNode*>& Nodes,
	const TMap<UEdGraphNode*, int32>& NodeToIndex,
	TArray<int32>& OutLayers)
{
	const int32 Count = Nodes.Num();
	OutLayers.Init(0, Count);

	if (Count == 0)
	{
		return;
	}

	struct FEdge
	{
		int32 From = 0;
		int32 To = 0;
	};

	TArray<FEdge> ExecEdges;
	TArray<FEdge> DataEdges;

	TArray<bool> bHasExecPin;
	bHasExecPin.Init(false, Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		for (UEdGraphPin* Pin : Nodes[Index]->Pins)
		{
			if (Pin == nullptr || Pin->bOrphanedPin)
			{
				continue;
			}

			const bool bExec = IsExecPin(Pin);
			bHasExecPin[Index] |= bExec;

			if (Pin->Direction != EGPD_Output)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin == nullptr)
				{
					continue;
				}

				const int32* TargetIndex = NodeToIndex.Find(LinkedPin->GetOwningNodeUnchecked());
				if (TargetIndex == nullptr || *TargetIndex == Index)
				{
					continue;
				}

				(bExec ? ExecEdges : DataEdges).Add({ Index, *TargetIndex });
			}
		}
	}

	// Longest path by relaxation, capped at Count sweeps. That bound is enough to settle any
	// acyclic graph, and it is also what stops a cyclic one -- an exec loop back-edge -- from
	// running away. Relaxation only ever pushes a node rightwards, so a later pass over a wider
	// edge set refines the ranking instead of discarding it.
	auto RelaxLongestPath = [Count, &OutLayers](const TArray<FEdge>& Edges) -> bool
	{
		for (int32 Sweep = 0; Sweep < Count; ++Sweep)
		{
			bool bChanged = false;
			for (const FEdge& Edge : Edges)
			{
				if (OutLayers[Edge.To] < OutLayers[Edge.From] + 1)
				{
					OutLayers[Edge.To] = OutLayers[Edge.From] + 1;
					bChanged = true;
				}
			}

			if (!bChanged)
			{
				return true;
			}
		}

		return false;
	};

	// Pass 1: exec flow alone decides the backbone of the ranking.
	if (!RelaxLongestPath(ExecEdges))
	{
		UE_LOG(LogBPFormatter, Verbose,
			TEXT("Exec layering did not settle within %d sweeps; the graph contains a cycle and the ranking is approximate."),
			Count);
	}

	// Pass 2: data edges running between two exec-capable nodes are a real ordering constraint
	// wherever no exec edge already covers it, so fold them in and relax again.
	TArray<FEdge> RefinedEdges = ExecEdges;
	for (const FEdge& Edge : DataEdges)
	{
		if (bHasExecPin[Edge.From] && bHasExecPin[Edge.To])
		{
			RefinedEdges.Add(Edge);
		}
	}

	if (RefinedEdges.Num() != ExecEdges.Num())
	{
		RelaxLongestPath(RefinedEdges);
	}

	// Pass 3: pure and data-only nodes have no rank of their own. Seat each one immediately to
	// the left of the earliest node that consumes it, and repeat so that chains of pure nodes
	// fan out leftwards instead of piling into a single column.
	TArray<TArray<int32>> DataConsumers;
	DataConsumers.SetNum(Count);
	for (const FEdge& Edge : DataEdges)
	{
		if (!bHasExecPin[Edge.From])
		{
			DataConsumers[Edge.From].AddUnique(Edge.To);
		}
	}

	for (int32 Sweep = 0; Sweep < Count; ++Sweep)
	{
		bool bChanged = false;

		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (bHasExecPin[Index] || DataConsumers[Index].Num() == 0)
			{
				continue;
			}

			int32 EarliestConsumer = TNumericLimits<int32>::Max();
			for (int32 ConsumerIndex : DataConsumers[Index])
			{
				EarliestConsumer = FMath::Min(EarliestConsumer, OutLayers[ConsumerIndex]);
			}

			const int32 Desired = EarliestConsumer - 1;
			if (OutLayers[Index] != Desired)
			{
				OutLayers[Index] = Desired;
				bChanged = true;
			}
		}

		if (!bChanged)
		{
			break;
		}
	}

	// Pure nodes can be pushed to negative layers; shift the whole cluster so the leftmost is 0.
	int32 MinLayer = TNumericLimits<int32>::Max();
	for (int32 Layer : OutLayers)
	{
		MinLayer = FMath::Min(MinLayer, Layer);
	}

	if (MinLayer != 0 && MinLayer != TNumericLimits<int32>::Max())
	{
		for (int32& Layer : OutLayers)
		{
			Layer -= MinLayer;
		}
	}
}

void FBPGraphLayoutEngine::OrderWithinLayers(
	const TArray<UEdGraphNode*>& Nodes,
	const TMap<UEdGraphNode*, int32>& NodeToIndex,
	TArray<TArray<int32>>& InOutLayerContents)
{
	// Predecessor and successor lists, used to pull a node towards the nodes it connects to.
	TArray<TArray<int32>> Predecessors;
	TArray<TArray<int32>> Successors;
	Predecessors.SetNum(Nodes.Num());
	Successors.SetNum(Nodes.Num());

	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		for (UEdGraphPin* Pin : Nodes[Index]->Pins)
		{
			if (Pin == nullptr || Pin->Direction != EGPD_Output || Pin->bOrphanedPin)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin == nullptr)
				{
					continue;
				}

				const int32* TargetIndex = NodeToIndex.Find(LinkedPin->GetOwningNodeUnchecked());
				if (TargetIndex != nullptr && *TargetIndex != Index)
				{
					Successors[Index].AddUnique(*TargetIndex);
					Predecessors[*TargetIndex].AddUnique(Index);
				}
			}
		}
	}

	TArray<int32> RowOf;
	RowOf.Init(0, Nodes.Num());

	auto RefreshRows = [&RowOf, &InOutLayerContents]()
	{
		for (const TArray<int32>& Layer : InOutLayerContents)
		{
			for (int32 Row = 0; Row < Layer.Num(); ++Row)
			{
				RowOf[Layer[Row]] = Row;
			}
		}
	};

	RefreshRows();

	// Barycentre sweeps: forward passes settle a layer against the one to its left, backward
	// passes against the one to its right. Two round trips is the usual point of diminishing
	// returns for graphs this size.
	auto SweepLayer = [&](TArray<int32>& Layer, const TArray<TArray<int32>>& Neighbours)
	{
		TMap<int32, float> Barycentre;
		Barycentre.Reserve(Layer.Num());

		for (int32 NodeIndex : Layer)
		{
			const TArray<int32>& Linked = Neighbours[NodeIndex];
			if (Linked.Num() == 0)
			{
				// Nothing to align against; hold position so stable nodes stop drifting.
				Barycentre.Add(NodeIndex, static_cast<float>(RowOf[NodeIndex]));
				continue;
			}

			float Sum = 0.0f;
			for (int32 LinkedIndex : Linked)
			{
				Sum += static_cast<float>(RowOf[LinkedIndex]);
			}
			Barycentre.Add(NodeIndex, Sum / static_cast<float>(Linked.Num()));
		}

		Layer.StableSort([&Barycentre](int32 A, int32 B)
		{
			return Barycentre[A] < Barycentre[B];
		});
	};

	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 LayerIndex = 1; LayerIndex < InOutLayerContents.Num(); ++LayerIndex)
		{
			SweepLayer(InOutLayerContents[LayerIndex], Predecessors);
			RefreshRows();
		}

		for (int32 LayerIndex = InOutLayerContents.Num() - 2; LayerIndex >= 0; --LayerIndex)
		{
			SweepLayer(InOutLayerContents[LayerIndex], Successors);
			RefreshRows();
		}
	}
}

// ---------------------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------------------

FBox2D FBPGraphLayoutEngine::PlaceCluster(
	FBPGraphCluster& Cluster,
	const FBPLayoutOptions& Options,
	float OriginY)
{
	TArray<UEdGraphNode*> Nodes;
	Nodes.Reserve(Cluster.Nodes.Num());
	for (UEdGraphNode* Node : Cluster.Nodes)
	{
		if (Node != nullptr)
		{
			Nodes.Add(Node);
		}
	}

	if (Nodes.Num() == 0)
	{
		return FBox2D(ForceInit);
	}

	TMap<UEdGraphNode*, int32> NodeToIndex;
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		NodeToIndex.Add(Nodes[Index], Index);
	}

	TArray<int32> Layers;
	AssignLayers(Nodes, NodeToIndex, Layers);

	int32 LayerCount = 0;
	for (int32 Layer : Layers)
	{
		LayerCount = FMath::Max(LayerCount, Layer + 1);
	}

	TArray<TArray<int32>> LayerContents;
	LayerContents.SetNum(LayerCount);

	// Seed each layer in the graph's own node order, so a re-run on an already-tidy graph is
	// stable rather than reshuffling equivalent nodes.
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		LayerContents[Layers[Index]].Add(Index);
	}

	OrderWithinLayers(Nodes, NodeToIndex, LayerContents);

	// Column steps. HorizontalPadding is the step, not the gap, so the defaults land on the
	// familiar Blueprint grid -- but a layer holding an unusually wide node widens its own step
	// rather than letting that node overlap the next column.
	TArray<float> LayerX;
	LayerX.SetNumZeroed(LayerCount);

	float CurrentX = 0.0f;
	for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
	{
		LayerX[LayerIndex] = CurrentX;

		float WidestInLayer = 0.0f;
		for (int32 NodeIndex : LayerContents[LayerIndex])
		{
			WidestInLayer = FMath::Max(WidestInLayer, static_cast<float>(EstimateNodeSize(Nodes[NodeIndex]).X));
		}

		CurrentX += FMath::Max(Options.HorizontalPadding, WidestInLayer + NodeMargin);
	}

	// Rows, same rule vertically.
	FBox2D Bounds(ForceInit);

	for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
	{
		float CurrentY = OriginY;

		for (int32 NodeIndex : LayerContents[LayerIndex])
		{
			UEdGraphNode* Node = Nodes[NodeIndex];
			const FVector2D Size = EstimateNodeSize(Node);
			const FVector2D Position(LayerX[LayerIndex], CurrentY);

			SetNodePosition(Node, Position);

			Bounds += Position;
			Bounds += Position + Size;

			CurrentY += FMath::Max(Options.VerticalPadding, static_cast<float>(Size.Y) + NodeMargin);
		}
	}

	Cluster.Bounds = Bounds;
	return Bounds;
}

void FBPGraphLayoutEngine::LayoutClusters(
	UEdGraph* Graph,
	TArray<FBPGraphCluster>& InOutClusters,
	const FBPLayoutOptions& Options)
{
	if (Graph == nullptr)
	{
		return;
	}

	float CurrentY = 0.0f;

	for (FBPGraphCluster& Cluster : InOutClusters)
	{
		const FBox2D Bounds = PlaceCluster(Cluster, Options, CurrentY);

		if (Bounds.bIsValid)
		{
			CurrentY = Bounds.Max.Y + Options.ClusterSpacing;
		}
	}
}

// ---------------------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------------------

FBPFormatResult FBPGraphLayoutEngine::FormatBlueprint(UBlueprint* Blueprint, const TArray<UEdGraph*>& Graphs)
{
	FBPFormatResult Result;

	if (Blueprint == nullptr)
	{
		Result.Warnings.Add(TEXT("No Blueprint selected."));
		return Result;
	}

	if (Graphs.Num() == 0)
	{
		Result.Warnings.Add(TEXT("No graphs to format."));
		return Result;
	}

	// The same graph arriving twice would be laid out twice, and the second pass would then
	// annotate against bounds the first pass had already moved.
	TArray<UEdGraph*> UniqueGraphs;
	UniqueGraphs.Reserve(Graphs.Num());
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph != nullptr)
		{
			UniqueGraphs.AddUnique(Graph);
		}
	}

	if (UniqueGraphs.Num() == 0)
	{
		Result.Warnings.Add(TEXT("No valid graphs to format."));
		return Result;
	}

	const UBPAIBridgeSettings* Settings = GetDefault<UBPAIBridgeSettings>();
	const FBPLayoutOptions Options = FBPLayoutOptions::FromSettings(Settings);

	// Snapshot before anything is touched. This records the Blueprint's logical structure, not
	// its geometry, so it is a safety net for the graph rather than an undo for the layout --
	// Ctrl+Z is what restores positions.
	FBPExportOptions ExportOptions;
	const FString ExportJson = FBPExporter::ExportBlueprint(Blueprint, ExportOptions);
	if (ExportJson.IsEmpty())
	{
		Result.Warnings.Add(TEXT("Could not export a pre-format snapshot; continuing without one."));
	}
	else if (FBPSnapshotStore::SaveSnapshot(Blueprint->GetPathName(), ExportJson).IsEmpty())
	{
		Result.Warnings.Add(TEXT("Pre-format snapshot could not be written to disk; continuing without one."));
	}

	FScopedFormatTransaction Transaction(LOCTEXT("BPFormatGraph", "Blueprint AI Bridge: Format Graph"));
	if (!Transaction.IsOpen())
	{
		Result.Warnings.Add(TEXT("No editor transaction available; formatting was not applied."));
		return Result;
	}

	Blueprint->Modify();

	for (UEdGraph* Graph : UniqueGraphs)
	{
		TArray<FBPGraphCluster> Clusters = FBPGraphClusterDetector::DetectClusters(Graph);
		if (Clusters.Num() == 0)
		{
			UE_LOG(LogBPFormatter, Log, TEXT("Graph '%s' has no layout-eligible nodes; skipped."), *Graph->GetName());
			continue;
		}

		const FBox2D BoundsBefore = ComputeGraphBounds(Graph);

		int32 GraphNodeCount = 0;
		for (const FBPGraphCluster& Cluster : Clusters)
		{
			GraphNodeCount += Cluster.Nodes.Num();
		}

		UE_LOG(LogBPFormatter, Log,
			TEXT("Formatting '%s': %d node(s) in %d cluster(s). Bounds before: %s"),
			*Graph->GetName(), GraphNodeCount, Clusters.Num(), *BoundsToString(BoundsBefore));

		for (const FBPGraphCluster& Cluster : Clusters)
		{
			UE_LOG(LogBPFormatter, Verbose,
				TEXT("  Cluster %d: %s, %d node(s)."),
				Cluster.ClusterIndex, *Cluster.GetTypeName(), Cluster.Nodes.Num());
		}

		Graph->Modify();
		LayoutClusters(Graph, Clusters, Options);

		if (Settings == nullptr || Settings->bAnnotateClusters)
		{
			const FBPAnnotationResult Annotation = FBPGraphAnnotator::AnnotateClusters(Graph, Clusters, Settings);
			Result.CommentsCreated += Annotation.Created;
			Result.CommentsUpdated += Annotation.Updated;
			Result.Warnings.Append(Annotation.Warnings);
		}

		const FBox2D BoundsAfter = ComputeGraphBounds(Graph);

		UE_LOG(LogBPFormatter, Log,
			TEXT("Formatted '%s'. Bounds after: %s"), *Graph->GetName(), *BoundsToString(BoundsAfter));

		Result.GraphCount++;
		Result.NodeCount += GraphNodeCount;
		Result.ClusterCount += Clusters.Num();
		Result.BoundsBefore += BoundsBefore;
		Result.BoundsAfter += BoundsAfter;

		Graph->NotifyGraphChanged();
	}

	if (Result.GraphCount > 0)
	{
		// Structural: a new comment node changes the graph's contents, not just its geometry.
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		Result.bSuccess = true;
	}

	UE_LOG(LogBPFormatter, Log, TEXT("%s"), *Result.ToString());

	return Result;
}

#undef LOCTEXT_NAMESPACE
