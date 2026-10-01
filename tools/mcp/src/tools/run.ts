/**
 * engine_run — 앱 실행(--frames N 자동 종료, 크래시 코드 해석).
 * 실행 산출물은 <buildDir> 하위의 <config>/<target>.exe에서 찾는다.
 * sample 입력 이름은 기존 MCP 클라이언트 호환을 위해 유지한다.
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
        "앱 실행 파일을 --frames N 으로 실행해 자동 종료시키고, exit 코드(크래시 시 NTSTATUS 해석 병기)와 " +
        "출력 꼬리를 반환한다. 전체 로그는 engine_logs(source=\"run\")로.",
      inputSchema: {
        sample: z.string().min(1).describe('실행 타깃(예: "MyEditor"). MyEditor의 --project는 args로 전달'),
        config: z.enum(["Debug", "Release"]).default("Debug").describe("빌드 구성"),
        frames: z.number().int().min(1).default(120).describe("렌더할 프레임 수(--frames N 전달)"),
        args: z.array(z.string()).optional().describe("앱에 넘길 추가 인자"),
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

/** 구성에 해당하는 실행 산출물을 찾는다. 없으면 null. */
export function findTargetExe(ctx: ServerContext, sample: string, config: string): string | null {
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
  return errorResult(
    `실행 파일을 찾지 못했습니다: ${sample} (${config}) — ${ctx.buildDirRel} 하위 구성 산출물 없음.\n` +
      `다음 행동: engine_build(target="${sample}", config="${config}")를 실행하거나 sample 입력의 타깃 이름을 확인하세요.`,
  );
}

async function doRun(ctx: ServerContext, p: RunParams): Promise<CallToolResult> {
  assertSafeName(p.sample, "타깃");

  const exe = findTargetExe(ctx, p.sample, p.config);
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
