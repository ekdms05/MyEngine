#!/usr/bin/env node
/**
 * smoke.mjs — dist 서버를 자식 프로세스로 띄워 JSON-RPC(stdio, 개행 구분) 스모크 테스트.
 *
 * 검증 항목(엔진 빌드는 절대 실행하지 않는다 — 격리 규칙):
 *  1. initialize 핸드셰이크(serverInfo.name === "myengine")
 *  2. tools/list — 9개 툴 전부 노출 + 각 inputSchema 존재
 *  3. project_status 호출 — 빌드 없이도 정상 텍스트 응답(항상 성공 규약)
 *  4. engine_logs 호출 — 기록 부재 시 isError + "engine_run 먼저" 우아한 실패
 *  5. engine_run 호출 — 빌드 산출물 부재 시 isError + "engine_build 먼저" 우아한 실패
 *
 * MYE_BUILD_DIR=build/_smoke_mcp_none으로 실행하여 실제 build/dev를 건드리지 않는다.
 */
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url)); // tools/mcp/scripts
const mcpDir = path.resolve(here, "..");
const serverJs = path.join(mcpDir, "dist", "index.js");
const repoRoot = path.resolve(mcpDir, "..", "..");

if (!fs.existsSync(serverJs)) {
  console.error(`[smoke] dist/index.js 가 없습니다: ${serverJs} — 먼저 npm run build 를 실행하세요.`);
  process.exit(1);
}

const child = spawn(process.execPath, [serverJs], {
  cwd: mcpDir,
  env: { ...process.env, MYE_ROOT: repoRoot, MYE_PROJECT_ROOT: repoRoot, MYE_BUILD_DIR: "build/_smoke_mcp_none" },
  stdio: ["pipe", "pipe", "pipe"],
});

let stderrBuf = "";
child.stderr.on("data", (d) => {
  stderrBuf += d.toString();
});

// --- 개행 구분 JSON-RPC 클라이언트 -----------------------------------------
let stdoutBuf = "";
const pending = new Map(); // id -> resolve
child.stdout.on("data", (d) => {
  stdoutBuf += d.toString();
  let idx;
  while ((idx = stdoutBuf.indexOf("\n")) >= 0) {
    const line = stdoutBuf.slice(0, idx).trim();
    stdoutBuf = stdoutBuf.slice(idx + 1);
    if (line === "") continue;
    let msg;
    try {
      msg = JSON.parse(line);
    } catch {
      continue; // 프로토콜 외 라인은 무시
    }
    if (msg.id !== undefined && pending.has(msg.id)) {
      const resolve = pending.get(msg.id);
      pending.delete(msg.id);
      resolve(msg);
    }
  }
});

let nextId = 1;
function request(method, params, timeoutMs = 15000) {
  const id = nextId++;
  const promise = new Promise((resolve, reject) => {
    pending.set(id, resolve);
    setTimeout(() => {
      if (pending.has(id)) {
        pending.delete(id);
        reject(new Error(`응답 타임아웃: ${method} (id ${id})`));
      }
    }, timeoutMs);
  });
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
  return promise;
}
function notify(method, params) {
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method, ...(params ? { params } : {}) }) + "\n");
}

// --- 체크 헬퍼 --------------------------------------------------------------
let failures = 0;
function check(name, cond, detail = "") {
  const mark = cond ? "PASS" : "FAIL";
  if (!cond) failures++;
  console.log(`[smoke] ${mark}  ${name}${detail ? ` — ${detail}` : ""}`);
}
function firstText(result) {
  const c = (result?.content ?? []).find((x) => x.type === "text");
  return c?.text ?? "";
}

// --- 시나리오 ---------------------------------------------------------------
const EXPECTED_TOOLS = [
  "engine_build",
  "engine_test",
  "engine_run",
  "engine_capture_frame",
  "engine_logs",
  "project_status",
  "project_inspect",
  "engine_reference",
  "asset_import",
];

async function main() {
  const { resolveBuildDirRel } = await import("../dist/root.js");
  const previousBuildDir = process.env.MYE_BUILD_DIR;
  try {
    delete process.env.MYE_BUILD_DIR;
    check("기본 빌드 경로 — build/dev", resolveBuildDirRel(repoRoot) === "build/dev");
    process.env.MYE_BUILD_DIR = "build/_smoke_mcp_none";
    check("빌드 경로 override 유지", resolveBuildDirRel(repoRoot) === "build/_smoke_mcp_none");
  } finally {
    if (previousBuildDir === undefined) delete process.env.MYE_BUILD_DIR;
    else process.env.MYE_BUILD_DIR = previousBuildDir;
  }
  // 1) initialize
  const init = await request("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: { name: "myengine-smoke", version: "0.0.0" },
  });
  check(
    "initialize — serverInfo.name === 'myengine'",
    init.result?.serverInfo?.name === "myengine",
    JSON.stringify(init.result?.serverInfo),
  );
  notify("notifications/initialized");

  // 2) tools/list
  const list = await request("tools/list", {});
  const tools = list.result?.tools ?? [];
  const names = tools.map((t) => t.name).sort();
  check(
    `tools/list — 9개 툴 전부 노출(개발 도구 6 + 제작 도구 3)`,
    EXPECTED_TOOLS.every((n) => names.includes(n)) && names.length === EXPECTED_TOOLS.length,
    names.join(", "),
  );
  const badSchema = tools.filter((t) => !t.inputSchema || t.inputSchema.type !== "object");
  check(
    "tools/list — 전 툴 inputSchema(type=object) 존재",
    badSchema.length === 0,
    badSchema.map((t) => t.name).join(", ") || "OK",
  );
  const buildTool = tools.find((t) => t.name === "engine_build");
  check(
    "engine_build 스키마 — config enum(Debug/Release) 포함",
    JSON.stringify(buildTool?.inputSchema?.properties?.config ?? {}).includes("Debug"),
  );

  // 3) project_status — 항상 성공 규약(빌드 없이도)
  const status = await request("tools/call", { name: "project_status", arguments: {} });
  const statusText = firstText(status.result);
  check(
    "project_status — isError 아님 + 서버 버전·configure 상태 포함",
    status.result?.isError !== true &&
      statusText.includes("MyEngine MCP") &&
      statusText.includes("configure"),
    statusText.split("\n")[0],
  );
  check(
    "project_status — 격리 빌드 디렉터리를 '안 됨'으로 보고",
    statusText.includes("안 됨"),
  );

  // 4) engine_logs — 기록 부재의 우아한 실패
  const logs = await request("tools/call", { name: "engine_logs", arguments: { source: "run" } });
  const logsText = firstText(logs.result);
  const logsGraceful = logs.result?.isError === true && /engine_run|실행 기록/.test(logsText);
  const logsOk = logs.result?.isError !== true && logsText.length > 0; // 이전 스모크 기록이 있으면 정상 조회도 허용
  check("engine_logs — 크래시 없이 응답(기록 부재 시 안내 에러)", logsGraceful || logsOk, logsText.split("\n")[0]);

  // 5) engine_run — 빌드 산출물 부재의 우아한 실패 (빌드는 실행하지 않는다)
  const run = await request("tools/call", {
    name: "engine_run",
    arguments: { sample: "MyEditor", frames: 1, timeoutSec: 5 },
  });
  const runText = firstText(run.result);
  check(
    "engine_run — 산출물 부재 시 isError + engine_build 안내",
    run.result?.isError === true && /engine_build/.test(runText),
    runText.split("\n")[0],
  );
  const unsupportedTicks = await request("tools/call", {
    name: "engine_run", arguments: { sample: "MyEditor", ticks: 4 },
  });
  check("engine_run — 지원하지 않는 앱의 고정 틱 거부",
    unsupportedTicks.result?.isError === true && firstText(unsupportedTicks.result).includes("MyGame/MyServer"));

  // Real file parsing in an isolated build/ project; no user data or engine build.
  const online = await request("tools/call", { name: "engine_reference", arguments: { topic: "online", search: "PhysicsWorld3D" } });
  check("engine_reference — XYZ 공통 물리/온라인 정본", online.result?.isError !== true && firstText(online.result).includes("21-3d-play-and-online.md") && firstText(online.result).includes("PhysicsWorld3D::Step"));
  const roadmap = await request("tools/call", { name: "engine_reference", arguments: { topic: "roadmap", search: "D05" } });
  check("engine_reference — 2D 목표와 앱 연결 완료 조건", roadmap.result?.isError !== true && firstText(roadmap.result).includes("22-2d-mmorpg-roadmap.md") && firstText(roadmap.result).includes("두 계정"));
  const motion = await request("tools/call", { name: "engine_reference", arguments: { topic: "scene", search: "MoveAndSlide2D" } });
  check("engine_reference — 연속 2D 계산과 실제 오류 전달 경로", motion.result?.isError !== true && firstText(motion.result).includes("MotionResult2D") && firstText(motion.result).includes("CastMotion2D") && firstText(motion.result).includes("ObjectSystem::Tick") && !firstText(motion.result).includes("PhysicsSystem.h"));
  const scene2D = await request("tools/call", { name: "engine_reference", arguments: { topic: "scene", search: "LoadOnlineScene2D" } });
  check("engine_reference — 2D 로더의 검증된 계약과 공유 추출", scene2D.result?.isError !== true && firstText(scene2D.result).includes("GatherCollisionBodies2D") && firstText(scene2D.result).includes("SceneSerializer"));
  const authority2D = await request("tools/call", { name: "engine_reference", arguments: { topic: "scene", search: "StepMotion2D" } });
  check("engine_reference — 인증 2D 계산과 공식 앱 소비", authority2D.result?.isError !== true && firstText(authority2D.result).includes("NetGameServer::Configure2D") && firstText(authority2D.result).includes("MyServer/MyGame"));
  const online2D = await request("tools/call", { name: "engine_reference", arguments: { topic: "online2d", search: "--input" } });
  check("engine_reference — 실제 2D 앱 입력 재생과 확인 경계", online2D.result?.isError !== true && firstText(online2D.result).includes("23-2d-online-play.md") && firstText(online2D.result).includes("고정 틱"));
  const camera2D = await request("tools/call", { name: "engine_reference", arguments: { topic: "camera2d", search: "followTarget" } });
  check("engine_reference — 저장 2D 카메라의 추종 계약", camera2D.result?.isError !== true && firstText(camera2D.result).includes("24-2d-camera.md") && firstText(camera2D.result).includes("월드 XY"));
  const cameraLua = await request("tools/call", { name: "engine_reference", arguments: { topic: "lua", search: "shake_camera" } });
  check("engine_reference — 실제 카메라 Entity Lua API", cameraLua.result?.isError !== true && firstText(cameraLua.result).includes("0~10") && firstText(cameraLua.result).includes("고정 틱"));
  const inputSettings = await request("tools/call", { name: "engine_reference", arguments: { topic: "input", search: "inputMap" } });
  check("engine_reference — 저장 입력 설정과 이전 프로젝트 호환", inputSettings.result?.isError !== true && firstText(inputSettings.result).includes("25-input-actions.md") && firstText(inputSettings.result).includes("기본 조작"));
  const inputLimits = await request("tools/call", { name: "engine_reference", arguments: { topic: "input", lines: 120 } });
  check("engine_reference — 사용자 액션 Lua와 온라인 권한 경계", inputLimits.result?.isError !== true && firstText(inputLimits.result).includes("is_action_just_pressed") && firstText(inputLimits.result).includes("클라이언트 Lua를 실행하지 않는다"));
  const inputLua = await request("tools/call", { name: "engine_reference", arguments: { topic: "lua", search: "입력 버퍼" } });
  check("engine_reference — 실제 고정 틱 액션과 종료 수명", inputLua.result?.isError !== true && firstText(inputLua.result).includes("on_destroy") && firstText(inputLua.result).includes("비소유"));
  const padInput = await request("tools/call", { name: "engine_reference", arguments: { topic: "input", search: "원시 패드 축" } });
  check("engine_reference — 원시 패드와 단일 작성 데드존", padInput.result?.isError !== true && firstText(padInput.result).includes("한 번 적용") && firstText(padInput.result).includes("겹쳐 적용하지 않는다"));
  const inputKeyboard = await request("tools/call", { name: "engine_reference", arguments: { topic: "input", search: "Enter", maxLines: 120 } });
  check("engine_reference — 입력 작성 키보드와 검수 경계", inputKeyboard.result?.isError !== true && firstText(inputKeyboard.result).includes("포커스별 Escape") && firstText(inputKeyboard.result).includes("실제 모니터 DPI"));
  const fixtureRel = `build/mcp-smoke-${Date.now()}`;
  const animation2D = await request("tools/call", { name: "engine_reference", arguments: { topic: "animation2d", lines: 120 } });
  check("engine_reference — 2D 모션의 공통 요청과 표시/틱 경계", animation2D.result?.isError !== true &&
    firstText(animation2D.result).includes("같은 이동 상태") && firstText(animation2D.result).includes("트리거") &&
    firstText(animation2D.result).includes("버전 2") && firstText(animation2D.result).includes("mirrorRight") &&
    firstText(animation2D.result).includes("진행률 보정은 다음 작업") && firstText(animation2D.result).includes("Play를 중단"));
  const fixture = path.join(repoRoot, fixtureRel);
  fs.mkdirSync(path.join(fixture, "assets", "scenes"), { recursive: true });
  fs.writeFileSync(path.join(fixture, "project.myeproj"), JSON.stringify({ version: 1, name: "Smoke", mainScene: "assets/scenes/main.scene" }));
  const sceneFile = path.join(fixture, "assets", "scenes", "main.scene");
  fs.writeFileSync(sceneFile, JSON.stringify({ __version: 1, entities: [
    { id: 1, components: { ObjectName: { __version: 1, value: "Player" } } },
    { id: 2, parent: 1, components: { ObjectName: { __version: 1, value: "Child" } } },
  ] }));
  const inspect = await request("tools/call", { name: "project_inspect", arguments: { project: `${fixtureRel}/project.myeproj`, filter: "Player" } });
  check("project_inspect — 실제 프로젝트/씬 파싱·필터", inspect.result?.isError !== true && firstText(inspect.result).includes("ID 1 · Player") && !firstText(inspect.result).includes("ID 2 · Child"));
  fs.writeFileSync(sceneFile, JSON.stringify({ __version: 1, entities: [
    { id: 1, parent: 2, components: {} }, { id: 2, parent: 1, components: {} },
  ] }));
  const cyclic = await request("tools/call", { name: "project_inspect", arguments: { project: `${fixtureRel}/project.myeproj` } });
  check("project_inspect — 순환 계층 거부", cyclic.result?.isError === true && firstText(cyclic.result).includes("순환"));
  const escaped = await request("tools/call", { name: "project_inspect", arguments: { project: "../project.myeproj" } });
  check("project_inspect — 경로 탈출 거부", escaped.result?.isError === true && firstText(escaped.result).includes("루트 밖"));
  const reference = await request("tools/call", { name: "engine_reference", arguments: { topic: "rendering", search: "48" } });
  check("engine_reference — 현재 렌더 계약 조회", reference.result?.isError !== true && firstText(reference.result).includes("48"));
  const asset = await request("tools/call", { name: "asset_import", arguments: {
    project: `${fixtureRel}/project.myeproj`, source: `${fixtureRel}/project.myeproj`, destination: "../escape.png",
  } });
  check("asset_import — assets 밖 쓰기 거부", asset.result?.isError === true && firstText(asset.result).includes("루트 밖"));

  const { resolveProjectRoot } = await import("../dist/root.js");
  const { readProject } = await import("../dist/tools/project.js");
  const engineScope = path.join(fixture, "engine");
  const projectScope = path.join(fixture, "external-game");
  fs.mkdirSync(engineScope); fs.mkdirSync(projectScope);
  fs.copyFileSync(path.join(fixture, "project.myeproj"), path.join(projectScope, "project.myeproj"));
  const previousProjectRoot = process.env.MYE_PROJECT_ROOT;
  try {
    process.env.MYE_PROJECT_ROOT = projectScope;
    const scoped = { root: engineScope, projectRoot: resolveProjectRoot(engineScope) };
    check("외부 제작 루트 — 엔진 밖 프로젝트 조회", readProject(scoped, "project.myeproj").metadata.name === "Smoke");
    let rejected = 0;
    for (const input of ["../project.myeproj", path.join(projectScope, "project.myeproj")]) {
      try { readProject(scoped, input); } catch { ++rejected; }
    }
    check("외부 제작 루트 — 상위 이동/절대 입력 거부", rejected === 2);
    const link = path.join(projectScope, "escape");
    fs.symlinkSync(fixture, link, process.platform === "win32" ? "junction" : "dir");
    try {
      let blocked = false;
      try { readProject(scoped, "escape/project.myeproj"); } catch { blocked = true; }
      check("외부 제작 루트 — junction/symlink 탈출 거부", blocked);
    } finally { fs.unlinkSync(link); }
    delete process.env.MYE_PROJECT_ROOT;
    check("제작 루트 미설정 — 기존 엔진 루트 유지", resolveProjectRoot(engineScope) === fs.realpathSync(engineScope));
  } finally {
    if (previousProjectRoot === undefined) delete process.env.MYE_PROJECT_ROOT;
    else process.env.MYE_PROJECT_ROOT = previousProjectRoot;
  }

  // 종료
  child.kill();
  console.log(`[smoke] ${failures === 0 ? "ALL PASS" : `${failures} FAILURE(S)`}`);
  if (stderrBuf.trim() !== "") {
    console.log(`[smoke] 서버 stderr: ${stderrBuf.trim().split("\n")[0]}`);
  }
  process.exit(failures === 0 ? 0 : 1);
}

main().catch((e) => {
  console.error(`[smoke] 실패: ${e instanceof Error ? e.message : String(e)}`);
  if (stderrBuf.trim() !== "") console.error(`[smoke] 서버 stderr:\n${stderrBuf}`);
  child.kill();
  process.exit(1);
});
