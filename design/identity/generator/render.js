'use strict';
// Renders the fly as contour line art (plate, engraving, new tab memory) or as the modelled colour icon.
const F = require('./anatomy.js');
const { anatomy, bristles, legGeometry, blobPts, blobIn, blobR, blobLight, catmull, pathOf, inPoly, ribbon, taper, mulberry, add, sub, mul, unit, perp, lerp, clamp, smooth, r1 } = F;

const SHADOW = unit([0.62, 0.78]);
function contour(pts, base, heavy) {
  const n = pts.length;
  const c = pts.reduce((a, p) => [a[0] + p[0] / n, a[1] + p[1] / n], [0, 0]);
  const ws = pts.map((p) => { const o = unit(sub(p, c)); return base + heavy * smooth(-0.2, 0.9, o[0] * SHADOW[0] + o[1] * SHADOW[1]); });
  return ribbon(pts.concat([pts[0]]), ws.concat([ws[0]]));
}
const dots = (list) => list.map(([x, y, r]) => `M${r1(x - r)},${r1(y)}a${r1(r)},${r1(r)} 0 1 0 ${r1(2 * r)},0a${r1(r)},${r1(r)} 0 1 0 ${r1(-2 * r)},0`).join('');

function geometry() {
  const A = anatomy();
  const rnd = mulberry(7);
  const hd = A.head;
  const fw = (y) => y < 250 ? 20 + 26 * Math.pow((y - 250) / 54, 2) : 20 + 14 * Math.pow((y - 250) / 90, 2);
  const headPts = blobPts(hd, 360);
  const eyes = [-1, 1].map((sd) => {
    const inside = (p) => sd * (p[0] - 500) > fw(p[1]);
    const k = headPts.findIndex((p) => !inside(p));
    const ring = headPts.slice(k).concat(headPts.slice(0, k));
    const a = ring.findIndex(inside);
    let b = a;
    while (b + 1 < ring.length && inside(ring[b + 1])) b++;
    const outer = ring.slice(a, b + 1);
    const y0 = outer[outer.length - 1][1], y1 = outer[0][1];
    const inner = [];
    for (let i = 1; i < 30; i++) { const y = lerp(y0, y1, i / 30); inner.push([500 + sd * fw(y), y]); }
    const poly = outer.concat(inner);
    const xs = poly.map((p) => p[0]), ys = poly.map((p) => p[1]);
    const bx = [Math.min(...xs), Math.max(...xs)], by = [Math.min(...ys), Math.max(...ys)];
    const E = { cx: (bx[0] + bx[1]) / 2, cy: (by[0] + by[1]) / 2, a: (bx[1] - bx[0]) / 2 * 1.05, bt: (by[1] - by[0]) / 2 * 1.05, bb: (by[1] - by[0]) / 2 * 1.05, n: 2, k: 0, rot: 0, dome: 1.3 };
    const gx = bx[0] + (bx[1] - bx[0]) * (sd < 0 ? 0.36 : 0.3), gy = by[0] + (by[1] - by[0]) * 0.3;
    const facets = [];
    const step = 9;
    for (let row = -14; row <= 14; row++) for (let col = -14; col <= 14; col++) {
      const x = E.cx + col * step + (row % 2 ? step / 2 : 0), y = E.cy + row * step * 0.866;
      if (!inPoly(poly, x, y)) continue;
      const lam = blobLight(E, x, y);
      const d = clamp((1 - lam) * 0.95 + 0.3);
      facets.push([x, y, lerp(1.2, 4.2, d)]);
    }
    return { sd, poly, E, gx, gy, facets };
  });
  const legs = A.legs.map((L, i) => ({ L, g: legGeometry(L) }));
  // A lighter set of bristles: only those that break the silhouette.
  const br = bristles(A, rnd).filter((b) => {
    const tip = add(b.p, mul(b.d, b.l));
    return ![A.thorax, A.abdomen, A.scutellum, A.head].some((B) => blobIn(B, tip[0], tip[1]));
  });
  return { A, eyes, legs, br, fw };
}

function legPaths(g, src) {
  const [wf, wt, wr] = src.w;
  const out = [taper(g.femur, wf * 0.9, wf * 0.75, wf * 1.08), taper(g.tibia, wt * 0.9, wt * 0.95, wt * 1.05)];
  const t = g.tarsus;
  for (let s = 0; s < 5; s++) {
    const a = Math.floor((s / 5) * (t.length - 1)), b = Math.floor(((s + 1) / 5) * (t.length - 1));
    const seg = t.slice(a, b + 1);
    if (seg.length > 1) { const w = lerp(wr, wr * 0.6, s / 5); out.push(taper(seg, w * 1.05, w * 0.8, w)); }
  }
  for (const k of [-1, 1]) out.push(taper(catmull([g.tip, add(g.tip, add(mul(g.dir, 10), mul(perp(g.dir), k * 6))), add(g.tip, add(mul(g.dir, 12), mul(perp(g.dir), k * 14)))], false, 6), 3.2, 0.6));
  for (const f of [0.4, 0.75]) {
    const i = Math.floor(f * (g.tibia.length - 1));
    const p = g.tibia[i], d = unit(sub(g.tibia[i + 1], p)), n = perp(d);
    const side = src.side;
    const b0 = add(p, mul(n, side * wt * 0.42));
    out.push(taper([b0, add(b0, add(mul(d, 10), mul(n, side * 6))), add(b0, add(mul(d, 16), mul(n, side * 12)))], 3, 0.4));
  }
  return out;
}

function tergites(A) {
  const abd = A.abdomen;
  return [-0.42, 0.1, 0.56].map((v) => {
    const pts = [];
    for (let i = 0; i <= 40; i++) {
      const u = -1 + (2 * i) / 40;
      const y = abd.cy + v * (v < 0 ? abd.bt : abd.bb) + 30 * (1 - u * u) - 8;
      const halfW = abd.a * Math.pow(1 - Math.pow(Math.abs(v), abd.n), 1 / abd.n) * (1 + abd.k * v);
      pts.push([abd.cx + u * halfW * 0.98, y]);
    }
    return pts.filter((p) => blobR(abd, p[0], p[1]) < 0.97);
  });
}

const antennae = () => {
  const out = [];
  for (const sd of [-1, 1]) {
    const X = (x) => 500 + sd * (500 - x);
    out.push(taper([[X(488), 204], [X(483), 186], [X(478), 172]], 11, 8, 12));
    out.push(taper(catmull([[X(477), 170], [X(468), 150], [X(452), 128], [X(436), 116]], false, 8), 3.4, 0.6));
  }
  return out;
};

const bristlePath = (b) => { const p0 = b.p, p1 = add(p0, mul(b.d, b.l * 0.55)), p2 = add(add(p0, mul(b.d, b.l)), mul(perp(b.d), b.l * 0.08)); return taper([p0, p1, p2], b.w, 0.5); };

// Line art
function line(o = {}) {
  o = Object.assign({ ink: '#151310', paper: '#ece6d8', eye: null, wash: 0.96, id: 'l' }, o);
  const G = geometry();
  const { A } = G;
  const s = [];
  s.push(`<defs><mask id="${o.id}lm" maskUnits="userSpaceOnUse" x="0" y="0" width="1000" height="1000"><rect width="1000" height="1000" fill="#fff"/>${A.wings.map((W) => `<path d="${pathOf(W.outline)}" fill="#000"/>`).join('')}</mask></defs>`);
  s.push(`<g fill="${o.ink}" mask="url(#${o.id}lm)">${G.legs.map(({ L, g }) => legPaths(g, L).map((d) => `<path d="${d}"/>`).join('')).join('')}</g>`);
  const part = (B, base, heavy) => { const p = blobPts(B); return `<path d="${pathOf(p)}" fill="${o.paper}"/><path d="${contour(p, base, heavy)}" fill="${o.ink}"/>`; };
  s.push(part(A.abdomen, 4.4, 7));
  s.push(`<g fill="${o.ink}">${tergites(A).map((l) => `<path d="${taper(l, 0.6, 0.6, 3)}"/>`).join('')}</g>`);
  for (const W of A.wings) {
    s.push(`<path d="${pathOf(W.outline)}" fill="${o.paper}" fill-opacity="${o.wash}"/>`);
    s.push(`<g fill="${o.ink}">${Object.entries(W.veins).map(([k, v]) => `<path d="${taper(v.pts, k === 'costa' ? 5 : v.w[0] * 0.85, v.w[1] * 0.7)}"/>`).join('')}<path d="${contour(W.outline, 1.8, 2.2)}"/></g>`);
  }
  s.push(part(A.thorax, 4.2, 6.5));
  s.push(part(A.scutellum, 3, 4.5));
  s.push(part(A.head, 4.2, 6.5));
  s.push(`<g fill="${o.ink}">${antennae().map((d) => `<path d="${d}"/>`).join('')}</g>`);
  for (const e of G.eyes) {
    s.push(`<path d="${pathOf(e.poly)}" fill="${o.eye || o.paper}"/><path d="${dots(e.facets)}" style="fill:var(--facet-ink,${o.ink});opacity:var(--facet,${o.eye ? 0.8 : 1})"/><path d="${contour(e.poly, 3.6, 5.5)}" fill="${o.ink}"/>`);
  }
  for (const [x, y] of [[500, 316], [491, 330], [509, 330]]) s.push(`<circle cx="${x}" cy="${y}" r="4" fill="${o.ink}"/>`);
  s.push(`<g fill="${o.ink}">${G.br.map((b) => `<path d="${bristlePath(b)}"/>`).join('')}</g>`);
  return { body: s.join(''), sil: silhouette(G) };
}

function silhouette(G) {
  const { A } = G;
  const s = [];
  for (const { L, g } of G.legs) s.push(`<path d="${taper(g.femur.concat(g.tibia, g.tarsus), L.w[0] + 6, L.w[2] + 6)}"/>`);
  for (const W of A.wings) s.push(`<path d="${pathOf(W.outline)}"/>`);
  for (const B of [A.abdomen, A.thorax, A.scutellum, A.head]) s.push(`<path d="${pathOf(blobPts(B))}"/>`);
  for (const d of antennae()) s.push(`<path d="${d}"/>`);
  for (const b of G.br) s.push(`<path d="${bristlePath(b)}"/>`);
  return `<g>${s.join('')}</g>`;
}

// Modelled colour for the desktop icon
function colour(o = {}) {
  o = Object.assign({ id: 'c' }, o);
  const G = geometry();
  const { A } = G;
  const defs = [], s = [];
  let n = 0;
  const INK = '#0d0d0e';
  const rg = (B, stops, fx = 0.3, fy = 0.26) => {
    const id = `${o.id}g${n++}`;
    const h = Math.max(B.bt, B.bb);
    defs.push(`<radialGradient id="${id}" gradientUnits="userSpaceOnUse" cx="${B.cx}" cy="${B.cy}" r="${B.a * 1.08}" fx="${B.cx - B.a * fx}" fy="${B.cy - h * fy}" gradientTransform="translate(${B.cx} ${B.cy}) scale(1 ${h / B.a}) translate(${-B.cx} ${-B.cy})">${stops.map(([of, c, op = 1]) => `<stop offset="${of}" stop-color="${c}" stop-opacity="${op}"/>`).join('')}</radialGradient>`);
    return `url(#${id})`;
  };
  const clip = (d) => { const id = `${o.id}k${n++}`; defs.push(`<clipPath id="${id}"><path d="${d}"/></clipPath>`); return `url(#${id})`; };
  defs.push(`<filter id="${o.id}blur" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="7"/></filter><filter id="${o.id}blur3" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="3"/></filter><filter id="${o.id}bloom" x="-60%" y="-60%" width="220%" height="220%"><feGaussianBlur stdDeviation="13"/></filter><filter id="${o.id}core" x="-60%" y="-60%" width="220%" height="220%"><feGaussianBlur stdDeviation="2"/></filter>`);
  defs.push(`<linearGradient id="${o.id}iri" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ffd6ec" stop-opacity=".05"/><stop offset=".4" stop-color="#d8ffee" stop-opacity=".3"/><stop offset=".7" stop-color="#d4e2ff" stop-opacity=".32"/><stop offset="1" stop-color="#f3dcff" stop-opacity=".14"/></linearGradient>`);
  defs.push(`<mask id="${o.id}lm" maskUnits="userSpaceOnUse" x="0" y="0" width="1000" height="1000"><rect width="1000" height="1000" fill="#fff"/>${A.wings.map((W) => `<path d="${pathOf(W.outline)}" fill="#000"/>`).join('')}</mask>`);

  s.push(`<g fill="${INK}" mask="url(#${o.id}lm)">${G.legs.map(({ L, g }) => legPaths(g, L).map((d) => `<path d="${d}"/>`).join('')).join('')}</g>`);
  for (const { L, g } of G.legs) for (const seg of [g.femur, g.tibia]) {
    const hl = seg.map((p, i) => add(p, mul(perp(unit(sub(seg[Math.min(seg.length - 1, i + 1)], seg[Math.max(0, i - 1)]))), -L.side * 4)));
    s.push(`<path d="${taper(hl.slice(2, -2), 0.4, 0.4, 2.6)}" fill="#8b929c" fill-opacity=".55" mask="url(#${o.id}lm)"/>`);
  }

  const abd = A.abdomen, abdD = pathOf(blobPts(abd));
  defs.push(`<mask id="${o.id}wm" maskUnits="userSpaceOnUse" x="-100" y="-100" width="1200" height="1200"><rect x="-100" y="-100" width="1200" height="1200" fill="#fff"/><path d="${abdD}" fill="#000"/></mask>`);
  s.push(`<path d="${abdD}" fill="${rg(abd, [[0, '#a4abb4'], [0.18, '#6a717b'], [0.5, '#30343b'], [0.86, '#131417'], [1, '#202328']])}"/>`);
  const ac = clip(abdD);
  for (const l of tergites(A)) {
    s.push(`<path d="${taper(l, 2, 2, 16)}" fill="#000" fill-opacity=".35" filter="url(#${o.id}blur)" clip-path="${ac}"/>`);
    s.push(`<path d="${taper(l, 0.6, 0.6, 2.6)}" fill="#000" fill-opacity=".6" clip-path="${ac}"/>`);
    s.push(`<path d="${taper(l.map((p) => [p[0], p[1] + 9]), 0.4, 0.4, 3)}" fill="#c9d0d8" fill-opacity=".22" clip-path="${ac}"/>`);
  }
  s.push(`<ellipse cx="${abd.cx - 52}" cy="${abd.cy - 40}" rx="56" ry="34" transform="rotate(-28 ${abd.cx - 52} ${abd.cy - 40})" fill="#fff" fill-opacity=".16" filter="url(#${o.id}blur)" clip-path="${ac}"/>`);
  s.push(`<path d="${contour(blobPts(abd), 3, 4.5)}" fill="${INK}"/>`);

  for (const W of A.wings) {
    const d = pathOf(W.outline);
    s.push(`<g mask="url(#${o.id}wm)"><path d="${d}" fill="#eef3f8" fill-opacity=".22"/><path d="${d}" fill="url(#${o.id}iri)" fill-opacity=".7"/></g><path d="${d}" fill="#05060a" fill-opacity=".16" clip-path="${ac}"/>`);
    s.push(`<g fill="#2a231d" fill-opacity=".9">${Object.entries(W.veins).map(([k, v]) => `<path d="${taper(v.pts, k === 'costa' ? 5 : v.w[0] * 0.85, v.w[1] * 0.7)}"/>`).join('')}</g><path d="${contour(W.outline, 1.8, 1.8)}" fill="${INK}" fill-opacity=".85"/>`);
  }

  const th = A.thorax, thD = pathOf(blobPts(th));
  s.push(`<path d="${thD}" fill="${rg(th, [[0, '#d2d5d8'], [0.16, '#9a9fa6'], [0.5, '#5b6068'], [0.85, '#2a2d32'], [1, '#383c42']])}"/>`);
  const tc = clip(thD);
  for (const [x, w] of [[-60, 22], [-20, 15], [20, 15], [60, 22]]) {
    const pts = catmull([[500 + x * 1.1, th.cy - th.bt * 0.98], [500 + x, th.cy - 20], [500 + x * 0.94, th.cy + th.bb * 0.6]], false, 10);
    s.push(`<path d="${taper(pts, w * 0.9, w * 0.7, w)}" fill="#141517" fill-opacity=".5" clip-path="${tc}" filter="url(#${o.id}blur3)"/>`);
  }
  s.push(`<ellipse cx="${th.cx - 48}" cy="${th.cy - 46}" rx="58" ry="34" transform="rotate(-24 ${th.cx - 48} ${th.cy - 46})" fill="#fff" fill-opacity=".22" filter="url(#${o.id}blur)" clip-path="${tc}"/>`);
  s.push(`<path d="${contour(blobPts(th), 3, 4.5)}" fill="${INK}"/>`);

  const scu = A.scutellum, scD = pathOf(blobPts(scu));
  s.push(`<path d="${scD}" fill="${rg(scu, [[0, '#8f959d'], [0.5, '#3d4148'], [1, '#17181b']])}"/><path d="${contour(blobPts(scu), 2, 3)}" fill="${INK}"/>`);

  const hd = A.head, hdD = pathOf(blobPts(hd));
  s.push(`<path d="${hdD}" fill="${rg(hd, [[0, '#5b5d61'], [0.5, '#2a2b2e'], [1, '#121214']])}"/>`);
  // frons with a silver sheen
  const fr = [];
  for (let i = 0; i <= 20; i++) { const y = lerp(186, 340, i / 20); fr.push([500 - G.fw(y), y]); }
  for (let i = 20; i >= 0; i--) { const y = lerp(186, 340, i / 20); fr.push([500 + G.fw(y), y]); }
  s.push(`<path d="${pathOf(fr)}" fill="#c4c2bb" fill-opacity=".28" clip-path="${clip(hdD)}"/>`);
  s.push(`<g fill="${INK}">${antennae().map((d) => `<path d="${d}"/>`).join('')}</g>`);
  for (const e of G.eyes) {
    const ed = pathOf(e.poly);
    s.push(`<path d="${ed}" fill="${rg(e.E, [[0, '#ff9c86'], [0.14, '#f2483a'], [0.45, '#c5231b'], [0.8, '#6c0e0e'], [1, '#3b0708']], e.sd < 0 ? 0.28 : 0.4, 0.3)}"/>`);
    s.push(`<path d="${dots(e.facets)}" fill="#2b0405" fill-opacity=".5"/>`);
    const rt = `rotate(${e.sd < 0 ? -35 : -25} ${r1(e.gx)} ${r1(e.gy)})`;
    // Bloom: a warm halo that barely spills past the eye, then a soft glow and a blurred core.
    s.push(`<ellipse cx="${r1(e.gx)}" cy="${r1(e.gy)}" rx="48" ry="32" transform="${rt}" fill="#ffe2d6" fill-opacity=".2" filter="url(#${o.id}bloom)"/>`);
    s.push(`<ellipse cx="${r1(e.gx)}" cy="${r1(e.gy)}" rx="25" ry="15" transform="${rt}" fill="#fff" fill-opacity=".38" filter="url(#${o.id}blur)"/>`);
    s.push(`<ellipse cx="${r1(e.gx)}" cy="${r1(e.gy)}" rx="11.5" ry="7" transform="${rt}" fill="#fff" fill-opacity=".9" filter="url(#${o.id}core)"/>`);
    s.push(`<path d="${contour(e.poly, 2.6, 3.6)}" fill="${INK}"/>`);
  }
  for (const [x, y] of [[500, 316], [491, 330], [509, 330]]) s.push(`<circle cx="${x}" cy="${y}" r="4" fill="#8a2a1f"/>`);
  s.push(`<path d="${contour(blobPts(hd), 2.6, 4)}" fill="${INK}"/>`);
  s.push(`<g fill="${INK}">${G.br.map((b) => `<path d="${bristlePath(b)}"/>`).join('')}</g>`);
  return { defs: defs.join(''), body: s.join(''), sil: silhouette(G) };
}

// Crisp die cut: hard edge, a thin cut line, a shadow that hugs the sticker.
const STICKER_FILTER = (id) => `<filter id="${id}" x="-12%" y="-12%" width="124%" height="128%" color-interpolation-filters="sRGB">
  <feMorphology in="SourceAlpha" operator="dilate" radius="28" result="d"/>
  <feGaussianBlur in="d" stdDeviation="14" result="b"/>
  <feComponentTransfer in="b" result="cut"><feFuncA type="linear" slope="40" intercept="-19"/></feComponentTransfer>
  <feGaussianBlur in="cut" stdDeviation="5" result="sb"/><feOffset in="sb" dx="2" dy="7" result="so"/>
  <feFlood flood-color="#000" flood-opacity=".42"/><feComposite in2="so" operator="in" result="shadow"/>
  <feFlood flood-color="#fbfaf6"/><feComposite in2="cut" operator="in" result="vinyl"/>
  <feMorphology in="cut" operator="erode" radius="2.2" result="inner"/>
  <feComposite in="cut" in2="inner" operator="out" result="edge"/>
  <feFlood flood-color="#bdb8ad"/><feComposite in2="edge" operator="in" result="edgec"/>
  <feMerge><feMergeNode in="shadow"/><feMergeNode in="vinyl"/><feMergeNode in="edgec"/></feMerge>
</filter>`;

function eyesOnly(o = {}) {
  const G = geometry();
  return G.eyes.map((e) => `<path d="${pathOf(e.poly)}" fill="${o.eye || '#e0261c'}"/>`).join('');
}

module.exports = { line, colour, eyesOnly, STICKER_FILTER };
