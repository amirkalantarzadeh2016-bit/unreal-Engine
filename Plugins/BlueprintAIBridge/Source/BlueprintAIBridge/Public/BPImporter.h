// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BPDiffEngine.h"
#include "Engine/Blueprint.h"

class UEdGraph;
class UK2Node;

/*
 * ---------------------------------------------------------------------------------------
 * AI RESPONSE SCHEMA
 *
 * Paste this block into the AI prompt. Only include what you are changing.
 *
 * {
 *   "blueprint_name": "BP_Door",
 *   "graphs": [
 *     {
 *       "graph_name": "EventGraph",
 *       "nodes": [
 *         {
 *           "id": "N1",
 *           "action": "modify",
 *           "comment": "Updated comment"
 *         },
 *         {
 *           "id": "N_NEW_1",
 *           "action": "add",
 *           "type": "UK2Node_CallFunction",
 *           "title": "Print String",
 *           "function_name": "PrintString",
 *           "function_class": "KismetSystemLibrary",
 *           "pins": []
 *         },
 *         {
 *           "id": "N2",
 *           "action": "delete"
 *         }
 *       ],
 *       "connections": [
 *         {
 *           "action": "add",
 *           "from_node": "N1", "from_pin": "then",
 *           "to_node": "N_NEW_1", "to_pin": "exec"
 *         },
 *         {
 *           "action": "remove",
 *           "from_node": "N1", "from_pin": "then",
 *           "to_node": "N2", "to_pin": "exec"
 *         }
 *       ]
 *     }
 *   ],
 *   "variables": [
 *     { "name": "bIsLocked", "action": "add", "type": "bool", "default": "false" }
 *   ]
 * }
 *
 * Node "action" values: "add", "modify", "delete".
 * Connection and variable "action" values: "add", "modify" (variables only), "remove".
 *
 * For an added node, "type" is the UK2Node class name. To make the node useful, also supply
 * the reference field its class needs:
 *   UK2Node_CallFunction              -> "function_name" (+ optional "function_class")
 *   UK2Node_Event / UK2Node_CustomEvent -> "event_name"
 *   UK2Node_VariableGet / VariableSet  -> "variable_name"
 * Pin default values on an added or modified node come from the node's "pins" array, using
 * each pin's "name" and "default_value".
 * ---------------------------------------------------------------------------------------
 */

/** Outcome of a single ApplyDiff run, surfaced in the panel's status box. */
struct FBPImportResult
{
	int32 AppliedCount = 0;
	int32 SkippedCount = 0;
	TArray<FString> Warnings;
	TArray<FString> Errors;
	bool bSuccess = false;

	/** Multi-line summary for the status box. */
	FString ToString() const;
};

/**
 * Applies accepted FBPDiffItems to a live UBlueprint through the UEdGraph / UK2Node APIs.
 *
 * The whole apply loop runs inside one editor transaction, so a single Ctrl+Z undoes it.
 * Anything that cannot be resolved (unknown node class, missing pin, stale node id) is
 * logged, counted as skipped, and stepped over -- a bad AI reply never takes the editor down.
 */
class BLUEPRINTAIBRIDGE_API FBPImporter
{
public:
	static FBPImportResult ApplyDiff(
		UBlueprint* Blueprint,
		const TArray<FBPDiffItem>& AcceptedItems);

private:
	static UEdGraph* FindOrCreateGraph(UBlueprint* Blueprint, const FString& GraphName, FBPImportResult& Result);

	/** Returns nullptr and logs a warning if NodeClass cannot be resolved. */
	static UK2Node* SpawnNodeSafe(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const FString& NodeTypeString,
		TSharedPtr<FJsonObject> NodeJson,
		FBPImportResult& Result);

	/** Fills in the class/function/variable reference an added node needs before pin allocation. */
	static bool ConfigureSpawnedNode(
		UBlueprint* Blueprint,
		UK2Node* Node,
		TSharedPtr<FJsonObject> NodeJson,
		FBPImportResult& Result);

	static void ApplyPinDefaults(UK2Node* Node, TSharedPtr<FJsonObject> NodeJson, FBPImportResult& Result);

	static void ApplyConnection(
		UEdGraph* Graph,
		TSharedPtr<FJsonObject> ConnectionJson,
		bool bAdd,
		const TMap<FString, UEdGraphNode*>& NodeIdMap,
		FBPImportResult& Result);

	static void DeleteNodeById(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const FString& NodeId,
		TMap<FString, UEdGraphNode*>& NodeIdMap,
		FBPImportResult& Result);

	static void ApplyVariableChange(
		UBlueprint* Blueprint,
		const FBPDiffItem& Item,
		FBPImportResult& Result);

	/** Resolves a pin by name, then by exporter pin id, then by a case-insensitive name match. */
	static UEdGraphPin* ResolvePin(UEdGraphNode* Node, const FString& PinRef, EEdGraphPinDirection Direction);

	/** Best-effort mapping from an exported type string back to a pin type. */
	static bool ParseTypeString(const FString& TypeString, FEdGraphPinType& OutPinType);

	/** Free slot to drop a newly spawned node into, so additions do not stack on the origin. */
	static void PlaceNode(UEdGraph* Graph, UEdGraphNode* Node, int32 SpawnIndex);
};
