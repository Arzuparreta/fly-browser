'use strict';
// Fly anatomy seen from above, on a 1000 unit canvas, with the geometry helpers the renderers share.

const TAU = Math.PI * 2;
const DEG = Math.PI / 180;
const r1 = (n) => Math.round(n * 10) / 10;

function mulberry(seed) {
  return () => {
    seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const add = (a, b) => [a[0] + b[0], a[1] + b[1]];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1]];
const mul = (a, k) => [a[0] * k, a[1] * k];
const len = (a) => Math.hypot(a[0], a[1]);
const unit = (a) => { const l = len(a) || 1; return [a[0] / l, a[1] / l]; };
const perp = (a) => [-a[1], a[0]];
const lerp = (a, b, t) => a + (b - a) * t;
const lerp2 = (a, b, t) => [lerp(a[0], b[0], t), lerp(a[1], b[1], t)];
const clamp = (x, a = 0, b = 1) => Math.max(a, Math.min(b, x));
const smooth = (e0, e1, x) => { const t = clamp((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); };
const mirrorPt = (p) => [1000 - p[0], p[1]];

function catmull(points, closed = false, samples = 10) {
  const out = [];
  const n = points.length;
  const get = (i) => closed ? points[(i + n) % n] : points[Math.max(0, Math.min(n - 1, i))];
  const segs = closed ? n : n - 1;
  for (let i = 0; i < segs; i++) {
    const p0 = get(i - 1), p1 = get(i), p2 = get(i + 1), p3 = get(i + 2);
    for (let s = 0; s < samples; s++) {
      const t = s / samples, t2 = t * t, t3 = t2 * t;
      out.push([0, 1].map((k) => 0.5 * ((2 * p1[k]) + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2 + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3)));
    }
  }
  if (!closed) out.push(points[n - 1]);
  return out;
}

const pathOf = (pts, close = true) => 'M' + pts.map((p) => r1(p[0]) + ',' + r1(p[1])).join('L') + (close ? 'Z' : '');

function inPoly(pts, x, y) {
  let c = false;
  for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
    const [xi, yi] = pts[i], [xj, yj] = pts[j];
    if ((yi > y) !== (yj > y) && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi) c = !c;
  }
  return c;
}

// Tapered stroke along a polyline, widths given per point.
function ribbon(pts, widths) {
  const left = [], right = [];
  for (let i = 0; i < pts.length; i++) {
    const a = pts[Math.max(0, i - 1)], b = pts[Math.min(pts.length - 1, i + 1)];
    const n = perp(unit(sub(b, a)));
    const w = widths[i] / 2;
    left.push(add(pts[i], mul(n, w)));
    right.push(sub(pts[i], mul(n, w)));
  }
  return pathOf(left.concat(right.reverse()));
}

function taper(pts, w0, w1, wMid) {
  const n = pts.length;
  const ws = pts.map((_, i) => {
    const t = i / (n - 1);
    const base = lerp(w0, w1, t);
    return wMid === undefined ? base : base + (wMid - (w0 + w1) / 2) * Math.sin(Math.PI * t);
  });
  return ribbon(pts, ws);
}

// Blobs: superellipses with separate front/back half-lengths and a width taper.
function Blob(o) {
  const B = Object.assign({ n: 2.2, rot: 0, k: 0, dome: 1 }, o);
  if (B.bt === undefined) B.bt = B.b;
  if (B.bb === undefined) B.bb = B.b;
  return B;
}
function toLocal(B, x, y) {
  const r = -B.rot * DEG, c = Math.cos(r), s = Math.sin(r);
  const dx = x - B.cx, dy = y - B.cy;
  return [dx * c - dy * s, dx * s + dy * c];
}
function toGlobal(B, lx, ly) {
  const r = B.rot * DEG, c = Math.cos(r), s = Math.sin(r);
  return [B.cx + lx * c - ly * s, B.cy + lx * s + ly * c];
}
function blobUV(B, x, y) {
  const [lx, ly] = toLocal(B, x, y);
  const b = ly < 0 ? B.bt : B.bb;
  const v = ly / b;
  const a = B.a * (1 + B.k * v);
  return [lx / a, v];
}
function blobR(B, x, y) {
  const [u, v] = blobUV(B, x, y);
  return Math.pow(Math.pow(Math.abs(u), B.n) + Math.pow(Math.abs(v), B.n), 1 / B.n);
}
const blobIn = (B, x, y) => blobR(B, x, y) <= 1;
function blobPts(B, N = 220) {
  const pts = [];
  for (let i = 0; i < N; i++) {
    const t = (i / N) * TAU, ct = Math.cos(t), st = Math.sin(t);
    const v = Math.sign(st) * Math.pow(Math.abs(st), 2 / B.n);
    const b = st < 0 ? B.bt : B.bb;
    const a = B.a * (1 + B.k * v);
    pts.push(toGlobal(B, a * Math.sign(ct) * Math.pow(Math.abs(ct), 2 / B.n), b * v));
  }
  return pts;
}
// Lambert term for a dome over the blob; light from the upper left, slightly in front.
const LIGHT = (() => { const v = [-0.5, -0.62, 0.6]; const l = Math.hypot(...v); return v.map((x) => x / l); })();
function blobLight(B, x, y) {
  const [u, v] = blobUV(B, x, y);
  const r = Math.min(0.999, Math.hypot(u, v));
  const z = Math.sqrt(1 - r * r) * B.dome;
  const rr = B.rot * DEG, c = Math.cos(rr), s = Math.sin(rr);
  const gx = u * c - v * s, gy = u * s + v * c;
  const l = Math.hypot(gx, gy, z) || 1;
  return Math.max(0, (gx * LIGHT[0] + gy * LIGHT[1] + z * LIGHT[2]) / l);
}

// Anatomy (1000 x 1000, head up)
function anatomy() {
  const A = {};
  A.abdomen = Blob({ cx: 500, cy: 706, a: 166, bt: 122, bb: 160, n: 2.08, k: -0.1, dome: 1.1 });
  A.thorax = Blob({ cx: 500, cy: 452, a: 150, bt: 112, bb: 108, n: 2.55, k: -0.05, dome: 0.9 });
  A.scutellum = Blob({ cx: 500, cy: 564, a: 84, bt: 20, bb: 40, n: 2.2, dome: 0.8 });
  A.head = Blob({ cx: 500, cy: 262, a: 176, bt: 84, bb: 90, n: 2.4, k: -0.14, dome: 0.85 });
  A.eyes = [
    Blob({ cx: 404, cy: 258, a: 80, bt: 86, bb: 84, n: 2.1, k: 0.06, rot: -24, dome: 1.25 }),
    Blob({ cx: 596, cy: 258, a: 80, bt: 86, bb: 84, n: 2.1, k: 0.06, rot: 24, dome: 1.25 })
  ];

  // Wings: local frame u along the wing, v positive towards the trailing edge.
  const wingLocal = {
    outline: [[0, -6], [40, -22], [120, -44], [220, -60], [320, -68], [400, -62], [448, -44], [472, -12], [468, 20], [442, 54], [392, 84], [322, 104], [242, 110], [172, 102], [120, 88], [92, 84], [66, 76], [50, 62], [38, 64], [22, 52], [12, 30], [4, 12]],
    veins: {
      costa: { pts: [[4, -9], [40, -24], [120, -46], [220, -62], [320, -70], [400, -64], [446, -47]], w: [7, 4.5] },
      sc: { pts: [[10, -10], [90, -32], [178, -54]], w: [4.5, 2.4] },
      r1: { pts: [[14, -5], [120, -32], [258, -62]], w: [5, 2.8] },
      r23: { pts: [[44, -7], [200, -35], [330, -57], [408, -61]], w: [4.2, 2.4] },
      r45: { pts: [[46, -1], [200, -14], [350, -22], [458, -27]], w: [4.4, 2.4] },
      m: { pts: [[38, 10], [180, 22], [306, 30], [366, 27], [416, 8], [456, -15]], w: [4, 2.2] },
      cua: { pts: [[32, 22], [170, 50], [300, 78], [370, 92]], w: [4, 2.2] },
      a1: { pts: [[24, 34], [86, 60], [126, 84]], w: [3.6, 2.2] },
      rm: { pts: [[206, -14], [211, 23]], w: [2.6, 2.6] },
      dmcu: { pts: [[300, 30], [312, 80]], w: [2.6, 2.6] },
      hum: { pts: [[30, -19], [33, -6]], w: [2.4, 2.4] }
    }
  };
  const sweep = 26;
  function wing(side) {
    const root = side < 0 ? [378, 452] : [622, 452];
    const ang = sweep * DEG;
    const U = [side * Math.sin(ang), Math.cos(ang)];
    // V points towards the body axis, which is the trailing edge of a swept wing.
    const Vfix = [-side * Math.cos(ang), Math.sin(ang)];
    const T = (p) => add(root, add(mul(U, p[0]), mul(Vfix, p[1])));
    const outline = catmull(wingLocal.outline.map(T), true, 8);
    const veins = {};
    for (const [k, v] of Object.entries(wingLocal.veins)) veins[k] = { pts: catmull(v.pts.map(T), false, 10), w: v.w };
    return { side, root, U, V: Vfix, T, outline, veins };
  }
  A.wings = [wing(-1), wing(1)];

  // Legs: coxa, knee, ankle, tarsal tip (left side, mirrored for right).
  const legsL = [
    { j: [[440, 352], [346, 330], [296, 258], [272, 178]], w: [24, 15, 9] },
    { j: [[420, 456], [300, 452], [212, 498], [140, 528]], w: [25, 15, 9] },
    { j: [[446, 566], [344, 606], [244, 650], [136, 690]], w: [27, 16, 9] }
  ];
  A.legs = [];
  for (const L of legsL) {
    A.legs.push({ side: -1, j: L.j, w: L.w });
    A.legs.push({ side: 1, j: L.j.map(mirrorPt), w: L.w });
  }
  return A;
}

// Bristles: base, direction, length, width.
function bristles(A, rnd) {
  const out = [];
  const push = (x, y, ang, l, w = 5) => out.push({ p: [x, y], d: [Math.sin(ang * DEG), Math.cos(ang * DEG)], l, w });
  for (const s of [-1, 1]) {
    for (let y = 420; y <= 520; y += 50) push(500 + s * 140, y, s * 40, 38 + rnd() * 6, 6);
    push(500 + s * 122, 368, s * 62, 38, 6);
    push(500 + s * 134, 392, s * 70, 34, 5.5);
    for (const a of [0.35, 0.75]) {
      const t = a * Math.PI / 2;
      push(500 + s * 86 * Math.sin(t), 566 + 46 * Math.cos(t), s * (18 + 40 * a), 54, 6.5);
    }
    // vertex and frontal row
    push(500 + s * 22, 334, s * 14, 30, 4.6);
    push(500 + s * 40, 326, s * 34, 26, 4.2);
    for (let i = 0; i < 4; i++) push(500 + s * (16 + i * 2), 214 + i * 24, s * (150 - i * 12), 20, 3.4);
    // abdominal marginal bristles near the tip
    for (let i = 1; i < 6; i += 2) {
      const t = -0.9 + i * 0.36;
      const x = 500 + s * 140 * Math.abs(Math.sin(t)) * 0.95;
      const y = 706 + 120 * Math.cos(t) * 0.92 + 18;
      push(x, y, s * (8 + i * 9), 30 + rnd() * 8, 5);
    }
  }
  return out;
}

function legGeometry(L) {
  // Returns segment polylines and widths for femur, tibia, tarsus (five tarsomeres).
  const [c, k, a, t] = L.j;
  const bow = (p0, p1, amt) => { const m = lerp2(p0, p1, 0.5); const n = perp(unit(sub(p1, p0))); return catmull([p0, add(m, mul(n, amt * L.side)), p1], false, 12); };
  const femur = bow(c, k, -10);
  const tibia = bow(k, a, 6);
  const tarsus = bow(a, t, 4);
  return { femur, tibia, tarsus, joints: [k, a], tip: t, dir: unit(sub(t, a)) };
}

module.exports = { anatomy, bristles, legGeometry, blobPts, blobIn, blobR, blobUV, blobLight, catmull, pathOf, inPoly, ribbon, taper, mulberry, add, sub, mul, len, unit, perp, lerp, lerp2, clamp, smooth, r1, mirrorPt, DEG, LIGHT };
