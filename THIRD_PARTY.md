# Third-party material

Failoring's own code (cloth core, wasm API, Godot GDExtension wrapper, browser app, tools) is licensed under the
GNU Affero General Public License v3.0 (`LICENSE`). This file lists the third-party material that is bundled in the
repository, compiled into a tracked binary, embedded in the built `failoring.html`, or needed by the build and tools.
Verbatim licence texts are in `LICENSES/`. Each entry says what was checked and against which upstream source;
anything not checked is marked **not verified**.

## Summary

| Component | Where it is used or shipped | Version / commit | Licence | Licence text |
|---|---|---|---|---|
| MakeHuman community assets | `app/assets/body_mh.bin` (built by `tools/convert_makehuman.py`), used by `app/src/body.js` | makehumancommunity/makehuman @ `a8bc2d54ff0ac92e78ff71431b1023eda42bf482` | CC0 1.0 Universal | `LICENSES/CC0-1.0-makehuman-LICENSE.ASSETS.txt` |
| FreeSewing (drafted pattern data, MIT designs) | `app/assets/freesewing/raw_{teagan,penelope,charlie}_*.json` (18 files) and `app/src/fsimport.js`, both in the repo; `tools/freesewing/` drafts them. Not in the built `failoring.html` | freesewing/freesewing @ `8a8de5a271d9823bb393735a41dd3993cf5bcf4b` (`@freesewing/core` 4.0.0) | MIT, Copyright (c) 2022 Joost De Cock | `LICENSES/freesewing-MIT.txt` |
| three.js r149 | `app/vendor/three.min.js`, inlined into `failoring.html` by `app/build.js` | mrdoob/three.js tag `r149` | MIT, Copyright © 2010-2023 three.js authors | `LICENSES/threejs-MIT.txt` |
| godot-cpp | compiled into the tracked binaries `godot/bin/libfailoring.so` (Linux) and `godot/bin/libfailoring.dylib` (macOS arm64, built against tag `godot-4.3-stable` according to the Mac session, not independently checked) (godot-cpp source is not in the repo) | godotengine/godot-cpp tag `godot-4.3-stable` (version of the binary not verified, see 4) | MIT, Copyright (c) 2017-present Godot Engine contributors | `LICENSES/godot-cpp-MIT.txt` |
| LLVM libc++, libc++abi, compiler-rt builtins | linked into `wasm/failoring.wasm`, which is embedded in `failoring.html` | llvm-project `llvmorg-18.1.2` (producer string: clang 18.1.2-wasi-sdk) | Apache-2.0 WITH LLVM-exception | `LICENSES/llvm-libcxx-*`, `llvm-libcxxabi-*`, `llvm-compiler-rt-*` |
| wasi-libc, with musl, cloudlibc and dlmalloc parts | linked into `wasm/failoring.wasm` | WebAssembly/wasi-libc @ `b9ef79d7dbd47c6c5bafdae760823467c2f60b70` (the commit pinned by wasi-sdk-24) | multi-licensed: wasi-libc's own code Apache-2.0 WITH LLVM-exception; musl MIT; cloudlibc BSD-2-Clause; dlmalloc CC0 | `LICENSES/wasi-libc-LICENSE.txt`, `wasi-libc-LICENSE-APACHE-LLVM.txt`, `musl-COPYRIGHT.txt`, `cloudlibc-BSD-2-Clause-LICENSE.txt`, `CC0-1.0-makehuman-LICENSE.ASSETS.txt` (CC0 1.0 text) |
| wasi-sdk 24 (build toolchain) | not shipped; used by `tools/build_wasm.sh` | WebAssembly/wasi-sdk tag `wasi-sdk-24` | Apache-2.0 WITH LLVM-exception | `LICENSES/wasi-sdk-LICENSE.txt` |
| CrochetPARADE | not shipped and not committed; cloned into the git-ignored `third_party/crochetparade/` and used by `tools/crochet/cp_export.mjs` (T4.0) | stassev/CrochetPARADE @ `06e987b13ccfc94cfdfcebc1fe4664d37d2c6687` (GitHub; the Codeberg head `be11c8b0a37f2fdeaca39bd2c1cd2b0cc7fc281d` is older) | GPL-3.0-or-later | `LICENSES/crochetparade-GPL-3.0.txt` |

Development tools that are not shipped (Playwright, numpy, matplotlib, bezier-js) are listed in section 7; CrochetPARADE (used by the crochet tools, not shipped) is section 8.

## 1. MakeHuman community assets (CC0 1.0)

* **Upstream:** https://github.com/makehumancommunity/makehuman, commit
  `a8bc2d54ff0ac92e78ff71431b1023eda42bf482` (committer date 2024-06-26).
* **Licence:** CC0 1.0 Universal. Stated in the upstream `LICENSE.ASSETS.md` (copied verbatim to
  `LICENSES/CC0-1.0-makehuman-LICENSE.ASSETS.txt`), in `LICENSE.md` section C ("These assets have been released under
  CC0 1.0 Universal"), and in the header of every file read by the converter ("This asset was explicitly released as CC0
  in september 2020").
* **Files read by `tools/convert_makehuman.py` (66 files):**
  * `data/3dobjs/base.obj` (base mesh)
  * `data/rigs/default.mhskel` (skeleton: joint parents and rest positions)
  * `data/rigs/default_weights.mhw` (symmetric skin weights)
  * `data/targets/macrodetails/{caucasian,african,asian}-female-young.target` (the three ethnic targets, averaged to form
    the neutral female body)
  * 60 further targets: `macrodetails/universal-female-young-*` (universal min/average/max), `macrodetails/height/`,
    `breast/`, `buttocks/`, `stomach/`, and 20 `measure/measure-*-{incr,decr}.target` pairs.
* **What was checked:** every one of the 66 files was read. Each carries a CC0 statement: `base.obj` in its header;
  the 63 target files in their headers; `default_weights.mhw` and `default.mhskel` in their `"license": "CC0"` fields.
  No file read by the converter has a licence other than CC0. The converter imports no MakeHuman program code, so the
  AGPL-3.0 MakeHuman application code (`LICENSE.CODE.md`) is not used.
* **Copyright lines, verbatim from the upstream files:**
  * `base.obj`: `Copyright (C) 2020 Data Collection AB, https://www.datacollection.se`, `Copyright (C) 2020 Joel Palmius`,
    `Copyright (C) 2020 Jonas Hauquier`.
  * the 63 target files: `Original copyright (C) 2014 Manuel Bastioni`, then the copyright was "explicitly transfered to
    other team members in 2016", then the same three 2020 holders as `base.obj`.
  * `default_weights.mhw`: `"copyright": "(c) 2021 Data Collection AB, Joel Palmius, Jonas Hauquier"`.
  * `default.mhskel`: `"copyright": "(c) 2020 Data Collection AB, Joel Palmius, Jonas Hauquier"`.
* **Derived in this repo:** `app/assets/body_mh.bin` (308,183 bytes, raw DEFLATE). It holds the quantised base mesh,
  quantised target deltas, the skin weights re-mapped onto a reduced skeleton, and landmark and region data computed by
  `tools/convert_makehuman.py`. Its JSON header records the source commit and file list. I decoded that header and it
  matches the commit above and the 66 files. The blending and fitting in `app/src/body.js` are Failoring's own code.
  CC0 requires no attribution; the copyright lines are kept here anyway.
* **Not verified:** whether any other MakeHuman asset (clothes, textures, proxies) is in use. None is used by the converter.
* **Texture coordinates (2026-10-15):** `godot/assets/body_uv.json` holds the `vt` coordinates of the `body` group of
  the same `data/3dobjs/base.obj` (same commit, same CC0 statement), written by `tools/export_body_uv.py`, so the
  Godot mannequin can use textures painted for MakeHuman's UV layout. No other file is read.
* **Photographic skins (not in the repo):** `tools/fetch_makehuman_skins.sh` downloads MakeHuman 1.1 skin textures
  (`download.tuxfamily.org/makehuman/assets/1.1/base/skins/textures/*_diffuse.png`, part of the MakeHuman system
  assets released as CC0 in 2020) into `godot/assets/skin/photo/`, which is git-ignored. Not downloaded or checked here
  (the host was not reachable from the machine that wrote this); check the licence statement on the asset pages before
  committing any of them.

## 1a. MPFB2 region masks (CC0 1.0)

* **Upstream:** https://github.com/makehumancommunity/mpfb2 at `3af0cd5a2ffba072e93a34e5f6f494fdc4813ddf` (HEAD on
  2026-10-15). Its `LICENSE.md` section C: the assets ("textures", "the base mesh", "JSON data") are CC0 1.0; the
  verbatim CC0 text is its `LICENSE.ASSETS.md`. The Python program code (GPLv3) is not used.
* **Files read:** `src/mpfb/data/textures/mpfb_{lips,fingernails,toenails,eyelids,inside-mouth}.jpg` (2048 px greyscale
  masks painted for MakeHuman's UV layout).
* **Derived in this repo:** `godot/assets/skin/regions.png` (RGBA: lips, nails, eyelids, inside of the mouth), written by
  `tools/pack_skin_masks.py`.

## 2. FreeSewing (MIT)

* **Upstream:** https://github.com/freesewing/freesewing at commit
  `8a8de5a271d9823bb393735a41dd3993cf5bcf4b`. The root `LICENSE` is the MIT licence:
  `Copyright (c) 2022 Joost De Cock` (verbatim copy in `LICENSES/freesewing-MIT.txt`).
* **Package licences:** `package.json` of `packages/core` and `packages/models`, and of the designs `teagan`, `penelope`,
  `charlie`, `aaron`, `brian` and `titan`, each declares `"license": "MIT"`. All are version 4.0.0 at this commit. The
  design directories have no licence file of their own; the root `LICENSE` covers them.
* **Used in the repo:**
  * `app/assets/freesewing/raw_{teagan,penelope,charlie}_{athletic,average,curvy,petite,plus,tall}.json`, 18 files.
    Each holds the output of drafting one design for one mannequin preset (`design`, `measurements`, `pieces`), written
    by `tools/freesewing/export.mjs` (driven by `tools/fs_batch.sh`). Their top-level keys are as listed; they contain no
    FreeSewing source or documentation text.
  * `app/src/fsimport.js` exposes `FL.fsimport.plan(raw, body)`, which turns one of these JSON files into a pattern plan.
    It records `license: 'MIT (FreeSewing)'` and links to `https://freesewing.org/docs/designs/<name>`. The link points to
    the documentation; no documentation text is included. `tools/fs_plan.js` runs it headless.
  * `tools/freesewing/` runs FreeSewing's code offline in Node (`loader.mjs` maps `@freesewing/*` to a local clone).
    `tools/freesewing/shims/{get,set,clonedeep,unset}.mjs` are small Failoring-written replacements for four lodash
    functions; they are not lodash code.
* **Not in the built app:** `app/build.js` does not include `fsimport.js` or the JSON files, and `failoring.html` contains
  no FreeSewing text (checked). `tools/freesewing/README.md` says the import is not yet wired into the app UI.
* **Not used:** `brian` and `titan` are not referenced anywhere in the repo. `aaron` is in the import table and in
  `export.mjs` but has no bundled JSON.
* **Runtime:** `@freesewing/core` runs only in the offline tools. No FreeSewing code is in the browser build.
* **Documentation:** FreeSewing's documentation text is under different terms and is not included.

## 3. three.js r149 (MIT)

* **Upstream:** https://github.com/mrdoob/three.js tag `r149`. Its `LICENSE` is:
  `The MIT License`, `Copyright © 2010-2023 three.js authors` (verbatim copy in `LICENSES/threejs-MIT.txt`).
  `app/vendor/three.LICENSE` is byte-identical to that file.
* **Used:** `app/vendor/three.min.js` is inlined into `failoring.html` by `app/build.js` (placeholder `/*THREE*/`).
  The file's header reads `@license Copyright 2010-2023 Three.js Authors, SPDX-License-Identifier: MIT`, and its
  version constant is `"149"`.
* **Not verified:** that `three.min.js` is byte-identical to the r149 build artifact. The version string and header
  match.
* **Other scripts:** `app/src/index.html` has no external script, stylesheet or font loads. The only script placeholders
  are `/*THREE*/` and `/*APP*/`.

## 4. godot-cpp (MIT)

* **Upstream:** https://github.com/godotengine/godot-cpp. Tag `godot-4.3-stable` was checked. Its `LICENSE.md` is the
  MIT licence, `Copyright (c) 2017-present Godot Engine contributors` (verbatim copy in `LICENSES/godot-cpp-MIT.txt`).
* **Used:** `gdextension/CMakeLists.txt` expects godot-cpp at `../third_party/godot-cpp`, which is not in this repo.
  The tracked binary `godot/bin/libfailoring.so` contains godot-cpp code: its strings include godot-cpp source and
  header paths, and `N5godot...` class-binding symbols.
* **Not verified:** the exact godot-cpp commit used to build the binary. `docs/ARCHITECTURE.md` says `godot-4.3-stable`.
* **Dynamic dependencies of the binary** (not bundled): `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`.
  The binary was built with GCC 13.3.0 (Ubuntu 24.04).

## 5. wasm runtime: LLVM libc++, libc++abi, compiler-rt; wasi-libc (see 5a and 5b)

`wasm/failoring.wasm` (731,175 bytes, tracked) is built from our code with `tools/build_wasm.sh`. The wasm-embedded
`producers` section records `clang_18.1.2-wasi-sdk (https://github.com/llvm/llvm-project 26a1d6601d727a96f4301d0d8647b5a42760ae0c)`.
The runtime code below is in the binary. I identified it from the debug-info source paths and symbol strings. This is
not a byte-level comparison against the upstream sources.

### 5a. LLVM (Apache-2.0 WITH LLVM-exception)

* **Upstream:** https://github.com/llvm/llvm-project tag `llvmorg-18.1.2`. The licence header of each component says
  "The LLVM Project is under the Apache License v2.0 with LLVM Exceptions".
* **Present in the wasm:** libc++ (`libcxx/src/*.cpp` paths), libc++abi (`libcxxabi/src` paths), and the compiler-rt
  builtins (`compiler-rt/lib/builtins/*.c` paths, for example `multf3.c`, `udivti3.c`).
* **Licence texts:** `LICENSES/llvm-libcxx-Apache-2.0-with-LLVM-exceptions.txt`,
  `LICENSES/llvm-libcxxabi-Apache-2.0-with-LLVM-exceptions.txt`,
  `LICENSES/llvm-compiler-rt-Apache-2.0-with-LLVM-exceptions.txt`. These three files differ slightly from each other;
  each is copied from its own directory.

### 5b. wasi-libc (commit `b9ef79d7dbd47c6c5bafdae760823467c2f60b70`)

* **Upstream:** https://github.com/WebAssembly/wasi-libc. This is the commit that the `src/wasi-libc` submodule of
  `WebAssembly/wasi-sdk` tag `wasi-sdk-24` points to (checked with `git ls-tree`).
* **Licence:** the wasi-libc `LICENSE` file says the library as a whole is multi-licensed under the Apache License v2.0
  with LLVM Exceptions, the Apache License v2.0, and the MIT License. It then names the third-party parts and their
  licences. Those parts are listed below. Copies are in `LICENSES/wasi-libc-LICENSE.txt` and
  `LICENSES/wasi-libc-LICENSE-APACHE-LLVM.txt`.
* **Present in the wasm, by source path:**
  * **musl** (`libc-top-half/musl/src`, about 337 path hits): MIT, `Copyright © 2005-2020 Rich Felker, et al.`
    (verbatim in `LICENSES/musl-COPYRIGHT.txt`).
  * **cloudlibc** (`libc-bottom-half/cloudlibc/src`): BSD-2-Clause, `Copyright (c) 2015-2017 Nuxi (https://nuxi.nl/) and
    contributors` (verbatim in `LICENSES/cloudlibc-BSD-2-Clause-LICENSE.txt`).
  * **dlmalloc** (`dlmalloc/src/dlmalloc.c`): Doug Lea's allocator. wasi-libc's `dlmalloc/src/malloc.c` says it was
    "released to the public domain" at the CC0 1.0 link. `LICENSES/CC0-1.0-makehuman-LICENSE.ASSETS.txt` is the CC0 1.0
    text as MakeHuman ships it (section 1). I did not compare it byte-for-byte with creativecommons.org.
  * **emmalloc** (MIT in upstream): no strings from it were found in the wasm.
* **wasi-libc's own sources** in the binary (`libc-bottom-half/sources/...`, `crt/crt1-reactor.c`): Apache-2.0 WITH
  LLVM-exception, per the wasi-libc `LICENSE`.

## 5c. wasi-sdk 24 (build toolchain, not shipped)

* **Upstream:** https://github.com/WebAssembly/wasi-sdk tag `wasi-sdk-24`. Its `LICENSE` is the Apache-2.0 text followed
  by the "LLVM Exceptions to the Apache 2.0 License" (verbatim copy in `LICENSES/wasi-sdk-LICENSE.txt`).
* **Used:** `tools/build_wasm.sh` (`W=` path to the wasi-sdk install). Documented in `docs/WIP_HANDOFF.md`.
* **Not shipped:** the toolchain is not in the repo. The compiled runtime is covered in 5a and 5b.

## 6. Considered and not used

* **Body models:** SMPL, SMPL-X and STAR (non-commercial research licences), MB-Lab (AGPL-3.0), and scan datasets
  (CAESAR, FAUST and others). These were rejected in `docs/BODY_DESIGN.md`. No code or data from them was found in the
  repo. The only occurrences are in the docs.
* **CSparse (T. Davis, LGPL):** `cloth_core/src/sparse_chol.cpp` implements its own sparse Cholesky. The header cites
  CSparse's up-looking algorithm as the origin and keeps a `factorReference()` for cross-checking. The file contains no
  CSparse code and no `cs_*` identifiers, so no CSparse licence text is included.
* **Fabric parameters:** `materials/fabrics.json` (and the identical `godot/materials/fabrics.json`) contains
  order-of-magnitude starter values. Its `_comment` says they are "NOT measured data" and were assembled from typical
  ranges reported for the Kawabata Evaluation System (KES-F) and common textile conversions. No dataset or table is
  copied, and the file cites no source document.

## 7. Development tools (not shipped)

These are used by the tools and are not part of the distributed app.

* **Playwright** (Apache-2.0): `tools/gait_sheet.js`, `tools/walk_sim.js` and `tools/shoot_walk.js` load it from
  `/opt/npm-tools`. Its LICENSE there is the Apache-2.0 text. Version not recorded.
* **numpy** (used by `tools/convert_makehuman.py`) and **matplotlib** (`tools/freesewing/plot*.py`): **not verified**.
* **bezier-js** 6.1.4 (MIT, `Copyright (c) 2023 Pomax`): loaded by `tools/freesewing/loader.mjs` from a local clone. It is
  a dependency of FreeSewing's drafting code and is not shipped.

## 8. CrochetPARADE (GPL-3.0-or-later, not shipped)

* **Upstream:** https://github.com/stassev/CrochetPARADE commit
  `06e987b13ccfc94cfdfcebc1fe4664d37d2c6687` (committer date 2026-05-26). The README names Codeberg
  as the primary home; the Codeberg head recorded during the T4.0 spike was
  `be11c8b0a37f2fdeaca39bd2c1cd2b0cc7fc281d` (2026-03-13), older than the GitHub commit, and GitHub
  was used. 29 MB checkout: the JavaScript parser (`parse64.js`), the Emscripten layout
  (`graph64.js`, `graph64.wasm`), C++ sources, and the user manual (`Manual.md`).
* **Licence:** GPL-3.0-or-later. Every source header says "either version 3 of the License, or (at
  your option) any later version"; the root `LICENSE` is the GPLv3 text (232 lines), copied
  verbatim to `LICENSES/crochetparade-GPL-3.0.txt`. GPLv3 code may be combined into Failoring's
  AGPL-3.0 work. The user manual is CC BY-NC-SA 4.0 and is **not** copied anywhere in this repo;
  Failoring describes the grammar in its own words and links to the manual.
* **Used:** not committed (`third_party/` is git-ignored) and not linked into any build.
  `tools/crochet/cp_export.mjs` (T4.0 prototype) loads `parse64.js` and `graph64.wasm` headlessly
  with `node` to turn `patterns/cp/*.txt` into the JSON of `docs/CROCHET_PLAN.md` section 5.0; if
  the checkout is missing the tool exits with clone instructions.
* **What was checked:** the commit and the licence headers above (2026-10-16); all ten
  `patterns/cp/*.txt` fixtures parse and export with zero errors, with the counts and timings in
  `docs/crochet/T4.0_crochetparade.md`; headless layout runs (deterministic). **Not verified:** the
  Codeberg-vs-GitHub diff was not re-checked in the final session, and the web UI was not run.

## Licence text files

`LICENSES/`:

| File | Source |
|---|---|
| `CC0-1.0-makehuman-LICENSE.ASSETS.txt` | MakeHuman `LICENSE.ASSETS.md` at `a8bc2d54` (CC0 1.0 legal code) |
| `freesewing-MIT.txt` | FreeSewing `LICENSE` at `8a8de5a2` |
| `threejs-MIT.txt` | three.js `LICENSE` at tag `r149` |
| `godot-cpp-MIT.txt` | godot-cpp `LICENSE.md` at tag `godot-4.3-stable` |
| `llvm-libcxx-Apache-2.0-with-LLVM-exceptions.txt` | llvm-project `libcxx/LICENSE.TXT` at `llvmorg-18.1.2` |
| `llvm-libcxxabi-Apache-2.0-with-LLVM-exceptions.txt` | llvm-project `libcxxabi/LICENSE.TXT` at `llvmorg-18.1.2` |
| `llvm-compiler-rt-Apache-2.0-with-LLVM-exceptions.txt` | llvm-project `compiler-rt/LICENSE.TXT` at `llvmorg-18.1.2` |
| `wasi-sdk-LICENSE.txt` | wasi-sdk `LICENSE` at tag `wasi-sdk-24` |
| `wasi-libc-LICENSE.txt` | wasi-libc `LICENSE` at `b9ef79d7` |
| `wasi-libc-LICENSE-APACHE-LLVM.txt` | wasi-libc `LICENSE-APACHE-LLVM` at `b9ef79d7` |
| `musl-COPYRIGHT.txt` | wasi-libc `libc-top-half/musl/COPYRIGHT` at `b9ef79d7` |
| `cloudlibc-BSD-2-Clause-LICENSE.txt` | wasi-libc `libc-bottom-half/cloudlibc/LICENSE` at `b9ef79d7` |
| `crochetparade-GPL-3.0.txt` | CrochetPARADE `LICENSE` at `06e987b1` (GPLv3 text) |

## Failoring's licence

`LICENSE` is the GNU AGPL v3 text. Its structure matches the canonical text: the preamble, sections 0 to 17, `END OF
TERMS AND CONDITIONS`, and "How to Apply These Terms", with the header `Version 3, 19 November 2007` and the FSF copyright
line. SHA-256 of the file as committed: `895609d3e3043a5ffca941bbd0eb75e34470798261e49ba5c890292723952fb9`.
**Checked word for word** (2026-10-12) against `text/AGPL-3.0-only.txt` from github.com/spdx/license-list-data at commit
`31ba1a50e5397e00a304dbadc76531740e89ee48` (committer date 2026-09-16): both texts have 5535 whitespace-separated words
and differ in exactly three places, the URLs `http://fsf.org/` and `http://www.gnu.org/licenses/` (twice), which `LICENSE`
writes as `https://`. Line wrapping differs (whitespace was not compared). **Not checked** byte for byte against
https://www.gnu.org/licenses/agpl-3.0.txt: that host is refused by this environment's egress policy (HTTP 403), so the
SPDX copy was the reference.
