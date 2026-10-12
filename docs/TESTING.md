# Testing

## Browser smoke test

`tools/smoke_browser.js` is a regression smoke test for the browser app. It builds the app, opens it in
headless Chromium over `file://`, and walks through the main flows so the demo can be checked in one command.

```
node tools/smoke_browser.js
```

Exit status is 0 when every step passes and 1 otherwise. Typical run: about 2 minutes with software GL (SwiftShader) on a 2-core machine. Timeouts are generous
(up to 3 minutes per simulation or walk step) to allow for a slow CPU.

### Prerequisites

- Node.js (tested with v22).
- Playwright with Chromium. The script loads Playwright from `$PLAYWRIGHT_MODULE` if set, otherwise from
  `playwright` on the module path, otherwise from `/opt/npm-tools/node_modules/playwright`.
  Browser binaries are found by Playwright (`PLAYWRIGHT_BROWSERS_PATH` if set).
- No network access is needed: the build inlines three.js, the wasm solver, the body asset and the fabrics.

### What it does

1. **build**: runs `node app/build.js out/smoke/failoring.html`. The tracked root `failoring.html` is not
   rewritten.
2. **load**: opens the page; waits for the status line to show a meshed plan; checks the body model loaded and
   lists the plans and presets from the app.
3. **body presets**: clicks every preset in `FL.PRESETS` and checks the Body sliders show that preset's values.
4. **body slider**: sets bust to 100 cm and muscle to 80% and checks the state and readout.
5. **plan `<key>`** (one step per entry in `PLANS`): at Draft mesh quality, selects the plan, waits for the
   simulation to finish, and checks the final frame is finite.
6. **motion walking**: switches Motion to Walking and waits for a few walking frames.
7. **photo shoot**: opens the Shoot tab, takes one preview-size (800 x 1000) shot of the front view, and checks
   the gallery.
8. **responsive**: a tab click, a page evaluate and a 2-second animation-frame count must all stay within limits.

A step fails if its check fails, if it times out, or if any uncaught page error or `console.error` happens
during it. A page error also ends a wait early, so a crash does not sit out the whole timeout.

### Output

- A PASS/FAIL table on stdout, with the time and a detail line per step.
- One screenshot per step in `out/smoke/` (named `NN-step.png`). `out/` is git-ignored.
- `out/smoke/failoring.html`, the built page used by the run.

Set `SMOKE_DEBUG=1` to print the first lines of the stack for a failing step.

### Scope and limits

- It checks that flows complete and that the page does not throw. It does not compare pixels or values against
  reference output.
- The shoot step takes one shot only; the full set of seven is not exercised.
- Mesh quality is set to Draft for speed. Standard, Fine and Photo are not exercised.
- Steps run in order and share one page, so a failing step can leave the app in a state that affects the next.
