// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPImporter.h"

#include "BPExporter.h"
#include "BlueprintAIBridgeModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_Variable.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

#include <initializer_list>

#define LOCTEXT_NAMESPACE "BlueprintAIBridge"

namespace BPImporterInternal
{
	/**
	 * FJsonObject's accessors are taken through FString on purpose: engine versions that also
	 * offer FStringView overloads would make a bare TEXT("...") literal ambiguous.
	 */
	bool TryGetString(TSharedPtr<FJsonObject> Object, const FString& Field, FString& OutValue)
	{
		return Object.IsValid() && Object->TryGetStringField(Field, OutValue);
	}

	bool HasField(TSharedPtr<FJsonObject> Object, const FString& Field)
	{
		return Object.IsValid() && Object->HasField(Field);
	}

	/** Reads a string field, returning Fallback when it is missing or not a string. */
	FString GetString(TSharedPtr<FJsonObject> Object, const FString& Field, const FString& Fallback = FString())
	{
		FString Value;
		return TryGetString(Object, Field, Value) ? Value : Fallback;
	}

	TSharedPtr<FJsonObject> GetObject(TSharedPtr<FJsonObject> Object, const FString& Field)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		if (Object.IsValid() && Object->TryGetObjectField(Field, Found) && Found != nullptr)
		{
			return *Found;
		}
		return nullptr;
	}

	TArray<TSharedPtr<FJsonValue>> GetArray(TSharedPtr<FJsonObject> Object, const FString& Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Found = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Found) && Found != nullptr)
		{
			return *Found;
		}
		return TArray<TSharedPtr<FJsonValue>>();
	}

	TSharedPtr<FJsonObject> AsObject(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return nullptr;
		}

		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Value->TryGetObject(Object) && Object != nullptr)
		{
			return *Object;
		}
		return nullptr;
	}

	/** Libraries searched when an added UK2Node_CallFunction gives a name but no owning class. */
	const TArray<UClass*>& GetFallbackFunctionLibraries()
	{
		static TArray<UClass*> Libraries = {
			UKismetSystemLibrary::StaticClass(),
			UKismetMathLibrary::StaticClass(),
			UKismetStringLibrary::StaticClass(),
			UKismetArrayLibrary::StaticClass(),
			UGameplayStatics::StaticClass()
		};
		return Libraries;
	}
}

using namespace BPImporterInternal;

FString FBPImportResult::ToString() const
{
	FString Text = FString::Printf(
		TEXT("%s: %d change(s) applied, %d skipped."),
		bSuccess ? TEXT("Import complete") : TEXT("Import failed"),
		AppliedCount,
		SkippedCount);

	for (const FString& Error : Errors)
	{
		Text += FString::Printf(TEXT("\nERROR: %s"), *Error);
	}
	for (const FString& Warning : Warnings)
	{
		Text += FString::Printf(TEXT("\nWARNING: %s"), *Warning);
	}

	return Text;
}

UEdGraph* FBPImporter::FindOrCreateGraph(UBlueprint* Blueprint, const FString& GraphName, FBPImportResult& Result)
{
	if (Blueprint == nullptr || GraphName.IsEmpty())
	{
		return nullptr;
	}

	TArray<UEdGraph*> Graphs;
	FBPExporter::CollectGraphs(Blueprint, Graphs);

	for (UEdGraph* Graph : Graphs)
	{
		if (Graph != nullptr && Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}

	// Nothing by that name, so the AI is asking for a new function graph.
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*GraphName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());

	if (NewGraph == nullptr)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Could not create graph '%s'."), *GraphName));
		return nullptr;
	}

	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, /*bIsUserCreated*/ true, nullptr);
	UE_LOG(LogBlueprintAIBridge, Log, TEXT("Created new function graph '%s'."), *GraphName);

	return NewGraph;
}

void FBPImporter::PlaceNode(UEdGraph* Graph, UEdGraphNode* Node, int32 SpawnIndex)
{
	if (Graph == nullptr || Node == nullptr)
	{
		return;
	}

	// Drop new nodes in a column to the right of everything that already exists, so an import
	// never buries an added node underneath the graph the developer is looking at.
	int32 RightEdge = 0;
	for (UEdGraphNode* Existing : Graph->Nodes)
	{
		if (Existing != nullptr && Existing != Node)
		{
			RightEdge = FMath::Max(RightEdge, Existing->NodePosX + 300);
		}
	}

	Node->NodePosX = RightEdge + 100;
	Node->NodePosY = SpawnIndex * 200;
}

bool FBPImporter::ConfigureSpawnedNode(
	UBlueprint* Blueprint,
	UK2Node* Node,
	TSharedPtr<FJsonObject> NodeJson,
	FBPImportResult& Result)
{
	if (Node == nullptr)
	{
		return false;
	}

	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		const FString FunctionName = GetString(NodeJson, TEXT("function_name"));
		if (FunctionName.IsEmpty())
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("Added UK2Node_CallFunction '%s' has no 'function_name'; it would spawn with no pins."),
				*GetString(NodeJson, TEXT("title"), TEXT("<untitled>"))));
			return false;
		}

		const FName FunctionFName(*FunctionName);
		UFunction* Function = nullptr;

		const FString FunctionClassName = GetString(NodeJson, TEXT("function_class"));
		if (!FunctionClassName.IsEmpty())
		{
			if (UClass* OwningClass = UClass::TryFindTypeSlow<UClass>(FunctionClassName))
			{
				Function = OwningClass->FindFunctionByName(FunctionFName);
			}
		}

		if (Function == nullptr && Blueprint != nullptr && Blueprint->GeneratedClass != nullptr)
		{
			Function = Blueprint->GeneratedClass->FindFunctionByName(FunctionFName);
		}

		if (Function == nullptr && Blueprint != nullptr && Blueprint->ParentClass != nullptr)
		{
			Function = Blueprint->ParentClass->FindFunctionByName(FunctionFName);
		}

		if (Function == nullptr)
		{
			for (UClass* Library : GetFallbackFunctionLibraries())
			{
				if (Library != nullptr)
				{
					Function = Library->FindFunctionByName(FunctionFName);
					if (Function != nullptr)
					{
						break;
					}
				}
			}
		}

		if (Function == nullptr)
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("Could not resolve function '%s'%s."),
				*FunctionName,
				FunctionClassName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" on class '%s'"), *FunctionClassName)));
			return false;
		}

		CallNode->SetFromFunction(Function);
		return true;
	}

	if (UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
	{
		const FString EventName = GetString(NodeJson, TEXT("event_name"), GetString(NodeJson, TEXT("title")));
		if (EventName.IsEmpty())
		{
			Result.Warnings.Add(TEXT("Added UK2Node_CustomEvent has no 'event_name'."));
			return false;
		}

		CustomEventNode->CustomFunctionName = FName(*EventName);
		return true;
	}

	if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
	{
		const FString EventName = GetString(NodeJson, TEXT("event_name"));
		if (EventName.IsEmpty())
		{
			Result.Warnings.Add(TEXT("Added UK2Node_Event has no 'event_name'."));
			return false;
		}

		UClass* ParentClass = Blueprint ? Blueprint->ParentClass : nullptr;
		if (ParentClass == nullptr || ParentClass->FindFunctionByName(FName(*EventName)) == nullptr)
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("Event '%s' is not overridable on the Blueprint's parent class."), *EventName));
			return false;
		}

		EventNode->EventReference.SetExternalMember(FName(*EventName), ParentClass);
		EventNode->bOverrideFunction = true;
		return true;
	}

	if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
	{
		const FString VariableName = GetString(NodeJson, TEXT("variable_name"));
		if (VariableName.IsEmpty())
		{
			Result.Warnings.Add(TEXT("Added variable node has no 'variable_name'."));
			return false;
		}

		VariableNode->VariableReference.SetSelfMember(FName(*VariableName));
		return true;
	}

	// Every other node type spawns with whatever pins its class allocates by default.
	return true;
}

UK2Node* FBPImporter::SpawnNodeSafe(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const FString& NodeTypeString,
	TSharedPtr<FJsonObject> NodeJson,
	FBPImportResult& Result)
{
	if (Graph == nullptr)
	{
		return nullptr;
	}

	if (NodeTypeString.IsEmpty())
	{
		Result.Warnings.Add(TEXT("Node addition has no 'type' field."));
		return nullptr;
	}

	UClass* NodeClass = UClass::TryFindTypeSlow<UClass>(NodeTypeString);
	if (NodeClass == nullptr)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Unknown node class '%s'."), *NodeTypeString));
		UE_LOG(LogBlueprintAIBridge, Warning, TEXT("SpawnNodeSafe: unknown node class '%s'."), *NodeTypeString);
		return nullptr;
	}

	if (!NodeClass->IsChildOf(UK2Node::StaticClass()) || NodeClass->HasAnyClassFlags(CLASS_Abstract))
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("Node class '%s' is not a concrete UK2Node."), *NodeTypeString));
		return nullptr;
	}

	UK2Node* NewNode = NewObject<UK2Node>(Graph, NodeClass, NAME_None, RF_Transactional);
	if (NewNode == nullptr)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Failed to construct node of class '%s'."), *NodeTypeString));
		return nullptr;
	}

	// References must be set before AllocateDefaultPins, otherwise the node comes up pinless.
	if (!ConfigureSpawnedNode(Blueprint, NewNode, NodeJson, Result))
	{
		NewNode->MarkAsGarbage();
		return nullptr;
	}

	Graph->Modify();
	Graph->AddNode(NewNode, /*bFromUI*/ false, /*bSelectNewNode*/ false);
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	const FString Comment = GetString(NodeJson, TEXT("comment"));
	if (!Comment.IsEmpty())
	{
		NewNode->NodeComment = Comment;
		NewNode->bCommentBubbleVisible = true;
	}

	return NewNode;
}

UEdGraphPin* FBPImporter::ResolvePin(UEdGraphNode* Node, const FString& PinRef, EEdGraphPinDirection Direction)
{
	if (Node == nullptr || PinRef.IsEmpty())
	{
		return nullptr;
	}

	if (UEdGraphPin* Pin = Node->FindPin(FName(*PinRef), Direction))
	{
		return Pin;
	}

	// The AI may answer with the exporter's pin id ("P3") or with different casing than the
	// engine's internal pin name ("exec" vs "execute"), so fall back to a tolerant scan.
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin != nullptr && Pin->Direction == Direction && Pin->PinName.ToString().Equals(PinRef, ESearchCase::IgnoreCase))
		{
			return Pin;
		}
	}

	// Common aliases for the two exec pins, which is what an AI is most likely to name loosely.
	const bool bWantsExecIn = Direction == EGPD_Input && (PinRef.Equals(TEXT("exec"), ESearchCase::IgnoreCase) || PinRef.Equals(TEXT("execute"), ESearchCase::IgnoreCase) || PinRef.Equals(TEXT("in"), ESearchCase::IgnoreCase));
	const bool bWantsExecOut = Direction == EGPD_Output && (PinRef.Equals(TEXT("then"), ESearchCase::IgnoreCase) || PinRef.Equals(TEXT("out"), ESearchCase::IgnoreCase) || PinRef.Equals(TEXT("exec"), ESearchCase::IgnoreCase));

	if (bWantsExecIn || bWantsExecOut)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin != nullptr && Pin->Direction == Direction && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}
	}

	return nullptr;
}

void FBPImporter::ApplyPinDefaults(UK2Node* Node, TSharedPtr<FJsonObject> NodeJson, FBPImportResult& Result)
{
	if (Node == nullptr || !NodeJson.IsValid())
	{
		return;
	}

	UEdGraph* Graph = Node->GetGraph();
	const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
	if (Schema == nullptr)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Node '%s' has no schema; pin defaults not applied."), *Node->GetName()));
		return;
	}

	for (const TSharedPtr<FJsonValue>& PinValue : GetArray(NodeJson, TEXT("pins")))
	{
		TSharedPtr<FJsonObject> PinJson = AsObject(PinValue);
		if (!PinJson.IsValid())
		{
			continue;
		}

		FString DefaultValue;
		if (!TryGetString(PinJson, TEXT("default_value"), DefaultValue))
		{
			continue;
		}

		const FString PinName = GetString(PinJson, TEXT("name"));
		if (PinName.IsEmpty())
		{
			continue;
		}

		// Defaults only ever live on input pins.
		UEdGraphPin* Pin = ResolvePin(Node, PinName, EGPD_Input);
		if (Pin == nullptr)
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("Node '%s' has no input pin named '%s'; default value skipped."),
				*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), *PinName));
			continue;
		}

		if (Pin->LinkedTo.Num() > 0)
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("Pin '%s' on '%s' is connected; its default value was left alone."),
				*PinName, *Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
			continue;
		}

		Node->Modify();
		Schema->TrySetDefaultValue(*Pin, DefaultValue);
	}
}

void FBPImporter::ApplyConnection(
	UEdGraph* Graph,
	TSharedPtr<FJsonObject> ConnectionJson,
	bool bAdd,
	const TMap<FString, UEdGraphNode*>& NodeIdMap,
	FBPImportResult& Result)
{
	if (Graph == nullptr || !ConnectionJson.IsValid())
	{
		Result.SkippedCount++;
		return;
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (Schema == nullptr)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Graph '%s' has no schema; connection skipped."), *Graph->GetName()));
		Result.SkippedCount++;
		return;
	}

	const FString FromNodeId = GetString(ConnectionJson, TEXT("from_node"));
	const FString ToNodeId = GetString(ConnectionJson, TEXT("to_node"));
	const FString FromPinRef = GetString(ConnectionJson, TEXT("from_pin_name"), GetString(ConnectionJson, TEXT("from_pin")));
	const FString ToPinRef = GetString(ConnectionJson, TEXT("to_pin_name"), GetString(ConnectionJson, TEXT("to_pin")));

	UEdGraphNode* const* FromNodeFound = NodeIdMap.Find(FromNodeId);
	UEdGraphNode* const* ToNodeFound = NodeIdMap.Find(ToNodeId);

	if (FromNodeFound == nullptr || *FromNodeFound == nullptr || ToNodeFound == nullptr || *ToNodeFound == nullptr)
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("Connection %s -> %s references a node id that is not in graph '%s'."),
			*FromNodeId, *ToNodeId, *Graph->GetName()));
		Result.SkippedCount++;
		return;
	}

	UEdGraphPin* FromPin = ResolvePin(*FromNodeFound, FromPinRef, EGPD_Output);
	UEdGraphPin* ToPin = ResolvePin(*ToNodeFound, ToPinRef, EGPD_Input);

	if (FromPin == nullptr || ToPin == nullptr)
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("Connection %s.%s -> %s.%s could not be resolved to real pins."),
			*FromNodeId, *FromPinRef, *ToNodeId, *ToPinRef));
		Result.SkippedCount++;
		return;
	}

	(*FromNodeFound)->Modify();
	(*ToNodeFound)->Modify();

	if (bAdd)
	{
		if (!Schema->TryCreateConnection(FromPin, ToPin))
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("The schema refused the connection %s.%s -> %s.%s (incompatible pins?)."),
				*FromNodeId, *FromPinRef, *ToNodeId, *ToPinRef));
			Result.SkippedCount++;
			return;
		}
	}
	else
	{
		Schema->BreakSinglePinLink(FromPin, ToPin);
	}

	Result.AppliedCount++;
}

void FBPImporter::DeleteNodeById(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const FString& NodeId,
	TMap<FString, UEdGraphNode*>& NodeIdMap,
	FBPImportResult& Result)
{
	UEdGraphNode** NodeFound = NodeIdMap.Find(NodeId);
	if (NodeFound == nullptr || *NodeFound == nullptr)
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("Cannot delete '%s': no such node in graph '%s'."), *NodeId, Graph ? *Graph->GetName() : TEXT("<null>")));
		Result.SkippedCount++;
		return;
	}

	UEdGraphNode* Node = *NodeFound;
	if (!Node->CanUserDeleteNode())
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("Node '%s' (%s) cannot be deleted."), *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), *NodeId));
		Result.SkippedCount++;
		return;
	}

	FBlueprintEditorUtils::RemoveNode(Blueprint, Node, /*bDontRecompile*/ true);

	// The id must not resolve to a dead node for any item processed after this one.
	NodeIdMap.Remove(NodeId);
	Result.AppliedCount++;
}

bool FBPImporter::ParseTypeString(const FString& TypeString, FEdGraphPinType& OutPinType)
{
	OutPinType = FEdGraphPinType();

	FString Working = TypeString.TrimStartAndEnd();
	if (Working.IsEmpty())
	{
		return false;
	}

	// Unwrap one level of container syntax: array<int>, set<name>, map<...> .
	auto TryUnwrap = [&Working](const TCHAR* Prefix, EPinContainerType Container, EPinContainerType& OutContainer) -> bool
	{
		const FString PrefixString(Prefix);
		if (Working.StartsWith(PrefixString, ESearchCase::IgnoreCase) && Working.EndsWith(TEXT(">")))
		{
			Working = Working.Mid(PrefixString.Len(), Working.Len() - PrefixString.Len() - 1);
			OutContainer = Container;
			return true;
		}
		return false;
	};

	EPinContainerType ContainerType = EPinContainerType::None;
	if (!TryUnwrap(TEXT("array<"), EPinContainerType::Array, ContainerType))
	{
		TryUnwrap(TEXT("set<"), EPinContainerType::Set, ContainerType);
	}

	// Maps need a value type as well; that is more than an "add a variable" convenience needs.
	if (Working.StartsWith(TEXT("map<"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	// Split "object:Actor" / "struct:Vector" / "byte:EMyEnum" into category and subtype.
	FString Category = Working;
	FString SubTypeName;
	Working.Split(TEXT(":"), &Category, &SubTypeName);
	if (Category.IsEmpty())
	{
		Category = Working;
	}

	Category = Category.ToLower();

	if (Category == TEXT("bool") || Category == TEXT("boolean"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (Category == TEXT("int") || Category == TEXT("int32") || Category == TEXT("integer"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (Category == TEXT("int64"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (Category == TEXT("byte"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else if (Category == TEXT("float") || Category == TEXT("double") || Category == TEXT("real"))
	{
		// UE5 models both float and double as PC_Real with a width subcategory.
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = (Category == TEXT("float")) ? UEdGraphSchema_K2::PC_Float : UEdGraphSchema_K2::PC_Double;
	}
	else if (Category == TEXT("string"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (Category == TEXT("name"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (Category == TEXT("text"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (Category == TEXT("object") || Category == TEXT("class") || Category == TEXT("struct"))
	{
		if (SubTypeName.IsEmpty())
		{
			return false;
		}

		if (Category == TEXT("struct"))
		{
			UScriptStruct* Struct = UClass::TryFindTypeSlow<UScriptStruct>(SubTypeName);
			if (Struct == nullptr)
			{
				return false;
			}
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = Struct;
		}
		else
		{
			UClass* Class = UClass::TryFindTypeSlow<UClass>(SubTypeName);
			if (Class == nullptr)
			{
				return false;
			}
			OutPinType.PinCategory = (Category == TEXT("class")) ? UEdGraphSchema_K2::PC_Class : UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = Class;
		}
	}
	else
	{
		return false;
	}

	OutPinType.ContainerType = ContainerType;
	return true;
}

void FBPImporter::ApplyVariableChange(UBlueprint* Blueprint, const FBPDiffItem& Item, FBPImportResult& Result)
{
	if (Blueprint == nullptr || !Item.PayloadJson.IsValid())
	{
		Result.SkippedCount++;
		return;
	}

	const FString Name = GetString(Item.PayloadJson, TEXT("name"));
	if (Name.IsEmpty())
	{
		Result.Warnings.Add(TEXT("Variable change has no 'name'."));
		Result.SkippedCount++;
		return;
	}

	const FName VarName(*Name);

	if (Item.Type == EBPDiffType::VariableRemoved)
	{
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VarName);
		Result.AppliedCount++;
		return;
	}

	const FString DefaultValue = GetString(Item.PayloadJson, TEXT("default"));
	const FString TypeString = GetString(
		Item.PayloadJson,
		TEXT("type"),
		GetString(GetObject(Item.PayloadJson, TEXT("baseline")), TEXT("type")));

	FEdGraphPinType PinType;
	if (!ParseTypeString(TypeString, PinType))
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("Variable '%s' has an unsupported type '%s'; skipped."), *Name, *TypeString));
		Result.SkippedCount++;
		return;
	}

	if (Item.Type == EBPDiffType::VariableAdded)
	{
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarName, PinType, DefaultValue))
		{
			Result.Warnings.Add(FString::Printf(TEXT("Could not add variable '%s'."), *Name));
			Result.SkippedCount++;
			return;
		}

		Result.AppliedCount++;
		return;
	}

	// VariableModified: the type change first, then the default value on the surviving entry.
	const int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VarName);
	if (VarIndex == INDEX_NONE)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Variable '%s' no longer exists on the Blueprint."), *Name));
		Result.SkippedCount++;
		return;
	}

	if (Blueprint->NewVariables[VarIndex].VarType != PinType)
	{
		FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, VarName, PinType);
	}

	const int32 RefreshedIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VarName);
	if (RefreshedIndex != INDEX_NONE && HasField(Item.PayloadJson, TEXT("default")))
	{
		Blueprint->Modify();
		Blueprint->NewVariables[RefreshedIndex].DefaultValue = DefaultValue;
	}

	Result.AppliedCount++;
}

FBPImportResult FBPImporter::ApplyDiff(UBlueprint* Blueprint, const TArray<FBPDiffItem>& AcceptedItems)
{
	FBPImportResult Result;

	if (Blueprint == nullptr)
	{
		Result.Errors.Add(TEXT("No Blueprint selected."));
		return Result;
	}

	if (AcceptedItems.Num() == 0)
	{
		Result.Errors.Add(TEXT("No changes were accepted, so nothing was applied."));
		return Result;
	}

	const bool bHasEditorTransactions = (GEditor != nullptr);
	if (bHasEditorTransactions)
	{
		GEditor->BeginTransaction(LOCTEXT("BPAIBridgeImport", "Blueprint AI Bridge: Apply Changes"));
	}

	Blueprint->Modify();

	// Node ids are per-graph and positional, so each graph gets its own live map. New nodes are
	// registered into the same map under the id the AI invented, which is what lets a connection
	// in the same batch refer to a node that did not exist a moment ago.
	TMap<FString, TMap<FString, UEdGraphNode*>> GraphNodeMaps;
	TMap<FString, UEdGraph*> ResolvedGraphs;

	auto GetGraph = [&](const FString& GraphName) -> UEdGraph*
	{
		if (UEdGraph** Found = ResolvedGraphs.Find(GraphName))
		{
			return *Found;
		}

		UEdGraph* Graph = FindOrCreateGraph(Blueprint, GraphName, Result);
		ResolvedGraphs.Add(GraphName, Graph);

		if (Graph != nullptr)
		{
			TMap<UEdGraphNode*, FString> NodeToId;
			FBPExporter::BuildNodeIdMap(Graph, NodeToId);

			TMap<FString, UEdGraphNode*>& IdToNode = GraphNodeMaps.FindOrAdd(GraphName);
			for (const TPair<UEdGraphNode*, FString>& Pair : NodeToId)
			{
				IdToNode.Add(Pair.Value, Pair.Key);
			}
		}

		return Graph;
	};

	// Guards against applying a change to a node the graph has since replaced.
	auto BaselineMatches = [](const FBPDiffItem& Item, UEdGraphNode* Node) -> bool
	{
		TSharedPtr<FJsonObject> Baseline = GetObject(Item.PayloadJson, TEXT("baseline"));
		if (!Baseline.IsValid() || Node == nullptr)
		{
			return true;
		}

		const FString ExpectedTitle = GetString(Baseline, TEXT("title"));
		if (ExpectedTitle.IsEmpty())
		{
			return true;
		}

		return Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString() == ExpectedTitle;
	};

	// Ordering matters: additions first so later connections can find the new nodes, deletions
	// last so connections that touch a doomed node are handled while it is still alive.
	auto ItemsOfType = [&AcceptedItems](std::initializer_list<EBPDiffType> Types)
	{
		TArray<const FBPDiffItem*> Matches;
		for (const FBPDiffItem& Item : AcceptedItems)
		{
			for (EBPDiffType Type : Types)
			{
				if (Item.Type == Type)
				{
					Matches.Add(&Item);
					break;
				}
			}
		}
		return Matches;
	};

	// 1. Variable additions, so nodes that reference them can resolve.
	for (const FBPDiffItem* Item : ItemsOfType({ EBPDiffType::VariableAdded }))
	{
		ApplyVariableChange(Blueprint, *Item, Result);
	}

	// 2. Node additions.
	int32 SpawnIndex = 0;
	for (const FBPDiffItem* Item : ItemsOfType({ EBPDiffType::NodeAdded }))
	{
		UEdGraph* Graph = GetGraph(Item->GraphName);
		if (Graph == nullptr)
		{
			Result.Warnings.Add(FString::Printf(TEXT("Graph '%s' could not be resolved; node '%s' skipped."), *Item->GraphName, *Item->NodeId));
			Result.SkippedCount++;
			continue;
		}

		UK2Node* NewNode = SpawnNodeSafe(Blueprint, Graph, GetString(Item->PayloadJson, TEXT("type")), Item->PayloadJson, Result);
		if (NewNode == nullptr)
		{
			Result.SkippedCount++;
			continue;
		}

		PlaceNode(Graph, NewNode, SpawnIndex++);
		ApplyPinDefaults(NewNode, Item->PayloadJson, Result);

		GraphNodeMaps.FindOrAdd(Item->GraphName).Add(Item->NodeId, NewNode);
		Result.AppliedCount++;
	}

	// 3. Node modifications.
	for (const FBPDiffItem* Item : ItemsOfType({ EBPDiffType::NodeModified }))
	{
		UEdGraph* Graph = GetGraph(Item->GraphName);
		if (Graph == nullptr)
		{
			Result.SkippedCount++;
			continue;
		}

		UEdGraphNode** NodeFound = GraphNodeMaps.FindOrAdd(Item->GraphName).Find(Item->NodeId);
		if (NodeFound == nullptr || *NodeFound == nullptr)
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("Cannot modify '%s': no such node in graph '%s'."), *Item->NodeId, *Item->GraphName));
			Result.SkippedCount++;
			continue;
		}

		if (!BaselineMatches(*Item, *NodeFound))
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("Node '%s' in graph '%s' no longer matches the exported snapshot; skipped. Re-export and try again."),
				*Item->NodeId, *Item->GraphName));
			Result.SkippedCount++;
			continue;
		}

		(*NodeFound)->Modify();

		FString Comment;
		if (Item->PayloadJson.IsValid() && TryGetString(Item->PayloadJson, TEXT("comment"), Comment))
		{
			(*NodeFound)->NodeComment = Comment;
			(*NodeFound)->bCommentBubbleVisible = !Comment.IsEmpty();
		}

		if (UK2Node* K2Node = Cast<UK2Node>(*NodeFound))
		{
			ApplyPinDefaults(K2Node, Item->PayloadJson, Result);
		}

		Result.AppliedCount++;
	}

	// 4. Connection removals, then additions.
	for (const FBPDiffItem* Item : ItemsOfType({ EBPDiffType::ConnectionRemoved }))
	{
		UEdGraph* Graph = GetGraph(Item->GraphName);
		ApplyConnection(Graph, Item->PayloadJson, /*bAdd*/ false, GraphNodeMaps.FindOrAdd(Item->GraphName), Result);
	}

	for (const FBPDiffItem* Item : ItemsOfType({ EBPDiffType::ConnectionAdded }))
	{
		UEdGraph* Graph = GetGraph(Item->GraphName);
		ApplyConnection(Graph, Item->PayloadJson, /*bAdd*/ true, GraphNodeMaps.FindOrAdd(Item->GraphName), Result);
	}

	// 5. Node deletions.
	for (const FBPDiffItem* Item : ItemsOfType({ EBPDiffType::NodeDeleted }))
	{
		UEdGraph* Graph = GetGraph(Item->GraphName);
		if (Graph == nullptr)
		{
			Result.SkippedCount++;
			continue;
		}

		TMap<FString, UEdGraphNode*>& IdToNode = GraphNodeMaps.FindOrAdd(Item->GraphName);

		UEdGraphNode** NodeFound = IdToNode.Find(Item->NodeId);
		if (NodeFound != nullptr && *NodeFound != nullptr && !BaselineMatches(*Item, *NodeFound))
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("Node '%s' in graph '%s' no longer matches the exported snapshot; deletion skipped."),
				*Item->NodeId, *Item->GraphName));
			Result.SkippedCount++;
			continue;
		}

		DeleteNodeById(Blueprint, Graph, Item->NodeId, IdToNode, Result);
	}

	// 6. Remaining variable work.
	for (const FBPDiffItem* Item : ItemsOfType({ EBPDiffType::VariableModified, EBPDiffType::VariableRemoved }))
	{
		ApplyVariableChange(Blueprint, *Item, Result);
	}

	if (Result.AppliedCount > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}

	if (bHasEditorTransactions)
	{
		GEditor->EndTransaction();
	}

	Result.bSuccess = (Result.AppliedCount > 0) && (Result.Errors.Num() == 0);

	UE_LOG(LogBlueprintAIBridge, Log, TEXT("%s"), *Result.ToString());

	return Result;
}

#undef LOCTEXT_NAMESPACE
