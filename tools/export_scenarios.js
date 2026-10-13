// Scenario library for the Godot showcase: body motions as per-bone rotations, plus the props they use.
//   node tools/export_scenarios.js [--out godot/assets/scenarios.json] [--preset average] [--quiet]
// Writes one JSON file (format 'failoring-scenarios', version 1). Motions are authored against the reference preset
// (average, the body the IK solves for) and are preset-independent: the Godot player applies the rotations to whatever
// mannequin is on screen, plants the feet with a floor shift computed from the skinned feet (as FL.Gait.pose does), and
// places the box from the hands it is carried in. Format and runtime: docs/SCENARIOS.md.
//
// Scenarios (seconds; stored at the solver's 60 steps per second, one sample per step):
//   catwalk     gait cycle (FL.Gait.rotations) with a longer stride, hip sway and a wider pelvis swing; loops
//   idle        standing, breathing (spine and shoulders), loops
//   dressing    arms up, T-pose, then A-pose (shoulder rotations only); does not loop
//   box         stand, reach down to a box on the floor, grip, lift to the chest, hold, lower, release, stand (IK)
//   chair       walk in place, stop, sit on a chair (feet planted, hips to seat height, IK), hold, stand up (IK)
// The IK (Levenberg-Marquardt over the browser's FK) keeps both ankles and toes on their standing positions, puts the
// hands on the box grip points and the hip joints on the seat height. The export checks every frame (see verify()) and
// exits non-zero when a check fails.
'use strict';
const fs = require('fs'), path = require('path');
const ROOT = path.join(__dirname, '..'), R = (...p) => path.join(ROOT, ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));

const FORMAT = 'failoring-scenarios', VERSION = 1;
const STORE_FPS = 60, SIM_FPS = 60;
const D = Math.PI / 180;

// ---------------------------------------------------------------- CLI
const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : def; };
const OUT = path.resolve(opt('--out', R('godot/assets/scenarios.json')));
const PRESET = opt('--preset', 'average');
const QUIET = args.includes('--quiet');
const log = (...a) => { if (!QUIET) console.log(...a); };

// ---------------------------------------------------------------- body
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const rest = Body.generate(M, FL.PRESETS[PRESET], null);
const NB = M.bones.length;
const BONE = {}; M.bones.forEach((b, k) => (BONE[b.joint] = k));
const FOOT_PART = [M.parts.indexOf('foot.L'), M.parts.indexOf('foot.R')];
const FOOT = FOOT_PART;
const zeroRot = () => M.bones.map(() => [0, 0, 0]);
const fk = (rot, root) => Body.fk(M, rest, rot, root || [0, 0, 0]);
// world position of a joint pivot: the bone that owns it (as pose_explicit does)
function jointAt(W, name) {
  const w = W[BONE[name]], J = rest.joints[name], A = w.A;
  return [A[0] * J[0] + A[1] * J[1] + A[2] * J[2] + w.t[0], A[3] * J[0] + A[4] * J[1] + A[5] * J[2] + w.t[1], A[6] * J[0] + A[7] * J[1] + A[8] * J[2] + w.t[2]];
}
const dist3 = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
const lerp3 = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
const smooth = (x) => { x = Math.max(0, Math.min(1, x)); return x * x * (3 - 2 * x); };

// the standing pose (unshifted): gait stand, the reference for planted feet and the arm rest
const STAND_ROT = FL.Gait.rotations(M, rest, 0, { stride: 0, arms: 0 });
const W_STAND = fk(STAND_ROT, [0, 0, 0]);
const STAND_ANKLE = { l: jointAt(W_STAND, 'l-ankle'), r: jointAt(W_STAND, 'r-ankle') };
const STAND_TOE = { l: jointAt(W_STAND, 'l-toe'), r: jointAt(W_STAND, 'r-toe') };
const STAND_HAND = { l: jointAt(W_STAND, 'l-hand'), r: jointAt(W_STAND, 'r-hand') };
// lowest foot vertex of the standing pose before any floor shift: IK scenarios must keep the feet at this height
const FMIN_STAND = (() => {
  const P = Body.pose(M, rest, STAND_ROT, [0, 0, 0]);
  let m = Infinity; for (let i = 0; i < M.nb; i++) if (M.part[i] === FOOT_PART[0] || M.part[i] === FOOT_PART[1]) m = Math.min(m, P.allPos[3 * i + 1]);
  return m;
})();
// Sole points: for each foot the lowest vertex (heel or ball) and the forward-most one (toe tip) of the standing pose.
// The IK holds them where they stand, so the sole cannot pitch even when the joints barely move.
const SOLE = (() => {
  const P = Body.pose(M, rest, STAND_ROT, [0, 0, 0]), out = [];
  for (const part of FOOT_PART) {
    let low = -1, front = -1;
    for (let i = 0; i < M.nb; i++) {
      if (M.part[i] !== part) continue;
      if (low < 0 || P.allPos[3 * i + 1] < P.allPos[3 * low + 1]) low = i;
      if (front < 0 || P.allPos[3 * i + 2] > P.allPos[3 * front + 2]) front = i;
    }
    out.push(low, front);
  }
  return out;
})();
// world position of a rest vertex under the skin (the bone matrices of W), as the browser's skinning
function skinPoint(W, i) {
  const x = rest.allPos[3 * i], y = rest.allPos[3 * i + 1], z = rest.allPos[3 * i + 2], o = [0, 0, 0];
  for (let j = 0; j < 4; j++) {
    const w = M.skinW[4 * i + j]; if (w === 0) continue;
    const b = W[M.skinIdx[4 * i + j]], A = b.A, t = b.t;
    o[0] += w * (A[0] * x + A[1] * y + A[2] * z + t[0]);
    o[1] += w * (A[3] * x + A[4] * y + A[5] * z + t[1]);
    o[2] += w * (A[6] * x + A[7] * y + A[8] * z + t[2]);
  }
  return o;
}
const SOLE_TARGET = (() => { const W = fk(STAND_ROT, [0, 0, 0]); return SOLE.map((i) => skinPoint(W, i)); })();

// ---------------------------------------------------------------- IK parameters
// Each parameter is one Euler angle of a bone (radians) or one root translation (metres). Bounds are joint limits in the
// browser's sign convention (hip flexion negative, knee flexion positive, elbow flexion negative).
const PARAMS = [];
const addRot = (bone, c, lo, hi) => PARAMS.push({ bone: BONE[bone], c, lo: lo * D, hi: hi * D, name: bone + '.' + 'xyz'[c] });
const addRoot = (c, lo, hi) => PARAMS.push({ bone: -1, c, lo, hi, name: 'root.' + 'xyz'[c] });
addRoot(0, -0.3, 0.3); addRoot(1, -0.3, 0.3); addRoot(2, -0.5, 0.5);
// spine and pelvis: positive x flexes forward (checked on the FK: the head moves forward), negative extends
addRot('pelvis', 0, -20, 70); addRot('pelvis', 1, -25, 25); addRot('pelvis', 2, -25, 25);
for (const b of ['spine-4', 'spine-3', 'spine-2', 'spine-1']) addRot(b, 0, -20, 45);
for (const s of ['l', 'r']) {
  addRot(s + '-upper-leg', 0, -130, 25); addRot(s + '-upper-leg', 2, -35, 35);
  addRot(s + '-knee', 0, 0, 150); addRot(s + '-ankle', 0, -45, 30); addRot(s + '-toe', 0, -40, 15);
  addRot(s + '-shoulder', 0, -170, 60); addRot(s + '-shoulder', 1, -70, 70); addRot(s + '-shoulder', 2, -110, 110);
  addRot(s + '-elbow', 0, -150, 5);
}
const NP = PARAMS.length;
const LO = PARAMS.map((p) => p.lo), HI = PARAMS.map((p) => p.hi);
const reg0 = PARAMS.map((q) => (q.bone < 0 ? 0 : STAND_ROT[q.bone][q.c]));   // the regulariser pulls towards the stand

// state -> rotations and root
function toState(p) {
  const rot = STAND_ROT.map((r) => r.slice());
  const root = [0, 0, 0];
  for (let i = 0; i < NP; i++) {
    const q = PARAMS[i];
    if (q.bone < 0) root[q.c] = p[i]; else rot[q.bone][q.c] = p[i];
  }
  return { rot, root };
}
const paramsOf = (rot, root) => PARAMS.map((q) => (q.bone < 0 ? root[q.c] : rot[q.bone][q.c]));

// targets: { hands: {l, r} (metres or null), hip: [y, z] or null }
function residuals(p, tg, reg) {
  const { rot, root } = toState(p);
  const W = fk(rot, root), r = [];
  for (const s of ['l', 'r']) {
    const a = jointAt(W, s + '-ankle'), t = jointAt(W, s + '-toe');
    r.push(4 * (a[0] - STAND_ANKLE[s][0]), 4 * (a[1] - STAND_ANKLE[s][1]), 4 * (a[2] - STAND_ANKLE[s][2]));
    r.push(4 * (t[0] - STAND_TOE[s][0]), 4 * (t[1] - STAND_TOE[s][1]), 4 * (t[2] - STAND_TOE[s][2]));
    if (tg.hands && tg.hands[s]) {
      const h = jointAt(W, s + '-hand'), g = tg.hands[s];
      r.push(3 * (h[0] - g[0]), 3 * (h[1] - g[1]), 3 * (h[2] - g[2]));
    }
  }
  for (let i = 0; i < SOLE.length; i++) {
    const q = skinPoint(W, SOLE[i]), g = SOLE_TARGET[i];
    r.push(4 * (q[0] - g[0]), 4 * (q[1] - g[1]), 4 * (q[2] - g[2]));
  }
  if (tg.hip) {
    const hl = jointAt(W, 'l-upper-leg'), hr = jointAt(W, 'r-upper-leg');
    r.push(0.5 * (hl[1] + hr[1]) - tg.hip[0], 0.5 * (hl[2] + hr[2]) - tg.hip[1]);
  }
  for (let i = 0; i < NP; i++) r.push(reg * (p[i] - reg0[i]));
  return r;
}
const REG = 0.01;   // per radian (metres per radian of the residual): keeps the posture near the stand without holding the reach

// dense symmetric solve (Gauss-Jordan with partial pivoting)
function solve(A, b) {
  const n = b.length, m = A.map((row, i) => [...row, b[i]]);
  for (let c = 0; c < n; c++) {
    let piv = c;
    for (let r = c + 1; r < n; r++) if (Math.abs(m[r][c]) > Math.abs(m[piv][c])) piv = r;
    [m[c], m[piv]] = [m[piv], m[c]];
    const d = m[c][c] || 1e-12;
    for (let k = c; k <= n; k++) m[c][k] /= d;
    for (let r = 0; r < n; r++) if (r !== c && m[r][c] !== 0) { const f = m[r][c]; for (let k = c; k <= n; k++) m[r][k] -= f * m[c][k]; }
  }
  return m.map((row) => row[n]);
}

// Levenberg-Marquardt from p0 (warm start) to the targets; returns { p, cost, iters }.
function ik(p0, tg, maxIters = 120) {
  let p = p0.map((v, i) => Math.min(HI[i], Math.max(LO[i], v)));
  let r = residuals(p, tg, REG), cost = r.reduce((s, x) => s + x * x, 0), lambda = 1e-3, iters = 0;
  const h = 1e-6, m = r.length;
  for (; iters < maxIters; iters++) {
    // numeric Jacobian
    const J = Array.from({ length: m }, () => new Array(NP).fill(0));
    for (let j = 0; j < NP; j++) {
      const q = p.slice(); q[j] += h;
      const rj = residuals(q, tg, REG);
      for (let i = 0; i < m; i++) J[i][j] = (rj[i] - r[i]) / h;
    }
    const A = Array.from({ length: NP }, () => new Array(NP).fill(0)), g = new Array(NP).fill(0);
    for (let i = 0; i < m; i++) for (let a = 0; a < NP; a++) {
      g[a] -= J[i][a] * r[i];
      for (let b = 0; b < NP; b++) A[a][b] += J[i][a] * J[i][b];
    }
    let improved = false;
    for (let attempt = 0; attempt < 12 && !improved; attempt++) {
      const B = A.map((row, a) => row.map((v, b) => (a === b ? v + lambda * (v + 1e-6) : v)));
      const step = solve(B, g);
      const q = p.map((v, i) => Math.min(HI[i], Math.max(LO[i], v + step[i])));
      const rq = residuals(q, tg, REG), cq = rq.reduce((s, x) => s + x * x, 0);
      if (cq < cost) {
        const dc = cost - cq, small = Math.hypot(...step) < 1e-7;
        p = q; r = rq; cost = cq; lambda = Math.max(lambda / 3, 1e-7); improved = true;
        if (small || dc < 1e-12) return { p, cost, iters: iters + 1 };
      } else lambda *= 4;
    }
    if (!improved) break;
  }
  return { p, cost, iters };
}

// ---------------------------------------------------------------- scenarios
const TAU = 2 * Math.PI;
const GRIP_HW = 0.17;                 // half the box width: the hands grip its sides
// The box on the floor sits in front of the toes (their tips are at z of about 0.2): its back face is at z 0.24. The
// grip reaches z 0.36 with the arms of this body (staged IK, 1 mm), so the box is 0.34 x 0.30 x 0.24 m.
const BOX = { half: [0.17, 0.15, 0.12], floor: [0, 0.15, 0.36], chest: [0, 1.00, 0.30], grip_y: 0.10 };

// Samples a scenario at time t: returns { rot, root, hands, hip } (rot as an array per bone, root in metres).
// IK scenarios need the previous solution (warm start), so the caller samples in order.
function catwalkSample(t, L) {
  const cyc = 64 / 60, ph = (t / cyc) % 1;
  const rot = FL.Gait.rotations(M, rest, ph, { stride: 1.2, arms: 1.15 });   // 1.2 x the walk's stride
  rot[BONE.pelvis][1] *= 1.4; rot[BONE.pelvis][2] *= 1.4;                    // wider pelvis swing (hip sway)
  return { rot, root: [0.03 * Math.sin(TAU * ph), 0, 0] };
}
function idleSample(t) {
  const rot = STAND_ROT.map((r) => r.slice()), b = 0.5 + 0.5 * Math.sin(TAU * t / 4);
  rot[BONE['spine-2']][0] -= 0.010 * b; rot[BONE['spine-3']][0] -= 0.006 * b; rot[BONE['spine-1']][0] -= 0.004 * b;
  rot[BONE['l-clavicle']][2] += 0.012 * b; rot[BONE['r-clavicle']][2] -= 0.012 * b;
  rot[BONE.neck][0] -= 0.004 * b;
  return { rot, root: [0, 0, 0] };
}
// the arm's angle from vertical (outward, degrees) for a shoulder rotation about z. A rotation about the shoulder's z
// axis adds to this angle linearly, so the shoulder angle for a target is solved from the slope (checked in armRz).
const armAngle = (side, rz) => {
  const rot = STAND_ROT.map((r) => r.slice()); rot[BONE[side + '-shoulder']][2] = rz;
  const W = fk(rot, [0, 0, 0]), sh = jointAt(W, side + '-shoulder'), hd = jointAt(W, side + '-hand');
  const dx = side === 'l' ? hd[0] - sh[0] : sh[0] - hd[0], dy = hd[1] - sh[1];
  return Math.atan2(dx, -dy) / D;
};
function armRz(side, deg) {
  const a0 = armAngle(side, 0), slope = (armAngle(side, 0.01) - a0) / 0.01;   // degrees per radian
  const rz = (deg - a0) / slope;
  const err = Math.abs(armAngle(side, rz) - deg);
  if (err > 0.05) throw new Error(`armRz: ${side} arm at ${deg} deg is off by ${err} deg`);
  return rz;
}
const KEYS_DRESS = [[0, 42], [1.5, 170], [3.0, 90], [4.5, 42], [6.0, 42]];   // arm angle from vertical, degrees
function keyed(keys, t) {
  for (let i = 0; i + 1 < keys.length; i++) if (t <= keys[i + 1][0]) {
    const u = smooth((t - keys[i][0]) / (keys[i + 1][0] - keys[i][0]));
    return keys[i][1] + (keys[i + 1][1] - keys[i][1]) * u;
  }
  return keys[keys.length - 1][1];
}
function dressingSample(t) {
  const rot = STAND_ROT.map((r) => r.slice()), a = keyed(KEYS_DRESS, t);
  rot[BONE['l-shoulder']][2] = armRz('l', a);
  rot[BONE['r-shoulder']][2] = armRz('r', a);
  return { rot, root: [0, 0, 0] };
}

// box: hand targets over time (null = free, the arms rest on the stand pose)
const T_BOX = { reach0: 0.6, grasp: 1.8, lift0: 2.2, lift1: 3.4, lower0: 4.2, lower1: 5.4, release: 5.8, free: 6.8, end: 7.0 };
function boxHands(t, standHand) {
  const g = (c) => ({ l: [GRIP_HW, c[1] + BOX.grip_y, c[2]], r: [-GRIP_HW, c[1] + BOX.grip_y, c[2]] });
  const floorG = g(BOX.floor), chestG = g(BOX.chest);
  const at = (a, b, u) => ({ l: lerp3(a.l, b.l, smooth(u)), r: lerp3(a.r, b.r, smooth(u)) });
  if (t < T_BOX.reach0) return null;
  if (t < T_BOX.grasp) return at({ l: standHand.l, r: standHand.r }, floorG, (t - T_BOX.reach0) / (T_BOX.grasp - T_BOX.reach0));
  if (t < T_BOX.lift0) return floorG;
  if (t < T_BOX.lift1) return at(floorG, chestG, (t - T_BOX.lift0) / (T_BOX.lift1 - T_BOX.lift0));
  if (t < T_BOX.lower0) return chestG;
  if (t < T_BOX.lower1) return at(chestG, floorG, (t - T_BOX.lower0) / (T_BOX.lower1 - T_BOX.lower0));
  if (t < T_BOX.release) return floorG;
  if (t < T_BOX.free) return at(floorG, { l: standHand.l, r: standHand.r }, (t - T_BOX.release) / (T_BOX.free - T_BOX.release));
  return null;
}

// chair: walk in place until t=1.6 (stride eases to 0 over the last 0.6 s), then IK: hips to seat height and back to standing
// the seat is under the hips (z -0.10: the hips move back 10 cm over the feet, which stay planted); the seated hip joints
// reach 0.60 m high and 0.10 m back with the feet on the floor (the IK in probe6: residual 1.5 mm at the feet)
const CHAIR = { seat: [0, 0.44, -0.08], seatHalf: [0.20, 0.02, 0.20], seatTop: 0.46, hipSit: [0.63, -0.07] };
const T_CHAIR = { walk: 1.6, sit0: 1.6, sit1: 2.9, stand0: 4.6, stand1: 5.9, end: 7.0 };
const HIP_STAND = (() => {
  const W = fk(STAND_ROT, [0, 0, 0]), l = jointAt(W, 'l-upper-leg'), r = jointAt(W, 'r-upper-leg');
  return [0.5 * (l[1] + r[1]), 0.5 * (l[2] + r[2])];
})();
// hip-joint height and depth targets: the standing hips until sit0, the seat height at sit1, back up after stand0
function chairHip(t) {
  if (t < T_CHAIR.sit0) return null;
  let u;
  if (t < T_CHAIR.sit1) u = smooth((t - T_CHAIR.sit0) / (T_CHAIR.sit1 - T_CHAIR.sit0));
  else if (t < T_CHAIR.stand0) u = 1;
  else if (t < T_CHAIR.stand1) u = 1 - smooth((t - T_CHAIR.stand0) / (T_CHAIR.stand1 - T_CHAIR.stand0));
  else u = 0;
  return [HIP_STAND[0] + (CHAIR.hipSit[0] - HIP_STAND[0]) * u, HIP_STAND[1] + (CHAIR.hipSit[1] - HIP_STAND[1]) * u];
}
// the walk-in: gait cycles with the stride easing to zero (zero stride is the standing pose)
function chairWalkSample(t) {
  const s = t < 1.0 ? 1 : 1 - smooth((t - 1.0) / 0.6), ph = (t / (64 / 60)) % 1;
  const rot = FL.Gait.rotations(M, rest, ph, { stride: s, arms: s });
  return { rot, root: [0.018 * s * Math.sin(TAU * ph), 0, 0] };
}

// ---------------------------------------------------------------- build a scenario
// A scenario is a list of samples at STORE_FPS. Motions without IK are direct; IK motions warm-start frame to frame.
function build(id) {
  const spec = SCEN[id], N = Math.round(spec.duration * STORE_FPS);
  const rot = [], root = [], hands = [], hip = [];
  let p = PARAMS.map((q) => (q.bone < 0 ? 0 : STAND_ROT[q.bone][q.c]));  // IK state (warm start): the standing pose
  const worst = { ik: 0, hands: 0 };
  for (let k = 0; k < N; k++) {
    const t = k / STORE_FPS;
    let s;
    if (spec.ik && t >= (spec.ikFrom || 0)) {
      const tg = spec.targets(t);
      p = ik(p, tg, 400).p;
      s = toState(p);
      const W = fk(s.rot, s.root);
      for (const sd of ['l', 'r']) {
        worst.ik = Math.max(worst.ik, dist3(jointAt(W, sd + '-ankle'), STAND_ANKLE[sd]));
        if (tg.hands && tg.hands[sd]) worst.hands = Math.max(worst.hands, dist3(jointAt(W, sd + '-hand'), tg.hands[sd]));
      }
    } else {
      s = spec.sample(t);
    }
    rot.push(s.rot.map((r) => r.map((v) => +v.toFixed(5))).flat());
    root.push(s.root.map((v) => +v.toFixed(5)));
  }
  return { rot, root, worst };
}

const SCEN = {
  catwalk: { name: 'Catwalk', duration: 64 / 60, loop: true, sample: catwalkSample, props: [] },
  idle: { name: 'Idle breathing', duration: 4.0, loop: true, sample: idleSample, props: [] },
  dressing: { name: 'Arms up to A-pose', duration: 6.0, loop: false, sample: dressingSample, props: [] },
  box: { name: 'Pick up a box', duration: T_BOX.end, loop: false, ik: true, ikFrom: 0,
    targets: (t) => ({ hands: boxHands(t, STAND_HAND), hip: null }),
    props: [{ type: 'box', id: 'box', half: BOX.half, center: BOX.floor, attach: [T_BOX.grasp, T_BOX.release], grip_offset: [0, BOX.grip_y, 0] }] },
  chair: { name: 'Sit on a chair', duration: T_CHAIR.end, loop: false, ik: true, ikFrom: T_CHAIR.walk, sample: chairWalkSample,
    targets: (t) => ({ hands: null, hip: chairHip(t) }),
    props: [{ type: 'chair', id: 'chair', parts: chairParts() }] },
};
function chairParts() {
  const [sx, sy, sz] = CHAIR.seatHalf, top = CHAIR.seatTop, c = CHAIR.seat;
  const parts = [{ half: [sx, sy, sz], center: [c[0], top - sy, c[2]] }];                 // seat
  const legH = top - 2 * sy;
  for (const x of [-0.17, 0.17]) for (const z of [-0.17, 0.17])
    parts.push({ half: [0.018, legH / 2, 0.018], center: [x, legH / 2, z] });              // legs
  parts.push({ half: [sx, 0.25, 0.02], center: [c[0], top + 0.25, c[2] - sz - 0.02] });    // back
  return parts;
}

// ---------------------------------------------------------------- main
function main() {
  const out = { format: FORMAT, version: VERSION, reference: PRESET, bones: M.bones.map((b) => b.joint), vertices: M.nb,
    store_fps: STORE_FPS, sim_fps: SIM_FPS, units: 'm, radians; y up, +z front, +x body left; rotations are [rx, ry, rz] per bone in bone order (FL.Body.fk); root = pelvis translation before the floor shift',
    scenarios: [] };
  const fails = [];
  const report = {};
  for (const id of Object.keys(SCEN)) {
    const spec = SCEN[id];
    const b = build(id);
    const entry = { id, name: spec.name, duration: +spec.duration.toFixed(6), frames: b.rot.length, fps: STORE_FPS, loop: spec.loop, props: spec.props, rot: b.rot, root: b.root };
    // checks: planted feet (before the floor shift), floor after the shift, and hands on the box
    const chk = verify(id, spec, b);
    entry.checks = chk.summary;
    entry.reference = chk.reference;
    out.scenarios.push(entry);
    report[id] = chk.summary;
    fails.push(...chk.fails);
  }
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, JSON.stringify(out));
  log(JSON.stringify(report, null, 1));
  log(`wrote ${OUT} (${(fs.statSync(OUT).size / 1024).toFixed(0)} KB)`);
  if (fails.length) { console.error('scenario checks FAILED:\n  ' + fails.join('\n  ')); process.exit(1); }
  log('scenario checks passed');
}

// Re-evaluates every stored frame the way the Godot player does (rotations -> FK -> floor shift from the feet -> skin),
// and checks the result. Thresholds: planted feet 1.5 cm (before the shift), floor -0.1 mm, hands 1.2 cm from the grip.
const LIMITS = { planted: 0.015, floor: -1e-4, hands: 0.012 };
function verify(id, spec, b) {
  const fails = [], s = { planted_max_m: 0, floor_min_m: Infinity, hand_box_max_m: 0, ik_ankle_max_m: 0, shift_max_m: 0, foot_box_min_m: Infinity };
  const boxSpec = spec.props.find((p) => p.type === 'box');
  const reference = {};
  let boxC = null, boxOff = null;
  const sample = Math.round(spec.duration * STORE_FPS / 2);  // a mid-scenario frame for the cross-language check
  for (let k = 0; k < b.rot.length; k++) {
    const rot = []; for (let i = 0; i < NB; i++) rot.push(b.rot[k].slice(3 * i, 3 * i + 3));
    const P = Body.pose(M, rest, rot, b.root[k]);
    let fmin = Infinity; for (let i = 0; i < M.nb; i++) if (FOOT.includes(M.part[i])) fmin = Math.min(fmin, P.allPos[3 * i + 1]);
    const shift = -fmin;
    const dev = Math.abs(fmin - FMIN_STAND);   // how far the feet moved from their standing height
    const planted = spec.ik && k / STORE_FPS >= (spec.ikFrom || 0);   // the IK part: the feet must not move
    if (planted) s.planted_max_m = Math.max(s.planted_max_m, dev);
    s.shift_max_m = Math.max(s.shift_max_m, Math.abs(shift));
    let vmin = Infinity; for (let i = 0; i < M.nb; i++) vmin = Math.min(vmin, P.allPos[3 * i + 1] + shift);
    s.floor_min_m = Math.min(s.floor_min_m, vmin);
    if (vmin < LIMITS.floor) fails.push(`${id} frame ${k}: body below the floor (${vmin.toFixed(5)} m)`);
    if (planted && dev > LIMITS.planted) fails.push(`${id} frame ${k}: feet ${dev.toFixed(4)} m off their standing height`);
    const J = {};
    for (const n of ['l-hand', 'r-hand', 'l-ankle', 'r-ankle', 'pelvis']) J[n] = jointAt(P.W, n).map((v, i) => (i === 1 ? v + shift : v));
    if (boxSpec && k / STORE_FPS < boxSpec.attach[0]) {   // resting on the floor: the feet must clear it
      for (let i = 0; i < M.nb; i++) if (FOOT.includes(M.part[i])) {
        const d = boxSurface([P.allPos[3 * i], P.allPos[3 * i + 1] + shift, P.allPos[3 * i + 2]], boxSpec.center, boxSpec.half);
        s.foot_box_min_m = Math.min(s.foot_box_min_m, d);
      }
    }
    if (boxSpec && k / STORE_FPS >= boxSpec.attach[0] && k / STORE_FPS <= boxSpec.attach[1]) {
      const t = k / STORE_FPS;
      const mid = [0, 1, 2].map((i) => 0.5 * (J['l-hand'][i] + J['r-hand'][i]));
      if (boxC === null) { boxC = boxSpec.center.slice(); boxOff = boxC.map((c, i) => c - mid[i]); }
      const cen = [0, 1, 2].map((i) => mid[i] + boxOff[i]);   // the box follows the hands it is carried in (the runtime's rule)
      for (const hd of ['l-hand', 'r-hand']) {
        const d = boxSurface(J[hd], cen, boxSpec.half);
        s.hand_box_max_m = Math.max(s.hand_box_max_m, Math.abs(d));
        if (Math.abs(d) > LIMITS.hands) fails.push(`${id} t=${t.toFixed(2)}: ${hd} is ${Math.abs(d).toFixed(4)} m from the box surface`);
      }
    }
    if (k === sample) reference.frame = k, reference.joints = Object.fromEntries(Object.entries(J).map(([n, v]) => [n, v.map((x) => +x.toFixed(5))]));
  }
  if (spec.ik) s.ik_ankle_max_m = +b.worst.ik.toFixed(5);
  const r = (v) => (Number.isFinite(v) ? +v.toFixed(5) : v);
  const summary = { planted_max_m: spec.ik ? r(s.planted_max_m) : null, planted_from_s: spec.ik ? (spec.ikFrom || 0) : null, floor_min_m: r(s.floor_min_m), hand_box_max_m: r(s.hand_box_max_m), shift_max_m: r(s.shift_max_m), ik_ankle_max_m: s.ik_ankle_max_m, ik_hand_max_m: r(b.worst.hands), frames: b.rot.length };
  if (boxSpec) {
    summary.foot_box_min_m = r(s.foot_box_min_m);
    if (s.foot_box_min_m < 0.005) fails.push(`${id}: a foot is ${s.foot_box_min_m.toFixed(4)} m from the box at rest`);
  }
  // the hands lag a moving grip by up to 2 cm; the contact that matters (hand to box, on the grip) is checked above
  if (spec.ik && b.worst.hands > 0.02) fails.push(`${id}: hand IK residual ${b.worst.hands.toFixed(4)} m`);
  return { summary, fails, reference };
}
// signed distance from p to an axis-aligned box (outside positive)
function boxSurface(p, c, h) {
  const q = [0, 1, 2].map((i) => Math.abs(p[i] - c[i]) - h[i]);
  const o = Math.hypot(...q.map((v) => Math.max(v, 0)));
  return o > 0 ? o : Math.max(...q);
}

if (require.main === module) main();
module.exports = { ik, residuals, toState, jointAt, fk, STAND_ROT, STAND_HAND, STAND_ANKLE, STAND_TOE, SCEN, BOX, boxHands, chairHip, HIP_STAND, CHAIR, PARAMS, LO, HI };
