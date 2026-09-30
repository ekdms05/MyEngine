#!/usr/bin/env node
// extract_characters.mjs — 사용자 제공 모험가 시트(남/여)에서 게임용 스프라이트 추출.
//
// 입력: assets/sprites/2.png(남), 3.png(여) — 픽셀풍 스프라이트시트(배경 채워짐).
// 처리: 행 밴드 → 열 클러스터(밸리 분할)로 프레임 셀 검출 → 셀 경계 플러드필로 배경 키잉
//       (내부 밝은 옷색 보존) → 최대 셀(온몸 프레임) 트림 → 최근접 리샘플 h=128.
// 출력: samples/mmo_demo/assets/sprites/adventurer_m.png / adventurer_f.png
// 실행: node tools/extract_characters.mjs   (pngjs 는 tools/mcp/node_modules 사용)
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
const require2 = createRequire(import.meta.url);
const { PNG } = require2('./mcp/node_modules/pngjs');

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const px = (d, W, x, y) => { const i = (y * W + x) * 4; return [d[i], d[i + 1], d[i + 2], d[i + 3]]; };
const dist = (a, b) => Math.max(Math.abs(a[0] - b[0]), Math.abs(a[1] - b[1]), Math.abs(a[2] - b[2]));

function load(p) {
  const png = PNG.sync.read(fs.readFileSync(p));
  return { d: png.data, W: png.width, H: png.height };
}

// 배경 추정: 테두리 픽셀 최빈값(중앙값 근사).
function bgOf(img) {
  const { d, W, H } = img;
  const samples = [];
  for (let x = 0; x < W; x += 7) { samples.push(px(d, W, x, 2)); samples.push(px(d, W, x, H - 3)); }
  for (let y = 0; y < H; y += 7) { samples.push(px(d, W, 2, y)); samples.push(px(d, W, W - 3, y)); }
  samples.sort((a, b) => (a[0] + a[1] + a[2]) - (b[0] + b[1] + b[2]));
  return samples[samples.length >> 1];
}

// 열/행 콘텐츠 카운트(배경과 다른 픽셀 수).
function projections(img, bg, x0, x1, y0, y1) {
  const cols = new Array(x1 - x0).fill(0), rows = new Array(y1 - y0).fill(0);
  for (let y = y0; y < y1; y += 2)
    for (let x = x0; x < x1; x += 2)
      if (dist(px(img.d, img.W, x, y), bg) > 55) { cols[x - x0]++; rows[y - y0]++; }
  return { cols, rows };
}

// 카운트 배열에서 연속 "빈" 구간(≥gap)을 경계로 한 콘텐츠 밴드 목록.
function bands(counts, gap, minSize) {
  const out = [];
  let run = -1;
  for (let i = 0; i <= counts.length; i++) {
    const empty = i < counts.length ? counts[i] === 0 : true;
    if (empty && run < 0) run = i;
    if (!empty && run >= 0) {
      if (out.length && i - run >= gap) out[out.length - 1].max = run; else if (!out.length) {}
      run = -1;
    }
  }
  // 단순 재계산: 빈 구간 목록 → 그 사이 콘텐츠 구간.
  const empties = [];
  let s = -1;
  for (let i = 0; i <= counts.length; i++) {
    const empty = i < counts.length ? counts[i] === 0 : true;
    if (empty && s < 0) s = i;
    if (!empty && s >= 0) { empties.push([s, i - 1]); s = -1; }
  }
  if (s >= 0) empties.push([s, counts.length - 1]);
  const cuts = [0];
  for (const [a, b] of empties) if (b - a + 1 >= gap) cuts.push(Math.floor((a + b) / 2));
  cuts.push(counts.length);
  for (let i = 0; i + 1 < cuts.length; i++) {
    const a = cuts[i], b = cuts[i + 1];
    if (b - a >= minSize) out.push({ min: a, max: b - 1 });
  }
  return out;
}

// 밸리(열 투영의 국소 최소) 재귀 분할 — 셀이 서로 붙어있을 때.
function splitValleys(cols, x0, x1, maxW) {
  const segs = [];
  const rec = (a, b) => {
    if (b - a <= maxW) { segs.push([a, b]); return; }
    // 구간 내 최대/최소 탐색(스무딩 후).
    const sm = [];
    for (let x = a; x <= b; x++) {
      let v = 0, n = 0;
      for (let k = -6; k <= 6; k++) { const xx = x + k; if (xx >= a && xx <= b) { v += cols[xx]; n++; } }
      sm.push(v / Math.max(1, n));
    }
    const mx = Math.max(...sm);
    let best = -1, bv = 1e9;
    for (let i = 8; i < sm.length - 8; i++)
      if (sm[i] < bv && sm[i] < mx * 0.16) { bv = sm[i]; best = i; }
    if (best < 0) { segs.push([a, b]); return; }
    const cut = a + best;
    rec(a, cut); rec(cut + 1, b);
  };
  rec(x0, x1);
  return segs;
}

// 셀 내 콘텐츠 bbox.
function cellBBox(img, bg, x0, x1, y0, y1) {
  let minx = 1e9, maxx = -1, miny = 1e9, maxy = -1;
  for (let y = y0; y < y1; y++)
    for (let x = x0; x < x1; x++)
      if (dist(px(img.d, img.W, x, y), bg) > 55) {
        if (x < minx) minx = x; if (x > maxx) maxx = x;
        if (y < miny) miny = y; if (y > maxy) maxy = y;
      }
  return maxx < 0 ? null : { x: minx, y: miny, w: maxx - minx + 1, h: maxy - miny + 1 };
}

// 셀 잘라내기 + 경계 플러드필 키잕(연결된 배경만 제거 → 옷과 같은색 보존).
function extractCell(img, bg, cell) {
  const { x, y, w, h } = cell;
  const out = new PNG({ width: w, height: h });
  for (let yy = 0; yy < h; yy++)
    out.data.set(img.d.subarray(((y + yy) * img.W + x) * 4, ((y + yy) * img.W + x + w) * 4), yy * w * 4);
  const seen = new Uint8Array(w * h);
  const stack = [];
  for (let xx = 0; xx < w; xx++) { stack.push(xx, 0, xx, h - 1); }
  for (let yy = 0; yy < h; yy++) { stack.push(0, yy, w - 1, yy); }
  while (stack.length) {
    const sy = stack.pop(), sx = stack.pop();
    const i = sy * w + sx;
    if (seen[i]) continue;
    seen[i] = 1;
    const p = px(out.data, w, sx, sy);
    if (dist(p, bg) > 46) continue;
    out.data[i * 4 + 3] = 0;
    if (sx > 0) stack.push(sx - 1, sy);
    if (sx + 1 < w) stack.push(sx + 1, sy);
    if (sy > 0) stack.push(sx, sy - 1);
    if (sy + 1 < h) stack.push(sx, sy + 1);
  }
  return out;
}

// 투명 아닌 픽셀 bbox 로 트림.
function trim(png) {
  const { width: w, height: h, data } = png;
  let minx = w, maxx = -1, miny = h, maxy = -1;
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++)
      if (data[(y * w + x) * 4 + 3] > 16) {
        if (x < minx) minx = x; if (x > maxx) maxx = x;
        if (y < miny) miny = y; if (y > maxy) maxy = y;
      }
  if (maxx < 0) return png;
  const tw = maxx - minx + 1, th = maxy - miny + 1;
  const out = new PNG({ width: tw, height: th });
  for (let y = 0; y < th; y++)
    out.data.set(data.subarray(((y + miny) * w + minx) * 4, ((y + miny) * w + minx + tw) * 4), y * tw * 4);
  return out;
}

// 최근접 리샘플(픽셀아트 보존).
function resizeNN(png, tw, th) {
  const out = new PNG({ width: tw, height: th });
  const { width: sw, height: sh, data } = png;
  for (let y = 0; y < th; y++)
    for (let x = 0; x < tw; x++) {
      const sx = Math.min(sw - 1, Math.floor((x * sw) / tw));
      const sy = Math.min(sh - 1, Math.floor((y * sh) / th));
      out.data.set(data.subarray((sy * sw + sx) * 4, (sy * sw + sx) * 4 + 4), (y * tw + x) * 4);
    }
  return out;
}

function process(src, dst, label) {
  const img = load(src);
  const bg = bgOf(img);
  const { rows } = projections(img, bg, 0, img.W, 0, img.H);
  const rowBands = bands(rows, 10, 60);
  console.log(`${label}: ${img.W}x${img.H} bg=${bg.join(',')} 행밴드=${JSON.stringify(rowBands)}`);
  const cells = [];
  for (const rb of rowBands) {
    const y0 = rb.min, y1 = rb.max + 1;
    const { cols } = projections(img, bg, 0, img.W, y0, y1);
    const colBands = bands(cols, 10, 60);
    let segs = colBands.map(c => [c.min, c.max]);
    if (segs.length <= 1 && img.W > 700) segs = splitValleys(cols, 0, img.W - 1, 560);
    for (const [a, b] of segs) {
      const bb = cellBBox(img, bg, Math.max(0, a - 4), Math.min(img.W, b + 5), Math.max(0, y0 - 4), Math.min(img.H, y1 + 4));
      if (bb && bb.w > 60 && bb.h > 100) cells.push(bb);
    }
  }
  cells.sort((p, q) => (q.w * q.h) - (p.w * p.h));
  console.log(`${label}: 셀 ${cells.length}개 — ${cells.slice(0, 6).map(c => `${c.w}x${c.h}@${c.x},${c.y}`).join(' | ')}`);
  const best = cells[0];
  let png = extractCell(img, bg, best);
  png = trim(png);
  const TARGET_H = 128;
  const scale = TARGET_H / png.height;
  const tw = Math.max(8, Math.round(png.width * scale));
  png = resizeNN(png, tw, TARGET_H);
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  fs.writeFileSync(dst, PNG.sync.write(png));
  console.log(`${label}: 저장 ${dst} (${png.width}x${png.height}) ← 셀 ${best.w}x${best.h}@${best.x},${best.y}`);
}

process(path.join(ROOT, 'assets/sprites/2.png'), path.join(ROOT, 'samples/mmo_demo/assets/sprites/adventurer_m.png'), '남(2.png)');
process(path.join(ROOT, 'assets/sprites/3.png'), path.join(ROOT, 'samples/mmo_demo/assets/sprites/adventurer_f.png'), '여(3.png)');
