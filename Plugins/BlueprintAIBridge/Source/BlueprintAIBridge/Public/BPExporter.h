// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

/** Why the Blueprint is being handed to an AI. Drives the wording of the generated prompt prefix. */
enum class EExportContext : uint8
{
	BugFix,
	Refactor,
	FeatureRequest,
	CodeReview,
	General
};

/** Tuning knobs for a single export run. */
struct FBPExportOptions
{
	EExportContext Context = EExportContext::General;

	/** If empty, export all graphs. Otherwise only graphs whose name appears here. */
	TArray<FString> GraphFilter;

	/** If true, export one graph at a time (chunked mode for large Blueprints). */
	bool bChunkedMode = false;

	/** Index into the flattened graph list used when bChunkedMode is true. */
	int32 ChunkGraphIndex = 0;

	/** Strip whitespace from the JSON. */
	bool bCompactJson = true;

	/** Leave out pin values the node shipped with. */
	bool bOmitUntouchedPinDefaults = true;

	/** List inert pins as bare names under "unset_pins" instead of as full objects. */
	bool bCollapseUntouchedPins = true;

	/** Export comment boxes and the nodes they enclose. */
	bool bExportCommentBoxes = true;

	/** Builds options from project settings, leaving graph scope to the caller. */
	static FBPExportOptions FromSettings();
};

/**
 * Converts a UBlueprint's graphs into a compact JSON structure an AI can reason about.
 *
 * Node GUIDs, coordinates and editor-only metadata are dropped; logical structure (nodes,
 * pins, connections, variables and comments that sit on connected nodes) is preserved.
 * Node ids are short, positional and stable for as long as the graph is not edited, which is
 * what lets FBPImporter map an AI's reply back onto live nodes.
 */
class BLUEPRINTAIBRIDGE_API FBPExporter
{
public:
	/** Returns the full export as a JSON string. Returns an empty string if Blueprint is null. */
	static FString ExportBlueprint(UBlueprint* Blueprint, const FBPExportOptions& Options);

	/** Returns a Markdown-formatted summary for use as a prompt prefix. */
	static FString BuildPromptPrefix(UBlueprint* Blueprint, EExportContext Context, const FString& UserTask);

	/** Prompt prefix variant that knows about chunked exports, so it can say "graph 3 of 7". */
	static FString BuildPromptPrefix(UBlueprint* Blueprint, const FBPExportOptions& Options, const FString& UserTask);

	/**
	 * Flattens the Blueprint's ubergraph, function and macro pages into one ordered list.
	 * Ordering is deterministic and is what both the exporter and the importer index into.
	 */
	static void CollectGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs);

	/**
	 * Rebuilds the exporter's node id assignment (N1, N2, ...) for a graph.
	 * The importer calls this to translate ids from an AI reply back into live nodes.
	 */
	static void BuildNodeIdMap(UEdGraph* Graph, TMap<UEdGraphNode*, FString>& OutNodeToId);

	/** Human-readable name for an export context, used in the prompt prefix. */
	static FString GetContextString(EExportContext Context);

	/** Renders a pin/variable type as a short string such as "bool", "array<int>" or "object:Actor". */
	static FString PinTypeToString(const FEdGraphPinType& PinType);

	/** "N1", "N2", ... for a zero-based index. */
	static FString MakeNodeId(int32 Index);

private:
	static TSharedPtr<FJsonObject> SerializeGraph(UBlueprint* Blueprint, UEdGraph* Graph, const FBPExportOptions& Options);

	static TSharedPtr<FJsonObject> SerializeNode(
		UEdGraphNode* Node,
		const TMap<UEdGraphNode*, FString>& NodeIdMap,
		const FBPExportOptions& Options);

	static TSharedPtr<FJsonObject> SerializePin(UEdGraphPin* Pin, const FBPExportOptions& Options);

	/** True when the pin's value is the type's zero, so its name alone tells the whole story. */
	static bool IsTrivialDefault(UEdGraphPin* Pin);

	/** True when the pin holds a value someone actually set, rather than the node's own. */
	static bool HasOverriddenDefault(UEdGraphPin* Pin, const FBPExportOptions& Options);

	/** Comment boxes as {title, nodes:[ids]}, using geometric containment. */
	static TArray<TSharedPtr<FJsonValue>> SerializeCommentBoxes(
		UEdGraph* Graph,
		const TMap<UEdGraphNode*, FString>& NodeIdMap);

	/** Returns only comments attached to nodes that have at least one connection. */
	static FString GetRelevantComment(UEdGraphNode* Node);

	/** Graph classification string ("EventGraph", "Function", "Macro"). */
	static FString GetGraphTypeString(UBlueprint* Blueprint, UEdGraph* Graph);

	/** True when a pin should be left out of the export (hidden plumbing, orphaned pins). */
	static bool ShouldSkipPin(UEdGraphPin* Pin);

	static TArray<TSharedPtr<FJsonValue>> SerializeBlueprintVariables(UBlueprint* Blueprint);
	static TArray<TSharedPtr<FJsonValue>> SerializeLocalVariables(UEdGraph* Graph);
};
