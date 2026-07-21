/**
 * root.ts — 리포 루트 해석과 경로 안전 검증.
 *
 * 규약(docs/08-mcp.md):
 * - 리포 루트는 서버 파일 위치 기준 ../../ (tools/mcp → E:/MyEngine).
 * - 환경변수 MYE_ROOT 로 오버라이드 가능.
 * - 모든 경로 파라미터는 루트 상대만 허용, '..' 탈출 거부.
 * - MYE_BUILD_DIR 로 빌드 디렉터리 오버라이드(기본 "build") — 사람(IDE) 빌드와
 *   AI 빌드를 분리하고 싶을 때 "build/dev" 등을 지정한다(08 오픈 이슈 #7).
 */
import path from "node:path";
import { fileURLToPath } from "node:url";

/** tools/mcp 패키지 디렉터리(컴파일 산출물 dist/의 부모). */
export function mcpPackageDir(): string {
  const here = path.dirname(fileURLToPath(import.meta.url)); // tools/mcp/dist
  return path.resolve(here, "..");
}

/** 리포 루트: MYE_ROOT 환경변수 우선, 기본은 tools/mcp/../.. */
export function resolveRepoRoot(): string {
  const env = process.env["MYE_ROOT"];
  if (env && env.trim().length > 0) {
    return path.resolve(env.trim());
  }
  return path.resolve(mcpPackageDir(), "..", "..");
}

/** 빌드 디렉터리(루트 상대): MYE_BUILD_DIR 환경변수, 기본 "build". */
export function resolveBuildDirRel(root: string): string {
  const env = process.env["MYE_BUILD_DIR"];
  const rel = env && env.trim().length > 0 ? env.trim() : "build";
  // 검증(탈출 거부)만 수행하고 상대 경로 문자열을 그대로 유지한다.
  resolveInRoot(root, rel);
  return rel;
}

/**
 * 루트 상대 경로를 절대 경로로 해석한다. 절대 경로 입력·루트 밖 탈출('..')은 거부.
 */
export function resolveInRoot(root: string, rel: string): string {
  if (path.isAbsolute(rel)) {
    throw new Error(`절대 경로는 허용되지 않습니다(루트 상대 경로만): ${rel}`);
  }
  const normRoot = path.resolve(root);
  const abs = path.resolve(normRoot, rel);
  if (abs !== normRoot && !abs.startsWith(normRoot + path.sep)) {
    throw new Error(`리포 루트 밖 경로는 허용되지 않습니다: ${rel}`);
  }
  return abs;
}

/** 샘플·타깃 등 이름 파라미터의 안전 검증(경로 구분자·상위 이동 금지). */
export function assertSafeName(name: string, what: string): void {
  if (!/^[A-Za-z0-9_.\-]+$/.test(name) || name.includes("..")) {
    throw new Error(`${what} 이름이 유효하지 않습니다: "${name}" (영숫자·_-. 만 허용)`);
  }
}
