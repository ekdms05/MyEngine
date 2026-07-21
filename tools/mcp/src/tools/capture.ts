/**
 * engine_capture_frame — 프레임 캡처(AI의 눈).
 * 샘플을 `--frames <frame> --dump <임시.bmp>` 로 실행 → BMP 디코드(24/32bpp,
 * top-down·bottom-up 모두) → 필요 시 최대 변 960px 다운스케일 → PNG 인코드 →
 * MCP 이미지 콘텐츠(base64) 반환.
 */
import fs from "node:fs";
import path from "node:path";
import { z } from "zod";
import { PNG } from "pngjs";
import bmp from "bmp-js";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import { runProcess, describeExitCode } from "../proc.js";
import { selectTailWithPriority } from "../summarize.js";
import { assertSafeName } from "../root.js";
import { type ServerContext, errorResult, caughtResult, clampText } from "../state.js";
import { findSampleExe, missingExeError } from "./run.js";

const MAX_SIDE = 960;

interface CaptureParams {
  sample: string;
  config: "Debug" | "Release";
  frame: number;
  args?: string[] | undefined;
  timeoutSec: number;
}

export function registerCaptureTool(server: McpServer, ctx: ServerContext): void {
  server.registerTool(
    "engine_capture_frame",
    {
      description:
        "샘플을 N프레임 실행해 마지막 프레임 백버퍼를 PNG 이미지로 캡처해 반환한다(AI의 눈). " +
        "샘플 CLI 계약 --frames/--dump 를 사용한다.",
      inputSchema: {
        sample: z.string().min(1).default("hello_triangle").describe("샘플 이름"),
        config: z.enum(["Debug", "Release"]).default("Debug").describe("빌드 구성"),
        frame: z.number().int().min(1).default(60).describe("캡처할 프레임 번호(--frames N)"),
        args: z.array(z.string()).optional().describe("샘플에 넘길 추가 인자"),
        timeoutSec: z.number().int().min(1).max(600).default(30).describe("타임아웃(초)"),
      },
    },
    async (p): Promise<CallToolResult> => {
      try {
        return await ctx.gate.run("engine_capture_frame", () => doCapture(ctx, p as CaptureParams));
      } catch (e) {
        return caughtResult(e);
      }
    },
  );
}

async function doCapture(ctx: ServerContext, p: CaptureParams): Promise<CallToolResult> {
  assertSafeName(p.sample, "샘플");

  const exe = findSampleExe(ctx, p.sample, p.config);
  if (exe === null) return missingExeError(ctx, p.sample, p.config);

  ctx.state.ensureDirs();
  const ts = ctx.state.timestamp();
  const bmpPath = path.join(ctx.state.capturesDir, `tmp-${p.sample}-${ts}.bmp`);

  const args = ["--frames", String(p.frame), "--dump", bmpPath, ...(p.args ?? [])];
  const res = await runProcess({ command: exe, args, cwd: ctx.root, timeoutMs: p.timeoutSec * 1000 });

  const logPath = ctx.state.writeLog(
    "capture",
    [
      `=== ${res.command}`,
      `=== exit: ${String(res.exitCode)} · timedOut: ${String(res.timedOut)} · duration: ${res.durationMs}ms`,
      "",
      res.output,
    ].join("\n"),
  );
  const tailOf = (n: number): string => selectTailWithPriority(res.output.split(/\r?\n/), n).join("\n");

  const fail = (reason: string, hint: string): CallToolResult => {
    ctx.state.recordStatus("capture", {
      at: new Date().toISOString(),
      ok: false,
      summary: `CAPTURE FAILED ${p.sample} — ${reason}`,
      config: p.config,
      durationMs: res.durationMs,
      logPath,
      extra: { sample: p.sample, frame: p.frame },
    });
    tryUnlink(bmpPath);
    return errorResult(
      clampText(
        [
          `CAPTURE FAILED — ${reason}`,
          `exit ${describeExitCode(res.exitCode)} · timedOut ${String(res.timedOut)} · ${res.durationMs}ms`,
          `다음 행동: ${hint}`,
          `프로세스 출력 꼬리:\n${tailOf(40)}`,
          `로그: ${logPath} (전체는 engine_logs source="capture")`,
        ].join("\n"),
      ),
    );
  };

  if (res.spawnError !== undefined) return fail(`샘플 실행 실패(${res.spawnError})`, "engine_build 후 재시도하세요.");
  if (res.timedOut) return fail(`타임아웃(${p.timeoutSec}s)`, "--frames 자동 종료 지원 여부를 확인하거나 timeoutSec 을 늘리세요.");
  if (res.exitCode !== 0) return fail("샘플이 비정상 종료", "engine_logs(source=\"capture\")로 크래시 원인을 확인하세요.");
  if (!fs.existsSync(bmpPath)) {
    return fail(
      "덤프 파일이 생성되지 않았습니다",
      "샘플이 --dump 플래그(샘플 CLI 계약, docs/08-mcp.md)를 지원하는지 확인하세요.",
    );
  }

  // BMP → RGBA
  let width: number;
  let height: number;
  let rgba: Buffer;
  try {
    const decoded = decodeBmpToRgba(fs.readFileSync(bmpPath));
    width = decoded.width;
    height = decoded.height;
    rgba = decoded.rgba;
  } catch (e) {
    return fail(
      `BMP 디코드 실패(${e instanceof Error ? e.message : String(e)})`,
      "샘플의 --dump 출력이 BMP 24/32bpp(BI_RGB) 형식인지 확인하세요.",
    );
  }
  tryUnlink(bmpPath);

  // 다운스케일(최대 변 960px)
  const origW = width;
  const origH = height;
  if (Math.max(width, height) > MAX_SIDE) {
    const s = MAX_SIDE / Math.max(width, height);
    const dw = Math.max(1, Math.round(width * s));
    const dh = Math.max(1, Math.round(height * s));
    rgba = downscaleRgba(rgba, width, height, dw, dh);
    width = dw;
    height = dh;
  }

  // PNG 인코드·저장
  const png = new PNG({ width, height });
  rgba.copy(png.data);
  const pngBuf = PNG.sync.write(png);
  const pngPath = ctx.state.newCapturePath(p.sample);
  fs.writeFileSync(pngPath, pngBuf);

  const scaledNote = origW !== width || origH !== height ? ` (원본 ${origW}x${origH} 에서 다운스케일)` : "";
  const meta = `해상도 ${width}x${height}${scaledNote} · frame ${p.frame} · exit 0 · 저장: ${pngPath}`;

  ctx.state.recordStatus("capture", {
    at: new Date().toISOString(),
    ok: true,
    summary: `CAPTURE OK ${p.sample} — ${width}x${height}, frame ${p.frame}`,
    config: p.config,
    durationMs: res.durationMs,
    logPath,
    extra: { sample: p.sample, frame: p.frame, png: pngPath, width, height },
  });

  return {
    content: [
      { type: "image", data: pngBuf.toString("base64"), mimeType: "image/png" },
      { type: "text", text: meta },
    ],
  };
}

function tryUnlink(file: string): void {
  try {
    fs.unlinkSync(file);
  } catch {
    /* 무시 */
  }
}

// ---------------------------------------------------------------------------
// BMP 디코드 — BI_RGB 24/32bpp, top-down(음수 height)·bottom-up 모두 직접 처리.
// 그 외 형식은 bmp-js 폴백.
// ---------------------------------------------------------------------------

export function decodeBmpToRgba(buf: Buffer): { width: number; height: number; rgba: Buffer } {
  try {
    return decodeSimpleBmp(buf);
  } catch (primaryErr) {
    // 폴백: bmp-js (ABGR 반환 → RGBA 변환)
    try {
      const d = bmp.decode(buf);
      const n = d.width * d.height;
      const rgba = Buffer.alloc(n * 4);
      for (let i = 0; i < n; i++) {
        const a = d.data[i * 4] ?? 255;
        const b = d.data[i * 4 + 1] ?? 0;
        const g = d.data[i * 4 + 2] ?? 0;
        const r = d.data[i * 4 + 3] ?? 0;
        rgba[i * 4] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = a === 0 ? 255 : a;
      }
      return { width: d.width, height: d.height, rgba };
    } catch {
      throw primaryErr instanceof Error ? primaryErr : new Error(String(primaryErr));
    }
  }
}

function decodeSimpleBmp(buf: Buffer): { width: number; height: number; rgba: Buffer } {
  if (buf.length < 54) throw new Error("파일이 너무 작습니다(BMP 헤더 불완전)");
  if (buf.toString("ascii", 0, 2) !== "BM") throw new Error('BMP 매직("BM") 불일치');
  const dataOffset = buf.readUInt32LE(10);
  const dibSize = buf.readUInt32LE(14);
  if (dibSize < 40) throw new Error(`지원하지 않는 DIB 헤더 크기: ${dibSize}`);
  const width = buf.readInt32LE(18);
  const heightRaw = buf.readInt32LE(22);
  const bpp = buf.readUInt16LE(28);
  const compression = buf.readUInt32LE(30);

  if (width <= 0 || width > 32768) throw new Error(`비정상 너비: ${width}`);
  const topDown = heightRaw < 0;
  const height = Math.abs(heightRaw);
  if (height === 0 || height > 32768) throw new Error(`비정상 높이: ${heightRaw}`);
  if (bpp !== 24 && bpp !== 32) throw new Error(`지원하지 않는 bpp: ${bpp} (24/32 만 지원)`);
  // BI_RGB(0) 허용. 32bpp 의 BI_BITFIELDS(3)는 표준 BGRA 마스크로 가정하고 허용.
  if (compression !== 0 && !(compression === 3 && bpp === 32)) {
    throw new Error(`지원하지 않는 압축 형식: ${compression}`);
  }

  const bytesPerPixel = bpp / 8;
  const rowSize = (width * bytesPerPixel + 3) & ~3;
  if (dataOffset + rowSize * height > buf.length) throw new Error("픽셀 데이터가 헤더 크기보다 작습니다");

  const rgba = Buffer.alloc(width * height * 4);
  let anyAlpha = false;
  for (let y = 0; y < height; y++) {
    const srcY = topDown ? y : height - 1 - y;
    const rowStart = dataOffset + srcY * rowSize;
    for (let x = 0; x < width; x++) {
      const s = rowStart + x * bytesPerPixel;
      const o = (y * width + x) * 4;
      rgba[o] = buf[s + 2] ?? 0; // R
      rgba[o + 1] = buf[s + 1] ?? 0; // G
      rgba[o + 2] = buf[s] ?? 0; // B
      if (bpp === 32) {
        const a = buf[s + 3] ?? 0;
        rgba[o + 3] = a;
        if (a !== 0) anyAlpha = true;
      } else {
        rgba[o + 3] = 255;
        anyAlpha = true;
      }
    }
  }
  // 백버퍼 덤프에서 알파 채널이 전부 0인 경우(흔함) 불투명 처리
  if (!anyAlpha) {
    for (let i = 3; i < rgba.length; i += 4) rgba[i] = 255;
  }
  return { width, height, rgba };
}

/** 영역 평균 다운스케일(RGBA8). */
export function downscaleRgba(src: Buffer, sw: number, sh: number, dw: number, dh: number): Buffer {
  const dst = Buffer.alloc(dw * dh * 4);
  for (let dy = 0; dy < dh; dy++) {
    const sy0 = Math.floor((dy * sh) / dh);
    const sy1 = Math.max(sy0 + 1, Math.ceil(((dy + 1) * sh) / dh));
    for (let dx = 0; dx < dw; dx++) {
      const sx0 = Math.floor((dx * sw) / dw);
      const sx1 = Math.max(sx0 + 1, Math.ceil(((dx + 1) * sw) / dw));
      let r = 0;
      let g = 0;
      let b = 0;
      let a = 0;
      let count = 0;
      for (let sy = sy0; sy < sy1; sy++) {
        for (let sx = sx0; sx < sx1; sx++) {
          const o = (sy * sw + sx) * 4;
          r += src[o] ?? 0;
          g += src[o + 1] ?? 0;
          b += src[o + 2] ?? 0;
          a += src[o + 3] ?? 0;
          count++;
        }
      }
      const d = (dy * dw + dx) * 4;
      dst[d] = Math.round(r / count);
      dst[d + 1] = Math.round(g / count);
      dst[d + 2] = Math.round(b / count);
      dst[d + 3] = Math.round(a / count);
    }
  }
  return dst;
}
