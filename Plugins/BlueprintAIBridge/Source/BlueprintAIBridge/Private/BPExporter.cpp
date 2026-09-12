// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPExporter.h"

#include "BlueprintAIBridgeModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/EngineVersion.h"
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
	FString Inner = PinType.PinCategory.ToString();

	// UE5 splits real numbers into a category + float/double subcategory; report the concrete type.
	if (!PinType.PinSubCategory.IsNone())
	{
		Inner = PinType.PinSubCategory.ToString();
	}
	else if (PinType.PinSubCategoryObject.IsValid())
	{
		Inner = FString::Printf(TEXT("%s:%s"), *PinType.PinCategory.ToString(), *PinType.PinSubCategoryObject->GetName());
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

TSharedPtr<FJsonObject> FBPExporter::SerializePin(UEdGraphPin* Pin, const FString& PinId)
{
	if (Pin == nullptr)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PinJson = MakeShared<FJsonObject>();
	PinJson->SetStringField(TEXT("pin_id"), PinId);
	PinJson->SetStringField(TEXT("name"), Pin->PinName.ToString());
	PinJson->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
	PinJson->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));

	if (!Pin->DefaultValue.IsEmpty())
	{
		PinJson->SetStringField(TEXT("default_value"), Pin->DefaultValue);
	}
	else if (Pin->DefaultObject != nullptr)
	{
		PinJson->SetStringField(TEXT("default_value"), Pin->DefaultObject->GetPathName());
	}
	else if (!Pin->DefaultTextValue.IsEmpty())
	{
		PinJson->SetStringField(TEXT("default_value"), Pin->DefaultTextValue.ToString());
	}

	return PinJson;
}

TSharedPtr<FJsonObject> FBPExporter::SerializeNode(
	UEdGraphNode* Node,
	const TMap<UEdGraphNode*, FString>& NodeIdMap,
	int32& InOutPinCounter,
	TMap<UEdGraphPin*, FString>& OutPinIdMap)
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

	TArray<TSharedPtr<FJsonValue>> PinValues;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (ShouldSkipPin(Pin))
		{
			continue;
		}

		const FString PinId = FString::Printf(TEXT("P%d"), ++InOutPinCounter);
		OutPinIdMap.Add(Pin, PinId);

		if (TSharedPtr<FJsonObject> PinJson = SerializePin(Pin, PinId))
		{
			PinValues.Add(MakeShared<FJsonValueObject>(PinJson));
		}
	}
	NodeJson->SetArrayField(TEXT("pins"), PinValues);

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
		VarJson->SetStringField(TEXT("default"), Variable.DefaultValue);
		VarJson->SetStringField(TEXT("scope"), TEXT("blueprint"));
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
			VarJson->SetStringField(TEXT("default"), Variable.DefaultValue);
			VarJson->SetStringField(TEXT("scope"), TEXT("local"));
			Values.Add(MakeShared<FJsonValueObject>(VarJson));
		}
	}

	return Values;
}

TSharedPtr<FJsonObject> FBPExporter::SerializeGraph(UBlueprint* Blueprint, UEdGraph* Graph)
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
	GraphJson->SetArrayField(TEXT("variables"), SerializeLocalVariables(Graph));

	TMap<UEdGraphPin*, FString> PinIdMap;
	int32 PinCounter = 0;

	TArray<TSharedPtr<FJsonValue>> NodeValues;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (TSharedPtr<FJsonObject> NodeJson = SerializeNode(Node, NodeIdMap, PinCounter, PinIdMap))
		{
			NodeValues.Add(MakeShared<FJsonValueObject>(NodeJson));
		}
	}
	GraphJson->SetArrayField(TEXT("nodes"), NodeValues);

	// Walk output pins only, so every link is recorded exactly once and always source -> sink.
	TArray<TSharedPtr<FJsonValue>> ConnectionValues;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
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

			const FString* FromPinId = PinIdMap.Find(Pin);

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

				const FString* ToPinId = PinIdMap.Find(LinkedPin);

				TSharedPtr<FJsonObject> ConnJson = MakeShared<FJsonObject>();
				ConnJson->SetStringField(TEXT("from_node"), *FromNodeId);
				ConnJson->SetStringField(TEXT("from_pin"), FromPinId ? **FromPinId : Pin->PinName.ToString());
				ConnJson->SetStringField(TEXT("from_pin_name"), Pin->PinName.ToString());
				ConnJson->SetStringField(TEXT("to_node"), *ToNodeId);
				ConnJson->SetStringField(TEXT("to_pin"), ToPinId ? **ToPinId : LinkedPin->PinName.ToString());
				ConnJson->SetStringField(TEXT("to_pin_name"), LinkedPin->PinName.ToString());

				ConnectionValues.Add(MakeShared<FJsonValueObject>(ConnJson));
			}
		}
	}
	GraphJson->SetArrayField(TEXT("connections"), ConnectionValues);

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
	Root->SetArrayField(TEXT("variables"), SerializeBlueprintVariables(Blueprint));

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
		if (TSharedPtr<FJsonObject> GraphJson = SerializeGraph(Blueprint, Graph))
		{
			GraphValues.Add(MakeShared<FJsonValueObject>(GraphJson));
		}
	}
	Root->SetArrayField(TEXT("graphs"), GraphValues);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
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
	Prefix += UserTask.IsEmpty() ? FString(TEXT("(no task description provided)")) : UserTask;
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
	Prefix += TEXT("you are changing or adding. Mark every entry with an \"action\" field: \"add\", \"modify\" ");
	Prefix += TEXT("or \"delete\" for nodes, \"add\" or \"remove\" for connections. Do not invent node types ");
	Prefix += TEXT("that do not exist in Unreal Engine 5.3. Do not change node ids for existing unchanged nodes.\n\n");
	Prefix += TEXT("Notes on the payload below:\n");
	Prefix += TEXT("- Blueprint-scope variables are listed once at the top level; a graph's \"variables\" ");
	Prefix += TEXT("array holds that graph's local variables only.\n");
	Prefix += TEXT("- Connections identify pins by \"from_pin\"/\"to_pin\" (the pin id) and carry the readable ");
	Prefix += TEXT("pin name alongside; you may answer with either form.\n");
	Prefix += TEXT("- For an added UK2Node_CallFunction include \"function_name\" (and \"function_class\" when ");
	Prefix += TEXT("it is not a standard Kismet library); for UK2Node_Event include \"event_name\"; for ");
	Prefix += TEXT("UK2Node_VariableGet/Set include \"variable_name\".\n");

	return Prefix;
}

#undef LOCTEXT_NAMESPACE
