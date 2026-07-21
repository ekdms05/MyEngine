#!/usr/bin/env node
/**
 * smoke.mjs — dist 서버를 자식 프로세스로 띄워 JSON-RPC(stdio, 개행 구분) 스모크 테스트.
 *
 * 검증 항목(엔진 빌드는 절대 실행하지 않는다 — 격리 규칙):
 *  1. initialize 핸드셰이크(serverInfo.name === "myengine")
 *  2. tools/list — 6개 툴 전부 노출 + 각 inputSchema 존재
 *  3. project_status 호출 — 빌드 없이도 정상 텍스트 응답(항상 성공 규약)
 *  4. engine_logs 호출 — 기록 부재 시 isError + "engine_run 먼저" 우아한 실패
 *  5. engine_run 호출 — 빌드 산출물 부재 시 isError + "engine_build 먼저" 우아한 실패
 *
 * MYE_BUILD_DIR=build/dev 로 띄우므로(존재하지 않는 디렉터리) 다른 워크플로우가 쓰는
 * build/ 를 건드리지 않는다.
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
  env: { ...process.env, MYE_ROOT: repoRoot, MYE_BUILD_DIR: "build/dev" },
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
];

async function main() {
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
    `tools/list — 6개 툴 전부 노출`,
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
    "project_status — 미구성 빌드 디렉터리(build/dev)를 '안 됨'으로 보고",
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
    arguments: { sample: "hello_triangle", frames: 1, timeoutSec: 5 },
  });
  const runText = firstText(run.result);
  check(
    "engine_run — 산출물 부재 시 isError + engine_build 안내",
    run.result?.isError === true && /engine_build/.test(runText),
    runText.split("\n")[0],
  );

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
