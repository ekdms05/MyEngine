/**
 * index.ts — MyEngine 개발도구 MCP 서버 엔트리 (Stage A).
 *
 * 규약(docs/08-mcp.md):
 * - McpServer + StdioServerTransport, 툴 일괄 등록(파일 1개 = 툴 1개 컨벤션).
 * - stdout 은 MCP 프로토콜 전용 — 서버 자체 로그는 stderr 로만 낸다(console.log 금지).
 * - 리포 루트: MYE_ROOT 환경변수 우선, 기본 tools/mcp/../.. — 빌드 디렉터리: MYE_BUILD_DIR(기본 "build").
 */
import fs from "node:fs";
import path from "node:path";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { mcpPackageDir, resolveRepoRoot, resolveBuildDirRel, resolveInRoot } from "./root.js";
import { StateManager, SerialGate, type ServerContext } from "./state.js";
import { registerBuildTool } from "./tools/build.js";
import { registerTestTool } from "./tools/test.js";
import { registerRunTool } from "./tools/run.js";
import { registerCaptureTool } from "./tools/capture.js";
import { registerLogsTool } from "./tools/logs.js";
import { registerStatusTool } from "./tools/status.js";

function readVersion(mcpDir: string): string {
  try {
    const pkg = JSON.parse(fs.readFileSync(path.join(mcpDir, "package.json"), "utf8")) as {
      version?: unknown;
    };
    return typeof pkg.version === "string" ? pkg.version : "0.0.0";
  } catch {
    return "0.0.0";
  }
}

async function main(): Promise<void> {
  const mcpDir = mcpPackageDir();
  const root = resolveRepoRoot();
  const buildDirRel = resolveBuildDirRel(root);
  const buildDir = resolveInRoot(root, buildDirRel);
  const version = readVersion(mcpDir);

  const ctx: ServerContext = {
    root,
    mcpDir,
    buildDirRel,
    buildDir,
    version,
    state: new StateManager(mcpDir),
    gate: new SerialGate(),
  };

  const server = new McpServer({ name: "myengine", version });
  registerBuildTool(server, ctx);
  registerTestTool(server, ctx);
  registerRunTool(server, ctx);
  registerCaptureTool(server, ctx);
  registerLogsTool(server, ctx);
  registerStatusTool(server, ctx);

  await server.connect(new StdioServerTransport());
  // stderr 전용 — stdout 오염 = 프로토콜 파손.
  console.error(`[myengine-mcp] v${version} ready — root=${root} · buildDir=${buildDirRel}`);
}

main().catch((e: unknown) => {
  console.error("[myengine-mcp] fatal:", e instanceof Error ? e.message : String(e));
  process.exit(1);
});
