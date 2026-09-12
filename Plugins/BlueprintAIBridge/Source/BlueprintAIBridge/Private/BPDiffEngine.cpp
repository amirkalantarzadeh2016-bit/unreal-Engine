// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPDiffEngine.h"

#include "BlueprintAIBridgeModule.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "BlueprintAIBridge"

namespace BPDiffEngineInternal
{
	/** Parses a JSON document, reporting failures through OutError rather than asserting. */
	bool ParseObject(const FString& Json, const TCHAR* Label, TSharedPtr<FJsonObject>& OutObject, FString* OutError)
	{
		OutObject.Reset();

		if (Json.IsEmpty())
		{
			if (OutError != nullptr)
			{
				*OutError = FString::Printf(TEXT("%s JSON is empty."), Label);
			}
			return false;
		}

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
		{
			if (OutError != nullptr)
			{
				*OutError = FString::Printf(TEXT("%s JSON could not be parsed: %s"), Label, *Reader->GetErrorMessage());
			}
			OutObject.Reset();
			return false;
		}

		return true;
	}

	/** Reads an array field without assuming it exists. Returns an empty array when absent. */
	TArray<TSharedPtr<FJsonValue>> GetArray(TSharedPtr<FJsonObject> Object, const FString& Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Found = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Found) && Found != nullptr)
		{
			return *Found;
		}
		return TArray<TSharedPtr<FJsonValue>>();
	}

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

	/** Lowercased "action" field, or an empty string when absent. */
	FString GetAction(TSharedPtr<FJsonObject> Object)
	{
		return GetString(Object, TEXT("action")).ToLower();
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

	/** Indexes an array of objects by the value of KeyField. */
	TMap<FString, TSharedPtr<FJsonObject>> IndexByField(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& KeyField)
	{
		TMap<FString, TSharedPtr<FJsonObject>> Map;
		for (const TSharedPtr<FJsonValue>& Value : Values)
		{
			TSharedPtr<FJsonObject> Object = AsObject(Value);
			if (!Object.IsValid())
			{
				continue;
			}

			const FString Key = GetString(Object, KeyField);
			if (!Key.IsEmpty())
			{
				Map.Add(Key, Object);
			}
		}
		return Map;
	}

	/** Deep-ish copy: enough to hand a payload to the importer without aliasing the reply. */
	TSharedPtr<FJsonObject> CloneObject(TSharedPtr<FJsonObject> Source)
	{
		if (!Source.IsValid())
		{
			return MakeShared<FJsonObject>();
		}

		TSharedPtr<FJsonObject> Copy = MakeShared<FJsonObject>();
		Copy->Values = Source->Values;
		return Copy;
	}

	/** Map of pin name -> default value for a node object. */
	TMap<FString, FString> GetPinDefaults(TSharedPtr<FJsonObject> NodeJson)
	{
		TMap<FString, FString> Defaults;
		for (const TSharedPtr<FJsonValue>& PinValue : GetArray(NodeJson, TEXT("pins")))
		{
			TSharedPtr<FJsonObject> PinJson = AsObject(PinValue);
			if (!PinJson.IsValid())
			{
				continue;
			}

			const FString PinName = GetString(PinJson, TEXT("name"));
			if (!PinName.IsEmpty())
			{
				Defaults.Add(PinName, GetString(PinJson, TEXT("default_value")));
			}
		}
		return Defaults;
	}
}

using namespace BPDiffEngineInternal;

FString FBPDiffEngine::DiffTypeToString(EBPDiffType Type)
{
	switch (Type)
	{
	case EBPDiffType::NodeAdded:         return TEXT("Node Added");
	case EBPDiffType::NodeModified:      return TEXT("Node Modified");
	case EBPDiffType::NodeDeleted:       return TEXT("Node Deleted");
	case EBPDiffType::ConnectionAdded:   return TEXT("Connection Added");
	case EBPDiffType::ConnectionRemoved: return TEXT("Connection Removed");
	case EBPDiffType::VariableAdded:     return TEXT("Variable Added");
	case EBPDiffType::VariableModified:  return TEXT("Variable Modified");
	case EBPDiffType::VariableRemoved:   return TEXT("Variable Removed");
	default:                             return TEXT("Change");
	}
}

FString FBPDiffEngine::DiffTypeToGlyph(EBPDiffType Type)
{
	switch (Type)
	{
	case EBPDiffType::NodeAdded:
	case EBPDiffType::ConnectionAdded:
	case EBPDiffType::VariableAdded:
		return TEXT("+");
	case EBPDiffType::NodeDeleted:
	case EBPDiffType::ConnectionRemoved:
	case EBPDiffType::VariableRemoved:
		return TEXT("-");
	default:
		return TEXT("~");
	}
}

void FBPDiffEngine::AttachBaseline(TSharedPtr<FJsonObject> Payload, TSharedPtr<FJsonObject> BaseNode)
{
	if (Payload.IsValid() && BaseNode.IsValid())
	{
		Payload->SetObjectField(TEXT("baseline"), BaseNode);
	}
}

bool FBPDiffEngine::DetectDeltaMode(TSharedPtr<FJsonObject> Root)
{
	if (!Root.IsValid())
	{
		return false;
	}

	auto AnyHasAction = [](const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		for (const TSharedPtr<FJsonValue>& Value : Values)
		{
			TSharedPtr<FJsonObject> Object = AsObject(Value);
			if (Object.IsValid() && HasField(Object, TEXT("action")))
			{
				return true;
			}
		}
		return false;
	};

	if (AnyHasAction(GetArray(Root, TEXT("variables"))))
	{
		return true;
	}

	for (const TSharedPtr<FJsonValue>& GraphValue : GetArray(Root, TEXT("graphs")))
	{
		TSharedPtr<FJsonObject> GraphJson = AsObject(GraphValue);
		if (!GraphJson.IsValid())
		{
			continue;
		}

		if (AnyHasAction(GetArray(GraphJson, TEXT("nodes")))
			|| AnyHasAction(GetArray(GraphJson, TEXT("connections")))
			|| AnyHasAction(GetArray(GraphJson, TEXT("variables"))))
		{
			return true;
		}
	}

	return false;
}

void FBPDiffEngine::BuildPinIdToNameMap(const TArray<TSharedPtr<FJsonValue>>& BaseNodes, TMap<FString, FString>& OutMap)
{
	for (const TSharedPtr<FJsonValue>& NodeValue : BaseNodes)
	{
		TSharedPtr<FJsonObject> NodeJson = AsObject(NodeValue);
		if (!NodeJson.IsValid())
		{
			continue;
		}

		for (const TSharedPtr<FJsonValue>& PinValue : GetArray(NodeJson, TEXT("pins")))
		{
			TSharedPtr<FJsonObject> PinJson = AsObject(PinValue);
			if (!PinJson.IsValid())
			{
				continue;
			}

			const FString PinId = GetString(PinJson, TEXT("pin_id"));
			const FString PinName = GetString(PinJson, TEXT("name"));
			if (!PinId.IsEmpty() && !PinName.IsEmpty())
			{
				OutMap.Add(PinId, PinName);
			}
		}
	}
}

FString FBPDiffEngine::MakeEndpointKey(
	const FString& NodeId,
	const FString& PinRef,
	const TMap<FString, FString>& PinIdToName)
{
	// The exporter emits pin ids, the AI usually answers with pin names. Normalise both to the
	// name so a connection the AI describes in its own words still matches the baseline.
	const FString* Resolved = PinIdToName.Find(PinRef);
	const FString PinName = Resolved ? *Resolved : PinRef;
	return FString::Printf(TEXT("%s|%s"), *NodeId, *PinName);
}

FString FBPDiffEngine::MakeConnectionKey(TSharedPtr<FJsonObject> Conn, const TMap<FString, FString>& PinIdToName)
{
	if (!Conn.IsValid())
	{
		return FString();
	}

	const FString FromNode = GetString(Conn, TEXT("from_node"));
	const FString ToNode = GetString(Conn, TEXT("to_node"));

	// Prefer the explicit *_pin_name fields when the document carries them.
	const FString FromPin = GetString(Conn, TEXT("from_pin_name"), GetString(Conn, TEXT("from_pin")));
	const FString ToPin = GetString(Conn, TEXT("to_pin_name"), GetString(Conn, TEXT("to_pin")));

	return FString::Printf(
		TEXT("%s -> %s"),
		*MakeEndpointKey(FromNode, FromPin, PinIdToName),
		*MakeEndpointKey(ToNode, ToPin, PinIdToName));
}

FString FBPDiffEngine::DescribeConnection(TSharedPtr<FJsonObject> Conn)
{
	if (!Conn.IsValid())
	{
		return TEXT("<malformed connection>");
	}

	const FString FromPin = GetString(Conn, TEXT("from_pin_name"), GetString(Conn, TEXT("from_pin")));
	const FString ToPin = GetString(Conn, TEXT("to_pin_name"), GetString(Conn, TEXT("to_pin")));

	return FString::Printf(
		TEXT("%s.%s -> %s.%s"),
		*GetString(Conn, TEXT("from_node")), *FromPin,
		*GetString(Conn, TEXT("to_node")), *ToPin);
}

FString FBPDiffEngine::DescribeNodeChanges(TSharedPtr<FJsonObject> BaseNode, TSharedPtr<FJsonObject> ProposedNode)
{
	if (!ProposedNode.IsValid())
	{
		return FString();
	}

	TArray<FString> Changes;

	// A delta reply only carries the fields it wants to change, so a field that is absent from
	// the proposal is "unchanged", never "cleared".
	FString ProposedComment;
	if (TryGetString(ProposedNode, TEXT("comment"), ProposedComment))
	{
		const FString BaseComment = GetString(BaseNode, TEXT("comment"));
		if (ProposedComment != BaseComment)
		{
			Changes.Add(FString::Printf(TEXT("comment '%s' -> '%s'"), *BaseComment, *ProposedComment));
		}
	}

	FString ProposedTitle;
	if (TryGetString(ProposedNode, TEXT("title"), ProposedTitle))
	{
		const FString BaseTitle = GetString(BaseNode, TEXT("title"));
		if (!BaseTitle.IsEmpty() && ProposedTitle != BaseTitle)
		{
			Changes.Add(FString::Printf(TEXT("title '%s' -> '%s'"), *BaseTitle, *ProposedTitle));
		}
	}

	const TMap<FString, FString> BaseDefaults = GetPinDefaults(BaseNode);
	const TMap<FString, FString> ProposedDefaults = GetPinDefaults(ProposedNode);
	for (const TPair<FString, FString>& Pair : ProposedDefaults)
	{
		const FString* BaseValue = BaseDefaults.Find(Pair.Key);
		const FString BaseString = BaseValue ? *BaseValue : FString();
		if (BaseString != Pair.Value)
		{
			Changes.Add(FString::Printf(TEXT("pin '%s' = '%s' (was '%s')"), *Pair.Key, *Pair.Value, *BaseString));
		}
	}

	return FString::Join(Changes, TEXT(", "));
}

void FBPDiffEngine::DiffNodes(
	const TArray<TSharedPtr<FJsonValue>>& BaseNodes,
	const TArray<TSharedPtr<FJsonValue>>& ProposedNodes,
	const FString& GraphName,
	bool bDeltaMode,
	TArray<FBPDiffItem>& OutDiff)
{
	const TMap<FString, TSharedPtr<FJsonObject>> BaseById = IndexByField(BaseNodes, TEXT("id"));
	const TMap<FString, TSharedPtr<FJsonObject>> ProposedById = IndexByField(ProposedNodes, TEXT("id"));

	for (const TSharedPtr<FJsonValue>& NodeValue : ProposedNodes)
	{
		TSharedPtr<FJsonObject> NodeJson = AsObject(NodeValue);
		if (!NodeJson.IsValid())
		{
			continue;
		}

		const FString NodeId = GetString(NodeJson, TEXT("id"));
		if (NodeId.IsEmpty())
		{
			UE_LOG(LogBlueprintAIBridge, Warning, TEXT("DiffNodes: node without an 'id' in graph '%s'; skipping."), *GraphName);
			continue;
		}

		TSharedPtr<FJsonObject> const* BaseFound = BaseById.Find(NodeId);
		TSharedPtr<FJsonObject> BaseNode = BaseFound ? *BaseFound : nullptr;

		FString Action = GetAction(NodeJson);
		if (!bDeltaMode)
		{
			Action = BaseNode.IsValid() ? TEXT("modify") : TEXT("add");
		}

		if (Action == TEXT("add"))
		{
			if (BaseNode.IsValid())
			{
				UE_LOG(LogBlueprintAIBridge, Warning,
					TEXT("DiffNodes: node id '%s' is marked 'add' but already exists in the baseline; treating it as a modification."),
					*NodeId);
			}
			else
			{
				FBPDiffItem Item;
				Item.Type = EBPDiffType::NodeAdded;
				Item.GraphName = GraphName;
				Item.NodeId = NodeId;
				Item.Description = FString::Printf(
					TEXT("Add node '%s' (%s) to %s"),
					*GetString(NodeJson, TEXT("title"), NodeId),
					*GetString(NodeJson, TEXT("type"), TEXT("unknown type")),
					*GraphName);
				Item.PayloadJson = CloneObject(NodeJson);
				OutDiff.Add(MoveTemp(Item));
				continue;
			}
		}

		if (Action == TEXT("delete") || Action == TEXT("remove"))
		{
			if (!BaseNode.IsValid())
			{
				UE_LOG(LogBlueprintAIBridge, Warning,
					TEXT("DiffNodes: node id '%s' is marked for deletion but is not in the baseline; skipping."),
					*NodeId);
				continue;
			}

			FBPDiffItem Item;
			Item.Type = EBPDiffType::NodeDeleted;
			Item.GraphName = GraphName;
			Item.NodeId = NodeId;
			Item.Description = FString::Printf(
				TEXT("Delete node '%s' (%s) from %s"),
				*GetString(BaseNode, TEXT("title"), NodeId),
				*NodeId,
				*GraphName);
			Item.PayloadJson = CloneObject(NodeJson);
			AttachBaseline(Item.PayloadJson, BaseNode);
			OutDiff.Add(MoveTemp(Item));
			continue;
		}

		// Anything left is a modification; emit one only when something actually differs.
		const FString ChangeSummary = DescribeNodeChanges(BaseNode, NodeJson);
		if (ChangeSummary.IsEmpty())
		{
			continue;
		}

		FBPDiffItem Item;
		Item.Type = EBPDiffType::NodeModified;
		Item.GraphName = GraphName;
		Item.NodeId = NodeId;
		Item.Description = FString::Printf(
			TEXT("Modify node '%s' (%s): %s"),
			*GetString(BaseNode, TEXT("title"), GetString(NodeJson, TEXT("title"), NodeId)),
			*NodeId,
			*ChangeSummary);
		Item.PayloadJson = CloneObject(NodeJson);
		AttachBaseline(Item.PayloadJson, BaseNode);
		OutDiff.Add(MoveTemp(Item));
	}

	// A full document also implies the deletion of anything it leaves out.
	if (!bDeltaMode)
	{
		for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : BaseById)
		{
			if (ProposedById.Contains(Pair.Key))
			{
				continue;
			}

			FBPDiffItem Item;
			Item.Type = EBPDiffType::NodeDeleted;
			Item.GraphName = GraphName;
			Item.NodeId = Pair.Key;
			Item.Description = FString::Printf(
				TEXT("Delete node '%s' (%s) from %s"),
				*GetString(Pair.Value, TEXT("title"), Pair.Key),
				*Pair.Key,
				*GraphName);

			Item.PayloadJson = MakeShared<FJsonObject>();
			Item.PayloadJson->SetStringField(TEXT("id"), Pair.Key);
			Item.PayloadJson->SetStringField(TEXT("action"), TEXT("delete"));
			AttachBaseline(Item.PayloadJson, Pair.Value);
			OutDiff.Add(MoveTemp(Item));
		}
	}
}

void FBPDiffEngine::DiffConnections(
	const TArray<TSharedPtr<FJsonValue>>& BaseConns,
	const TArray<TSharedPtr<FJsonValue>>& ProposedConns,
	const TMap<FString, FString>& PinIdToName,
	const FString& GraphName,
	bool bDeltaMode,
	TArray<FBPDiffItem>& OutDiff)
{
	TMap<FString, TSharedPtr<FJsonObject>> BaseByKey;
	for (const TSharedPtr<FJsonValue>& Value : BaseConns)
	{
		TSharedPtr<FJsonObject> Conn = AsObject(Value);
		if (Conn.IsValid())
		{
			BaseByKey.Add(MakeConnectionKey(Conn, PinIdToName), Conn);
		}
	}

	TSet<FString> ProposedKeys;

	for (const TSharedPtr<FJsonValue>& Value : ProposedConns)
	{
		TSharedPtr<FJsonObject> Conn = AsObject(Value);
		if (!Conn.IsValid())
		{
			continue;
		}

		const FString Key = MakeConnectionKey(Conn, PinIdToName);
		ProposedKeys.Add(Key);

		FString Action = GetAction(Conn);
		if (!bDeltaMode)
		{
			// In full-document mode a listed connection is only news if the baseline lacks it.
			if (BaseByKey.Contains(Key))
			{
				continue;
			}
			Action = TEXT("add");
		}

		const bool bRemove = (Action == TEXT("remove") || Action == TEXT("delete"));

		if (bRemove && !BaseByKey.Contains(Key))
		{
			UE_LOG(LogBlueprintAIBridge, Warning,
				TEXT("DiffConnections: '%s' is marked for removal but is not in the baseline; skipping."), *Key);
			continue;
		}

		if (!bRemove && BaseByKey.Contains(Key))
		{
			// Already wired exactly this way; nothing to review.
			continue;
		}

		FBPDiffItem Item;
		Item.Type = bRemove ? EBPDiffType::ConnectionRemoved : EBPDiffType::ConnectionAdded;
		Item.GraphName = GraphName;
		Item.NodeId = GetString(Conn, TEXT("from_node"));
		Item.Description = FString::Printf(
			TEXT("%s connection %s in %s"),
			bRemove ? TEXT("Remove") : TEXT("Add"),
			*DescribeConnection(Conn),
			*GraphName);
		Item.PayloadJson = CloneObject(Conn);
		OutDiff.Add(MoveTemp(Item));
	}

	if (!bDeltaMode)
	{
		for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : BaseByKey)
		{
			if (ProposedKeys.Contains(Pair.Key))
			{
				continue;
			}

			FBPDiffItem Item;
			Item.Type = EBPDiffType::ConnectionRemoved;
			Item.GraphName = GraphName;
			Item.NodeId = GetString(Pair.Value, TEXT("from_node"));
			Item.Description = FString::Printf(
				TEXT("Remove connection %s in %s"), *DescribeConnection(Pair.Value), *GraphName);
			Item.PayloadJson = CloneObject(Pair.Value);
			Item.PayloadJson->SetStringField(TEXT("action"), TEXT("remove"));
			OutDiff.Add(MoveTemp(Item));
		}
	}
}

void FBPDiffEngine::DiffVariables(
	const TArray<TSharedPtr<FJsonValue>>& BaseVars,
	const TArray<TSharedPtr<FJsonValue>>& ProposedVars,
	const FString& GraphName,
	bool bDeltaMode,
	TArray<FBPDiffItem>& OutDiff)
{
	const TMap<FString, TSharedPtr<FJsonObject>> BaseByName = IndexByField(BaseVars, TEXT("name"));
	const TMap<FString, TSharedPtr<FJsonObject>> ProposedByName = IndexByField(ProposedVars, TEXT("name"));

	for (const TSharedPtr<FJsonValue>& Value : ProposedVars)
	{
		TSharedPtr<FJsonObject> VarJson = AsObject(Value);
		if (!VarJson.IsValid())
		{
			continue;
		}

		const FString Name = GetString(VarJson, TEXT("name"));
		if (Name.IsEmpty())
		{
			continue;
		}

		TSharedPtr<FJsonObject> const* BaseFound = BaseByName.Find(Name);
		TSharedPtr<FJsonObject> BaseVar = BaseFound ? *BaseFound : nullptr;

		FString Action = GetAction(VarJson);
		if (!bDeltaMode)
		{
			Action = BaseVar.IsValid() ? TEXT("modify") : TEXT("add");
		}

		if (Action == TEXT("remove") || Action == TEXT("delete"))
		{
			if (!BaseVar.IsValid())
			{
				continue;
			}

			FBPDiffItem Item;
			Item.Type = EBPDiffType::VariableRemoved;
			Item.GraphName = GraphName;
			Item.Description = FString::Printf(TEXT("Remove variable '%s'"), *Name);
			Item.PayloadJson = CloneObject(VarJson);
			AttachBaseline(Item.PayloadJson, BaseVar);
			OutDiff.Add(MoveTemp(Item));
			continue;
		}

		if (Action == TEXT("add") && !BaseVar.IsValid())
		{
			FBPDiffItem Item;
			Item.Type = EBPDiffType::VariableAdded;
			Item.GraphName = GraphName;
			Item.Description = FString::Printf(
				TEXT("Add variable '%s' (%s, default '%s')"),
				*Name,
				*GetString(VarJson, TEXT("type"), TEXT("unknown")),
				*GetString(VarJson, TEXT("default")));
			Item.PayloadJson = CloneObject(VarJson);
			OutDiff.Add(MoveTemp(Item));
			continue;
		}

		if (!BaseVar.IsValid())
		{
			continue;
		}

		TArray<FString> Changes;

		FString ProposedType;
		if (TryGetString(VarJson, TEXT("type"), ProposedType))
		{
			const FString BaseType = GetString(BaseVar, TEXT("type"));
			if (ProposedType != BaseType)
			{
				Changes.Add(FString::Printf(TEXT("type '%s' -> '%s'"), *BaseType, *ProposedType));
			}
		}

		FString ProposedDefault;
		if (TryGetString(VarJson, TEXT("default"), ProposedDefault))
		{
			const FString BaseDefault = GetString(BaseVar, TEXT("default"));
			if (ProposedDefault != BaseDefault)
			{
				Changes.Add(FString::Printf(TEXT("default '%s' -> '%s'"), *BaseDefault, *ProposedDefault));
			}
		}

		if (Changes.Num() == 0)
		{
			continue;
		}

		FBPDiffItem Item;
		Item.Type = EBPDiffType::VariableModified;
		Item.GraphName = GraphName;
		Item.Description = FString::Printf(TEXT("Modify variable '%s': %s"), *Name, *FString::Join(Changes, TEXT(", ")));
		Item.PayloadJson = CloneObject(VarJson);
		AttachBaseline(Item.PayloadJson, BaseVar);
		OutDiff.Add(MoveTemp(Item));
	}

	if (!bDeltaMode)
	{
		for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : BaseByName)
		{
			if (ProposedByName.Contains(Pair.Key))
			{
				continue;
			}

			FBPDiffItem Item;
			Item.Type = EBPDiffType::VariableRemoved;
			Item.GraphName = GraphName;
			Item.Description = FString::Printf(TEXT("Remove variable '%s'"), *Pair.Key);
			Item.PayloadJson = MakeShared<FJsonObject>();
			Item.PayloadJson->SetStringField(TEXT("name"), Pair.Key);
			Item.PayloadJson->SetStringField(TEXT("action"), TEXT("remove"));
			AttachBaseline(Item.PayloadJson, Pair.Value);
			OutDiff.Add(MoveTemp(Item));
		}
	}
}

void FBPDiffEngine::DiffGraphs(
	TSharedPtr<FJsonObject> BaseGraph,
	TSharedPtr<FJsonObject> ProposedGraph,
	bool bDeltaMode,
	TArray<FBPDiffItem>& OutDiff)
{
	if (!ProposedGraph.IsValid())
	{
		return;
	}

	const FString GraphName = GetString(ProposedGraph, TEXT("graph_name"), TEXT("<unnamed graph>"));

	const TArray<TSharedPtr<FJsonValue>> BaseNodes = GetArray(BaseGraph, TEXT("nodes"));

	TMap<FString, FString> PinIdToName;
	BuildPinIdToNameMap(BaseNodes, PinIdToName);
	// New nodes carry their own pin ids; fold them in so the AI can reference either form.
	BuildPinIdToNameMap(GetArray(ProposedGraph, TEXT("nodes")), PinIdToName);

	// In full-document mode anything the baseline has and the proposal lacks reads as a
	// deletion -- but only for sections the proposal actually speaks to. A reply that simply
	// omits "connections" is saying nothing about them, not asking to unwire the graph, so the
	// baseline is withheld from the comparison and the removal sweep finds nothing.
	const TArray<TSharedPtr<FJsonValue>> Empty;

	DiffNodes(
		HasField(ProposedGraph, TEXT("nodes")) ? BaseNodes : Empty,
		GetArray(ProposedGraph, TEXT("nodes")),
		GraphName,
		bDeltaMode,
		OutDiff);

	DiffConnections(
		HasField(ProposedGraph, TEXT("connections")) ? GetArray(BaseGraph, TEXT("connections")) : Empty,
		GetArray(ProposedGraph, TEXT("connections")),
		PinIdToName,
		GraphName,
		bDeltaMode,
		OutDiff);

	DiffVariables(
		HasField(ProposedGraph, TEXT("variables")) ? GetArray(BaseGraph, TEXT("variables")) : Empty,
		GetArray(ProposedGraph, TEXT("variables")),
		GraphName,
		bDeltaMode,
		OutDiff);
}

TArray<FBPDiffItem> FBPDiffEngine::ComputeDiff(const FString& BaselineJson, const FString& ProposedJson, FString* OutError)
{
	TArray<FBPDiffItem> Diff;

	if (OutError != nullptr)
	{
		OutError->Reset();
	}

	TSharedPtr<FJsonObject> BaseRoot;
	TSharedPtr<FJsonObject> ProposedRoot;

	if (!ParseObject(BaselineJson, TEXT("Baseline"), BaseRoot, OutError))
	{
		return Diff;
	}
	if (!ParseObject(ProposedJson, TEXT("AI response"), ProposedRoot, OutError))
	{
		return Diff;
	}

	const bool bDeltaMode = DetectDeltaMode(ProposedRoot);

	// Blueprint-scope variables live at the top level of the export. As above, a reply that
	// does not mention them is not asking for them to be removed.
	const TArray<TSharedPtr<FJsonValue>> EmptyVariables;
	DiffVariables(
		HasField(ProposedRoot, TEXT("variables")) ? GetArray(BaseRoot, TEXT("variables")) : EmptyVariables,
		GetArray(ProposedRoot, TEXT("variables")),
		FString(),
		bDeltaMode,
		Diff);

	TMap<FString, TSharedPtr<FJsonObject>> BaseGraphsByName =
		IndexByField(GetArray(BaseRoot, TEXT("graphs")), TEXT("graph_name"));

	const TArray<TSharedPtr<FJsonValue>> ProposedGraphs = GetArray(ProposedRoot, TEXT("graphs"));
	if (ProposedGraphs.Num() == 0 && Diff.Num() == 0 && OutError != nullptr)
	{
		*OutError = TEXT("The AI response contains no 'graphs' array and no variable changes.");
		return Diff;
	}

	for (const TSharedPtr<FJsonValue>& GraphValue : ProposedGraphs)
	{
		TSharedPtr<FJsonObject> ProposedGraph = AsObject(GraphValue);
		if (!ProposedGraph.IsValid())
		{
			continue;
		}

		const FString GraphName = GetString(ProposedGraph, TEXT("graph_name"));
		TSharedPtr<FJsonObject> const* BaseFound = BaseGraphsByName.Find(GraphName);

		if (BaseFound == nullptr)
		{
			UE_LOG(LogBlueprintAIBridge, Warning,
				TEXT("ComputeDiff: graph '%s' is not in the baseline; every entry in it will be treated as an addition."),
				*GraphName);
		}

		DiffGraphs(BaseFound ? *BaseFound : nullptr, ProposedGraph, bDeltaMode, Diff);
	}

	return Diff;
}

FString FBPDiffEngine::DiffToMarkdown(const TArray<FBPDiffItem>& Diff)
{
	if (Diff.Num() == 0)
	{
		return TEXT("No changes detected.\n");
	}

	FString Markdown = FString::Printf(TEXT("## Proposed changes (%d)\n\n"), Diff.Num());

	for (const FBPDiffItem& Item : Diff)
	{
		Markdown += FString::Printf(
			TEXT("- `%s` **%s** - %s\n"),
			*DiffTypeToGlyph(Item.Type),
			*DiffTypeToString(Item.Type),
			*Item.Description);
	}

	return Markdown;
}

#undef LOCTEXT_NAMESPACE
