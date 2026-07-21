/**
 * engine_run — 샘플 실행(--frames N 으로 자동 종료, 크래시 코드 해석).
 * exe 탐색: <buildDir>/samples/<sample>/<config>/<sample>.exe 우선,
 * 폴백 재귀 탐색 <buildDir> 하위의 <config>/<sample>.exe.
 */
import fs from "node:fs";
import path from "node:path";
import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import { runProcess, describeExitCode } from "../proc.js";
import { selectTailWithPriority } from "../summarize.js";
import { assertSafeName } from "../root.js";
import { type ServerContext, textResult, errorResult, caughtResult } from "../state.js";

interface RunParams {
  sample: string;
  config: "Debug" | "Release";
  frames: number;
  args?: string[] | undefined;
  timeoutSec: number;
}

export function registerRunTool(server: McpServer, ctx: ServerContext): void {
  server.registerTool(
    "engine_run",
    {
      description:
        "샘플 exe 를 --frames N 으로 실행해 자동 종료시키고, exit 코드(크래시 시 NTSTATUS 해석 병기)와 " +
        "출력 꼬리를 반환한다. 전체 로그는 engine_logs(source=\"run\")로.",
      inputSchema: {
        sample: z.string().min(1).describe('샘플 이름(예: "hello_triangle")'),
        config: z.enum(["Debug", "Release"]).default("Debug").describe("빌드 구성"),
        frames: z.number().int().min(1).default(120).describe("렌더할 프레임 수(--frames N 전달)"),
        args: z.array(z.string()).optional().describe("샘플에 넘길 추가 인자"),
        timeoutSec: z.number().int().min(1).max(600).default(30).describe("타임아웃(초)"),
      },
    },
    async (p): Promise<CallToolResult> => {
      try {
        return await ctx.gate.run("engine_run", () => doRun(ctx, p as RunParams));
      } catch (e) {
        return caughtResult(e);
      }
    },
  );
}

/** 샘플 exe 탐색. 없으면 null. buildDir 부재는 별도 판단. */
export function findSampleExe(ctx: ServerContext, sample: string, config: string): string | null {
  const primary = path.join(ctx.buildDir, "samples", sample, config, `${sample}.exe`);
  if (fs.existsSync(primary)) return primary;
  // 폴백: <buildDir>/**/<config>/<sample>.exe (깊이 제한 글롭)
  const target = `${sample}.exe`.toLowerCase();
  const configLower = config.toLowerCase();
  const skip = new Set(["cmakefiles", ".vs", "_deps", "node_modules"]);
  const found: string[] = [];
  const walk = (dir: string, depth: number): void => {
    if (depth > 8 || found.length > 0) return;
    let entries: fs.Dirent[];
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      if (found.length > 0) return;
      const full = path.join(dir, e.name);
      if (e.isDirectory()) {
        if (!skip.has(e.name.toLowerCase())) walk(full, depth + 1);
      } else if (
        e.name.toLowerCase() === target &&
        path.basename(dir).toLowerCase() === configLower
      ) {
        found.push(full);
      }
    }
  };
  if (fs.existsSync(ctx.buildDir)) walk(ctx.buildDir, 0);
  return found[0] ?? null;
}

/** 산출물 부재 시의 공통 안내 에러(engine_run·engine_capture_frame 공용). */
export function missingExeError(ctx: ServerContext, sample: string, config: string): CallToolResult {
  if (!fs.existsSync(ctx.buildDir)) {
    return errorResult(
      `빌드 디렉터리가 없습니다: ${ctx.buildDir}\n다음 행동: engine_build 를 먼저 실행하세요.`,
    );
  }
  let known = "";
  try {
    const samplesDir = path.join(ctx.root, "samples");
    const names = fs
      .readdirSync(samplesDir, { withFileTypes: true })
      .filter((e) => e.isDirectory())
      .map((e) => e.name);
    if (names.length > 0) known = `\n리포의 샘플 목록: ${names.join(", ")}`;
  } catch {
    /* samples/ 없음 — 무시 */
  }
  return errorResult(
    `샘플 실행 파일을 찾지 못했습니다: ${sample} (${config}) — ` +
      `${ctx.buildDirRel}/samples/${sample}/${config}/${sample}.exe 및 하위 글롭 탐색 실패.${known}\n` +
      `다음 행동: engine_build 를 먼저 실행하거나 sample 이름·config 를 확인하세요.`,
  );
}

async function doRun(ctx: ServerContext, p: RunParams): Promise<CallToolResult> {
  assertSafeName(p.sample, "샘플");

  const exe = findSampleExe(ctx, p.sample, p.config);
  if (exe === null) return missingExeError(ctx, p.sample, p.config);

  const args = ["--frames", String(p.frames), ...(p.args ?? [])];
  const res = await runProcess({ command: exe, args, cwd: ctx.root, timeoutMs: p.timeoutSec * 1000 });

  const logPath = ctx.state.writeLog(
    "run",
    [
      `=== ${res.command}`,
      `=== cwd: ${ctx.root}`,
      `=== exit: ${String(res.exitCode)} · timedOut: ${String(res.timedOut)} · duration: ${res.durationMs}ms`,
      "",
      res.output,
    ].join("\n"),
  );

  const ok = !res.timedOut && res.exitCode === 0 && res.spawnError === undefined;
  const exitDesc = describeExitCode(res.exitCode);
  const summary = `RUN ${p.sample} (${p.config}, ${p.frames} frames) — exit ${exitDesc} · ${res.durationMs}ms · timedOut ${String(res.timedOut)}`;

  const body: string[] = [summary];
  if (res.spawnError !== undefined) body.push(`실행 실패: ${res.spawnError}`);
  if (res.timedOut) {
    body.push(
      `타임아웃(${p.timeoutSec}s) 초과 — 프로세스 트리를 강제 종료했습니다. ` +
        `--frames 자동 종료가 동작하는지 확인하거나 timeoutSec 을 늘리세요.`,
    );
  }
  const tail = selectTailWithPriority(res.output.split(/\r?\n/), 80);
  if (tail.length > 0) {
    body.push(`출력 꼬리(${tail.length}줄, 에러·경고 우선):`);
    body.push(tail.join("\n"));
  } else {
    body.push("(출력 없음)");
  }
  body.push(`로그: ${logPath} (전체는 engine_logs source="run")`);

  ctx.state.recordStatus("run", {
    at: new Date().toISOString(),
    ok,
    summary,
    config: p.config,
    durationMs: res.durationMs,
    logPath,
    extra: { sample: p.sample, frames: p.frames, exitCode: res.exitCode, timedOut: res.timedOut },
  });
  return ok ? textResult(body.join("\n")) : errorResult(body.join("\n"));
}
