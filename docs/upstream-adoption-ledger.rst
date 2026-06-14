Upstream Adoption Ledger
========================

Purpose
-------

This ledger records decisions about changes that exist in the original
``paceholder/nodeeditor`` repository and have not been carried over as commits
into this fork. It is a decision record, not an implementation plan.

Status on 2026-06-14
--------------------

- Fork branch reviewed: ``master`` at ``d622a7b``.
- Original repository branch reviewed: ``upstream/master`` at ``1b173f8``.
- Shared base: ``35b11b6``.
- Conclusion: no additional upstream-only changes from this comparison will be
  carried over now. Anything from this set that is intended to be retained has
  already been represented in this fork, or would need a fork-specific
  implementation if the need appears later.

Decision Entries
----------------

.. list-table::
   :widths: 16 28 18 38
   :header-rows: 1

   * - Upstream commit
     - Topic
     - Decision
     - Rationale
   * - ``163ed49``
     - Node nickname functionality (#506)
     - Not adopted
     - The feature is not currently needed. The upstream implementation spans
       public roles, node delegate APIs, serialization, default geometry,
       painting, keyboard editing, and examples. If this becomes useful, it
       should be implemented around this fork's current geometry base,
       zoom-aware text rendering, and serialization invariants.
   * - ``bcb0b35``
     - Progress value display (#505)
     - Not adopted
     - The feature is only useful for nodes that need textual progress feedback.
       Upstream's implementation uses direct painter placement that should not
       be copied into this fork without integrating it with the current node
       style, processing indicator, layout, and resize-handle behavior.
   * - ``49c2f0c``
     - QUuid loading and NodeId conversion support (#519)
     - Not adopted
     - This is mainly an import/API compatibility addition. Current clipboard,
       full-scene load, and group-file workflows already have fork-specific
       validated paths. If external consumers need UUID-mapped memory loading or
       ID-list group restoration, that should be added narrowly while preserving
       the fork's validation and rollback behavior.
   * - ``1b173f8``
     - Copy/paste fix for unwanted connections (#527)
     - Already represented
     - The desired behavior already exists in this fork: copied connections are
       filtered so both endpoints must be selected, and duplicate serialized
       connections are suppressed. Directly applying the upstream patch is not
       useful and could lose fork-local validation and group-lock handling.

Operational Notes
-----------------

- Do not merge or cherry-pick these upstream commits as a batch.
- Reconsidering any entry should create a new ledger entry with the date,
  reason, intended behavior, and tests required for the fork-specific
  implementation.
- A focused regression test for the copy/paste case remains reasonable:
  copying one node from a connected pair should not paste a connection to the
  unselected node.

Verification
------------

The current fork test suite was run on 2026-06-14 with Qt available on ``PATH``
and ``QT_QPA_PLATFORM=offscreen``:

.. code-block:: powershell

   ctest --output-on-failure

Result: all registered tests passed.
