/**
 * dot.ts — 도트(픽셀아트) 파이프라인 MCP 툴.
 *
 * 규약(docs/08-mcp.md): 파일 1개 = 툴 그룹. 결과 PNG 는 리포 루트 assets/sprites/ 에 저장되고
 *   base64 이미지로 반환(AI/사용자가 바로 확인). 모든 경로는 루트 상대만 허용(root.ts).
 *
 * 제공 툴:
 *   - dot_write_sprite : AI 가 직접 팔레트+픽셀 그리드를 만들어 스프라이트 PNG 로 "찍는다".
 *   - dot_from_photo   : 사진(PNG/JPG/BMP)을 다운스케일+색상 양자화해 도트로 변환
 *                        (style: flat2d / hd / shaded3d).
 */
import fs from "node:fs";
import path from "node:path";
import { z } from "zod";
import { PNG } from "pngjs";
import * as bmp from "bmp-js";
import * as jpeg from "jpeg-js";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import { assertSafeName, resolveInRoot } from "../root.js";
import { type ServerContext, textResult, errorResult, caughtResult } from "../state.js";

// ---------------------------------------------------------------------------
// 공용 타입/유틸
// ---------------------------------------------------------------------------

/** RGBA8 이미지(픽셀당 4바이트, R,G,B,A 순서). */
interface Rgba {
  width: number;
  height: number;
  data: Uint8Array;
}

const SPRITES_DIR_REL = "assets/sprites";

function clamp255(v: number): number {
  return v < 0 ? 0 : v > 255 ? 255 : Math.round(v);
}

function hexToRgb(hex: string): [number, number, number] {
  return [
    parseInt(hex.slice(1, 3), 16),
    parseInt(hex.slice(3, 5), 16),
    parseInt(hex.slice(5, 7), 16),
  ];
}

/** 결과 PNG 를 assets/sprites/ 에 저장하고 base64 이미지 + 텍스트를 반환. */
function saveAndReturn(ctx: ServerContext, img: Rgba, fileName: string, note: string): CallToolResult {
  const dir = resolveInRoot(ctx.root, SPRITES_DIR_REL);
  fs.mkdirSync(dir, { recursive: true });
  const outPath = path.join(dir, fileName);

  const png = new PNG({ width: img.width, height: img.height });
  png.data = Buffer.from(img.data.buffer, img.data.byteOffset, img.data.byteLength);
  const buf = PNG.sync.write(png);
  fs.writeFileSync(outPath, buf);

  const rel = path.relative(ctx.root, outPath).replace(/\\/g, "/");
  return {
    content: [
      { type: "image", data: buf.toString("base64"), mimeType: "image/png" },
      { type: "text", text: `${note}\n경로: ${rel}\n해상도: ${img.width}x${img.height}` },
    ],
  };
}

// ---------------------------------------------------------------------------
// 이미지 디코드(PNG / JPG / BMP → RGBA)
// ---------------------------------------------------------------------------

function decodeImage(absPath: string): Rgba {
  const buf = fs.readFileSync(absPath);
  const ext = path.extname(absPath).toLowerCase();

  if (ext === ".png") {
    const png = PNG.sync.read(buf);
    return { width: png.width, height: png.height, data: new Uint8Array(png.data) };
  }
  if (ext === ".jpg" || ext === ".jpeg") {
    const j = jpeg.decode(buf, { formatAsRGBA: true });
    return { width: j.width, height: j.height, data: new Uint8Array(j.data) };
  }
  if (ext === ".bmp") {
    const b = bmp.decode(buf);
    // bmp-js 는 ABGR 순서 → RGBA 로 재배열.
    const src = b.data;
    const out = new Uint8Array(b.width * b.height * 4);
    for (let i = 0; i < b.width * b.height; i++) {
      const a = src[i * 4 + 0] ?? 255;
      const bl = src[i * 4 + 1] ?? 0;
      const g = src[i * 4 + 2] ?? 0;
      const r = src[i * 4 + 3] ?? 0;
      out[i * 4 + 0] = r;
      out[i * 4 + 1] = g;
      out[i * 4 + 2] = bl;
      out[i * 4 + 3] = a;
    }
    return { width: b.width, height: b.height, data: out };
  }
  throw new Error(`지원하지 않는 이미지 형식: ${ext} (png/jpg/bmp 만 지원)`);
}

// ---------------------------------------------------------------------------
// 다운스케일(박스 평균) — 알파 가중
// ---------------------------------------------------------------------------

function downscale(src: Rgba, dw: number, dh: number): Rgba {
  const out = new Uint8Array(dw * dh * 4);
  const { width: sw, height: sh, data } = src;
  for (let dy = 0; dy < dh; dy++) {
    const sy0 = Math.floor((dy * sh) / dh);
    const sy1 = Math.max(sy0 + 1, Math.floor(((dy + 1) * sh) / dh));
    for (let dx = 0; dx < dw; dx++) {
      const sx0 = Math.floor((dx * sw) / dw);
      const sx1 = Math.max(sx0 + 1, Math.floor(((dx + 1) * sw) / dw));
      let r = 0, g = 0, b = 0, aSum = 0, wSum = 0, n = 0;
      for (let sy = sy0; sy < sy1; sy++) {
        for (let sx = sx0; sx < sx1; sx++) {
          const o = (sy * sw + sx) * 4;
          const a = data[o + 3] ?? 255;
          const w = a; // 알파 가중 평균(투명 픽셀은 색 기여 낮춤)
          r += (data[o] ?? 0) * w;
          g += (data[o + 1] ?? 0) * w;
          b += (data[o + 2] ?? 0) * w;
          aSum += a;
          wSum += w;
          n++;
        }
      }
      const d = (dy * dw + dx) * 4;
      if (wSum > 0) {
        out[d] = clamp255(r / wSum);
        out[d + 1] = clamp255(g / wSum);
        out[d + 2] = clamp255(b / wSum);
      }
      out[d + 3] = clamp255(aSum / Math.max(1, n));
    }
  }
  return { width: dw, height: dh, data: out };
}

// ---------------------------------------------------------------------------
// 색상 양자화(k-means, 결정론적 시드) — 불투명 픽셀만 대상
// ---------------------------------------------------------------------------

/** 재현 가능한 결과를 위한 간단한 LCG. */
function makeRng(seed: number): () => number {
  let s = seed >>> 0;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 0xffffffff;
  };
}

function quantize(img: Rgba, k: number): [number, number, number][] {
  const { data } = img;
  const pts: [number, number, number][] = [];
  for (let i = 0; i < data.length; i += 4) {
    if ((data[i + 3] ?? 255) < 128) continue; // 투명 제외
    pts.push([data[i] ?? 0, data[i + 1] ?? 0, data[i + 2] ?? 0]);
  }
  if (pts.length === 0) return [[0, 0, 0]];
  const kk = Math.min(k, pts.length);

  // k-means++ 초기화(결정론적).
  const rng = makeRng(0x9e3779b9 ^ pts.length);
  const centers: [number, number, number][] = [];
  centers.push([...pts[Math.floor(rng() * pts.length)]!]);
  while (centers.length < kk) {
    const d2: number[] = new Array(pts.length);
    let total = 0;
    for (let i = 0; i < pts.length; i++) {
      const p = pts[i]!;
      let best = Infinity;
      for (const c of centers) {
        const dd = (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2;
        if (dd < best) best = dd;
      }
      d2[i] = best;
      total += best;
    }
    let target = rng() * total;
    let idx = 0;
    for (let i = 0; i < pts.length; i++) {
      target -= d2[i]!;
      if (target <= 0) { idx = i; break; }
    }
    centers.push([...pts[idx]!]);
  }

  // Lloyd 반복.
  for (let iter = 0; iter < 10; iter++) {
    const sums = centers.map(() => [0, 0, 0, 0] as [number, number, number, number]);
    for (const p of pts) {
      let best = Infinity, bi = 0;
      for (let i = 0; i < centers.length; i++) {
        const c = centers[i]!;
        const dd = (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2;
        if (dd < best) { best = dd; bi = i; }
      }
      const s = sums[bi]!;
      s[0] += p[0]; s[1] += p[1]; s[2] += p[2]; s[3] += 1;
    }
    for (let i = 0; i < centers.length; i++) {
      const s = sums[i]!;
      if (s[3] > 0) centers[i] = [Math.round(s[0] / s[3]), Math.round(s[1] / s[3]), Math.round(s[2] / s[3])];
    }
  }
  return centers;
}

function nearest(palette: [number, number, number][], r: number, g: number, b: number): number {
  let best = Infinity, bi = 0;
  for (let i = 0; i < palette.length; i++) {
    const c = palette[i]!;
    const dd = (r - c[0]) ** 2 + (g - c[1]) ** 2 + (b - c[2]) ** 2;
    if (dd < best) { best = dd; bi = i; }
  }
  return bi;
}

/** 팔레트 매핑(옵션: Floyd–Steinberg 디더). 결과는 새 RGBA(알파 보존). */
function applyPalette(img: Rgba, palette: [number, number, number][], dither: boolean): Rgba {
  const { width: w, height: h } = img;
  const src = new Float32Array(img.data.length);
  for (let i = 0; i < img.data.length; i++) src[i] = img.data[i] ?? 0;
  const out = new Uint8Array(img.data.length);

  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const o = (y * w + x) * 4;
      const a = img.data[o + 3] ?? 255;
      if (a < 128) { out[o + 3] = 0; continue; } // 투명 유지
      const r = src[o]!, g = src[o + 1]!, b = src[o + 2]!;
      const pi = nearest(palette, r, g, b);
      const c = palette[pi]!;
      out[o] = c[0]; out[o + 1] = c[1]; out[o + 2] = c[2]; out[o + 3] = 255;
      if (dither) {
        const er = r - c[0], eg = g - c[1], eb = b - c[2];
        const spread = (nx: number, ny: number, f: number) => {
          if (nx < 0 || nx >= w || ny < 0 || ny >= h) return;
          const no = (ny * w + nx) * 4;
          src[no] = (src[no] ?? 0) + er * f;
          src[no + 1] = (src[no + 1] ?? 0) + eg * f;
          src[no + 2] = (src[no + 2] ?? 0) + eb * f;
        };
        spread(x + 1, y, 7 / 16);
        spread(x - 1, y + 1, 3 / 16);
        spread(x, y + 1, 5 / 16);
        spread(x + 1, y + 1, 1 / 16);
      }
    }
  }
  return { width: w, height: h, data: out };
}

/** shaded3d: 밝기 에지에 1px 어두운 아웃라인 + 약한 대비 강화 → 입체감(도트 특유 볼륨). */
function shade3d(img: Rgba): Rgba {
  const { width: w, height: h, data } = img;
  const out = new Uint8Array(data);
  const lum = (o: number) => 0.299 * (data[o] ?? 0) + 0.587 * (data[o + 1] ?? 0) + 0.114 * (data[o + 2] ?? 0);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const o = (y * w + x) * 4;
      if ((data[o + 3] ?? 0) < 128) continue;
      // 오른쪽·아래 이웃보다 눈에 띄게 밝으면(경계) 살짝 어둡게 → 도트 셰이딩.
      const ro = (y * w + Math.min(w - 1, x + 1)) * 4;
      const bo = (Math.min(h - 1, y + 1) * w + x) * 4;
      const edge = Math.max(lum(o) - lum(ro), lum(o) - lum(bo));
      const k = edge > 40 ? 0.72 : 1.0;
      out[o] = clamp255((data[o] ?? 0) * k);
      out[o + 1] = clamp255((data[o + 1] ?? 0) * k);
      out[o + 2] = clamp255((data[o + 2] ?? 0) * k);
    }
  }
  return { width: w, height: h, data: out };
}

// ---------------------------------------------------------------------------
// 툴 1: dot_write_sprite — AI 가 픽셀 그리드를 직접 그려 저장
// ---------------------------------------------------------------------------

function registerWriteSprite(server: McpServer, ctx: ServerContext): void {
  server.registerTool(
    "dot_write_sprite",
    {
      description:
        "AI 가 직접 픽셀아트 스프라이트를 '찍어' PNG 로 저장한다. palette 는 '#RRGGBB' HEX 배열, " +
        "pixels 는 팔레트 인덱스의 2D 배열(높이×너비). 인덱스 -1 은 투명. 결과는 assets/sprites/ 에 저장되고 이미지로 반환.",
      inputSchema: {
        name: z.string().min(1).describe("파일명(확장자 제외, 영숫자·_-. )"),
        width: z.number().int().min(1).max(256).describe("너비(픽셀)"),
        height: z.number().int().min(1).max(256).describe("높이(픽셀)"),
        palette: z.array(z.string().regex(/^#[0-9A-Fa-f]{6}$/)).min(1).max(256).describe("팔레트 HEX 색상"),
        pixels: z.array(z.array(z.number().int().min(-1))).describe("픽셀 인덱스[height][width] (-1=투명)"),
        scale: z.number().int().min(1).max(32).default(1).describe("정수 확대 배율(저장 시 픽셀 블록 크기)"),
      },
    },
    async (p): Promise<CallToolResult> => {
      try {
        return doWriteSprite(ctx, p as WriteSpriteParams);
      } catch (e) {
        return caughtResult(e);
      }
    },
  );
}

interface WriteSpriteParams {
  name: string;
  width: number;
  height: number;
  palette: string[];
  pixels: number[][];
  scale: number;
}

function doWriteSprite(ctx: ServerContext, p: WriteSpriteParams): CallToolResult {
  assertSafeName(p.name, "스프라이트");
  if (p.pixels.length !== p.height) return errorResult(`pixels 행 수(${p.pixels.length}) ≠ height(${p.height})`);

  const rgb = p.palette.map(hexToRgb);
  const base = new Uint8Array(p.width * p.height * 4);
  for (let y = 0; y < p.height; y++) {
    const row = p.pixels[y]!;
    if (row.length !== p.width) return errorResult(`pixels[${y}] 길이(${row.length}) ≠ width(${p.width})`);
    for (let x = 0; x < p.width; x++) {
      const idx = row[x]!;
      const o = (y * p.width + x) * 4;
      if (idx < 0) { base[o + 3] = 0; continue; }
      if (idx >= rgb.length) return errorResult(`pixels[${y}][${x}] 인덱스 ${idx} 팔레트 범위 초과(max ${rgb.length - 1})`);
      const c = rgb[idx]!;
      base[o] = c[0]; base[o + 1] = c[1]; base[o + 2] = c[2]; base[o + 3] = 255;
    }
  }

  // 정수 확대(nearest) — 도트 보존.
  const s = p.scale;
  const img: Rgba = s === 1
    ? { width: p.width, height: p.height, data: base }
    : nearestScale({ width: p.width, height: p.height, data: base }, s);

  return saveAndReturn(ctx, img, `${p.name}.png`, `스프라이트 '${p.name}' 저장 완료 (팔레트 ${p.palette.length}색, x${s})`);
}

function nearestScale(img: Rgba, s: number): Rgba {
  const w = img.width * s, h = img.height * s;
  const out = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const so = (Math.floor(y / s) * img.width + Math.floor(x / s)) * 4;
      const d = (y * w + x) * 4;
      out[d] = img.data[so] ?? 0;
      out[d + 1] = img.data[so + 1] ?? 0;
      out[d + 2] = img.data[so + 2] ?? 0;
      out[d + 3] = img.data[so + 3] ?? 0;
    }
  }
  return { width: w, height: h, data: out };
}

// ---------------------------------------------------------------------------
// 툴 2: dot_from_photo — 사진 → 도트 변환(스타일 프리셋)
// ---------------------------------------------------------------------------

function registerFromPhoto(server: McpServer, ctx: ServerContext): void {
  server.registerTool(
    "dot_from_photo",
    {
      description:
        "사진(png/jpg/bmp, 리포 루트 상대 경로)을 도트로 변환한다. 다운스케일+색상 양자화(k-means). " +
        "style: flat2d(기본, 선명한 2D 도트) / hd(디더링·많은 색, 고화질 도트) / shaded3d(에지 셰이딩으로 입체감). " +
        "결과 PNG 는 assets/sprites/ 에 저장되고 이미지로 반환.",
      inputSchema: {
        inputPath: z.string().min(1).describe("입력 이미지 경로(리포 루트 상대, png/jpg/bmp)"),
        targetWidth: z.number().int().min(8).max(256).default(64).describe("목표 도트 너비(px). 높이는 비율 유지"),
        style: z.enum(["flat2d", "hd", "shaded3d"]).default("flat2d").describe("변환 스타일"),
        paletteSize: z.number().int().min(2).max(256).optional().describe("팔레트 색상 수(생략 시 스타일 기본값)"),
        outputScale: z.number().int().min(1).max(16).default(1).describe("저장 시 정수 확대 배율"),
        name: z.string().optional().describe("출력 파일명(생략 시 입력명 기반)"),
      },
    },
    async (p): Promise<CallToolResult> => {
      try {
        return doFromPhoto(ctx, p as FromPhotoParams);
      } catch (e) {
        return caughtResult(e, "입력 경로(리포 루트 상대)·형식(png/jpg/bmp)을 확인하세요.");
      }
    },
  );
}

interface FromPhotoParams {
  inputPath: string;
  targetWidth: number;
  style: "flat2d" | "hd" | "shaded3d";
  paletteSize?: number;
  outputScale: number;
  name?: string;
}

function doFromPhoto(ctx: ServerContext, p: FromPhotoParams): CallToolResult {
  const abs = resolveInRoot(ctx.root, p.inputPath);
  if (!fs.existsSync(abs)) return errorResult(`입력 파일 없음: ${p.inputPath}`);

  const src = decodeImage(abs);

  // 스타일 프리셋.
  const preset = {
    flat2d: { palette: 16, dither: false, shade: false },
    hd: { palette: 48, dither: true, shade: false },
    shaded3d: { palette: 24, dither: false, shade: true },
  }[p.style];
  const paletteSize = p.paletteSize ?? preset.palette;

  const dw = Math.min(p.targetWidth, src.width);
  const dh = Math.max(1, Math.round((src.height / src.width) * dw));

  let img = downscale(src, dw, dh);
  const palette = quantize(img, paletteSize);
  img = applyPalette(img, palette, preset.dither);
  if (preset.shade) img = shade3d(img);
  if (p.outputScale > 1) img = nearestScale(img, p.outputScale);

  const baseName = p.name ?? path.basename(abs, path.extname(abs));
  assertSafeName(baseName, "스프라이트");
  const fileName = `${baseName}_${p.style}.png`;

  return saveAndReturn(
    ctx,
    img,
    fileName,
    `사진→도트 변환 완료 (style=${p.style}, ${dw}x${dh}, 팔레트 ${palette.length}색${preset.dither ? ", 디더" : ""}${preset.shade ? ", 셰이딩" : ""}, x${p.outputScale})`,
  );
}

// ---------------------------------------------------------------------------
// 등록
// ---------------------------------------------------------------------------

export function registerDotTools(server: McpServer, ctx: ServerContext): void {
  registerWriteSprite(server, ctx);
  registerFromPhoto(server, ctx);
}
