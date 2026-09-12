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

## Graph Formatter

**Format Graph** (toolbar button at the top of the panel) tidies the graphs the export scope
combo currently selects — all of them, or just the one.

What a run does, in order:

1. Writes a pre-format snapshot via `FBPSnapshotStore`.
2. Opens one editor transaction covering everything below.
3. `FBPGraphClusterDetector` splits each graph into connected subgraphs (union-find over every
   pin connection) and classifies each one.
4. `FBPGraphLayoutEngine` lays each cluster out left to right and stacks the clusters vertically.
5. `FBPGraphAnnotator` draws or resizes one comment box per cluster.

Ctrl+Z restores every node position and removes every comment box the run created. The snapshot
records the Blueprint's logical structure, not its geometry — it is a safety net for the graph,
not an undo for the layout.

Re-running is idempotent. Within a session, comment boxes are tracked per graph by pointer;
across sessions they are recognised by title plus overlap with the region the cluster occupied
before the run's layout. Comment nodes are excluded from layout, so a box from a previous run is
still sitting over its old cluster when the next run looks for it. A box no cluster claims is
left alone and reported as a warning rather than deleted.

### Layout

Layering is Sugiyama-style and exec-first: nodes with exec pins are ranked by longest path over
exec edges alone, then data edges between two exec nodes refine that ranking, and finally pure
and data-only nodes are seated immediately left of the earliest node that consumes them. Within
a layer, a barycentre sweep orders nodes to cut edge crossings.

`HorizontalPadding` and `VerticalPadding` (defaults 280 x 160) are the grid *step*, not the gap.
A layer holding an unusually wide node widens its own column rather than letting that node
overlap the next one; the same applies vertically.

Node footprints are estimated from title length and pin count. A node's true size is computed by
its Slate widget, which only exists while the graph is open, so `NodeWidth`/`NodeHeight` are
usually zero outside the graph editor; where they are set they are trusted instead.

### Cluster types

Classification runs in a fixed precedence, first match wins:

| Type | Test |
| --- | --- |
| Event | contains a `UK2Node_Event` |
| Pure | every node is pure (reroute nodes do not count against it) |
| Control Flow | contains `UK2Node_IfThenElse`, `UK2Node_Switch`, or a loop macro instance |
| Error Handling | a node title contains "error", "validate" or "validation" |
| Output | contains `UK2Node_VariableSet` or `UK2Node_FunctionResult` |
| Logic | anything else |

### Settings

Project Settings > Plugins > **Blueprint Formatter** (`UBPAIBridgeSettings`): padding and
cluster spacing, `bAutoFormatAfterApply`, comment padding, the minimum cluster size worth
annotating, and a colour per cluster type.

Logging goes to `LogBPFormatter`: node count, cluster count, and the graph's bounding box before
and after each run.
