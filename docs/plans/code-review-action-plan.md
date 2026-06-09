# QtNodes — Code Review Action Plan (Validated)

## Purpose & provenance

This plan turns an external code review of the QtNodes library into a concrete,
prioritized implementation roadmap. The review was produced statically (without a
build) by another tool and was written in hedged language ("appears to", "seems
to"). **Every claim in it was independently re-verified against the current source
before being admitted here.** Claims that did not survive verification are recorded
in [Rejected / already-handled](#rejected--already-handled) with the reason, so they
are not silently re-attempted.

Line numbers below are accurate as of validation against the current tree. Treat them
as anchors, not contracts — confirm the surrounding code before editing.

Severity reflects the real-world impact of the *verified* issue, not the review's
original framing. Effort is a rough size (S ≈ a few lines, M ≈ one file + a test,
L ≈ multi-file refactor).

## Priority summary

| ID | Issue | Severity | Category | Effort | Phase |
|----|-------|----------|----------|--------|-------|
| C4a | `onModelReset()` leaves `_groups` dangling after `clear()` | **High** | correctness | M | 1 |
| C10 | Mutate-while-iterating a by-reference connection set | **High** | correctness | S | 1 |
| C11 | `static_cast` of `event->widget()` (viewport) to `QGraphicsView*` | **High** | correctness | M | 1 |
| C6 | `deleteNode()` emits `nodeDeleted` for nonexistent nodes | Medium | correctness | S | 2 |
| C4c | `addNodeToGroup()` does not detach node from previous group | Medium | correctness | S | 2 |
| C9 | `setLockedState()` never updates `_locked` (two divergent lock paths) | Medium | correctness | M | 2 |
| C5a | `setScene(nullptr)` leaves `_cutSelectionAction` + shortcut installed (asymmetric teardown) | Medium | correctness | S | 2 |
| C5b | undo/redo actions recreated as locals each `setScene`, never removed → duplicate shortcuts accumulate | Medium | correctness | S | 2 |
| C5c | `setupScale()` clamps to 0 when max=0 ("unlimited") breaking zoom | Medium | correctness | S | 2 |
| C5d | `contextMenuEvent`/`zoomFitAll`/`zoomFitSelected` deref scene with no null guard | Medium | correctness | S | 2 |
| C10b | `PasteCommand` rollback deletes *selected* nodes, not the inserted set | Medium | correctness | M | 2 |
| C1 | `cxx_std_14` advertised PUBLIC while code requires C++17 | Medium | build | S | 3 |
| C16 | `connectionPossible()` runs DFS per-port during drag-paint | Medium | performance | M | 4 |
| C17 | `collidingItems()` during drag/hover with `NoIndex` scene (O(N)) | Medium | performance | M | 4 |
| C7 | `load()` appends without asserting an empty model | Low | correctness | S | 2 |
| C8 | `PortType::None` silently coerced to `In` in two helpers | Low | correctness | S | 5 |
| C12 | Asymmetric connection bounding-rect padding (+2× on bottom-right) | Low | correctness | S | 5 |
| C13 | `MoveNodeCommand::id()` uses `typeid().hash_code()` truncated to int | Low | correctness | S | 5 |
| C14 | `CreateCommand` mutates the model in its constructor | Low | architecture | M | 5 |
| C15 | Style color-array parsing has no length/type/range validation | Low | correctness | S | 5 |
| C20 | Bulk `load()` emits per-item signals, no batch path | Low | performance | M | 4 |
| C18 | Duplicated per-paint model role lookups in `DefaultNodePainter` | Low | performance | M | 4 |
| C19 | GUI-thread-only caches guarded by needless `std::mutex` | Low | performance | S | 4 |
| C25 | `hash_combine` defined in the global namespace | Low | architecture | S | 5 |
| C23 | `GraphicsView` action lifecycle hand-maintained as parallel per-member blocks | Low | dedup | M | 2 |
| C21 | `simple_graph_model` and `vertical_layout` models ~99% duplicated | Low | dedup | M | 5 |
| C26 | Test-color option name mismatch (`NE_` vs `QT_NODES_`) makes it dead | Low | build | S | 3 |
| C27 | OpenGL found/linked/included but unused; CMP0072 vs LEGACY conflict | Low | build | S | 3 |

---

## Phase 1 — High-severity correctness (UB / crash risk)

### C4a — `onModelReset()` leaves group state dangling
**Severity: High · correctness · Effort M**

`BasicGraphicsScene::onModelReset()` (`src/BasicGraphicsScene.cpp:355-363`) clears
`_connectionGraphicsObjects` and `_nodeGraphicsObjects`, calls `clear()`, and
re-traverses — but it never touches `_groups` or `_nextGroupId`. Each `NodeGroup`
holds **raw** `NodeGraphicsObject*` child pointers (`NodeGroup.hpp:126`) sourced from
the now-cleared node map, and owns its `GroupGraphicsObject` via `unique_ptr`
(`NodeGroup.hpp:132`) while that object is also registered with the scene via
`addItem(this)` (`GroupGraphicsObject.cpp:59`). `clear()` (`BasicGraphicsScene.cpp:360`)
destroys all scene items including the `GroupGraphicsObject`, so surviving `NodeGroup`s
hold dangling child pointers **and** a `unique_ptr` to an already-destroyed object →
later dangling access / double-free.

**Fix:** At the very **top** of `onModelReset()` — *before* `_nodeGraphicsObjects.clear()`
(line 357) — do `_groups.clear(); _nextGroupId = 0;`. Destroying the `NodeGroup`s first
runs each group's `unique_ptr<GroupGraphicsObject>` destructor (removing the `addItem`'d
item) while the scene is still intact, so the subsequent `clear()` (line 360) and
re-traversal start from clean group state.

**Do NOT** mirror `setGroupingEnabled(false)`'s child-node iteration here. That path
walks each group's raw `NodeGraphicsObject*` children and calls `unsetNodeGroup()` /
`lock(false)` on them — but in `onModelReset()` those node objects are destroyed by
`_nodeGraphicsObjects.clear()` at line 357, so iterating `_childNodes` after that point
would dereference freed memory. The child nodes are being torn down wholesale anyway;
just drop the groups.

**Subsumes C4b** — `setOrientation()` (`BasicGraphicsScene.cpp:205-222`) calls
`onModelReset()` at line 220, so fixing the root cause here also fixes the
orientation-change path. No separate change needed in `setOrientation()`.

**Tests:** Extend `TestNodeGroup` / `TestBasicGraphicsScene`: create a group, then
(a) reset the model and (b) toggle orientation; assert no crash and that groups are
cleared (or correctly rebuilt) with no dangling references.

### C10 — Deleting connections while iterating a by-reference set
**Severity: High · correctness · Effort S**

In `NodeGraphicsObject::mousePressEvent` the connected set is bound **by reference**
(`src/NodeGraphicsObject.cpp:267`):
`auto const &connected = _graphModel.connections(_nodeId, portToCheck, portIndex);`
For an Out port with `ConnectionPolicy::One` it deletes during a live range-for
(`NodeGraphicsObject.cpp:288-292`). `DataFlowGraphModel::connections()` returns a
reference straight into `_connectionIndex` (`DataFlowGraphModel.cpp:33-37`,
`ConnectionIdIndex.hpp:29-48`), and `deleteConnection()` erases from — and can destroy —
that exact `unordered_set` (`ConnectionIdIndex.hpp:117-124`). Erasing the current element
of an `unordered_set` being range-iterated is undefined behavior; destroying the set
leaves `connected` dangling.

**Fix:** Replace the live loop at `NodeGraphicsObject.cpp:288-292`
(`for (auto &cnId : connected) _graphModel.deleteConnection(cnId);`) with a snapshot,
using the same copy-before-delete strategy as `DataFlowGraphModel::deleteNode`
(`DataFlowGraphModel.cpp:506-515`, which snapshots via `reserve` + `push_back`):
```cpp
std::vector<ConnectionId> toDelete(connected.begin(), connected.end());
for (auto const &cnId : toDelete)
    _graphModel.deleteConnection(cnId);
```

**Tests:** `TestUIInteraction` — single-output port with `ConnectionPolicy::One` and ≥2
existing connections; press to start a new drag; assert all prior connections removed and
no crash under ASan.

### C11 — Unsafe cast of `event->widget()` to `QGraphicsView*`
**Severity: High · correctness · Effort M**

`ConnectionGraphicsObject::mouseMoveEvent` (`src/ConnectionGraphicsObject.cpp:266-267`)
and `mouseReleaseEvent` (`:298-302`) do
`auto view = static_cast<QGraphicsView *>(event->widget());` then immediately call
`view->transform()`. `QGraphicsSceneMouseEvent::widget()` returns the **viewport**
(a plain `QWidget`), not the `QGraphicsView`; the `static_cast` to an unrelated type and
the subsequent call is undefined behavior. `mouseReleaseEvent` guards only with a debug
`Q_ASSERT` (compiled out in release).

A safe resolver already exists but is file-local: `graphics_view_from_widget(QWidget*)`
in an anonymous namespace in `NodeGraphicsObject.cpp:26-36` (walks `parentWidget()` +
`qobject_cast<GraphicsView*>`).

**Fix:** Promote that helper to a shared internal utility (e.g. a small
`view_from_event_widget()` in an existing internal header) and use it in both
`ConnectionGraphicsObject` handlers. If no `GraphicsView` is resolved, bail out / fall
back to `QTransform()` instead of dereferencing.

**Tests:** Covered indirectly by interaction tests; add a guard test that the release/move
handlers tolerate a null/foreign widget without UB.

---

## Phase 2 — Medium-severity correctness (C7 is Low, grouped here for locality)

### C6 — `deleteNode()` accepts nonexistent nodes
**Severity: Medium · correctness · Effort S**

`DataFlowGraphModel::deleteNode` (`src/DataFlowGraphModel.cpp:503-524`) has no existence
guard: it erases (harmlessly) and **unconditionally emits `nodeDeleted` and returns
`true`** for unknown ids. The reference model guards this
(`test/include/TestGraphModel.hpp:190-193`) and the contract is asserted in
`test/src/TestAbstractGraphModelSignals.cpp:248-250`
(`deleteNode(999999)` must return false and emit nothing). `DataFlowGraphModel` violates it.

**Fix:** Add `if (!nodeExists(nodeId)) return false;` at the top of `deleteNode`.

**Tests:** Add the `deleteNode(bogusId)` case to a `DataFlowGraphModel`-targeted signal
test (the existing assertion may only exercise the reference model).

### C4c — `addNodeToGroup()` doesn't detach from previous group
**Severity: Medium · correctness · Effort S**

`addNodeToGroup` (`src/BasicGraphicsSceneGroups.cpp:140-154`) does
`group->addNode(node); node->setNodeGroup(group);` with no prior detach. `setNodeGroup`
just overwrites the pointer (`NodeGraphicsObject.cpp:195-198`). If the node already
belongs to group A, A's `_childNodes` still contains it while the node now points to B —
double-listed, with A's geometry/serialization still referencing it. `createGroup`
already detaches first (`BasicGraphicsSceneGroups.cpp:104-107`).

**Fix:** Detach from the prior group *before* capturing the target group, and short-circuit
the self-re-add case — `addNodeToGroup` reuses an existing `_groups` entry, so a naive
detach can invalidate it. Specifically: (a) if the node already belongs to `groupId`,
return early (nothing to do); (b) otherwise run
`if (!node->nodeGroup().expired()) removeNodeFromGroup(nodeId);` **before** the
`_groups.find(groupId)` / `group` capture (lines 145-150). Re-`find` the target group after
the detach. This avoids the hazard where the node's prior group equals `groupId` and is its
sole member: `removeNodeFromGroup` erases an emptied group from `_groups`
(`BasicGraphicsSceneGroups.cpp:168-170`), which would otherwise invalidate the captured
`groupIt`/`group`. Unlike `createGroup` (which builds a fresh group after detaching), this
path must order the detach ahead of the lookup.

**Tests:** `TestNodeGroup` — add a grouped node to a second group; assert it appears in
exactly one group.

### C9 — Locked-node state split between `_locked` and item flags
**Severity: Medium · correctness · Effort M**

`setLockedState()` (`src/NodeGraphicsObject.cpp:173-182`) reads the model's
`NodeFlag::Locked` and toggles `QGraphicsItem` flags **but never sets `_locked`**. The
only writer of `_locked` is `lock(bool)` (`:500-506`), called only by group code. The
only reader is `mousePressEvent` (`:252`): `if (_locked) { ...; return; }`. So a node
locked **via the model flag** keeps `_locked == false`: the mouse early-out does not fire
and port-hit/drag logic still runs even though movement/selection flags are cleared. The
two lock mechanisms (model flag vs group lock) diverge.

**Fix:** Make `_locked` the single source of truth derived from the model flag — set
`_locked = flags.testFlag(NodeFlag::Locked)` inside `setLockedState()` and reconcile
`lock(bool)` so both converge — **or** remove `_locked` and have `mousePressEvent` test
`_graphModel.nodeFlags(_nodeId).testFlag(NodeFlag::Locked)` plus group state directly.

**Tests:** No node-lock-state test exists today (`lock_nodes_and_connections` is an
*example* dir, not a test; `TestNodeGroup.cpp` exercises only group membership and
`weak_ptr` lifetime via `.lock()`/`.expired()`, never `NodeFlag::Locked` or
`NodeGraphicsObject::lock(bool)`).
Author a new case in `test/src/TestUIInteraction.cpp`: lock a node via the model
`NodeFlag::Locked`, then assert mouse-press is a no-op (no draft connection, no drag).

### C5a–C5d, C23 — `GraphicsView` action & null-scene lifecycle
**Severity: Medium (C5a–d) / Low (C23) · correctness/dedup · Effort S each, or M combined**

These are best fixed together; the structural fix (C23) eliminates the root cause of
C5a/C5b.

- **C5a** — `setScene(nullptr)` (`src/GraphicsView.cpp:213-226`) deletes/nulls only five
  actions and **omits `_cutSelectionAction`** (a real member, `GraphicsView.hpp:125`,
  created at `:256-268`). This is **asymmetric teardown**, not a leak/dangling pointer: the
  action is view-parented (`new QAction(…, this)`) so it stays alive and is `delete`d on the
  next non-null `setScene` (`:256`). The defect is that the stale Cut shortcut (Ctrl+X) and
  its `[this]`-capturing `triggered` lambda remain installed on a now scene-less view.
- **C5b** — undo/redo actions are created as **locals** on every `setScene` (`:309-315`)
  and `addAction`'d to the view, but never stored as members and never removed on scene
  change. They are parented to the view (`createUndoAction(this, …)`), so they are
  reclaimed only at view destruction — the defect is not a per-call leak but that repeated
  `setScene` **accumulates** duplicate `Undo`/`Redo` actions/shortcuts on the same view.
- **C5c** — `setupScale` (`:669-686`) clamps literally; with `_scaleRange.maximum == 0`
  (documented as "unlimited", `GraphicsView.hpp:52-53`) `std::min(0, scale)` drives any
  positive scale ≤ 0 and `:675 if (scale <= 0) return;` aborts — zoom dead. Sibling paths
  (`advanceZoomAnimation`, `scaleUp`, `scaleDown`) correctly use `> 0` guards.
  **Fix:** apply each bound only when `> 0`.
- **C5d** — `contextMenuEvent` (`:344,349,356,358`), `zoomFitAll` (`:869`), and
  `zoomFitSelected` (`:876`) dereference `scene()`/`nodeScene()` with no null guard,
  unlike `mouseMoveEvent` (`:767`) and the `on*SelectedObjects` slots which do guard.
  **Fix:** add early `if (!scene()) return;` / `if (!nodeScene()) return;`.
- **C23 — Consolidate actions (recommended structural fix for C5a/C5b)** — `setScene`
  (`:230-315`) repeats delete/new/setShortcut/connect/addAction across ~7 near-identical
  blocks, and the teardown in the `if (!scene)` branch is a separate hand-maintained
  parallel block of per-member `delete`/`= nullptr` statements (`:215-224`, five members,
  omitting `_cutSelectionAction`). There is no action container today; C5a and C5b are
  direct consequences of keeping the create and destroy code in two separately-edited
  per-member blocks. Replace with a single owned container (`QList<QAction*>` or a struct
  of permanent members) populated by one helper and torn down by one loop, so create and
  destroy are structurally identical and omissions become impossible.

**Recommended approach:** implement C23 (owned action container incl. undo/redo) — it
resolves C5a and C5b by construction — then apply the local fixes C5c and C5d. If a
minimal patch is preferred over the refactor, at least: delete+null `_cutSelectionAction`
in the null branch (C5a) and store+free undo/redo as members (C5b).

**Tests:** `TestZoomFeatures` — add an "unlimited range" case (`setScaleRange(0,0)` then
zoom in/out works). Add a `setScene`→`setScene(nullptr)`→`setScene` cycle test asserting no
action/shortcut accumulation and no crash on context menu / zoom-fit with no scene.

### C10b — `PasteCommand` rollback targets the selection, not the inserted set
**Severity: Medium · correctness · Effort M**

`PasteCommand::redo()` (`src/UndoCommands.cpp:414-431`) clears selection, inserts items in
a try block, and on `catch (...)` deletes **all currently-selected nodes** and marks the
command obsolete. Because `insertSerializedItems` selects each node as it loads it
(`:137-140`) and redo clears selection first, the selection usually equals the inserted
subset — but the recovery is fragile: if a failing node yields no selectable graphics
object, or selection is mutated mid-insert, the deleted set diverges (leaking
partially-inserted nodes or deleting unrelated ones).

**Fix:** Have `insertSerializedItems` return/out-param the exact `NodeId`s it created, and
delete precisely that set in the catch handler — removing the dependency on live selection.

**Note (mutate-while-iterating here is NOT a bug):** `for_each_selected` iterates a
`selectedItems()` `QList` snapshot (`selection_utils.hpp:13-19`) and `DeleteCommand`
already serializes connection ids before deleting (`UndoCommands.cpp:289-291`) and dedups
via a `processedNodes` set (`:279-294`). Those paths are already safe.

**Tests:** `TestCopyPaste` — inject a paste payload that fails partway; assert the model is
left unchanged (no leaked nodes), independent of prior selection.

### C7 — `load()` appends without asserting emptiness
**Severity: Low · correctness · Effort S**

`load()` (`src/DataFlowGraphModel.cpp:623-692`) appends into the existing model and relies
on the caller having cleared it; the only guard against pre-existing content is the
per-node id-collision throw in `loadNode` (`:577-579`), which fires only when ids happen to
overlap. (The review's "partial mutation on bad JSON" half is **false** — see Rejected C3.)

**Fix (optional hardening):** add an explicit `if (!_nodeIds.empty()) throw …;` (or assert)
at the top of `load()` so "load into a clean model" is enforced rather than incidental.

---

## Phase 3 — Build & packaging

### C1 — Public C++ standard understated as C++14
**Severity: Medium · build · Effort S**

`CMakeLists.txt:198` `target_compile_features(QtNodes PUBLIC cxx_std_14)` is the only
standard setting in the tree (no `CMAKE_CXX_STANDARD` override). The code — including
**public headers** — requires C++17: `std::optional` (`NodeRenderingUtils.hpp:50`,
`NodeGraphicsObject.cpp:96`, `DefaultNodePainter.cpp:193`, `DefaultNodeGeometryBase.cpp:31`),
`std::clamp` (`node_shadow_atlas.cpp:41,55`, `GraphicsView.cpp:395`), `if constexpr`
(`HashUtils.hpp:16`), nested-namespace definitions (`SerializationValidation.{hpp:11,cpp:6}`,
`node_shadow_atlas.cpp:17`, `selection_utils.hpp:8`). The PUBLIC declaration advertises
C++14 to consumers while the headers they include need C++17 → downstream build breakage.

**Fix:** `target_compile_features(QtNodes PUBLIC cxx_std_17)`. Update `README.rst`,
`docs/getting-started/installation.rst`, and any CI matrix to state C++17.

### C26 — Dead test-color option (name mismatch)
**Severity: Low · build · Effort S**

`CMakeLists.txt:33` defines `option(QT_NODES_FORCE_TEST_COLOR …)` but
`test/CMakeLists.txt:47` reads `$<$<BOOL:${NE_FORCE_TEST_COLOR}>:--use-colour=yes>` — a
variable that is never defined. The option is dead; colorized test output never engages.

**Fix:** rename the generator-expression variable to `QT_NODES_FORCE_TEST_COLOR`.

### C27 — Unused OpenGL dependency + policy contradiction
**Severity: Low · build · Effort S**

OpenGL is required (`CMakeLists.txt:57`), linked (`:168 Qt::OpenGL`), and included
(`GraphicsView.cpp:25 #include <QtOpenGL>`), but **no OpenGL API is used** — the only
hit is `setViewportUpdateMode(...)` (`GraphicsView.cpp:175`), a plain `QGraphicsView`
method. Separately, `CMakeLists.txt:3` sets `CMP0072 NEW` (documented as preferring GLVND)
while `:13 set(OpenGL_GL_PREFERENCE LEGACY)` overrides to LEGACY — a self-contradiction.

**Fix:** Drop the unused dependency — remove the `OpenGL` component from `find_package`,
the `Qt::OpenGL` link, and the `<QtOpenGL>` include; that also makes the CMP0072/LEGACY
lines moot (remove them). If OpenGL is intentionally retained for an optional accelerated
viewport, make it an explicit option and reconcile the preference contradiction.

---

## Phase 4 — Performance

### C16 — `connectionPossible()` DFS during connection-drag paint
**Severity: Medium · performance · Effort M**

`DataFlowGraphModel::connectionPossible` (`src/DataFlowGraphModel.cpp:64-143`) runs a DFS
cycle check (`hasLoops`) — O(V+E) of the reachable subgraph per candidate port — and is
called from the painter (`DefaultNodePainter.cpp:318`). It does **not** run on ordinary
paint/hover: it is gated by `connectionForReaction()` (`DefaultNodePainter.cpp:311`), set
only during an active connection drag (`ConnectionGraphicsObject.cpp:269`). But during a
drag the DFS runs for every In port of the hovered node on each repaint.

**Fix:** Cache loop-reachability per drag gesture (the source out-node is fixed for the
whole drag) or compute reachability once per `mouseMove` rather than once per port inside
paint. Bounded severity — only during drag, only the hovered node's ports.

### C17 — `collidingItems()` with a `NoIndex` scene
**Severity: Medium · performance · Effort M**

`BasicGraphicsScene.cpp:36` sets `setItemIndexMethod(QGraphicsScene::NoIndex)`, making
`collidingItems()` an O(N) linear scan. It is called per drag-move frame
(`NodeGraphicsObject.cpp:369`) and per hover-enter (`:434`).

**Fix (pick one):** use the default `BspTreeIndex` so `collidingItems` is accelerated; or
restrict the query to group rectangles via `scene()->items(rect, IntersectsItemBoundingRect)`
over the node's bounding rect rather than whole-scene `collidingItems()`. If `NoIndex` is a
deliberate choice for highly dynamic scenes, make it a documented, configurable trade-off.

### C20 — Per-item signals on bulk load
**Severity: Low · performance · Effort M**

`load()` emits `nodeCreated` per node (via `loadNode`, `DataFlowGraphModel.cpp:607`) and `connectionCreated`
per connection (`:204-206`); `insertSerializedItems` does the same (`UndoCommands.cpp:131-156`).
The scene updates after each, O(N) incremental work and flicker. A `modelReset()` signal
already exists (`AbstractGraphModel.hpp:256`, wired to `onModelReset` at
`BasicGraphicsScene.cpp:70`) but `load()` never uses it.

**Fix:** For large bulk loads, emit `modelReset()` once after loading (scene rebuilds via
`onModelReset`) instead of, or in addition to, per-item signals. Optimization only — per-item
signals are correct. (Coordinate with C4a: `onModelReset` must clear groups first.)

### C18 — Duplicated per-paint model lookups
**Severity: Low · performance · Effort M**

The review's "dynamic_cast to `DataFlowGraphModel`" claim is **overstated** — there is a
single guarded cast, once per paint, in `drawProcessingIndicator`
(`DefaultNodePainter.cpp:457`). The real cost is duplicated role lookups within one paint:
e.g. `ValidationState` fetched twice (`:223`, `:507`), `nodeFlags` twice (`:445`, `:483`),
plus per-port loops calling `portData`/`connections`.

**Fix (only if profiling warrants):** fetch `ValidationState`/`nodeFlags` once in `paint()`
and thread them into the `draw*` helpers; optionally replace the lone `dynamic_cast` with a
virtual on `AbstractGraphModel`. Low priority — each lookup is a cheap `QVariant` fetch and
Qt repaints only exposed items.

### C19 — Needless mutexes on GUI-thread caches
**Severity: Low · performance · Effort S**

Three caches are reached only from `QGraphicsItem::paint` (always GUI-thread) yet are
`std::mutex`-guarded: `s_text_path_cache` (`DefaultNodePainter.cpp:90-91,156`),
`s_validation_icon_cache` (`:113-114,118`), `s_shadow_cache`
(`node_shadow_atlas.cpp:128-129,133`). No `QtConcurrent`/`std::thread`/`std::async` touches
them; no comment documents intended off-thread use.

**Fix:** Remove the three mutexes/lock_guards (hygiene), **or** add a comment justifying them
if worker-thread rendering is a planned use. Document the "GUI thread only" contract either
way.

---

## Phase 5 — Hygiene, dedup, low-severity correctness

### C8 — `PortType::None` silently treated as `In`
**Severity: Low · correctness · Effort S**

In `include/QtNodes/internal/ConnectionIdUtils.hpp`, two helpers coerce `None` to `In`:
`portCountRole` (`:134-137`, `!= Out ⇒ InPortCount`) and the
`makeIncompleteConnectionId(ConnectionId, PortType)` overload (`:68-74`, `!= Out ⇒ In`).
`PortType::None` is a real enum value (`Definitions.hpp:78-82`).

**Fix:** For those two helpers add `Q_ASSERT(portType != PortType::None)` or branch on all
three states (as `connectionNodeId`/`connectionPortIndex` already do at `:15-19`/`:28-32`).
**Do NOT** "fix" the `makeIncompleteConnectionId(NodeId, PortType, PortIndex)` overload
(`:56-58`) — it branches on `== In` and the review mis-describes its behavior. No live bug
today (callers pass concrete In/Out); robustness/clarity only.

### C12 — Asymmetric connection bounding-rect padding
**Severity: Low · correctness · Effort S**

`ConnectionGraphicsObject::rebuildCachedGeometry` (`src/ConnectionGraphicsObject.cpp:124-129`)
pads top-left by `diam` but bottom-right by `2*diam`. Every side is padded by ≥ the endpoint
dot radius, so geometry stays enclosed (no paint artifact) — the only effect is an inflated
rect on the bottom-right (affects `sceneRect` growth / hit extent).

**Fix:** `commonRect.adjust(-diam, -diam, diam, diam)` for uniform padding; drop the `2×`.

### C13 — `MoveNodeCommand::id()` via `typeid().hash_code()`
**Severity: Low · correctness · Effort S**

`src/UndoCommands.cpp:597-600` returns `static_cast<int>(typeid(MoveNodeCommand).hash_code())`
— non-deterministic across builds/STL implementations and truncated from `size_t` to `int`.
`MoveNodeCommand` is the only command overriding `id()` (`UndoCommands.hpp:111`), and
`mergeWith` re-checks `_selectedNodes` equality (`:602-611`), so practical merge risk is low.

**Fix:** Return a stable named constant (e.g. `return 0x4d564e44; // 'MVND'`) or a small
project-wide command-id enum.

### C14 — `CreateCommand` mutates the model in its constructor
**Severity: Low · architecture · Effort M**

The constructor performs `addNode` + `setNodeData(Position)` directly
(`src/UndoCommands.cpp:228-240`); the first `redo()` is then a guaranteed no-op via an
empty-`_sceneJson` check (`:251-257`) that acts as an implicit first-run flag. Correct in
normal push/undo/redo cycles, but surprising.

**Fix:** Move `addNode`/`setNodeData` into `redo()` (store only name + position as members)
so `redo()` creates unconditionally and `undo()` serializes+deletes — symmetric, no implicit
first-run special case. (Review's "`_firstRun` flag" wording is approximate — the guard is
`_sceneJson` emptiness, not a boolean.)

### C15 — Permissive style color parsing
**Severity: Low · correctness · Effort S**

`detail::readColor` (`include/QtNodes/internal/Style.hpp:48-62`) reads `colorArray[0..2]`
with no size check (missing entries → `0`), no `isDouble`/type check, no `[0,255]` clamp,
and the string branch never checks `QColor::isValid()`. Parse failures are silent (only a
file-open `qWarning` exists, `:37`). Routed through by `NodeStyle.cpp:56-69`,
`ConnectionStyle.cpp:46-50`, `GraphicsViewStyle.cpp:41-43`.

**Fix:** Validate `size() >= 3`, each element `isDouble()`, clamp to `[0,255]`; check
`color.isValid()` on the string path; on failure keep the default and `qWarning` the key.
Low severity — styles normally come from the trusted built-in `DefaultStyle.json`.

### C25 — `hash_combine` in the global namespace
**Severity: Low · architecture · Effort S**

`include/QtNodes/internal/HashUtils.hpp:11-19` defines `hash_combine` at file scope with no
namespace; consumed unqualified inside `namespace std` in `ConnectionIdHash.hpp:15` via
global lookup. Risks ODR/overload clashes with Boost/Qt `hash_combine`.

**Fix:** Move it into `namespace QtNodes::detail` and qualify the call site. Internal header
only — contained blast radius.

### C21 — Duplicated example graph models
**Severity: Low · dedup · Effort M**

The review's specific claim (`connection_colors` ≈ `styles`) is **false** — they share only
a ~22-line `MyNodeData` boilerplate header, then define different models. The **real**
duplication: `simple_graph_model/SimpleGraphModel.cpp` (273 lines) vs
`vertical_layout/SimpleGraphModel.cpp` (273 lines) differ in ~5 trivial spots; their `.hpp`s
(112 vs 113) differ by one `private:` line — ~99% identical, ~385 shared lines.
`custom_painter/SimpleGraphModel.cpp` (237) is a slimmer variant of the same model.

**Fix:** Factor the shared `SimpleGraphModel` into one reusable example model (e.g.
`examples/common/`) used by `simple_graph_model` and `vertical_layout`. Do not cite
`connection_colors`/`styles` as duplicate models. Low priority — standalone examples have
some pedagogical value in being self-contained.

---

## Architectural recommendations (optional, evidence-qualified)

The review's headline architectural proposals are recorded here for completeness, but
validation weakened their justification. They are **not** scheduled into the phases above
and should only be undertaken as deliberate strategic investments, not as fixes.

- **`GraphStore` / `StoredGraphModel` base** — Proposed to remove "hundreds of lines" of
  duplicated storage across models and examples. Validation found the actual duplication is
  far smaller than claimed: principally the two example models in C21 (~385 lines), not a
  sweeping cross-cutting pattern. `DataFlowGraphModel` already centralizes its storage via
  `ConnectionIdIndex`. The LOC payoff is real but modest; treat as an API-ergonomics project
  (making custom models smaller), not a debt-reduction emergency.
- **Transactional graph-mutation API (`beginTransaction`/`rollback`)** — Validation
  **refuted** the premise that load/paste partially mutate on failure: `load()` does
  reverse-order rollback (`DataFlowGraphModel.cpp:662-691`), `loadNode()` cleans up on
  delegate failure (`:611-616`), and `PasteCommand::redo()` has a try/catch
  (`UndoCommands.cpp:419-430`). A general transaction API is **not** needed for correctness.
  The only residual is C10b (make paste rollback target the inserted set, already scheduled).
- **Serialization/import centralization (`GraphSerializer`/`GraphImporter`/…)** — Partly
  pre-empted: `SerializationValidation.{hpp,cpp}` already centralizes connection validation
  and `load()` pre-validates the whole connection set before mutating. Worth a light
  consolidation pass alongside C10b, not a new layer.
- **Layered library split (`QtNodesCore`/`DataFlow`/`Graphics`/`Widgets`)** — A large,
  high-risk reorganization with no validated correctness driver. Out of scope for this plan.

## Documentation suggestions

The review proposed a set of new docs (architecture, model contracts, serialization schema,
undo/import, rendering pipeline, threading). The repo **already** ships a substantial Sphinx
guide that covers much of this: `docs/guide/graph-models.rst`, `serialization.rst`,
`data-flow.rst`, `undo-redo.rst`, `visualization.rst`, `advanced.rst`,
`getting-started/concepts.rst`, plus architecture/dataflow PlantUML diagrams under
`docs/_static/diagrams/`. **Before writing anything new, audit these for gaps.** The
genuinely under-documented, high-value additions are likely narrow:

- A **`NodeRole`/`PortRole` contract table** (QVariant type, read/write, required/optional,
  signal emitted, affects geometry/paint/serialization). Confirm it is not already in
  `concepts.rst`/`graph-models.rst`.
- The **dynamic-ports protocol** (`portsAboutToBeInserted` → mutate count →
  `portsInserted`, connection-shift semantics). Confirm coverage in `advanced.rst`.
- A short **threading contract** ("all model mutation on the GUI thread"), which also
  motivates C19.

Treat docs work as low priority and gap-driven, not as authoring the full proposed set from
scratch.

---

## Rejected / already-handled

Recorded so these are not re-attempted:

- **C3 — "make load/paste transactional" — REJECTED (already handled).** `load()` rolls back
  loaded connections then nodes in reverse order on any exception
  (`DataFlowGraphModel.cpp:662-691`); `loadNode()` deletes the partial node on delegate
  failure (`:611-616`); `PasteCommand::redo()` recovers in `catch` (`UndoCommands.cpp:419-430`).
  Connections are pre-validated before mutation (`:644-655`, re-checked at `:672`). No
  transaction API required. (The one real residual — deterministic paste rollback — is C10b.)
- **C8 (partial)** — the `makeIncompleteConnectionId(NodeId, PortType, PortIndex)` overload is
  mis-described by the review (branches on `== In`). Only the other two helpers are fixed.
- **C10b mutate-while-iterating (partial)** — REJECTED. `for_each_selected` iterates a
  `selectedItems()` snapshot and `DeleteCommand` already copies-before-delete; those paths are
  safe. Only the selection-based rollback target is changed.
- **C18 "pervasive dynamic_cast" (partial)** — REJECTED as overstated. One guarded cast per
  paint; only the duplicated role lookups are worth addressing.
- **C21 "`connection_colors` ≈ `styles`" (partial)** — REJECTED. They share only a 22-line
  boilerplate. The real duplicate pair is `simple_graph_model` ⇄ `vertical_layout`.
- **C7 "partial mutation on bad JSON" (partial)** — REJECTED. `load()` validates and rolls
  back; only the optional empty-model assertion remains.

## Suggested sequencing

1. **Phase 1 (High)** first — C4a, C10, C11 are UB/crash risks; each is small and
   independently shippable with a regression test.
2. **Phase 2 (Medium correctness)** — group the `GraphicsView` items (C5a–d via the C23
   container) into one change; group the group-lifecycle items (C4a already done in Phase 1,
   then C4c, C9) into another.
3. **Phase 3 (Build)** — C1/C26/C27 are trivial, low-risk, and unblock consumers; can land
   anytime, ideally early.
4. **Phase 4 (Performance)** — only after correctness; gate C16/C17/C18 on a quick profile so
   effort follows measured cost.
5. **Phase 5 (Hygiene)** — opportunistic; bundle with nearby edits.

Each Phase 1–2 item should land with a unit test (extend the existing Catch2 suite under
`test/src/`). Build under both GCC/MinGW and MSVC and run the full suite after each phase.
