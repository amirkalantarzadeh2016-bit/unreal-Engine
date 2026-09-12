// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPGraphClusterDetector.h"

#include "BPGraphLayoutEngine.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "K2Node.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Switch.h"
#include "K2Node_VariableSet.h"

namespace BPClusterDetectorInternal
{
	/** Disjoint-set forest with path compression and union by size. */
	class FUnionFind
	{
	public:
		explicit FUnionFind(int32 Count)
		{
			Parent.SetNum(Count);
			Size.Init(1, Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Parent[Index] = Index;
			}
		}

		int32 Find(int32 Index)
		{
			while (Parent[Index] != Index)
			{
				Parent[Index] = Parent[Parent[Index]];
				Index = Parent[Index];
			}
			return Index;
		}

		void Union(int32 A, int32 B)
		{
			int32 RootA = Find(A);
			int32 RootB = Find(B);
			if (RootA == RootB)
			{
				return;
			}

			if (Size[RootA] < Size[RootB])
			{
				Swap(RootA, RootB);
			}

			Parent[RootB] = RootA;
			Size[RootA] += Size[RootB];
		}

	private:
		TArray<int32> Parent;
		TArray<int32> Size;
	};

	/** Lowercased full title, used for the name-based error-handling test. */
	FString GetLowerTitle(UEdGraphNode* Node)
	{
		return Node ? Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().ToLower() : FString();
	}
}

using namespace BPClusterDetectorInternal;

bool FBPGraphClusterDetector::ShouldIgnoreNode(UEdGraphNode* Node)
{
	if (Node == nullptr)
	{
		return true;
	}

	// Comment boxes are annotation, not logic. Laying them out alongside real nodes would both
	// scramble the annotation and let a stale box glue two unrelated clusters together.
	if (Node->IsA<UEdGraphNode_Comment>())
	{
		return true;
	}

	return false;
}

bool FBPGraphClusterDetector::IsEventNode(UEdGraphNode* Node)
{
	return Node != nullptr && Node->IsA<UK2Node_Event>();
}

bool FBPGraphClusterDetector::IsPureNode(UEdGraphNode* Node)
{
	const UK2Node* K2Node = Cast<UK2Node>(Node);
	if (K2Node == nullptr)
	{
		return false;
	}

	// Reroute nodes carry no semantics; they should not stop a cluster from reading as pure.
	if (K2Node->IsA<UK2Node_Knot>())
	{
		return true;
	}

	return K2Node->IsNodePure();
}

bool FBPGraphClusterDetector::IsControlFlowNode(UEdGraphNode* Node)
{
	if (Node == nullptr)
	{
		return false;
	}

	if (Node->IsA<UK2Node_IfThenElse>() || Node->IsA<UK2Node_Switch>())
	{
		return true;
	}

	// ForEachLoop, ForLoop and WhileLoop are macro instances, so there is no class to test
	// against -- the macro graph's own name is the only thing that identifies them.
	if (const UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
	{
		if (const UEdGraph* MacroGraph = MacroNode->GetMacroGraph())
		{
			const FString MacroName = MacroGraph->GetName().ToLower();
			if (MacroName.Contains(TEXT("loop")) || MacroName.Contains(TEXT("foreach")) || MacroName.Contains(TEXT("while")))
			{
				return true;
			}
		}
	}

	return false;
}

bool FBPGraphClusterDetector::IsErrorHandlingNode(UEdGraphNode* Node)
{
	const FString Title = GetLowerTitle(Node);
	if (Title.IsEmpty())
	{
		return false;
	}

	return Title.Contains(TEXT("error")) || Title.Contains(TEXT("validate")) || Title.Contains(TEXT("validation"));
}

bool FBPGraphClusterDetector::IsOutputNode(UEdGraphNode* Node)
{
	return Node != nullptr && (Node->IsA<UK2Node_VariableSet>() || Node->IsA<UK2Node_FunctionResult>());
}

EBPClusterType FBPGraphClusterDetector::ClassifyCluster(const TArray<UEdGraphNode*>& Nodes)
{
	if (Nodes.Num() == 0)
	{
		return EBPClusterType::Logic;
	}

	bool bHasEvent = false;
	bool bAllPure = true;
	bool bHasControlFlow = false;
	bool bHasErrorHandling = false;
	bool bHasOutput = false;

	for (UEdGraphNode* Node : Nodes)
	{
		if (Node == nullptr)
		{
			continue;
		}

		bHasEvent |= IsEventNode(Node);
		bHasControlFlow |= IsControlFlowNode(Node);
		bHasErrorHandling |= IsErrorHandlingNode(Node);
		bHasOutput |= IsOutputNode(Node);

		if (!IsPureNode(Node))
		{
			bAllPure = false;
		}
	}

	if (bHasEvent)          { return EBPClusterType::Event; }
	if (bAllPure)           { return EBPClusterType::Pure; }
	if (bHasControlFlow)    { return EBPClusterType::ControlFlow; }
	if (bHasErrorHandling)  { return EBPClusterType::ErrorHandling; }
	if (bHasOutput)         { return EBPClusterType::Output; }

	return EBPClusterType::Logic;
}

TArray<FBPGraphCluster> FBPGraphClusterDetector::DetectClusters(UEdGraph* Graph)
{
	TArray<FBPGraphCluster> Clusters;

	if (Graph == nullptr)
	{
		return Clusters;
	}

	// Index only the nodes the formatter is willing to move.
	TArray<UEdGraphNode*> Nodes;
	TMap<UEdGraphNode*, int32> NodeToIndex;
	for (const auto& Element : Graph->Nodes)
	{
		UEdGraphNode* Node = Element;
		if (ShouldIgnoreNode(Node))
		{
			continue;
		}

		NodeToIndex.Add(Node, Nodes.Num());
		Nodes.Add(Node);
	}

	if (Nodes.Num() == 0)
	{
		return Clusters;
	}

	FUnionFind Sets(Nodes.Num());

	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		for (UEdGraphPin* Pin : Nodes[Index]->Pins)
		{
			if (Pin == nullptr)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin == nullptr)
				{
					continue;
				}

				UEdGraphNode* LinkedNode = LinkedPin->GetOwningNodeUnchecked();
				if (const int32* LinkedIndex = NodeToIndex.Find(LinkedNode))
				{
					Sets.Union(Index, *LinkedIndex);
				}
			}
		}
	}

	// Group by representative, preserving the graph's node order inside each cluster.
	TMap<int32, int32> RootToCluster;
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const int32 Root = Sets.Find(Index);

		int32* ClusterIndex = RootToCluster.Find(Root);
		if (ClusterIndex == nullptr)
		{
			ClusterIndex = &RootToCluster.Add(Root, Clusters.Num());
			Clusters.AddDefaulted();
		}

		Clusters[*ClusterIndex].Nodes.Add(Nodes[Index]);
	}

	// Biggest first: the main execution path should get cluster 0 in the logs and the first
	// vertical slot when the layout engine stacks clusters.
	Clusters.Sort([](const FBPGraphCluster& A, const FBPGraphCluster& B)
	{
		return A.Nodes.Num() > B.Nodes.Num();
	});

	for (int32 Index = 0; Index < Clusters.Num(); ++Index)
	{
		Clusters[Index].ClusterIndex = Index;
		Clusters[Index].Type = ClassifyCluster(Clusters[Index].Nodes);
		Clusters[Index].Bounds = FBPGraphLayoutEngine::ComputeNodesBounds(Clusters[Index].Nodes);
		Clusters[Index].PreLayoutBounds = Clusters[Index].Bounds;
	}

	return Clusters;
}
