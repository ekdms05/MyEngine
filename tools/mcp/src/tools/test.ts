/**
 * engine_test — ctest 실행·요약. 자동 빌드는 하지 않는다(부재 시 engine_build 안내).
 */
import fs from "node:fs";
import path from "node:path";
import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import { runProcess } from "../proc.js";
import { parseCtestOutput } from "../summarize.js";
import { type ServerContext, textResult, errorResult, caughtResult } from "../state.js";

interface TestParams {
  config: "Debug" | "Release";
  filter?: string | undefined;
  timeoutSec: number;
}

export function registerTestTool(server: McpServer, ctx: ServerContext): void {
  server.registerTool(
    "engine_test",
    {
      description:
        "엔진 테스트를 실행한다(ctest --output-on-failure). 자동 빌드는 하지 않는다 — " +
        "빌드 산출물이 없으면 engine_build 를 먼저 실행하라. 전체 로그는 engine_logs(source=\"test\")로.",
      inputSchema: {
        config: z.enum(["Debug", "Release"]).default("Debug").describe("빌드 구성"),
        filter: z.string().optional().describe("ctest -R 정규식(테스트 이름 필터)"),
        timeoutSec: z.number().int().min(1).max(1800).default(120).describe("타임아웃(초)"),
      },
    },
    async (p): Promise<CallToolResult> => {
      try {
        return await ctx.gate.run("engine_test", () => doTest(ctx, p as TestParams));
      } catch (e) {
        return caughtResult(e);
      }
    },
  );
}

async function doTest(ctx: ServerContext, p: TestParams): Promise<CallToolResult> {
  const cachePath = path.join(ctx.buildDir, "CMakeCache.txt");
  if (!fs.existsSync(cachePath)) {
    return errorResult(
      `빌드가 구성되지 않았습니다(${ctx.buildDirRel}/CMakeCache.txt 없음).\n` +
        `다음 행동: engine_build 를 먼저 실행하세요.`,
    );
  }

  const args = ["--test-dir", ctx.buildDirRel, "-C", p.config, "--output-on-failure"];
  if (p.filter !== undefined && p.filter !== "") args.push("-R", p.filter);

  const started = Date.now();
  const res = await runProcess({ command: "ctest", args, cwd: ctx.root, timeoutMs: p.timeoutSec * 1000 });

  if (res.spawnError !== undefined) {
    return errorResult(
      `ctest 실행 실패: ${res.spawnError}\n다음 행동: CMake(ctest 포함)가 PATH 에 있는지 확인하세요.`,
    );
  }

  const logPath = ctx.state.writeLog("test", `=== ${res.command} (exit ${String(res.exitCode)}) ===\n${res.output}`);
  const durationMs = Date.now() - started;
  const sum = parseCtestOutput(res.output);
  const secs = (sum.elapsedSec ?? durationMs / 1000).toFixed(1);

  if (sum.noTestsFound || (sum.parsed && sum.total === 0)) {
    const summary = `TESTS 없음 (${p.config}${p.filter !== undefined ? `, filter "${p.filter}"` : ""})`;
    ctx.state.recordStatus("test", { at: new Date().toISOString(), ok: false, summary, config: p.config, durationMs, logPath });
    return errorResult(
      [
        summary,
        p.filter !== undefined
          ? "필터에 일치하는 테스트가 없습니다. filter 를 확인하거나 생략해 보세요."
          : "테스트 실행 파일이 없습니다.",
        "다음 행동: engine_build 를 먼저 실행해 테스트 타깃을 빌드하세요.",
        `로그: ${logPath}`,
      ].join("\n"),
    );
  }

  if (!sum.parsed) {
    const summary = `TESTS 실행 불가 (${p.config}, exit ${String(res.exitCode)}${res.timedOut ? ", 타임아웃" : ""})`;
    ctx.state.recordStatus("test", { at: new Date().toISOString(), ok: false, summary, config: p.config, durationMs, logPath });
    const tail = res.output.split(/\r?\n/).filter((l) => l.trim() !== "").slice(-20).join("\n");
    return errorResult(
      [
        summary,
        "ctest 총계를 파싱하지 못했습니다. 출력 꼬리:",
        tail,
        res.timedOut ? `타임아웃(${p.timeoutSec}s) — timeoutSec 을 늘리거나 filter 로 범위를 좁히세요.` : "다음 행동: engine_build 로 빌드 상태를 확인하세요.",
        `로그: ${logPath} (전체는 engine_logs source="test")`,
      ].join("\n"),
    );
  }

  const ok = sum.failed === 0 && !res.timedOut;
  const summary = `TESTS passed ${sum.passed} / failed ${sum.failed} / total ${sum.total} (${secs}s)`;
  const body: string[] = [summary];
  if (res.timedOut) body.push(`타임아웃(${p.timeoutSec}s) 초과 — 결과가 불완전할 수 있습니다.`);
  for (const f of sum.failedTests) {
    body.push(`--- FAILED: ${f.name} (${f.reason}) ---`);
    if (f.tail.length > 0) body.push(f.tail.join("\n"));
  }
  body.push(`로그: ${logPath} (전체는 engine_logs source="test")`);

  ctx.state.recordStatus("test", {
    at: new Date().toISOString(),
    ok,
    summary,
    config: p.config,
    durationMs,
    logPath,
  });
  return ok ? textResult(body.join("\n")) : errorResult(body.join("\n"));
}
