/**
 * engine_logs — .state/logs/ 의 최신 세션 로그 조회(tail + level/정규식 필터).
 */
import fs from "node:fs";
import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import {
  type ServerContext,
  type LogKind,
  textResult,
  errorResult,
  caughtResult,
  TEXT_LIMIT,
} from "../state.js";

const LEVEL_ERROR_RE = /\b(error|fatal|fail(ed|ure)?|exception|assert(ion)?|abort)\b|(?:C|LNK|MSB)\d{3,5}\s*:/i;
const LEVEL_WARN_RE = /\bwarn(ing)?\b/i;

interface LogsParams {
  source: LogKind;
  lines: number;
  filter?: string | undefined;
  level: "all" | "warn" | "error";
}

export function registerLogsTool(server: McpServer, ctx: ServerContext): void {
  server.registerTool(
    "engine_logs",
    {
      description:
        "최근 빌드/테스트/실행/캡처 세션의 원본 로그를 조회한다(tail + 레벨/정규식 필터). " +
        "다른 툴이 요약만 반환할 때 전체 원문을 보는 용도.",
      inputSchema: {
        source: z.enum(["run", "build", "test", "capture"]).default("run").describe("로그 종류"),
        lines: z.number().int().min(1).max(2000).default(100).describe("tail 줄 수"),
        filter: z.string().optional().describe("정규식 필터(라인 단위)"),
        level: z
          .enum(["all", "warn", "error"])
          .default("all")
          .describe("all=전체, warn=경고 이상, error=에러 이상 라인만"),
      },
    },
    async (p): Promise<CallToolResult> => {
      try {
        return doLogs(ctx, p as LogsParams);
      } catch (e) {
        return caughtResult(e);
      }
    },
  );
}

function doLogs(ctx: ServerContext, p: LogsParams): CallToolResult {
  const logPath = ctx.state.latestLog(p.source);
  if (logPath === null) {
    return errorResult(
      `해당 종류의 실행 기록이 없습니다: source="${p.source}".\n` +
        `다음 행동: ${hintFor(p.source)} 를 먼저 실행하세요.`,
    );
  }

  let raw: string;
  try {
    raw = fs.readFileSync(logPath, "utf8");
  } catch (e) {
    return errorResult(`로그 파일을 읽지 못했습니다: ${logPath} (${e instanceof Error ? e.message : String(e)})`);
  }
  const allLines = raw.split(/\r?\n/);
  const totalLines = allLines.length;

  let filterRe: RegExp | null = null;
  if (p.filter !== undefined && p.filter !== "") {
    try {
      filterRe = new RegExp(p.filter, "i");
    } catch (e) {
      return errorResult(
        `filter 정규식이 유효하지 않습니다: ${e instanceof Error ? e.message : String(e)}\n` +
          `다음 행동: 정규식 문법을 확인하세요(예: "error C\\d+").`,
      );
    }
  }

  let matched = allLines;
  if (p.level === "error") {
    matched = matched.filter((l) => LEVEL_ERROR_RE.test(l));
  } else if (p.level === "warn") {
    matched = matched.filter((l) => LEVEL_ERROR_RE.test(l) || LEVEL_WARN_RE.test(l));
  }
  if (filterRe !== null) {
    const re = filterRe;
    matched = matched.filter((l) => re.test(l));
  }

  const isFiltered = p.level !== "all" || filterRe !== null;
  const matchedCount = matched.length;
  let tail = matched.slice(-p.lines);

  // 반환 상한(≈8KB): 헤더 여유분을 남기고 앞줄부터 버린다(최근 줄 보존).
  const budget = TEXT_LIMIT - 512;
  let dropped = 0;
  while (tail.length > 1 && tail.join("\n").length > budget) {
    tail = tail.slice(1);
    dropped++;
  }

  const header = [
    `로그: ${logPath}`,
    `파일 총 ${totalLines}줄` +
      (isFiltered ? ` · 필터 매치 ${matchedCount}줄` : "") +
      ` · 표시 ${tail.length}줄` +
      (dropped > 0 ? ` (상한으로 ${dropped}줄 추가 생략)` : ""),
    "---",
  ];
  return textResult(header.concat(tail.length > 0 ? tail : ["(매치되는 라인 없음)"]).join("\n"));
}

function hintFor(source: LogKind): string {
  switch (source) {
    case "build":
      return "engine_build";
    case "test":
      return "engine_test";
    case "capture":
      return "engine_capture_frame";
    default:
      return "engine_run";
  }
}
