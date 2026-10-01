/**
 * project_status — configure 상태·마지막 빌드/테스트/실행/캡처 요약·앱 목록·최근 캡처.
 * 항상 성공한다(부재 항목은 "없음" 표기).
 */
import fs from "node:fs";
import path from "node:path";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import { type ServerContext, type StatusEntry, textResult } from "../state.js";

export function registerStatusTool(server: McpServer, ctx: ServerContext): void {
  server.registerTool(
    "project_status",
    {
      description:
        "프로젝트 상태 요약: configure 상태(제너레이터), 마지막 빌드/테스트/실행/캡처 결과, " +
        "앱 소스 목록, 최근 캡처 파일, MCP 서버 버전.",
      inputSchema: {},
    },
    async (): Promise<CallToolResult> => doStatus(ctx),
  );
}

function doStatus(ctx: ServerContext): CallToolResult {
  const out: string[] = [];
  out.push(`MyEngine MCP 서버 v${ctx.version} (개발 도구)`);
  out.push(`리포 루트: ${ctx.root}`);
  out.push(`제작 루트: ${ctx.projectRoot ?? ctx.root}`);
  out.push(`빌드 디렉터리: ${ctx.buildDirRel}${process.env["MYE_BUILD_DIR"] ? " (MYE_BUILD_DIR)" : " (기본)"}`);
  out.push("");

  // configure 상태
  const cachePath = path.join(ctx.buildDir, "CMakeCache.txt");
  if (fs.existsSync(cachePath)) {
    let generator = "(파싱 실패)";
    let platform = "";
    try {
      const cache = fs.readFileSync(cachePath, "utf8");
      const g = /^CMAKE_GENERATOR:INTERNAL=(.*)$/m.exec(cache);
      if (g !== null) generator = (g[1] ?? "").trim();
      const p = /^CMAKE_GENERATOR_PLATFORM:INTERNAL=(.*)$/m.exec(cache);
      if (p !== null && (p[1] ?? "").trim() !== "") platform = ` (${(p[1] ?? "").trim()})`;
    } catch {
      /* 무시 */
    }
    out.push(`configure: 완료 — 제너레이터 "${generator}"${platform}`);
  } else {
    out.push(`configure: 안 됨 (${ctx.buildDirRel}/CMakeCache.txt 없음) — engine_build가 CMake 기본 제너레이터/x64로 configure 합니다`);
  }
  out.push("");

  // 마지막 작업 요약
  const status = ctx.state.readStatus();
  out.push("최근 작업:");
  out.push(`  빌드:   ${formatEntry(status.build)}`);
  out.push(`  테스트: ${formatEntry(status.test)}`);
  out.push(`  실행:   ${formatEntry(status.run)}`);
  out.push(`  캡처:   ${formatEntry(status.capture)}`);
  out.push("");

  // 앱 소스 목록은 실행 산출물이나 프로젝트 연결 상태를 뜻하지 않는다.
  const appsDir = path.join(ctx.root, "apps");
  let apps: string[] = [];
  try {
    apps = fs.readdirSync(appsDir, { withFileTypes: true })
      .filter(entry => entry.isDirectory() && fs.existsSync(path.join(appsDir, entry.name, "CMakeLists.txt")))
      .map(entry => entry.name).sort();
  } catch {
    /* 앱 소스가 없는 fixture도 상태 조회를 지원한다. */
  }
  out.push(`앱 소스 (apps/): ${apps.length > 0 ? apps.join(", ") : "없음"}`);

  // 최근 캡처
  const captures = ctx.state.recentCaptures(5);
  if (captures.length > 0) {
    out.push("최근 캡처:");
    for (const c of captures) out.push(`  ${c}`);
  } else {
    out.push("최근 캡처: 없음");
  }

  return textResult(out.join("\n"));
}

function formatEntry(e: StatusEntry | undefined): string {
  if (e === undefined) return "없음";
  const cfg = e.config !== undefined ? `, ${e.config}` : "";
  const dur = e.durationMs !== undefined ? `, ${(e.durationMs / 1000).toFixed(1)}s` : "";
  return `${e.ok ? "OK" : "FAILED"} — ${e.summary} (${e.at}${cfg}${dur})`;
}
