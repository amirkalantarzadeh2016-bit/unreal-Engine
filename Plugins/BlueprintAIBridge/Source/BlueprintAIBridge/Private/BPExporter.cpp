// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPExporter.h"

#include "BPFormatterSettings.h"
#include "BlueprintAIBridgeModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Knot.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/EngineVersion.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#define LOCTEXT_NAMESPACE "BlueprintAIBridge"

namespace
{
	/** Engine version string reported in the export header. */
	FString GetEngineVersionString()
	{
		return FString::Printf(TEXT("%d.%d"), FEngineVersion::Current().GetMajor(), FEngineVersion::Current().GetMinor());
	}
}

FBPExportOptions FBPExportOptions::FromSettings()
{
	FBPExportOptions Options;

	if (const UBPAIBridgeSettings* Settings = GetDefault<UBPAIBridgeSettings>())
	{
		Options.bCompactJson = Settings->bCompactExportJson;
		Options.bOmitUntouchedPinDefaults = Settings->bOmitUntouchedPinDefaults;
		Options.bCollapseUntouchedPins = Settings->bCollapseUntouchedPins;
		Options.bExportCommentBoxes = Settings->bExportCommentBoxes;
	}

	return Options;
}

FString FBPExporter::MakeNodeId(int32 Index)
{
	return FString::Printf(TEXT("N%d"), Index + 1);
}

FString FBPExporter::GetContextString(EExportContext Context)
{
	switch (Context)
	{
	case EExportContext::BugFix:         return TEXT("bug_fix");
	case EExportContext::Refactor:       return TEXT("refactor");
	case EExportContext::FeatureRequest: return TEXT("feature_request");
	case EExportContext::CodeReview:     return TEXT("code_review");
	case EExportContext::General:        return TEXT("general");
	default:                             return TEXT("general");
	}
}

void FBPExporter::CollectGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs)
{
	OutGraphs.Reset();

	if (Blueprint == nullptr)
	{
		return;
	}

	// Generic over the container: UE5 declares these as TArray<TObjectPtr<UEdGraph>>.
	auto AppendGraphs = [&OutGraphs](const auto& Source)
	{
		for (const auto& Element : Source)
		{
			if (UEdGraph* Graph = Element)
			{
				OutGraphs.AddUnique(Graph);
			}
		}
	};

	AppendGraphs(Blueprint->UbergraphPages);
	AppendGraphs(Blueprint->FunctionGraphs);
	AppendGraphs(Blueprint->MacroGraphs);
}

void FBPExporter::BuildNodeIdMap(UEdGraph* Graph, TMap<UEdGraphNode*, FString>& OutNodeToId)
{
	OutNodeToId.Reset();

	if (Graph == nullptr)
	{
		return;
	}

	// Ids are positional. The importer reproduces this exact walk to map an AI reply back onto
	// live nodes, so the ordering here is load-bearing -- do not sort or filter.
	int32 Index = 0;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node == nullptr)
		{
			++Index;
			continue;
		}

		OutNodeToId.Add(Node, MakeNodeId(Index));
		++Index;
	}
}

FString FBPExporter::GetGraphTypeString(UBlueprint* Blueprint, UEdGraph* Graph)
{
	if (Blueprint == nullptr || Graph == nullptr)
	{
		return TEXT("Unknown");
	}

	if (Blueprint->UbergraphPages.Contains(Graph))
	{
		return TEXT("EventGraph");
	}
	if (Blueprint->FunctionGraphs.Contains(Graph))
	{
		return TEXT("Function");
	}
	if (Blueprint->MacroGraphs.Contains(Graph))
	{
		return TEXT("Macro");
	}

	return TEXT("Unknown");
}

FString FBPExporter::PinTypeToString(const FEdGraphPinType& PinType)
{
	// Categories and subcategories are FNames whose casing varies by node ("Exec" from a macro
	// tunnel, "exec" from a K2 schema pin), which makes the same type read as two types. Only
	// the category is normalised -- a subcategory object's name is a real type name.
	FString Inner = PinType.PinCategory.ToString().ToLower();

	// UE5 splits real numbers into a category + float/double subcategory; report the concrete type.
	if (!PinType.PinSubCategory.IsNone())
	{
		Inner = PinType.PinSubCategory.ToString().ToLower();
	}
	else if (PinType.PinSubCategoryObject.IsValid())
	{
		Inner = FString::Printf(TEXT("%s:%s"), *Inner, *PinType.PinSubCategoryObject->GetName());
	}

	switch (PinType.ContainerType)
	{
	case EPinContainerType::Array:
		return FString::Printf(TEXT("array<%s>"), *Inner);
	case EPinContainerType::Set:
		return FString::Printf(TEXT("set<%s>"), *Inner);
	case EPinContainerType::Map:
		return FString::Printf(TEXT("map<%s,%s>"), *Inner, *PinType.PinValueType.TerminalCategory.ToString());
	default:
		return Inner;
	}
}

bool FBPExporter::ShouldSkipPin(UEdGraphPin* Pin)
{
	if (Pin == nullptr)
	{
		return true;
	}

	// Hidden and orphaned pins are editor plumbing; they add tokens without adding meaning.
	if (Pin->bHidden || Pin->bOrphanedPin)
	{
		return true;
	}

	return false;
}

FString FBPExporter::GetRelevantComment(UEdGraphNode* Node)
{
	if (Node == nullptr || Node->NodeComment.IsEmpty())
	{
		return FString();
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin != nullptr && Pin->LinkedTo.Num() > 0)
		{
			return Node->NodeComment;
		}
	}

	return FString();
}

bool FBPExporter::HasOverriddenDefault(UEdGraphPin* Pin, const FBPExportOptions& Options)
{
	if (Pin == nullptr)
	{
		return false;
	}

	if (!Pin->DefaultValue.IsEmpty())
	{
		// A pin still carrying the value its node was created with tells the reader nothing the
		// node type does not already imply, and most pins in a real graph are in that state.
		return !Options.bOmitUntouchedPinDefaults || Pin->DefaultValue != Pin->AutogeneratedDefaultValue;
	}

	return Pin->DefaultObject != nullptr || !Pin->DefaultTextValue.IsEmpty();
}

TSharedPtr<FJsonObject> FBPExporter::SerializePin(UEdGraphPin* Pin, const FBPExportOptions& Options)
{
	if (Pin == nullptr)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PinJson = MakeShared<FJsonObject>();
	PinJson->SetStringField(TEXT("name"), Pin->PinName.ToString());
	PinJson->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
	PinJson->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));

	if (HasOverriddenDefault(Pin, Options))
	{
		if (!Pin->DefaultValue.IsEmpty())
		{
			PinJson->SetStringField(TEXT("default_value"), Pin->DefaultValue);
		}
		else if (Pin->DefaultObject != nullptr)
		{
			PinJson->SetStringField(TEXT("default_value"), Pin->DefaultObject->GetPathName());
		}
		else
		{
			PinJson->SetStringField(TEXT("default_value"), Pin->DefaultTextValue.ToString());
		}
	}

	return PinJson;
}

TSharedPtr<FJsonObject> FBPExporter::SerializeNode(
	UEdGraphNode* Node,
	const TMap<UEdGraphNode*, FString>& NodeIdMap,
	const FBPExportOptions& Options)
{
	if (Node == nullptr)
	{
		return nullptr;
	}

	const FString* NodeId = NodeIdMap.Find(Node);
	if (NodeId == nullptr)
	{
		UE_LOG(LogBlueprintAIBridge, Warning, TEXT("SerializeNode: node '%s' is missing from the id map; skipping."), *Node->GetName());
		return nullptr;
	}

	TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();
	NodeJson->SetStringField(TEXT("id"), *NodeId);
	NodeJson->SetStringField(TEXT("type"), Node->GetClass()->GetName());
	NodeJson->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

	const FString Comment = GetRelevantComment(Node);
	if (!Comment.IsEmpty())
	{
		NodeJson->SetStringField(TEXT("comment"), Comment);
	}

	// A pin that is unconnected and still at its factory value carries no information beyond its
	// own existence, and those are most of the pins in a real graph. They stay listed by name so
	// the node's full signature is still visible, but they do not each cost an object.
	TArray<TSharedPtr<FJsonValue>> PinValues;
	TArray<TSharedPtr<FJsonValue>> UnsetPinNames;
	bool bHasOverriddenPinValue = false;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (ShouldSkipPin(Pin))
		{
			continue;
		}

		TSharedPtr<FJsonObject> PinJson = SerializePin(Pin, Options);
		if (!PinJson.IsValid())
		{
			continue;
		}

		const bool bOverridden = HasOverriddenDefault(Pin, Options);
		bHasOverriddenPinValue |= bOverridden;

		const bool bCarriesInformation =
			Pin->LinkedTo.Num() > 0
			|| bOverridden
			|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;

		if (bCarriesInformation || !Options.bCollapseUntouchedPins)
		{
			PinValues.Add(MakeShared<FJsonValueObject>(PinJson));
		}
		else
		{
			UnsetPinNames.Add(MakeShared<FJsonValueString>(Pin->PinName.ToString()));
		}
	}

	// A variable getter's pins are its variable (named by the node, typed by the variables list)
	// plus an optional "self"; a reroute node's are always InputPin and OutputPin. Nothing in
	// either list has to be written down -- and in a real graph these are a third of the nodes.
	const bool bPinsAreImplied =
		Options.bCollapseUntouchedPins
		&& UnsetPinNames.Num() + PinValues.Num() > 0
		&& (Node->IsA<UK2Node_VariableGet>() || Node->IsA<UK2Node_Knot>())
		&& !bHasOverriddenPinValue;

	if (!bPinsAreImplied)
	{
		NodeJson->SetArrayField(TEXT("pins"), PinValues);
		if (UnsetPinNames.Num() > 0)
		{
			NodeJson->SetArrayField(TEXT("unset_pins"), UnsetPinNames);
		}
	}

	return NodeJson;
}

TArray<TSharedPtr<FJsonValue>> FBPExporter::SerializeBlueprintVariables(UBlueprint* Blueprint)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (Blueprint == nullptr)
	{
		return Values;
	}

	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarJson = MakeShared<FJsonObject>();
		VarJson->SetStringField(TEXT("name"), Variable.VarName.ToString());
		VarJson->SetStringField(TEXT("type"), PinTypeToString(Variable.VarType));
		// Position says the scope: this array is the Blueprint's, a graph's is that graph's locals.
		if (!Variable.DefaultValue.IsEmpty())
		{
			VarJson->SetStringField(TEXT("default"), Variable.DefaultValue);
		}
		Values.Add(MakeShared<FJsonValueObject>(VarJson));
	}

	return Values;
}

TArray<TSharedPtr<FJsonValue>> FBPExporter::SerializeLocalVariables(UEdGraph* Graph)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (Graph == nullptr)
	{
		return Values;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node);
		if (Entry == nullptr)
		{
			continue;
		}

		for (const FBPVariableDescription& Variable : Entry->LocalVariables)
		{
			TSharedPtr<FJsonObject> VarJson = MakeShared<FJsonObject>();
			VarJson->SetStringField(TEXT("name"), Variable.VarName.ToString());
			VarJson->SetStringField(TEXT("type"), PinTypeToString(Variable.VarType));
			if (!Variable.DefaultValue.IsEmpty())
			{
				VarJson->SetStringField(TEXT("default"), Variable.DefaultValue);
			}
			Values.Add(MakeShared<FJsonValueObject>(VarJson));
		}
	}

	return Values;
}

TArray<TSharedPtr<FJsonValue>> FBPExporter::SerializeCommentBoxes(
	UEdGraph* Graph,
	const TMap<UEdGraphNode*, FString>& NodeIdMap)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (Graph == nullptr)
	{
		return Values;
	}

	for (const auto& Element : Graph->Nodes)
	{
		UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(ToRawPtr(Element));
		if (Comment == nullptr || Comment->NodeComment.IsEmpty())
		{
			continue;
		}

		const int32 Left = Comment->NodePosX;
		const int32 Top = Comment->NodePosY;
		const int32 Right = Left + Comment->NodeWidth;
		const int32 Bottom = Top + Comment->NodeHeight;

		// Containment by position rather than by the comment's own NodesUnderComment: the editor
		// only refreshes that set when the box is moved, so it goes stale as the graph is edited.
		TArray<TSharedPtr<FJsonValue>> ContainedIds;
		for (const TPair<UEdGraphNode*, FString>& Pair : NodeIdMap)
		{
			UEdGraphNode* Node = Pair.Key;
			if (Node == nullptr || Node == Comment || Node->IsA<UEdGraphNode_Comment>())
			{
				continue;
			}

			if (Node->NodePosX >= Left && Node->NodePosX <= Right
				&& Node->NodePosY >= Top && Node->NodePosY <= Bottom)
			{
				ContainedIds.Add(MakeShared<FJsonValueString>(Pair.Value));
			}
		}

		if (ContainedIds.Num() == 0)
		{
			continue;
		}

		TSharedPtr<FJsonObject> CommentJson = MakeShared<FJsonObject>();
		CommentJson->SetStringField(TEXT("title"), Comment->NodeComment);
		CommentJson->SetArrayField(TEXT("nodes"), ContainedIds);
		Values.Add(MakeShared<FJsonValueObject>(CommentJson));
	}

	return Values;
}

TSharedPtr<FJsonObject> FBPExporter::SerializeGraph(UBlueprint* Blueprint, UEdGraph* Graph, const FBPExportOptions& Options)
{
	if (Graph == nullptr)
	{
		return nullptr;
	}

	TMap<UEdGraphNode*, FString> NodeIdMap;
	BuildNodeIdMap(Graph, NodeIdMap);

	TSharedPtr<FJsonObject> GraphJson = MakeShared<FJsonObject>();
	GraphJson->SetStringField(TEXT("graph_name"), Graph->GetName());
	GraphJson->SetStringField(TEXT("graph_type"), GetGraphTypeString(Blueprint, Graph));

	const TArray<TSharedPtr<FJsonValue>> LocalVariables = SerializeLocalVariables(Graph);
	if (LocalVariables.Num() > 0)
	{
		GraphJson->SetArrayField(TEXT("variables"), LocalVariables);
	}

	TArray<TSharedPtr<FJsonValue>> NodeValues;
	for (const auto& Element : Graph->Nodes)
	{
		if (TSharedPtr<FJsonObject> NodeJson = SerializeNode(Element, NodeIdMap, Options))
		{
			NodeValues.Add(MakeShared<FJsonValueObject>(NodeJson));
		}
	}
	GraphJson->SetArrayField(TEXT("nodes"), NodeValues);

	// Walk output pins only, so every link is recorded exactly once and always source -> sink.
	// Endpoints are pin names: they are unique within a node and direction, and they are the
	// form an AI reads and writes naturally, so there is nothing to gain from a parallel id.
	TArray<TSharedPtr<FJsonValue>> ConnectionValues;
	for (const auto& Element : Graph->Nodes)
	{
		UEdGraphNode* Node = Element;
		if (Node == nullptr)
		{
			continue;
		}

		const FString* FromNodeId = NodeIdMap.Find(Node);
		if (FromNodeId == nullptr)
		{
			continue;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin == nullptr || Pin->Direction != EGPD_Output || ShouldSkipPin(Pin))
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin == nullptr || LinkedPin->GetOwningNodeUnchecked() == nullptr)
				{
					continue;
				}

				const FString* ToNodeId = NodeIdMap.Find(LinkedPin->GetOwningNode());
				if (ToNodeId == nullptr)
				{
					continue;
				}

				TSharedPtr<FJsonObject> ConnJson = MakeShared<FJsonObject>();
				ConnJson->SetStringField(TEXT("from_node"), *FromNodeId);
				ConnJson->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
				ConnJson->SetStringField(TEXT("to_node"), *ToNodeId);
				ConnJson->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());

				ConnectionValues.Add(MakeShared<FJsonValueObject>(ConnJson));
			}
		}
	}
	GraphJson->SetArrayField(TEXT("connections"), ConnectionValues);

	if (Options.bExportCommentBoxes)
	{
		const TArray<TSharedPtr<FJsonValue>> Comments = SerializeCommentBoxes(Graph, NodeIdMap);
		if (Comments.Num() > 0)
		{
			GraphJson->SetArrayField(TEXT("comments"), Comments);
		}
	}

	return GraphJson;
}

FString FBPExporter::ExportBlueprint(UBlueprint* Blueprint, const FBPExportOptions& Options)
{
	if (Blueprint == nullptr)
	{
		UE_LOG(LogBlueprintAIBridge, Warning, TEXT("ExportBlueprint: null Blueprint."));
		return FString();
	}

	TArray<UEdGraph*> AllGraphs;
	CollectGraphs(Blueprint, AllGraphs);

	// Apply the name filter first, so chunk indices refer to the filtered list the user sees.
	TArray<UEdGraph*> CandidateGraphs;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Options.GraphFilter.Num() == 0 || Options.GraphFilter.Contains(Graph->GetName()))
		{
			CandidateGraphs.Add(Graph);
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	Root->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Root->SetStringField(TEXT("export_context"), GetContextString(Options.Context));
	Root->SetStringField(TEXT("ue_version"), GetEngineVersionString());
	const TArray<TSharedPtr<FJsonValue>> BlueprintVariables = SerializeBlueprintVariables(Blueprint);
	if (BlueprintVariables.Num() > 0)
	{
		Root->SetArrayField(TEXT("variables"), BlueprintVariables);
	}

	TArray<UEdGraph*> GraphsToExport;
	if (Options.bChunkedMode)
	{
		if (!CandidateGraphs.IsValidIndex(Options.ChunkGraphIndex))
		{
			UE_LOG(LogBlueprintAIBridge, Warning,
				TEXT("ExportBlueprint: chunk index %d is out of range (%d graphs)."),
				Options.ChunkGraphIndex, CandidateGraphs.Num());
			return FString();
		}

		UEdGraph* ChunkGraph = CandidateGraphs[Options.ChunkGraphIndex];
		GraphsToExport.Add(ChunkGraph);

		TSharedPtr<FJsonObject> ChunkInfo = MakeShared<FJsonObject>();
		ChunkInfo->SetNumberField(TEXT("graph_index"), Options.ChunkGraphIndex);
		ChunkInfo->SetNumberField(TEXT("total_graphs"), CandidateGraphs.Num());
		ChunkInfo->SetStringField(TEXT("graph_name"), ChunkGraph->GetName());
		Root->SetObjectField(TEXT("chunk_info"), ChunkInfo);
	}
	else
	{
		GraphsToExport = CandidateGraphs;
	}

	TArray<TSharedPtr<FJsonValue>> GraphValues;
	for (UEdGraph* Graph : GraphsToExport)
	{
		if (TSharedPtr<FJsonObject> GraphJson = SerializeGraph(Blueprint, Graph, Options))
		{
			GraphValues.Add(MakeShared<FJsonValueObject>(GraphJson));
		}
	}
	Root->SetArrayField(TEXT("graphs"), GraphValues);

	FString Output;
	bool bSerialized = false;

	if (Options.bCompactJson)
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		bSerialized = FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	}
	else
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		bSerialized = FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	}

	if (!bSerialized)
	{
		UE_LOG(LogBlueprintAIBridge, Error, TEXT("ExportBlueprint: JSON serialisation failed for '%s'."), *Blueprint->GetName());
		return FString();
	}

	return Output;
}

FString FBPExporter::BuildPromptPrefix(UBlueprint* Blueprint, EExportContext Context, const FString& UserTask)
{
	FBPExportOptions Options;
	Options.Context = Context;
	return BuildPromptPrefix(Blueprint, Options, UserTask);
}

FString FBPExporter::BuildPromptPrefix(UBlueprint* Blueprint, const FBPExportOptions& Options, const FString& UserTask)
{
	const FString BlueprintName = Blueprint ? Blueprint->GetName() : TEXT("<no Blueprint selected>");

	FString Prefix;
	Prefix += TEXT("## Task\n");
	Prefix += UserTask.IsEmpty()
		? FString(TEXT("Review this Blueprint and report anything that looks wrong or worth improving. "
			"Do not propose changes unless you are confident."))
		: UserTask;
	Prefix += TEXT("\n\n");

	Prefix += TEXT("## Context\n");
	Prefix += FString::Printf(TEXT("- Blueprint: %s\n"), *BlueprintName);
	Prefix += FString::Printf(TEXT("- Purpose: %s\n"), *GetContextString(Options.Context));
	Prefix += FString::Printf(TEXT("- UE Version: %s\n"), *GetEngineVersionString());

	if (Options.bChunkedMode && Blueprint != nullptr)
	{
		TArray<UEdGraph*> AllGraphs;
		CollectGraphs(Blueprint, AllGraphs);

		const int32 TotalGraphs = Options.GraphFilter.Num() > 0 ? Options.GraphFilter.Num() : AllGraphs.Num();
		Prefix += FString::Printf(
			TEXT("\nNote: This is graph %d of %d. Focus only on this graph.\n"),
			Options.ChunkGraphIndex + 1,
			TotalGraphs);
	}

	Prefix += TEXT("\n## Instructions for AI\n");
	Prefix += TEXT("Return ONLY valid JSON matching the same schema. Only include nodes and connections ");
	Prefix += TEXT("you are changing or adding -- a reply that repeats unchanged nodes is treated as a ");
	Prefix += TEXT("full document and anything you leave out of a section you do mention reads as a ");
	Prefix += TEXT("deletion. Mark every entry with an \"action\" field: \"add\", \"modify\" or \"delete\" ");
	Prefix += TEXT("for nodes, \"add\" or \"remove\" for connections. Do not change node ids for existing ");
	Prefix += FString::Printf(
		TEXT("unchanged nodes, and do not invent node types that do not exist in Unreal Engine %s.\n\n"),
		*GetEngineVersionString());

	Prefix += TEXT("Notes on the payload below:\n");
	Prefix += TEXT("- Node ids are positional (N1 is the graph's first node) and are only valid for this ");
	Prefix += TEXT("export.\n");
	Prefix += TEXT("- Connections name pins directly: \"from_pin\" and \"to_pin\" are pin names, which are ");
	Prefix += TEXT("unique within a node and direction.\n");
	Prefix += TEXT("- A pin with no \"default_value\" is still at the value its node was created with; ");
	Prefix += TEXT("only overridden values are listed.\n");
	Prefix += TEXT("- \"unset_pins\" lists, by name only, the pins that are neither connected nor ");
	Prefix += TEXT("overridden. They exist and can be connected to or given a value like any other.\n");
	Prefix += TEXT("- A variable getter or reroute node has no \"pins\" array because its pins are ");
	Prefix += TEXT("implied: a getter exposes its variable plus an optional \"self\", a reroute node ");
	Prefix += TEXT("\"InputPin\" and \"OutputPin\". Connections name them normally.\n");
	Prefix += TEXT("- Blueprint-scope variables are listed once at the top level; a graph's \"variables\" ");
	Prefix += TEXT("array holds that graph's local variables only. Either is omitted when empty.\n");
	Prefix += TEXT("- A graph's \"comments\" array is the author's own labelling: each entry names a region ");
	Prefix += TEXT("and lists the nodes inside it.\n");
	Prefix += TEXT("- For an added UK2Node_CallFunction include \"function_name\" (and \"function_class\" when ");
	Prefix += TEXT("it is not a standard Kismet library); for UK2Node_Event include \"event_name\"; for ");
	Prefix += TEXT("UK2Node_VariableGet/Set include \"variable_name\".\n");

	return Prefix;
}

#undef LOCTEXT_NAMESPACE
