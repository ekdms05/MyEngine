/**
 * engine_build — 엔진 빌드 (자동 configure + MSVC 요약 파서).
 * build/CMakeCache.txt 부재 시 `cmake -S . -B <buildDir> -G "Visual Studio 18 2026" -A x64` 자동 수행.
 */
import fs from "node:fs";
import path from "node:path";
import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import { runProcess } from "../proc.js";
import { parseMsvcDiagnostics, formatDiagnostics, selectTailWithPriority } from "../summarize.js";
import { assertSafeName } from "../root.js";
import {
  type ServerContext,
  textResult,
  errorResult,
  caughtResult,
} from "../state.js";

export const CMAKE_GENERATOR = "Visual Studio 18 2026";

interface BuildParams {
  config: "Debug" | "Release";
  target?: string | undefined;
  clean: boolean;
  timeoutSec: number;
}

export function registerBuildTool(server: McpServer, ctx: ServerContext): void {
  server.registerTool(
    "engine_build",
    {
      description:
        "엔진을 빌드한다(CMake + MSVC). configure 가 안 되어 있으면 자동 configure 후 빌드하고, " +
        "에러·경고를 파일(줄) 단위로 요약해 반환한다. 전체 로그는 engine_logs(source=\"build\")로.",
      inputSchema: {
        config: z.enum(["Debug", "Release"]).default("Debug").describe("빌드 구성"),
        target: z.string().optional().describe("CMake 타깃 이름(생략 시 전체 빌드)"),
        clean: z.boolean().default(false).describe("true 면 --clean-first 로 클린 빌드"),
        timeoutSec: z.number().int().min(1).max(3600).default(600).describe("타임아웃(초)"),
      },
    },
    async (p): Promise<CallToolResult> => {
      try {
        return await ctx.gate.run("engine_build", () => doBuild(ctx, p as BuildParams));
      } catch (e) {
        return caughtResult(e);
      }
    },
  );
}

async function doBuild(ctx: ServerContext, p: BuildParams): Promise<CallToolResult> {
  if (p.target !== undefined) assertSafeName(p.target, "CMake 타깃");

  const started = Date.now();
  const cachePath = path.join(ctx.buildDir, "CMakeCache.txt");
  const logParts: string[] = [];
  let didConfigure = false;

  // 1) 자동 configure
  if (!fs.existsSync(cachePath)) {
    const cfgArgs = ["-S", ".", "-B", ctx.buildDirRel, "-G", CMAKE_GENERATOR, "-A", "x64"];
    const cfg = await runProcess({
      command: "cmake",
      args: cfgArgs,
      cwd: ctx.root,
      timeoutMs: p.timeoutSec * 1000,
    });
    didConfigure = true;
    logParts.push(`=== configure: ${cfg.command} (exit ${String(cfg.exitCode)}) ===\n${cfg.output}`);

    if (cfg.spawnError !== undefined) {
      const logPath = ctx.state.writeLog("build", logParts.join("\n"));
      const summary = `BUILD FAILED (${p.config}, configure 불가)`;
      ctx.state.recordStatus("build", statusEntry(false, summary, p.config, Date.now() - started, logPath));
      return errorResult(
        `${summary}\ncmake 실행 실패: ${cfg.spawnError}\n다음 행동: cmake 가 PATH 에 있는지 확인하세요.\n로그: ${logPath}`,
      );
    }
    if (cfg.timedOut || cfg.exitCode !== 0) {
      const diag = parseMsvcDiagnostics(cfg.output);
      const logPath = ctx.state.writeLog("build", logParts.join("\n"));
      const secs = ((Date.now() - started) / 1000).toFixed(1);
      const summary = `BUILD FAILED (${p.config}, ${secs}s, configure 실패${cfg.timedOut ? " — 타임아웃" : ""})`;
      ctx.state.recordStatus("build", statusEntry(false, summary, p.config, Date.now() - started, logPath));
      const tail = selectTailWithPriority(cfg.output.split(/\r?\n/), 20).join("\n");
      return errorResult(
        [
          summary,
          `자동 configure 수행: cmake -S . -B ${ctx.buildDirRel} -G "${CMAKE_GENERATOR}" -A x64 → 실패`,
          diag.cmakeErrors.length > 0 ? formatDiagnostics({ ...diag, errors: [], warnings: [] }) : `출력 꼬리:\n${tail}`,
          `다음 행동: CMakeLists.txt 와 제너레이터("${CMAKE_GENERATOR}") 설치 여부를 확인하세요.`,
          `로그: ${logPath} (전체는 engine_logs source="build")`,
        ].join("\n"),
      );
    }
  }

  // 2) 빌드
  const buildArgs = ["--build", ctx.buildDirRel, "--config", p.config];
  if (p.target !== undefined) buildArgs.push("--target", p.target);
  if (p.clean) buildArgs.push("--clean-first");
  buildArgs.push("--", "/m");

  const remainMs = Math.max(5_000, p.timeoutSec * 1000 - (Date.now() - started));
  const build = await runProcess({ command: "cmake", args: buildArgs, cwd: ctx.root, timeoutMs: remainMs });
  logParts.push(`=== build: ${build.command} (exit ${String(build.exitCode)}) ===\n${build.output}`);
  const logPath = ctx.state.writeLog("build", logParts.join("\n"));
  const durationMs = Date.now() - started;
  const secs = (durationMs / 1000).toFixed(1);

  if (build.spawnError !== undefined) {
    const summary = `BUILD FAILED (${p.config}, cmake 실행 불가)`;
    ctx.state.recordStatus("build", statusEntry(false, summary, p.config, durationMs, logPath));
    return errorResult(
      `${summary}\ncmake 실행 실패: ${build.spawnError}\n다음 행동: cmake 가 PATH 에 있는지 확인하세요.\n로그: ${logPath}`,
    );
  }

  const diag = parseMsvcDiagnostics(build.output);
  const ok = !build.timedOut && build.exitCode === 0;
  const summary = `BUILD ${ok ? "OK" : "FAILED"} (${p.config}${p.target !== undefined ? `, target ${p.target}` : ""}, ${secs}s, errors ${diag.errors.length}, warnings ${diag.warnings.length})`;

  const body: string[] = [summary];
  if (didConfigure) body.push(`자동 configure 수행: cmake -S . -B ${ctx.buildDirRel} -G "${CMAKE_GENERATOR}" -A x64 → 성공`);
  if (build.timedOut) body.push(`타임아웃(${p.timeoutSec}s) 초과 — 프로세스 트리를 강제 종료했습니다. timeoutSec 을 늘려 재시도하세요.`);
  const diagText = formatDiagnostics(diag);
  if (diagText !== "") body.push(diagText);
  if (!ok && diag.errors.length === 0 && diag.cmakeErrors.length === 0) {
    body.push(`진단을 찾지 못했습니다(exit ${String(build.exitCode)}). 출력 꼬리:`);
    body.push(selectTailWithPriority(build.output.split(/\r?\n/), 15).join("\n"));
  }
  body.push(`로그: ${logPath} (전체는 engine_logs source="build")`);

  ctx.state.recordStatus("build", statusEntry(ok, summary, p.config, durationMs, logPath, { didConfigure }));
  return ok ? textResult(body.join("\n")) : errorResult(body.join("\n"));
}

function statusEntry(
  ok: boolean,
  summary: string,
  config: string,
  durationMs: number,
  logPath: string,
  extra?: Record<string, unknown>,
): { at: string; ok: boolean; summary: string; config: string; durationMs: number; logPath: string; extra?: Record<string, unknown> } {
  const e: {
    at: string;
    ok: boolean;
    summary: string;
    config: string;
    durationMs: number;
    logPath: string;
    extra?: Record<string, unknown>;
  } = { at: new Date().toISOString(), ok, summary, config, durationMs, logPath };
  if (extra !== undefined) e.extra = extra;
  return e;
}
