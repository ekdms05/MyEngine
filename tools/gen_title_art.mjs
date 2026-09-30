#!/usr/bin/env node
// gen_title_art.mjs — 타이틀 화면 픽셀 아트 생성(기차 풍경).
//
// 논리 픽셀 캔버스에 그린 뒤 4배 최근접 업스케일 → 도트 질감 통일.
//   title_bg.png   1920×540 — 수평 타일링 가능한 노을 풍경(하늘·별·구름·산·언덕·들판·철길).
//                  기차는 없음(별도 스프라이트). 레일 상면 = 논리 y 117 → 실제 468px.
//   title_train.png 440×176 — 증기 기관차(우향)+탠더+객차, 투명 배경, 바퀴 바닥=캔버스 하단.
//   smoke.png      32×32 — 연기 펍(투명 배경).
//   title_fg.png   960×96 — 전경 풀/수풀 실루엣 스트립(타일링, 기차 앞에 빠르게 스크롤).
// 실행: node tools/gen_title_art.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
const require2 = createRequire(import.meta.url);
const { PNG } = require2('./mcp/node_modules/pngjs');

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const OUT = path.join(ROOT, 'samples/mmo_demo/assets/sprites');
const SCALE = 4;

// 결정론 PRNG(시드 고정 — 재생산 가능).
function mulberry32(seed) {
  return function () {
    seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
const rng = mulberry32(20260825);

// ---- 픽셀 캔버스(논리 좌표) ----
class Canvas {
  constructor(w, h) { this.w = w; this.h = h; this.buf = new Array(w * h).fill(null); }
  set(x, y, c) {
    x = Math.round(x); y = Math.round(y);
    if (x < 0 || y < 0 || x >= this.w || y >= this.h || !c) return;
    this.buf[y * this.w + x] = c;
  }
  get(x, y) {
    x = Math.round(x); y = Math.round(y);
    return (x < 0 || y < 0 || x >= this.w || y >= this.h) ? null : this.buf[y * this.w + x];
  }
  rect(x, y, w, h, c) {
    for (let yy = y; yy < y + h; yy++) for (let xx = x; xx < x + w; xx++) this.set(xx, yy, c);
  }
  // x축 랩 그리기(타일링 — 오른쪽 경계 걸침 요소를 왼쪽에도 스탬프).
  wrapRect(x, y, w, h, c, W) {
    this.rect(x, y, w, h, c);
    if (x + w > W) this.rect(x - W, y, w, h, c);
    if (x < 0) this.rect(x + W, y, w, h, c);
  }
  circle(cx, cy, r, c) {
    for (let y = -r; y <= r; y++)
      for (let x = -r; x <= r; x++)
        if (x * x + y * y <= r * r + r * 0.4) this.set(cx + x, cy + y, c);
  }
  ring(cx, cy, r, c) {
    for (let a = 0; a < 360; a += 6) this.set(cx + Math.cos(a * Math.PI / 180) * r, cy + Math.sin(a * Math.PI / 180) * r, c);
  }
  // 50% 체커 디더 밴드(y0..y1 두 색 혼합).
  dither(y0, y1, cA, cB, W) {
    for (let y = y0; y < y1; y++)
      for (let x = 0; x < W; x++)
        this.set(x, y, ((x + y) & 1) ? cB : cA);
  }
  toPng(scale) {
    const png = new PNG({ width: this.w * scale, height: this.h * scale });
    for (let y = 0; y < this.h; y++)
      for (let x = 0; x < this.w; x++) {
        const c = this.buf[y * this.w + x] || [0, 0, 0, 0];
        for (let dy = 0; dy < scale; dy++)
          for (let dx = 0; dx < scale; dx++) {
            const i = ((y * scale + dy) * png.width + x * scale + dx) * 4;
            png.data[i] = c[0]; png.data[i + 1] = c[1]; png.data[i + 2] = c[2]; png.data[i + 3] = c[3];
          }
      }
    return png;
  }
}
const rgb = (hex, a = 255) => [(hex >> 16) & 255, (hex >> 8) & 255, hex & 255, a];

function save(name, png) {
  fs.mkdirSync(OUT, { recursive: true });
  fs.writeFileSync(path.join(OUT, name), PNG.sync.write(png));
  console.log(`저장 ${name} ${png.width}x${png.height}`);
}

// ============================================================================
// title_bg.png — 논리 480×135 (실제 1920×540), 수평 타일링.
// ============================================================================
{
  const W = 480, H = 135;
  const cv = new Canvas(W, H);

  // --- 하늘: 밴드 그라디언트 + 디더 전환 + 별 ---
  const skyBands = [
    [0, 0x141628], [10, 0x1c2040], [20, 0x272c52], [32, 0x353262],
    [44, 0x4c3a6e], [56, 0x6b4478], [66, 0x8a4e76], [74, 0xa85a6e],
    [80, 0xc76a5e], [84, 0xe08a58], [88, 0xf2aa62],
  ];
  for (let i = 0; i + 1 < skyBands.length; i++) {
    const [y0, cA] = skyBands[i], [y1, cB] = skyBands[i + 1];
    cv.rect(0, y0, W, y1 - y0, rgb(cA));
    if (y1 - y0 >= 4) cv.dither(y1 - 2, y1, rgb(cA), rgb(cB), W);   // 밴드 경계 부드럽게
  }
  cv.rect(0, 88, W, 2, rgb(0xf2aa62));

  // 별(상단 45px, 랩 처리).
  for (let i = 0; i < 90; i++) {
    const x = Math.floor(rng() * W), y = Math.floor(rng() * 44);
    const c = rng() < 0.25 ? 0xfff4c8 : (rng() < 0.5 ? 0xcdd3f0 : 0x9aa4d8);
    cv.wrapRect(x, y, 1, 1, rgb(c), W);
    if (rng() < 0.12) { cv.wrapRect(x - 1, y, 1, 1, rgb(0x6a74a8), W); cv.wrapRect(x + 1, y, 1, 1, rgb(0x6a74a8), W); }
  }

  // 석양(지평선에 걸친 반원 + 가로 새겨진 슬릿).
  const sunX = 352, sunR = 13, sunY = 88;
  cv.circle(sunX, sunY, sunR, rgb(0xffe9a8));
  cv.circle(sunX, sunY, sunR - 5, rgb(0xfff6d0));
  cv.wrapRect(sunX - sunR - 2, sunY - 6, sunR * 2 + 4, 1, rgb(0xa85a6e), W);   // 슬릿1
  cv.wrapRect(sunX - sunR - 1, sunY - 2, sunR * 2 + 2, 1, rgb(0xc76a5e), W);   // 슬릿2
  for (let dx = -18; dx <= 18; dx += 2) if (Math.abs(dx) > sunR)
    cv.wrapRect(sunX + dx, sunY - 1, 1, 1, rgb(0xf2b06a), W);                  // 수면 반짝(들판 글린트 흉내)

  // 구름(노을빛 아래배 불리고 위 밝음, 랩).
  const cloud = (x, y, w) => {
    const top = 0xd9a084, body = 0xb07a86;
    cv.wrapRect(x, y + 1, w, 2, rgb(body), W);
    cv.wrapRect(x + 2, y, w - 4, 1, rgb(top), W);
    cv.wrapRect(x + Math.floor(w / 3), y - 1, Math.floor(w / 3), 1, rgb(top), W);
    cv.wrapRect(x + Math.floor(w / 2), y - 2, 4, 1, rgb(0xf2c890), W);
  };
  cloud(30, 18, 34); cloud(140, 8, 24); cloud(238, 30, 40); cloud(330, 14, 20); cloud(410, 26, 30);

  // 새(v자).
  for (const [bx, by] of [[100, 40], [108, 42], [260, 34], [266, 36], [420, 46]]) {
    cv.wrapRect(bx - 2, by, 2, 1, rgb(0x2a2440), W);
    cv.wrapRect(bx + 1, by, 2, 1, rgb(0x2a2440), W);
    cv.wrapRect(bx, by - 1, 1, 1, rgb(0x2a2440), W);
  }

  // --- 원산(주기 함수 = 완벽 타일링) ---
  const ridge = (base, amp1, k1, p1, amp2, k2, p2, color) => {
    for (let x = 0; x < W; x++) {
      const y = Math.round(base + amp1 * Math.sin((2 * Math.PI * k1 * x) / W + p1)
                                + amp2 * Math.sin((2 * Math.PI * k2 * x) / W + p2));
      for (let yy = y; yy < 100; yy++) cv.set(x, yy, rgb(color));
      // 눈 캡(봉우리 상단).
      if (cv.get(x, y) && cv.get(x, y - 1) === null) {
        cv.set(x, y, rgb(0x8a7fae)); cv.set(x, y + 1, rgb(0x6a5f92));
      }
    }
  };
  ridge(70, 9, 3, 0.4, 4, 7, 1.7, 0x2e2a52);      // 원산(멀리, 어둡고 푸른 보라)
  ridge(80, 7, 4, 2.9, 3, 9, 0.6, 0x3d3560);      // 근산

  // --- 구릉지(나무 그늘) ---
  for (let x = 0; x < W; x++) {
    const y = Math.round(96 + 3 * Math.sin((2 * Math.PI * 5 * x) / W + 1.2)
                            + 2 * Math.sin((2 * Math.PI * 9 * x) / W + 4.0));
    for (let yy = y; yy < 120; yy++) cv.set(x, yy, rgb(yy < y + 3 ? 0x3a6a52 : 0x2f5148));
  }
  // 나무 군락(삼각 실루엣, 랩).
  const tree = (x, baseY, s) => {
    for (let i = 0; i < s; i++) cv.wrapRect(x - Math.floor(i / 2), baseY - i, Math.max(1, i + 1), 1, rgb(0x1e3a34), W);
    cv.wrapRect(x, baseY, 1, 1, rgb(0x16281f), W);
  };
  const treeXs = [];
  for (let i = 0; i < 26; i++) treeXs.push(Math.floor(rng() * W));
  for (const tx of treeXs) tree(tx, 97 + Math.floor(rng() * 4), 3 + Math.floor(rng() * 4));

  // --- 들판 ---
  for (let x = 0; x < W; x++) {
    const y = 104;
    for (let yy = y; yy < 118; yy++)
      cv.set(x, yy, rgb(yy < 106 ? 0x4a7a4e : (yy < 110 ? 0x427046 : 0x3c6342)));
  }
  cv.dither(106, 108, rgb(0x4a7a4e), rgb(0x427046), W);
  cv.dither(110, 112, rgb(0x427046), rgb(0x3c6342), W);
  // 풀 포기.
  for (let i = 0; i < 120; i++) {
    const x = Math.floor(rng() * W), y = 105 + Math.floor(rng() * 11);
    cv.wrapRect(x, y, 1, 2, rgb(0x2f5a3a), W);
  }
  // 울타리(기둥+가로줄, 주기 24 = 480 약수 → 타일링).
  for (let x = 0; x < W; x += 24) {
    cv.wrapRect(x, 107, 1, 5, rgb(0x5a4030), W);
    cv.wrapRect(x, 108, 1, 1, rgb(0x7a5a40), W);
  }
  cv.rect(0, 108, W, 1, rgb(0x4e3828));
  cv.rect(0, 110, W, 1, rgb(0x4e3828));

  // --- 철길(자갈 도상 + 침목 + 레일 2본) ---
  cv.rect(0, 114, W, 21, rgb(0x4e463f));
  cv.dither(112, 114, rgb(0x3c6342), rgb(0x4e463f), W);
  for (let i = 0; i < 700; i++) {   // 자갈 노이즈.
    const x = Math.floor(rng() * W), y = 115 + Math.floor(rng() * 20);
    cv.set(x, y, rgb(rng() < 0.5 ? 0x5a5148 : 0x63605c));
  }
  for (let x = 0; x < W; x += 16) {   // 침목(주기 16 → 타일링).
    cv.wrapRect(x, 119, 10, 7, rgb(0x4a3626), W);
    cv.wrapRect(x, 119, 10, 1, rgb(0x5f4832), W);
  }
  // 레일: 상면 y 116..118(바퀴 접지), 하면 y 129..130.
  cv.rect(0, 116, W, 1, rgb(0xb8bdc4));
  cv.rect(0, 117, W, 2, rgb(0x8a8f96));
  cv.rect(0, 129, W, 1, rgb(0x9aa0a8));
  cv.rect(0, 130, W, 1, rgb(0x6e747c));

  save('title_bg.png', cv.toPng(SCALE));
  console.log(`  레일 상면: 논리 y=116 → 실제 ${116 * SCALE}px → 월드 y=${(270 - 116 * SCALE) / 48}`);
}

// ============================================================================
// title_train.png — 논리 110×44 (실제 440×176), 우향 증기 기관차+탠더+객차.
// ============================================================================
{
  const W = 110, H = 44;
  const cv = new Canvas(W, H);
  const K = (hex, a) => rgb(hex, a);

  // ---- 객차(왼쪽, x 0..34) ----
  cv.rect(0, 14, 34, 20, K(0x6a4a34));          // 몸체
  cv.rect(0, 14, 34, 1, K(0x7d5a40));           // 상단 하이라이트
  cv.rect(-1, 11, 36, 3, K(0x3a2c26));          // 지붕(처마 돌출)
  cv.rect(0, 12, 34, 1, K(0x54413a));
  for (const wx of [4, 13, 22]) {               // 창 3개(따뜻한 실내광).
    cv.rect(wx, 18, 6, 7, K(0x2a2018));
    cv.rect(wx + 1, 19, 4, 5, K(0xffd98a));
    cv.rect(wx + 1, 21, 4, 1, K(0xffeab0));
  }
  cv.rect(0, 30, 34, 2, K(0x33261e));           // 하부 프레임
  for (const cx of [6, 26]) {                   // 바퀴 r=4.
    cv.circle(cx, 37, 4, K(0x14141a));
    cv.ring(cx, 37, 4, K(0x3a3a44));
    cv.set(cx, 37, K(0x8a8f96)); cv.set(cx + 1, 37, K(0x8a8f96)); cv.set(cx - 1, 37, K(0x8a8f96));
    cv.set(cx, 37 - 1, K(0x5a5f66)); cv.set(cx, 37 + 1, K(0x5a5f66));
  }

  // 연결기(객차↔탠더).
  cv.rect(34, 31, 3, 1, K(0x1a1a1f));

  // ---- 탠더(석탄차, x 36..58) ----
  cv.rect(36, 16, 22, 18, K(0x51322e));         // 측면
  cv.rect(36, 16, 22, 1, K(0x6a4640));
  cv.rect(35, 13, 24, 3, K(0x2e2026));          // 석탄 트레이
  for (let i = 0; i < 10; i++)                   // 석탄 덩어리.
    cv.rect(37 + Math.floor(rng() * 20), 12 - Math.floor(rng() * 2), 2, 2, K(0x15151a));
  cv.rect(36, 30, 22, 2, K(0x2a1c20));
  for (const cx of [41, 53]) {
    cv.circle(cx, 37, 4, K(0x14141a));
    cv.ring(cx, 37, 4, K(0x3a3a44));
    cv.set(cx, 37, K(0x8a8f96)); cv.set(cx - 1, 37, K(0x8a8f96)); cv.set(cx + 1, 37, K(0x8a8f96));
  }
  cv.rect(58, 31, 3, 1, K(0x1a1a1f));           // 연결기(탠더↔기관차).

  // ---- 기관차(x 60..110, 우향: 굴뚝·선두가 오른쪽) ----
  // 운전실(왼쪽 끝).
  cv.rect(60, 8, 20, 26, K(0x7a2f2f));          // 붉은 운전실 벽
  cv.rect(60, 8, 20, 1, K(0x964540));
  cv.rect(58, 5, 24, 3, K(0x3a2222));           // 지붕
  cv.rect(59, 6, 22, 1, K(0x54302c));
  cv.rect(64, 12, 10, 9, K(0x241a16));          // 운전실 창(따뜻한 조명).
  cv.rect(65, 13, 8, 7, K(0xffd98a));
  cv.rect(65, 15, 8, 2, K(0xffeab0));
  cv.rect(61, 26, 18, 2, K(0x57201f));          // 측면 스커트
  cv.rect(62, 16, 1, 8, K(0x57201f));           // 벤트 라인

  // 보일러(실린더).
  cv.rect(80, 10, 22, 16, K(0x2d4a3e));         // 짙은 녹색 보일러
  cv.rect(80, 10, 22, 1, K(0x3f6a56));          // 광택 라인
  cv.rect(80, 25, 22, 1, K(0x1f362c));
  for (const bx of [85, 93]) {                   // 밴드+리벳.
    cv.rect(bx, 10, 1, 16, K(0x1f362c));
    cv.set(bx, 11, K(0x5f8a72)); cv.set(bx, 24, K(0x5f8a72));
  }
  // 선두 연기상자(진한)+전조등.
  cv.rect(100, 9, 4, 18, K(0x22282e));
  cv.rect(100, 9, 4, 1, K(0x3a4148));
  cv.circle(103, 15, 2, K(0xffe9a8));           // 헤드라이트
  cv.ring(103, 15, 2, K(0x8a6a3a));
  // 굴뚝(꽃받침형).
  cv.rect(96, 2, 6, 8, K(0x1a1a1f));
  cv.rect(95, 1, 8, 2, K(0x2a2a32));
  cv.rect(95, 1, 8, 1, K(0xb06a3a));           // 동판 트림
  // 증기 돔+안전밸브(황동).
  cv.circle(88, 10, 3, K(0xc9973f));
  cv.rect(86, 10, 5, 2, K(0xe0b058));
  cv.rect(93, 8, 2, 3, K(0xc9973f));
  // 주행대.
  cv.rect(60, 26, 44, 2, K(0x24242c));
  // 배기관.
  cv.rect(82, 6, 2, 5, K(0x8a8f96));

  // 크로우캐처(쐐기 착석기, 우향 사선).
  for (let i = 0; i < 8; i++) {
    cv.rect(104 + i, 28 + Math.floor(i * 1.2), 1, 34 - (28 + Math.floor(i * 1.2)), K(i % 2 ? 0x3a3a44 : 0x565660));
  }
  cv.rect(103, 27, 3, 2, K(0x565660));

  // 바퀴: 동륜 2(대형 r=6, 붉은 허브) + 선소 r=4 + 탠더측 r=4.
  const driver = (cx) => {
    cv.circle(cx, 36, 6, K(0x14141a));
    cv.ring(cx, 36, 6, K(0x3a3a44));
    cv.ring(cx, 36, 5, K(0x24242c));
    for (const [dx, dy] of [[0, -4], [0, 4], [-4, 0], [4, 0], [3, -3], [-3, 3]])
      cv.set(cx + dx, 36 + dy, K(0x4a4a54));   // 스포크
    cv.circle(cx, 36, 2, K(0x8a2f2f));          // 붉은 허브
    cv.set(cx, 36, K(0xc9973f));
    // 크랭크 핀.
    cv.set(cx + 3, 36 - 3, K(0xb8bdc4));
  };
  driver(84); driver(95);
  cv.circle(106, 38, 3, K(0x14141a)); cv.ring(106, 38, 3, K(0x3a3a44)); cv.set(106, 38, K(0x8a8f96));
  cv.circle(66, 38, 3, K(0x14141a));  cv.ring(66, 38, 3, K(0x3a3a44));  cv.set(66, 38, K(0x8a8f96));
  // 연결봉.
  cv.rect(87, 32, 5, 1, K(0xb8bdc4));
  cv.rect(87, 39, 5, 1, K(0x8a8f96));

  save('title_train.png', cv.toPng(SCALE));
}

// ============================================================================
// smoke.png — 논리 8×8 (실제 32×32) 연기 펍.
// ============================================================================
{
  const cv = new Canvas(8, 8);
  const puffs = [[3, 4, 2], [5, 3, 1], [2, 5, 1], [4, 6, 1], [6, 5, 1]];
  for (const [x, y, r] of puffs) {
    cv.circle(x, y, r, rgb(0xd8d8e0, 235));
    cv.set(x, y - r + 1, rgb(0xf0f0f4, 240));
  }
  cv.set(3, 2, rgb(0xb8b8c4, 220)); cv.set(5, 2, rgb(0xb8b8c4, 220));
  save('smoke.png', cv.toPng(SCALE));
}

// ============================================================================
// title_fg.png — 논리 240×24 (실제 960×96) 전경 실루엣(타일링).
// ============================================================================
{
  const W = 240, H = 24;
  const cv = new Canvas(W, H);
  // 기본 바디(아래로 꽉 찬 짙은 풀색).
  cv.rect(0, 8, W, 16, rgb(0x18332a));
  cv.rect(0, 8, W, 1, rgb(0x1f4034));
  // 윤곽 굴곡(주기 함수 → 타일링).
  for (let x = 0; x < W; x++) {
    const b = Math.round(2 * Math.sin((2 * Math.PI * 7 * x) / W + 0.8)
                        + 1.5 * Math.sin((2 * Math.PI * 11 * x) / W + 2.1));
    for (let y = 8 + b; y < 8 + b + 3; y++) cv.set(x, y, rgb(0x1f4034));
    for (let y = 8; y < 8 + b; y++) cv.set(x, y, rgb(0x18332a));
  }
  // 수풀 언덕 몇 개(랩).
  const bush = (x, w, h) => {
    for (let i = 0; i <= h; i++)
      cv.wrapRect(x - Math.floor((i * w) / (2 * h)), 7 + h - i, Math.max(2, Math.floor((i * w) / h)), 1, rgb(0x142b23), W);
  };
  bush(20, 14, 6); bush(70, 10, 4); bush(120, 16, 7); bush(180, 12, 5); bush(220, 9, 4);
  // 키 큰 풀잎.
  for (let i = 0; i < 60; i++) {
    const x = Math.floor(rng() * W);
    const h = 2 + Math.floor(rng() * 4);
    cv.wrapRect(x, 7 - h, 1, h, rgb(0x1a382d), W);
    cv.wrapRect(x + 1, 8 - h, 1, h - 1, rgb(0x142b23), W);
  }
  // 작은 들꽃 점 몇 개.
  for (let i = 0; i < 14; i++) {
    const x = Math.floor(rng() * W), y = 2 + Math.floor(rng() * 5);
    cv.wrapRect(x, y, 1, 1, rgb(0xd8a058), W);
  }
  save('title_fg.png', cv.toPng(SCALE));
}

console.log('완료 — samples/mmo_demo/assets/sprites/');
