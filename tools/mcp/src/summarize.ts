/**
 * summarize.ts — MSVC/CMake/ctest 출력 파서(토큰 절약의 핵심).
 *
 * 규약(docs/08-mcp.md "빌드 요약 원칙"):
 * - MSVC 진단 정규식: ^\s*(파일)\((줄)(,열)?\)\s*: (fatal )?(error|warning) (C\d+|LNK\d+|MSB\d+): (메시지)
 *   + "CMake Error at" 블록.
 * - 동일 (파일,줄,코드) 중복 제거(헤더 에러가 TU마다 반복되는 문제).
 * - 반환은 에러 최대 30건·경고 최대 10건 + 총계.
 */

export interface Diag {
  file: string;
  line: number;
  col?: number;
  level: "error" | "warning";
  code: string;
  message: string;
  fatal: boolean;
}

export interface BuildDiagnostics {
  errors: Diag[];
  warnings: Diag[];
  cmakeErrors: string[];
}

/** 파일(줄[,열]) : [fatal] error|warning CODE: 메시지 */
const MSVC_RE =
  /^\s*(.+?)\((\d+)(?:,(\d+))?\)\s*:\s*(fatal\s+)?(error|warning)\s+((?:C|LNK|MSB|RC|D)\d+)\s*:\s*(.*)$/;

/** 링커 등 줄 번호 없는 형태: foo.obj : error LNK2019: ... */
const NOLINE_RE = /^\s*(.+?)\s*:\s*(fatal\s+)?(error|warning)\s+((?:C|LNK|MSB|RC|D)\d+)\s*:\s*(.*)$/;

export function parseMsvcDiagnostics(text: string): BuildDiagnostics {
  const errors: Diag[] = [];
  const warnings: Diag[] = [];
  const cmakeErrors: string[] = [];
  const seen = new Set<string>();

  const lines = text.split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i] ?? "";

    // CMake Error 블록: 해당 줄 + 이어지는 비어있지 않은 줄(최대 8줄)
    if (/^CMake Error(\s|:)/.test(line.trim())) {
      const block: string[] = [line.trim()];
      let j = i + 1;
      while (j < lines.length && (lines[j] ?? "").trim() !== "" && block.length < 8) {
        block.push((lines[j] ?? "").trim());
        j++;
      }
      if (cmakeErrors.length < 10) cmakeErrors.push(block.join(" | "));
      i = j - 1;
      continue;
    }

    let d: Diag | null = null;
    const m = MSVC_RE.exec(line);
    if (m !== null) {
      d = {
        file: (m[1] ?? "").trim(),
        line: Number(m[2] ?? "0"),
        level: (m[5] ?? "error") as "error" | "warning",
        code: m[6] ?? "",
        message: (m[7] ?? "").trim(),
        fatal: m[4] !== undefined,
      };
      if (m[3] !== undefined) d.col = Number(m[3]);
    } else {
      const n = NOLINE_RE.exec(line);
      if (n !== null) {
        d = {
          file: (n[1] ?? "").trim(),
          line: 0,
          level: (n[3] ?? "error") as "error" | "warning",
          code: n[4] ?? "",
          message: (n[5] ?? "").trim(),
          fatal: n[2] !== undefined,
        };
      }
    }
    if (d === null) continue;

    // MSBuild 가 붙이는 " [프로젝트.vcxproj]" 꼬리 제거
    d.message = d.message.replace(/\s*\[[^\]]+\.(?:vcxproj|proj)\]\s*$/, "");

    const key = `${d.file.toLowerCase()}|${d.line}|${d.code}`;
    if (seen.has(key)) continue;
    seen.add(key);
    (d.level === "error" ? errors : warnings).push(d);
  }

  return { errors, warnings, cmakeErrors };
}

export function formatDiag(d: Diag): string {
  const loc = d.line > 0 ? `${d.file}(${d.line})` : d.file;
  return `${loc}: ${d.fatal ? "fatal " : ""}${d.level} ${d.code}: ${d.message}`;
}

/** 에러 ≤30·경고 ≤10 + 총계 형식의 진단 본문을 만든다. */
export function formatDiagnostics(diag: BuildDiagnostics): string {
  const out: string[] = [];
  if (diag.cmakeErrors.length > 0) {
    out.push("CMake 에러:");
    for (const c of diag.cmakeErrors) out.push(`  ${c}`);
  }
  if (diag.errors.length > 0) {
    out.push(`에러 ${diag.errors.length}건${diag.errors.length > 30 ? " (상위 30건 표시)" : ""}:`);
    for (const d of diag.errors.slice(0, 30)) out.push(`  ${formatDiag(d)}`);
    if (diag.errors.length > 30) out.push(`  ... 외 ${diag.errors.length - 30}건`);
  }
  if (diag.warnings.length > 0) {
    out.push(`경고 ${diag.warnings.length}건${diag.warnings.length > 10 ? " (상위 10건 표시)" : ""}:`);
    for (const d of diag.warnings.slice(0, 10)) out.push(`  ${formatDiag(d)}`);
    if (diag.warnings.length > 10) out.push(`  ... 외 ${diag.warnings.length - 10}건`);
  }
  return out.join("\n");
}

// ---------------------------------------------------------------------------
// ctest 파서
// ---------------------------------------------------------------------------

export interface FailedTest {
  name: string;
  reason: string;
  tail: string[]; // 실패 테스트 출력 꼬리 (≤50줄)
}

export interface CtestSummary {
  parsed: boolean; // 총계 라인을 찾았는가
  total: number;
  passed: number;
  failed: number;
  elapsedSec?: number;
  noTestsFound: boolean;
  failedTests: FailedTest[];
}

export function parseCtestOutput(text: string): CtestSummary {
  const lines = text.split(/\r?\n/);
  const sum: CtestSummary = {
    parsed: false,
    total: 0,
    passed: 0,
    failed: 0,
    noTestsFound: /No tests were found/i.test(text),
    failedTests: [],
  };

  const totals = /(\d+)% tests passed,\s*(\d+) tests? failed out of (\d+)/.exec(text);
  if (totals !== null) {
    sum.parsed = true;
    sum.failed = Number(totals[2] ?? "0");
    sum.total = Number(totals[3] ?? "0");
    sum.passed = sum.total - sum.failed;
  }
  const elapsed = /Total Test time \(real\)\s*=\s*([\d.]+)\s*sec/.exec(text);
  if (elapsed !== null) sum.elapsedSec = Number(elapsed[1] ?? "0");

  // "The following tests FAILED:" 섹션에서 이름·사유 수집
  const failedNames: { name: string; reason: string }[] = [];
  const idx = lines.findIndex((l) => /The following tests FAILED:/.test(l));
  if (idx >= 0) {
    for (let i = idx + 1; i < lines.length; i++) {
      const m = /^\s*(\d+)\s*-\s*(.+?)\s*\(([^)]+)\)\s*$/.exec(lines[i] ?? "");
      if (m === null) break;
      failedNames.push({ name: m[2] ?? "", reason: m[3] ?? "" });
    }
  }

  // --output-on-failure: "N/M Test #k: name ...***Failed" 다음에 테스트 출력이 이어진다.
  for (const f of failedNames) {
    const tail: string[] = [];
    const startRe = new RegExp(`Test\\s+#\\d+:\\s*${escapeRegExp(f.name)}\\b.*\\*\\*\\*(Failed|Timeout|Exception)`);
    const start = lines.findIndex((l) => startRe.test(l));
    if (start >= 0) {
      for (let i = start + 1; i < lines.length; i++) {
        const l = lines[i] ?? "";
        if (
          /^\s*\d+\/\d+\s+Test\s+#\d+:/.test(l) ||
          /^\s*Start\s+\d+:/.test(l) ||
          /% tests passed/.test(l) ||
          /The following tests FAILED:/.test(l)
        ) {
          break;
        }
        tail.push(l);
      }
    }
    sum.failedTests.push({ name: f.name, reason: f.reason, tail: tail.slice(-50) });
  }

  return sum;
}

export function escapeRegExp(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

// ---------------------------------------------------------------------------
// 출력 꼬리 선별
// ---------------------------------------------------------------------------

const PRIORITY_RE = /\b(error|fatal|fail(ed|ure)?|exception|assert(ion)?|abort|crash)\b|\bwarn(ing)?\b/i;

/**
 * 출력 꼬리 ≤max 줄. 에러·경고 라인을 우선 선별하고 남는 자리를 마지막 줄들로 채운다.
 * 원래 순서를 유지하며, 생략 구간은 표식으로 알린다.
 */
export function selectTailWithPriority(allLines: string[], max: number): string[] {
  const lines = allLines.filter((l, i) => l.trim() !== "" || i === allLines.length - 1);
  if (lines.length <= max) return lines;

  const pickedIdx = new Set<number>();
  // 1) 우선 라인(에러·경고) — 오래된 것부터, 최대 절반까지
  const priorityBudget = Math.floor(max / 2);
  for (let i = 0; i < lines.length && pickedIdx.size < priorityBudget; i++) {
    if (PRIORITY_RE.test(lines[i] ?? "")) pickedIdx.add(i);
  }
  // 2) 나머지는 꼬리에서 채운다
  for (let i = lines.length - 1; i >= 0 && pickedIdx.size < max; i--) {
    pickedIdx.add(i);
  }
  const sorted = [...pickedIdx].sort((a, b) => a - b);
  const out: string[] = [];
  let prev = -1;
  for (const i of sorted) {
    if (prev >= 0 && i > prev + 1) out.push(`[... ${i - prev - 1}줄 생략 ...]`);
    out.push(lines[i] ?? "");
    prev = i;
  }
  return out;
}
