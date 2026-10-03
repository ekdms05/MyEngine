import fs from "node:fs";
import path from "node:path";
import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { resolveExistingInRoot, resolveInRoot } from "../root.js";
import { readProject } from "./project.js";
import { findTargetExe, missingExeError } from "./run.js";
import { runProcess } from "../proc.js";
import { type ServerContext, textResult, errorResult, caughtResult } from "../state.js";

export function registerAssetImportTool(server: McpServer, ctx: ServerContext): void {
  server.registerTool("asset_import", {
    description: "MyEditor의 기존 에셋 임포트 경계로 PNG/애니메이션/Lua/저장한 .ui 문서를 프로젝트 assets/에 복사·등록한다. " +
      "설정된 엔진/프로젝트 루트의 파일만 허용하며 기존 파일 덮어쓰기와 GUI 메모리 원격 편집은 지원하지 않는다. GUID/.meta는 엔진이 생성한다.",
    annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: false },
    inputSchema: {
      project: z.string().min(1).describe("MYE_PROJECT_ROOT 상대 .myeproj(기본 MYE_ROOT)"),
      source: z.string().min(1).describe("sourceScope 루트 상대 원본 파일"),
      sourceScope: z.enum(["engine", "project"]).default("engine").describe("원본의 허용 루트. 외부 프로젝트 제작 파일은 project"),
      destination: z.string().min(1).describe("프로젝트 assets/ 상대 경로. 대상 폴더는 미리 생성(예: sprites/player.png)"),
      config: z.enum(["Debug", "Release"]).default("Debug"),
    },
  }, async (p) => {
    try {
      return await ctx.gate.run("asset_import", async () => {
        const project = readProject(ctx, p.project);
        const source = resolveExistingInRoot(p.sourceScope === "project" ? ctx.projectRoot ?? ctx.root : ctx.root, p.source);
        if (!fs.statSync(source).isFile()) throw new Error("원본은 일반 파일이어야 합니다");
        const assets = resolveExistingInRoot(project.directory, "assets");
        const destination = resolveInRoot(assets, p.destination);
        if (fs.existsSync(destination) || fs.existsSync(destination + ".meta")) throw new Error("대상 또는 .meta가 이미 존재합니다");
        const directory = path.dirname(destination);
        if (!fs.existsSync(directory) || !fs.statSync(directory).isDirectory())
          throw new Error("대상 폴더를 에셋 브라우저에서 먼저 생성하세요");
        resolveExistingInRoot(assets, path.relative(assets, directory));
        const exe = findTargetExe(ctx, "MyEditor", p.config);
        if (!exe) return missingExeError(ctx, "MyEditor", p.config);
        const result = await runProcess({ command: exe, args: ["--project", project.file, "--import-asset", source,
          "--asset-destination", p.destination, "--headless", "--frames", "1"], cwd: ctx.root, timeoutMs: 30000 });
        const log = ctx.state.writeLog("run", result.output);
        if (result.exitCode !== 0 || result.timedOut || result.spawnError || !fs.existsSync(destination + ".meta"))
          return errorResult(`임포트 실패: exit=${result.exitCode} timeout=${result.timedOut}\n${result.output}\n로그: ${log}`);
        return textResult(`등록 완료: ${p.destination}\n${fs.readFileSync(destination + ".meta", "utf8")}\n로그: ${log}`);
      });
    } catch (e) { return caughtResult(e, "원본/대상 경로와 MyEditor 빌드를 확인하세요. 기존 에셋을 보존하려면 새 이름을 사용하세요"); }
  });
}
