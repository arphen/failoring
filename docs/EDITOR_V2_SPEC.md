# Godot editor v2: product spec (from the user, 2026-10-13)

Goal: a top-notch editor for tailors. A person who has never seen the tool must be able to design a garment from a few
pieces of cloth, sew it, and see it on a mannequin. Reference feel: Fusion 360 (sketch tools, context menus, history).

## Modes
1. **Edit mode (WYSIWYG, no walking)**: the 2D cutting table and the 3D mannequin are shown together and are *two-way linked*.
   Pieces, edges, corners and seams can be picked in either view and are highlighted in both. Seams can be created in either view.
   The 3D view shows the pieces arranged around the mannequin before the seams close (the pre-sew state the user finds very
   useful). Rendering in this mode is cheap (no walking, few render steps).
2. **Sew** button, always available: the garment is pulled together and snaps onto the mannequin; the user watches it settle.
   The settle result can be resumed back to the arranged state (edit again).
3. **Showcase**: scenarios the user can pre-render on demand and play back: Catwalk (walking), Pick up a box, Sit on a chair,
   later more. Pre-rendered sequences are cached; playing a cached one is cheap and smooth. Walking is no longer the natural
   continuation of the settle step.

## Cutting table tools
* Add basic shapes: square/rectangle, circle, triangle, trapezoid, and more squares of cloth at any time; each can be edited,
  have its seams edited, and be joined to others.
* Holes: add a hole to a piece (a neck hole in a square makes a poncho: the library gets a Poncho = a square with a neck hole,
  draped over the mannequin's shoulders).
* Edit tools like Fusion 360: add intermediate vertices on an edge (click on the edge, or a Split tool), move/delete vertices,
  convert an edge to a curve, offset, mirror, rotate (handle and numeric), scale, align/distribute, snap, measure.
* Rich context menu (right click) that depends on what is under the cursor: vertex, edge, seam, piece, empty table.
* Action list (history panel) and a real undo/redo stack; every edit is an action with a readable name; click an entry to go back.
* Selection feedback: hover highlights, selected highlights, which seam an edge belongs to, linked highlight in 3D.
* **Partial seams**: a seam can join part of an edge to part of another edge (a range on each edge) so that a hole stays open
  where the edges are not sewn together (armholes, vents, slits). No continuous full-edge seam is required.
* Seam allowance, grain line, notches are later items.

## Library
Fusion-360-like library panel to load garments (tee, skirt, chinos, poncho, dress, ...) and put several on one mannequin.
(First version exists: Library tab and Outfit tab.)

## Non-goals for now
Grading, DXF/PDF import, printing 1:1 (see docs/DOGFOOD_REPORT.md for the full list).
