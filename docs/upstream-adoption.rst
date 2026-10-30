Upstream Adoption
=================

Changes from ``paceholder/nodeeditor`` are selected by the needs and contracts
of this fork. The following capabilities have not been adopted:

* **Node nicknames.** A nickname API would affect node roles, delegate APIs,
  serialization, geometry, painting, keyboard editing, and examples. Any
  future adoption must fit this fork's geometry base, zoom-aware text
  rendering, and serialization invariants.
* **Textual progress values.** Nodes with a concrete need for textual progress
  feedback would require integration with the node style, processing
  indicator, layout, and resize handle; upstream painter placement is not a
  substitute for that integration.
* **QUuid-mapped loading and ID-list group restoration.** Clipboard, scene
  loading, and group files use this fork's validated paths. An external
  consumer requirement for these additional import APIs would need a narrow
  change preserving validation and rollback behavior.

These are scope decisions, not scheduled implementation work. Reconsider a
capability when a concrete consumer need establishes its intended behavior.
