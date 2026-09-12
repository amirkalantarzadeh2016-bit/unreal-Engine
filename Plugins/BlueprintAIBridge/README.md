# Blueprint AI Bridge

Editor-only UE 5.3+ plugin that exports a Blueprint's graphs as compact JSON, and imports an
AI's suggested changes back into the asset behind a reviewable diff.

## Workflow

1. Open **Tools > Blueprint AI Bridge** (or Window > Developer Tools > Blueprint AI Bridge).
2. Pick a Blueprint, pick a context (Bug Fix / Refactor / Feature Request / Code Review /
   General) and an export scope (all graphs, or one graph at a time for large assets).
3. **Export for AI** writes a snapshot to `Saved/BlueprintAIBridge/Snapshots/`.
4. Type what you want changed in **Task Description**, then **Copy to Clipboard** — you get the
   prompt prefix and the JSON in one paste.
5. Paste the AI's JSON reply into **AI Response JSON** and click **Analyze Diff**.
6. Untick anything you do not want, then **Apply Selected Changes**. The whole apply is one
   transaction, so Ctrl+Z undoes it.

**Revert to Snapshot** diffs the live graph against the newest snapshot on disk and applies the
difference, rolling the Blueprint back to the exported state.

## Export format

```json
{
  "blueprint_name": "BP_Door",
  "asset_path": "/Game/Blueprints/BP_Door.BP_Door",
  "export_context": "bug_fix",
  "ue_version": "5.3",
  "variables": [ { "name": "bIsOpen", "type": "bool", "default": "false", "scope": "blueprint" } ],
  "graphs": [
    {
      "graph_name": "EventGraph",
      "graph_type": "EventGraph",
      "variables": [],
      "nodes": [
        {
          "id": "N1",
          "type": "K2Node_Event",
          "title": "Event BeginPlay",
          "comment": "Initialize door state",
          "pins": [ { "pin_id": "P1", "name": "then", "direction": "output", "type": "exec" } ]
        }
      ],
      "connections": [
        {
          "from_node": "N1", "from_pin": "P1", "from_pin_name": "then",
          "to_node": "N2", "to_pin": "P2", "to_pin_name": "execute"
        }
      ]
    }
  ]
}
```

Blueprint-scope variables are listed once at the top level; a graph's `variables` array holds
that graph's local variables only. Node GUIDs and coordinates are dropped. A node's comment is
exported only when the node has at least one connected pin. Hidden and orphaned pins are
skipped.

With **Export This Graph Only**, a `chunk_info` object is added:

```json
"chunk_info": { "graph_index": 2, "total_graphs": 7, "graph_name": "OpenDoor" }
```

## Response format

The AI may answer with a delta document (every entry carries an `action`) or with a full
document (no `action` anywhere), which is compared set-wise against the baseline. The full
schema is documented at the top of `Public/BPImporter.h`.

Node ids are **positional** — `N1` is the first node in the graph's node array. They are stable
only while the graph is untouched, so do not edit the Blueprint between exporting and applying.
The importer checks each targeted node's title against the snapshot and skips anything that no
longer matches rather than editing the wrong node.

## Layout

| File | Role |
| --- | --- |
| `BPExporter` | Blueprint to JSON, plus the Markdown prompt prefix |
| `BPSnapshotStore` | Snapshot files under `Saved/BlueprintAIBridge/Snapshots/` |
| `BPDiffEngine` | Baseline vs. AI reply to a list of reviewable `FBPDiffItem`s |
| `BPImporter` | Accepted diff items to live `UEdGraph` / `UK2Node` mutations |
| `SBPAIBridgePanel` | The dockable Slate panel |
