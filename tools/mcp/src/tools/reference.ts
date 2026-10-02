import fs from "node:fs";
import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { resolveExistingInRoot } from "../root.js";
import { type ServerContext, textResult, caughtResult } from "../state.js";

const references = {
  lua: "docs/19-lua-api.md",
  editor: "docs/07-editor-ui.md",
  assets: "docs/04-asset-pipeline.md",
  scene: "docs/03-scene-world.md",
  rendering: "docs/02-rendering.md",
  components: "docs/20-components.md",
  online: "docs/21-3d-play-and-online.md",
  online2d: "docs/23-2d-online-play.md",
  camera2d: "docs/24-2d-camera.md",
  input: "docs/25-input-actions.md",
  roadmap: "docs/22-2d-mmorpg-roadmap.md",
} as const;

export function registerReferenceTool(server: McpServer, ctx: ServerContext): void {
  server.registerTool("engine_reference", {
    description: "현재 MyEngine의 Lua API·컴포넌트·에디터·에셋·씬·좌표/깊이 계약을 저장소 문서에서 읽는다. " +
      "검색 또는 줄 범위로 필요한 근거만 조회하며 다른 엔진 API를 추측하지 않는다.",
    annotations: { readOnlyHint: true, destructiveHint: false },
    inputSchema: {
      topic: z.enum(["lua", "editor", "assets", "scene", "rendering", "components", "online", "online2d", "camera2d", "input", "roadmap"]),
      search: z.string().max(128).default(""),
      startLine: z.number().int().min(1).default(1),
      lines: z.number().int().min(1).max(120).default(60),
    },
  }, async (p) => {
    try {
      const file = references[p.topic];
      const all = fs.readFileSync(resolveExistingInRoot(ctx.root, file), "utf8").split(/\r?\n/);
      const search = p.search.toLocaleLowerCase();
      const rows = all.map((line, index) => ({ line, index }))
        .filter(row => search ? row.line.toLocaleLowerCase().includes(search) : row.index >= p.startLine - 1)
        .slice(0, p.lines);
      return textResult([`${file} · 총 ${all.length}줄`, ...rows.map(row => `${row.index + 1}: ${row.line}`)].join("\n"));
    } catch (e) { return caughtResult(e, "현재 저장소의 가이드 파일이 있는지 확인하세요"); }
  });
}
