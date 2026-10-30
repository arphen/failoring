# Agent toolkit: what cheap agents ran into, and what changed

2026-10-30. The request each time: "Make a new garment for me: a frilly dress. It should show up in the editor's garment
library ... Check that it actually drapes well". Agents: opencode 2.0.22 with the free models glm-5.3-flash and
mimo-v2.6-flash, `--auto`, each in its own git worktree. Transcripts: opencode JSON event logs (scratchpad of that day).

## Round 1: the repo as it was

| | glm-5.3-flash | mimo-v2.6-flash |
|---|---|---|
| after 45 min | still not done, not verified | still reading code, no file written |
| what it did | read `editor.gd` (5000+ lines), `wrap.gd`, `plan_loader.gd`; ran `api_smoke` twice into 3 and 10 minute timeouts; concluded the native library was stale and rebuilt it (10 min); wrote the dress as GDScript into `builtin_plans.gd`, changed `library.gd`, wrote its own test scene | read the same files plus `garment_health.gd`, `sew_check.gd`, `outfit.gd`, `library_panel.gd`; looked for how a garment is registered |

Problems:

1. No entry point: nothing says how a garment is made, so the agent reads the engine.
2. The garment format is raw: outlines in metres, seams by edge index, a `wrap` placement with a dozen modes. A
   bodice from scratch is out of reach; the agent cannot reuse the drafted tank because its edge names are erased.
3. A fresh checkout has no Godot import cache. The first headless run fails with `Native class "FailoringSim" not
   found` and a scene whose `_ready` stops on a script error never quits: the agent waits on a timeout and blames the
   native library.
4. No way to see the result: no headless drape report, no photos.
5. Changing a garment means editing engine code (`builtin_plans.gd`, `library.gd`).

## What was built

- `./garment` (repo root): `body`, `list`, `export`, `check`, `drape`, `shots`, `install`, `wear`, `editor`. Plain text,
  `ERROR` lines that carry the fix, exit 1 on an error, a watchdog instead of a hang, a one-time import on a fresh
  checkout.
- Recipes (`godot/garment_recipe.gd`, `docs/GARMENT_RECIPES.md`): expressions over the body (`"waist * 0.55"`), shapes
  with named edges, `include` of the drafted tank/crop/shirt/pants with their edge names, `around` (copies round the
  body with the side seams sewn), edge families (`"neck*"`) and part edges for frills along a neckline.
- Editor: recipes in the library (new Dresses category), expanded on the live body (they follow body changes);
  `./garment wear` puts a garment on the running editor's mannequin, a changed recipe file is reloaded;
  `godot/tests/recipe_library_check.tscn`.
- `AGENTS.md`: the loop and the rules (no engine edits, no native rebuilds).

## Round 2: with the toolkit

| | glm-5.3-flash | mimo-v2.6-flash |
|---|---|---|
| time to a draped, photographed, worn dress | 6.5 min | 15 min (then kept iterating) |
| engine files changed | none | none |

What still went wrong, and the change it led to:

| seen | change |
|---|---|
| copies named `tier11`, `tier12` (piece `tier1`, copy 1): read as a typo; glm invented a seam key `"off": true` to get round it | copies are `tier1_front` / `tier1_back` (or `_1`.. for more than two); unknown keys are reported |
| tiers "ride up 27 cm", "crumpled puff", an explosion in the photos at the editor's Preview mesh; both agents redesigned the dress to fight it | gathered pieces start gathered (`wrap.gather`: accordion folds on the ring of the shorter edge, `wrap.gd`), set automatically and carried down a chain of tiers; `shots` drapes on the 1.5 cm GPU mesh and prints where each piece really hangs; `drape` says its coarse mesh exaggerates gathers |
| chiffon chosen for "floaty" frills crumples all over | the guide says which fabrics make frills |
| an agent's `wear` was picked up by a scripted editor run (two dresses drawn on each other) | scripted runs (`FL_GARMENT`, `FL_AUTO`) ignore wear requests |
| the agent wanted the previous version to compare (`git show`) | `install`/`wear` keep the replaced version and print its path |

A/B of the pre-gathered start, same four-tier dress, Ultra: without it the lowest tier's hem rides up 9 cm and bubbles;
with it the hem hangs level at the length the recipe asks for.

## Voice

`tools/voice/server.py`: LFM3-Audio-2B (Liquid AI, released 2026-10-27) via transformers on the Apple GPU, 14 s to
load, about 3 s for a sentence. The editor's Talk button (hold V) records, the server transcribes and passes the text
to one opencode session. Tested end to end with a spoken clip: "Make the skirt of the frilly dress 10 centimeters
shorter and add one more ruffle at the hem." was transcribed word for word; glm-5.3-flash shortened the dress by 10 cm,
added a fourth tier, draped, photographed and wore it in about 5 minutes. That dress is `garments/frilly-dress.json`.

## Open

- The drafted tank is the only bodice; a sweetheart or off-shoulder neckline needs a raw outline.
- A frill sewn onto the face of a skirt (not to an edge) is not expressible: seams join edges.
- Gathered frills look best at Ultra (GPU); the editor's default Preview still crumples them.
