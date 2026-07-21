/**
 * state.ts — .state/ 로그·캡처·상태 파일 관리 + 서버 전역 직렬화 뮤텍스 + 결과 헬퍼.
 *
 * 규약(docs/08-mcp.md):
 * - 빌드·테스트·실행·캡처는 서버 전역 뮤텍스 1개로 직렬화. 진행 중이면 대기하지 않고
 *   즉시 "다른 작업 진행 중(무엇이, 언제부터)" 에러.
 * - 각 툴의 결과는 .state/status.json 에 기록(project_status 의 데이터원).
 * - 반환 텍스트 상한 약 8KB — 원문은 .state/logs/<종류>-<timestamp>.log.
 */
import fs from "node:fs";
import path from "node:path";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";

export type LogKind = "build" | "test" | "run" | "capture";

/** 반환 텍스트 상한(대략 8KB). */
export const TEXT_LIMIT = 8 * 1024;

// ---------------------------------------------------------------------------
// 전역 직렬화 뮤텍스
// ---------------------------------------------------------------------------

export class BusyError extends Error {
  constructor(
    public readonly what: string,
    public readonly since: string,
  ) {
    super(`다른 작업 진행 중: ${what} (${since} 시작). 완료를 기다렸다가 다시 시도하세요.`);
    this.name = "BusyError";
  }
}

export class SerialGate {
  private current: { what: string; since: string } | null = null;

  /** 사용 중이면 대기 없이 BusyError. */
  async run<T>(what: string, fn: () => Promise<T>): Promise<T> {
    if (this.current !== null) {
      throw new BusyError(this.current.what, this.current.since);
    }
    this.current = { what, since: new Date().toISOString() };
    try {
      return await fn();
    } finally {
      this.current = null;
    }
  }
}

// ---------------------------------------------------------------------------
// .state/ 관리
// ---------------------------------------------------------------------------

export interface StatusEntry {
  at: string; // ISO 시각
  ok: boolean;
  summary: string; // 1행 요약
  config?: string;
  durationMs?: number;
  logPath?: string;
  extra?: Record<string, unknown>;
}

export type StatusFile = Partial<Record<LogKind, StatusEntry>>;

export class StateManager {
  readonly stateDir: string;
  readonly logsDir: string;
  readonly capturesDir: string;
  readonly statusFile: string;

  constructor(mcpDir: string) {
    this.stateDir = path.join(mcpDir, ".state");
    this.logsDir = path.join(this.stateDir, "logs");
    this.capturesDir = path.join(this.stateDir, "captures");
    this.statusFile = path.join(this.stateDir, "status.json");
  }

  ensureDirs(): void {
    fs.mkdirSync(this.logsDir, { recursive: true });
    fs.mkdirSync(this.capturesDir, { recursive: true });
  }

  /** 파일명에 안전한 타임스탬프(사전순 = 시간순). */
  timestamp(): string {
    return new Date().toISOString().replace(/[:.]/g, "-");
  }

  /** 세션 로그 저장 → 절대 경로 반환. */
  writeLog(kind: LogKind, content: string): string {
    this.ensureDirs();
    const file = path.join(this.logsDir, `${kind}-${this.timestamp()}.log`);
    fs.writeFileSync(file, content, "utf8");
    return file;
  }

  /** 해당 종류의 최신 로그 경로(없으면 null). 타임스탬프 파일명 사전순 최댓값. */
  latestLog(kind: LogKind): string | null {
    if (!fs.existsSync(this.logsDir)) return null;
    const files = fs
      .readdirSync(this.logsDir)
      .filter((f) => f.startsWith(`${kind}-`) && f.endsWith(".log"))
      .sort();
    const last = files[files.length - 1];
    return last !== undefined ? path.join(this.logsDir, last) : null;
  }

  readStatus(): StatusFile {
    try {
      return JSON.parse(fs.readFileSync(this.statusFile, "utf8")) as StatusFile;
    } catch {
      return {};
    }
  }

  recordStatus(kind: LogKind, entry: StatusEntry): void {
    try {
      this.ensureDirs();
      const all = this.readStatus();
      all[kind] = entry;
      fs.writeFileSync(this.statusFile, JSON.stringify(all, null, 2), "utf8");
    } catch {
      // 상태 기록 실패가 툴 실패로 번지지 않게 한다.
    }
  }

  newCapturePath(sample: string): string {
    this.ensureDirs();
    return path.join(this.capturesDir, `${sample}-${this.timestamp()}.png`);
  }

  /** 최근 캡처 PNG 목록(신규순, 최대 n). */
  recentCaptures(n: number): string[] {
    if (!fs.existsSync(this.capturesDir)) return [];
    return fs
      .readdirSync(this.capturesDir)
      .filter((f) => f.endsWith(".png"))
      .sort()
      .reverse()
      .slice(0, n)
      .map((f) => path.join(this.capturesDir, f));
  }
}

// ---------------------------------------------------------------------------
// 서버 컨텍스트
// ---------------------------------------------------------------------------

export interface ServerContext {
  /** 리포 루트 절대 경로. */
  root: string;
  /** tools/mcp 절대 경로. */
  mcpDir: string;
  /** 빌드 디렉터리(루트 상대, 기본 "build"). */
  buildDirRel: string;
  /** 빌드 디렉터리 절대 경로. */
  buildDir: string;
  version: string;
  state: StateManager;
  gate: SerialGate;
}

// ---------------------------------------------------------------------------
// CallToolResult 헬퍼 — "실패의 우아함" 규약
// ---------------------------------------------------------------------------

/** 텍스트를 상한으로 자른다. keep="tail" 이면 끝부분을 보존(로그 꼬리용). */
export function clampText(text: string, limit: number = TEXT_LIMIT, keep: "head" | "tail" = "head"): string {
  if (text.length <= limit) return text;
  if (keep === "tail") {
    return `[... 앞 ${text.length - limit}자 생략 ...]\n` + text.slice(text.length - limit);
  }
  return text.slice(0, limit) + `\n[... 뒤 ${text.length - limit}자 생략 — 전체는 로그 파일 참조 ...]`;
}

export function textResult(text: string): CallToolResult {
  return { content: [{ type: "text", text: clampText(text) }] };
}

export function errorResult(text: string): CallToolResult {
  return { content: [{ type: "text", text: clampText(text) }], isError: true };
}

/**
 * 예외 → isError 결과. BusyError 는 규약 메시지 그대로, 그 외에는 원인 + 다음 행동 제안.
 * 스택 트레이스를 그대로 던지지 않는다.
 */
export function caughtResult(e: unknown, hint?: string): CallToolResult {
  if (e instanceof BusyError) return errorResult(e.message);
  const msg = e instanceof Error ? e.message : String(e);
  return errorResult(hint !== undefined ? `${msg}\n다음 행동: ${hint}` : msg);
}
