# FreeSewing export (work in progress)

`export.mjs` drafts a FreeSewing design (MIT, https://github.com/freesewing/freesewing) for the mannequin's measurements
(`node tools/fs_measures.js <preset>` -> measures json) and writes the seam-line outlines, cut counts and grain.
It runs offline in node through `loader.mjs` (a custom ESM loader that maps `@freesewing/*` to a sparse clone of the
FreeSewing repo in /tmp/fs_probe, with tiny lodash shims in `shims/`). The app side is `app/src/fsimport.js`;
`tools/fs_plan.js` and `tools/walk_sim.js` (PLANFILE=...) run it headless.

    node --import ./reg.mjs export.mjs teagan /tmp/m.json /tmp/fs_teagan.json
    node tools/fs_plan.js /tmp/fs_teagan.json average /tmp/plan.json
    STAND=1 PLANFILE=/tmp/plan.json node tools/walk_sim.js file average 70 /tmp/out

Status: tee (Teagan) drapes standing and walking; pencil skirt (Penelope) standing; trousers not done; not yet wired into the app UI.
