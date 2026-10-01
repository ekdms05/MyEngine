import fs from "node:fs";
import path from "node:path";
import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { resolveExistingInRoot, resolveInRoot } from "../root.js";
import { type ServerContext, textResult, caughtResult } from "../state.js";

const projectSchema = z.object({ version: z.literal(1), name: z.string().min(1), mainScene: z.string() });
const sceneSchema = z.object({
  __version: z.literal(1),
  entities: z.array(z.object({
    id: z.number().int().min(1).max(0xffffffff),
    parent: z.number().int().min(0).max(0xffffffff).optional(),
    components: z.record(z.record(z.unknown())),
  })),
});

export function readJson(file: string): unknown {
  if (!fs.statSync(file).isFile() || fs.statSync(file).size > 32 * 1024 * 1024)
    throw new Error("JSON 파일은 32 MiB 이하의 일반 파일이어야 합니다");
  return JSON.parse(fs.readFileSync(file, "utf8").replace(/^\uFEFF/, "")) as unknown;
}

export function readProject(ctx: ServerContext, relative: string) {
  const file = resolveExistingInRoot(ctx.projectRoot ?? ctx.root, relative);
  if (path.extname(file) !== ".myeproj") throw new Error(".myeproj 파일을 선택하세요");
  return { file, directory: path.dirname(file), metadata: projectSchema.parse(readJson(file)) };
}

export function registerProjectTools(server: McpServer, ctx: ServerContext): void {
  server.registerTool("project_inspect", {
    description: "프로젝트·씬의 이름/로컬 ID/부모/컴포넌트와 에셋 GUID를 읽기 전용으로 조회한다. " +
      "프로젝트·씬 버전과 계층을 검사하며, 컴포넌트 타입/값의 최종 검증은 엔진 로더가 수행한다. 에디터 메모리 편집은 하지 않는다.",
    annotations: { readOnlyHint: true, destructiveHint: false },
    inputSchema: {
      project: z.string().min(1).describe("MYE_PROJECT_ROOT 상대 .myeproj 경로(기본 MYE_ROOT)"),
      scene: z.string().optional().describe("프로젝트 상대 .scene 경로(생략하면 mainScene)"),
      section: z.enum(["scene", "assets"]).default("scene"),
      filter: z.string().max(128).default("").describe("오브젝트 이름 또는 에셋 경로 부분 검색"),
      offset: z.number().int().min(0).default(0),
      limit: z.number().int().min(1).max(50).default(20),
    },
  }, async (p) => {
    try {
      const project = readProject(ctx, p.project);
      const out = [`프로젝트: ${project.metadata.name} (version 1)`, `파일: ${p.project}`];
      const filter = p.filter.toLocaleLowerCase();
      if (p.section === "assets") {
        const assetsRoot = resolveExistingInRoot(project.directory, "assets");
        const files: string[] = [];
        const walk = (directory: string, depth: number): void => {
          if (depth > 32) throw new Error("에셋 탐색 상한(깊이 32)을 넘었습니다");
          for (const item of fs.readdirSync(directory, { withFileTypes: true })) {
            if (item.isSymbolicLink()) continue;
            const file = path.join(directory, item.name);
            if (item.isDirectory()) walk(file, depth + 1);
            else if (item.isFile() && !item.name.endsWith(".meta")) {
              if (files.length === 25000) throw new Error("에셋 탐색 상한(파일 25000)을 넘었습니다");
              files.push(file);
            }
          }
        };
        walk(assetsRoot, 0);
        const matches = files.map(file => path.relative(project.directory, file).replaceAll("\\", "/"))
          .filter(file => file.toLocaleLowerCase().includes(filter)).sort();
        out.push(`에셋: ${files.length}, 검색: ${matches.length}, offset: ${p.offset}`);
        for (const relative of matches.slice(p.offset, p.offset + p.limit)) {
          const metaFile = resolveInRoot(project.directory, relative + ".meta");
          const meta = fs.existsSync(metaFile)
            ? readJson(resolveExistingInRoot(project.directory, relative + ".meta")) : undefined;
          const guid = typeof meta === "object" && meta !== null && "guid" in meta ? String(meta.guid) : "미등록";
          out.push(`${relative} · GUID ${guid}`);
        }
      } else {
        const relative = p.scene ?? project.metadata.mainScene;
        if (!relative) return textResult(out.concat("시작 씬 없음: 하이어라키에서 씬을 만들고 저장하세요").join("\n"));
        const file = resolveExistingInRoot(project.directory, relative);
        if (path.extname(file) !== ".scene") throw new Error(".scene 파일을 선택하세요");
        const scene = sceneSchema.parse(readJson(file));
        const entities = new Map(scene.entities.map(entity => [entity.id, entity]));
        if (entities.size !== scene.entities.length) throw new Error("씬에 중복 로컬 ID가 있습니다");
        const visited = new Set<number>();
        for (const entity of scene.entities) {
          const chain = new Set<number>();
          for (let current = entity.id; current !== 0 && !visited.has(current);) {
            if (chain.has(current)) throw new Error("씬 부모 계층에 순환이 있습니다");
            const node = entities.get(current);
            if (!node) throw new Error(`씬 부모 ${current}가 존재하지 않습니다`);
            chain.add(current);
            current = node.parent ?? 0;
          }
          for (const id of chain) visited.add(id);
        }
        const nameOf = (entity: typeof scene.entities[number]): string => {
          const name = entity.components["ObjectName"]?.["value"];
          return typeof name === "string" ? name : `Object ${entity.id}`;
        };
        const matches = scene.entities.filter(entity => nameOf(entity).toLocaleLowerCase().includes(filter));
        out.push(`씬: ${relative} (version 1) · 오브젝트 ${scene.entities.length} · 검색 ${matches.length} · offset ${p.offset}`);
        for (const entity of matches.slice(p.offset, p.offset + p.limit)) {
          out.push(`ID ${entity.id} · ${nameOf(entity)} · 부모 ${entity.parent ?? 0} · ${Object.keys(entity.components).join(", ")}`);
          const values = JSON.stringify(entity.components);
          out.push(values.length <= 1200 ? values : `${values.slice(0, 1200)} … (긴 속성 생략: 씬 파일에서 확인)`);
        }
        out.push("구조 검사 완료. 컴포넌트 스키마·충돌·Lua 실행 검증은 MyEditor 로드/Play와 engine_test로 확인하세요.");
      }
      return textResult(out.join("\n"));
    } catch (e) { return caughtResult(e, "프로젝트 경로·파일 버전을 확인하고 offset/limit 또는 filter로 필요한 부분을 조회하세요"); }
  });
}
