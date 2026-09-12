// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Kinds of change FBPDiffEngine can detect between a baseline export and an AI reply. */
enum class EBPDiffType : uint8
{
	NodeAdded,
	NodeModified,   // pin default value or comment changed
	NodeDeleted,
	ConnectionAdded,
	ConnectionRemoved,
	VariableAdded,
	VariableModified,
	VariableRemoved
};

/** One reviewable change. The UI shows Description; the importer consumes PayloadJson. */
struct FBPDiffItem
{
	EBPDiffType Type = EBPDiffType::NodeModified;

	/** Graph the change applies to. Empty for Blueprint-scope variable changes. */
	FString GraphName;

	/** Exporter node id ("N1") or, for additions, the id the AI invented. Empty for variable diffs. */
	FString NodeId;

	/** Human-readable, shown in the UI. */
	FString Description;

	/**
	 * Raw JSON for this item.
	 *
	 * For node and variable diffs this is the proposed object. When a baseline counterpart
	 * exists it is attached under the "baseline" field so the importer can sanity-check that
	 * the live graph still matches what was exported.
	 */
	TSharedPtr<FJsonObject> PayloadJson;
};

/**
 * Compares a baseline snapshot (what was exported) against an AI reply and produces a
 * structured diff that the panel can display and FBPImporter can apply.
 *
 * Two reply shapes are supported:
 *  - Delta documents, where nodes/connections/variables carry an "action" field. Only the
 *    listed entries are considered.
 *  - Full documents with no "action" fields anywhere, which are compared set-wise against
 *    the baseline.
 */
class BLUEPRINTAIBRIDGE_API FBPDiffEngine
{
public:
	/**
	 * @param OutError  Optional. Receives a parse/shape error message; the return value is
	 *                  empty when an error is reported.
	 */
	static TArray<FBPDiffItem> ComputeDiff(
		const FString& BaselineJson,
		const FString& ProposedJson,
		FString* OutError = nullptr);

	/** Returns a Markdown summary of the diff for display. */
	static FString DiffToMarkdown(const TArray<FBPDiffItem>& Diff);

	/** Short label for a diff type, e.g. "Node Added". */
	static FString DiffTypeToString(EBPDiffType Type);

	/** "+" for additions, "~" for modifications, "-" for removals. */
	static FString DiffTypeToGlyph(EBPDiffType Type);

private:
	static void DiffGraphs(
		TSharedPtr<FJsonObject> BaseGraph,
		TSharedPtr<FJsonObject> ProposedGraph,
		bool bDeltaMode,
		TArray<FBPDiffItem>& OutDiff);

	static void DiffNodes(
		const TArray<TSharedPtr<FJsonValue>>& BaseNodes,
		const TArray<TSharedPtr<FJsonValue>>& ProposedNodes,
		const FString& GraphName,
		bool bDeltaMode,
		TArray<FBPDiffItem>& OutDiff);

	static void DiffConnections(
		const TArray<TSharedPtr<FJsonValue>>& BaseConns,
		const TArray<TSharedPtr<FJsonValue>>& ProposedConns,
		const TMap<FString, FString>& PinIdToName,
		const FString& GraphName,
		bool bDeltaMode,
		TArray<FBPDiffItem>& OutDiff);

	static void DiffVariables(
		const TArray<TSharedPtr<FJsonValue>>& BaseVars,
		const TArray<TSharedPtr<FJsonValue>>& ProposedVars,
		const FString& GraphName,
		bool bDeltaMode,
		TArray<FBPDiffItem>& OutDiff);

	/** True if any node/connection/variable anywhere in the document carries an "action" field. */
	static bool DetectDeltaMode(TSharedPtr<FJsonObject> Root);

	/** Builds "<pin_id> -> <pin name>" for every pin in the baseline graph. */
	static void BuildPinIdToNameMap(
		const TArray<TSharedPtr<FJsonValue>>& BaseNodes,
		TMap<FString, FString>& OutMap);

	/** Canonical "node|pin" key, resolving exporter pin ids to pin names where possible. */
	static FString MakeEndpointKey(
		const FString& NodeId,
		const FString& PinRef,
		const TMap<FString, FString>& PinIdToName);

	/** Canonical key for a whole connection. */
	static FString MakeConnectionKey(
		TSharedPtr<FJsonObject> Conn,
		const TMap<FString, FString>& PinIdToName);

	/** Describes a connection in words, for the UI. */
	static FString DescribeConnection(TSharedPtr<FJsonObject> Conn);

	/** Compares two node objects and returns a description of what changed, or an empty string. */
	static FString DescribeNodeChanges(
		TSharedPtr<FJsonObject> BaseNode,
		TSharedPtr<FJsonObject> ProposedNode);

	/** Copies BaseNode under the "baseline" key of Payload, when BaseNode is valid. */
	static void AttachBaseline(TSharedPtr<FJsonObject> Payload, TSharedPtr<FJsonObject> BaseNode);
};
