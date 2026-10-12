# Third-party material

Failoring itself is licensed under the GNU Affero General Public License v3.0 (see `LICENSE`).

| Material | Where | Licence |
|---|---|---|
| MakeHuman 1.x assets (base mesh, targets, default skin weights), converted by `tools/convert_makehuman.py` into `app/assets/body_mh.bin` | upstream https://github.com/makehumancommunity/makehuman, commit `a8bc2d54ff0ac92e78ff71431b1023eda42bf482`, files under `makehuman/data/` (3dobjs/base.obj, rigs/default_weights.mhw, targets/measure, macrodetails, breast, buttocks, pelvis, stomach) | **CC0 1.0** (upstream `LICENSE.ASSETS.md`). No MakeHuman application code (AGPL) is used; blending, fitting and conversion are Failoring's own. |
| three.js r149 | `app/vendor/three.min.js`, inlined into `failoring.html` | MIT |
| wasi-sdk 24 (build tool only) | not redistributed | Apache-2.0 with LLVM exception |

`LICENSE` is the canonical GNU AGPL v3 text (supplied by the project owner).
