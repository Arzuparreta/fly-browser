'use strict';
// Exports the Fly identity masters into ../assets.
// SVG output needs only Node. PNG output also needs Playwright with a Chromium build
// (for example `npm i -g playwright`); without it the PNG step is skipped.
const fs = require('fs');
const path = require('path');
const R = require('./render.js');

const OUT = path.join(__dirname, '..', 'assets');
const PNG = path.join(OUT, 'png');
const ROT = 'rotate(13 500 520)';

const THEMES = {
  chasis: { ink: '#e7e1d3', inkOpacity: 0.46, eyeOpacity: 0.75, lit: ['#fff', 0.17], cut: ['#000', 0.72] },
  papel: { ink: '#1a1814', inkOpacity: 0.4, eyeOpacity: 0.7, lit: ['#fffaf0', 0.95], cut: ['#6b5b40', 0.5] }
};

// Line art with every colour behind a CSS custom property, so one file serves both themes.
function themeableLine() {
  const l = R.line({ ink: 'INK', paper: 'PAPER', eye: 'EYE', id: 'fl' });
  const body = l.body
    .replace(/fill="INK"/g, 'style="fill:var(--ink)"')
    .replace(/fill="PAPER" fill-opacity="([\d.]+)"/g, 'style="fill:var(--paper);fill-opacity:var(--wash,$1)"')
    .replace(/fill="PAPER"/g, 'style="fill:var(--paper)"')
    .replace(/fill="EYE"/g, 'style="fill:var(--eye,var(--paper))"')
    .replace(/var\(--facet-ink,INK\)/g, 'var(--facet-ink,var(--ink))');
  if (/INK|PAPER|EYE/.test(body)) throw new Error('unreplaced colour token');
  return `<g transform="${ROT}">${body}</g>`;
}

function eyes() {
  return `<g transform="${ROT}">${R.eyesOnly()}</g>`;
}

const svgDoc = (inner, viewBox = '0 0 1000 1000', style = '') =>
  `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}">${style ? `<style>${style}</style>` : ''}${inner}</svg>\n`;

function lineSvg() {
  return svgDoc(themeableLine(), '0 0 1000 1000', 'svg{--ink:#151310;--paper:#ece6d8;--eye:#e0261c;--facet:.8;--wash:.96}');
}

function iconSvg() {
  const c = R.colour({ id: 'fi' });
  return svgDoc(`<defs>${c.defs}${R.STICKER_FILTER('cut')}</defs><g transform="${ROT}"><g filter="url(#cut)">${c.sil}</g>${c.body}</g>`, '-40 -40 1080 1080');
}

// Engraving: the line art cut into the surface. Light catches the lower edge, shadow sits in the cut.
function engravingSvg(theme) {
  const t = THEMES[theme];
  const filter = `<filter id="eg" x="-5%" y="-5%" width="110%" height="110%" color-interpolation-filters="sRGB">
    <feColorMatrix in="SourceGraphic" type="matrix" values="0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  -1 0 0 1 0" result="a"/>
    <feFlood flood-color="${t.cut[0]}" flood-opacity="${t.cut[1]}"/><feComposite in2="a" operator="in" result="cut"/>
    <feOffset in="a" dx="5" dy="5" result="ao"/><feFlood flood-color="${t.lit[0]}" flood-opacity="${t.lit[1]}"/><feComposite in2="ao" operator="in" result="lit"/>
    <feMerge><feMergeNode in="lit"/><feMergeNode in="cut"/></feMerge></filter>`;
  return svgDoc(`<defs>${filter}</defs><g filter="url(#eg)">${themeableLine()}</g>`, '0 0 1000 1000',
    'svg{--ink:#000;--paper:#fff;--eye:#fff;--facet:1;--facet-ink:#000;--wash:1}');
}

// New tab memory: only the lines, fading towards the tail, with the eyes as the one colour.
function memorySvg(theme) {
  const t = THEMES[theme];
  return svgDoc(`<defs>
    <mask id="m" maskUnits="userSpaceOnUse" x="0" y="0" width="1000" height="1000"><g style="--ink:#fff;--paper:#000;--eye:#000;--facet:1;--facet-ink:#fff;--wash:1">${themeableLine()}</g></mask>
    <linearGradient id="f" x1="0" y1="0" x2="0" y2="1"><stop offset=".22" stop-color="#fff"/><stop offset=".78" stop-color="#fff" stop-opacity=".12"/></linearGradient>
    <mask id="fm" maskUnits="userSpaceOnUse" x="0" y="0" width="1000" height="1000"><rect width="1000" height="1000" fill="url(#f)"/></mask>
  </defs><g mask="url(#fm)"><g opacity="${t.eyeOpacity}">${eyes()}</g><rect width="1000" height="1000" fill="${t.ink}" fill-opacity="${t.inkOpacity}" mask="url(#m)"/></g>`);
}

async function rasterise(jobs) {
  let chromium;
  try { ({ chromium } = require('playwright')); } catch { console.log('Playwright not found: PNG export skipped.'); return; }
  const browser = await chromium.launch();
  const page = await browser.newPage();
  fs.mkdirSync(PNG, { recursive: true });
  for (const job of jobs) {
    await page.setViewportSize({ width: job.size, height: job.size });
    await page.setContent(`<!doctype html><html><body style="margin:0;background:transparent"><img src="data:image/svg+xml;base64,${Buffer.from(job.svg).toString('base64')}" width="${job.size}" height="${job.size}" style="display:block"></body></html>`);
    await page.waitForFunction(() => document.images[0] && document.images[0].complete);
    await page.screenshot({ path: path.join(PNG, job.name), omitBackground: true, clip: { x: 0, y: 0, width: job.size, height: job.size } });
    console.log('png', job.name);
  }
  await browser.close();
}

async function main() {
  fs.mkdirSync(OUT, { recursive: true });
  const files = {
    'fly-line.svg': lineSvg(),
    'fly-icon.svg': iconSvg(),
    'fly-engraving-chasis.svg': engravingSvg('chasis'),
    'fly-engraving-papel.svg': engravingSvg('papel'),
    'fly-memory-chasis.svg': memorySvg('chasis'),
    'fly-memory-papel.svg': memorySvg('papel')
  };
  for (const [name, svg] of Object.entries(files)) {
    fs.writeFileSync(path.join(OUT, name), svg);
    console.log('svg', name, Math.round(svg.length / 1024) + ' KB');
  }
  const jobs = [1024, 512, 256, 128, 64, 48].map((size) => ({ name: `icon-${size}.png`, size, svg: files['fly-icon.svg'] }));
  for (const theme of Object.keys(THEMES)) {
    jobs.push({ name: `engraving-${theme}@2x.png`, size: 512, svg: files[`fly-engraving-${theme}.svg`] });
    jobs.push({ name: `memory-${theme}@2x.png`, size: 1200, svg: files[`fly-memory-${theme}.svg`] });
  }
  await rasterise(jobs);
}

main().catch((err) => { console.error(err); process.exit(1); });
