/**
 * proc.ts — 자식 프로세스 spawn 공통.
 *
 * 규약(docs/08-mcp.md):
 * - 셸 미사용(인자 배열) — 인젝션 원천 차단. 임의 명령 실행 툴은 만들지 않는다.
 * - 모든 자식 프로세스에 타임아웃, 초과 시 taskkill /PID <pid> /T /F 로 프로세스 트리 정리.
 * - 비정상 종료 코드는 알려진 NTSTATUS 를 해석해 병기한다.
 */
import { spawn, spawnSync } from "node:child_process";

export interface RunOptions {
  command: string;
  args: string[];
  cwd: string;
  timeoutMs: number;
}

export interface RunResult {
  /** 표시용 커맨드 라인(공백 결합). */
  command: string;
  exitCode: number | null;
  timedOut: boolean;
  durationMs: number;
  /** stdout+stderr 병합(도착 순서). */
  output: string;
  /** spawn 자체가 실패한 경우(ENOENT 등)의 메시지. */
  spawnError?: string;
}

/** 출력 캡처 상한(메모리 보호). 초과분은 버리고 표식을 남긴다. */
const MAX_CAPTURE_BYTES = 32 * 1024 * 1024;

/** 프로세스 트리 강제 종료(자식 exe 가 빌드 파일 락을 잡는 사고 방지). */
export function killProcessTree(pid: number): void {
  if (process.platform === "win32") {
    try {
      spawnSync("taskkill", ["/PID", String(pid), "/T", "/F"], { windowsHide: true });
    } catch {
      /* 이미 종료된 경우 등 — 무시 */
    }
  } else {
    try {
      process.kill(pid, "SIGKILL");
    } catch {
      /* 무시 */
    }
  }
}

/** 셸 미사용 spawn + 타임아웃 + 트리 kill. 절대 reject 하지 않는다(결과로 보고). */
export function runProcess(opts: RunOptions): Promise<RunResult> {
  return new Promise((resolve) => {
    const started = Date.now();
    const display = [opts.command, ...opts.args].join(" ");

    let child;
    try {
      child = spawn(opts.command, opts.args, {
        cwd: opts.cwd,
        shell: false,
        windowsHide: true,
        stdio: ["ignore", "pipe", "pipe"],
      });
    } catch (e) {
      resolve({
        command: display,
        exitCode: null,
        timedOut: false,
        durationMs: Date.now() - started,
        output: "",
        spawnError: e instanceof Error ? e.message : String(e),
      });
      return;
    }

    const chunks: Buffer[] = [];
    let captured = 0;
    let truncated = false;
    const onData = (d: Buffer): void => {
      if (captured < MAX_CAPTURE_BYTES) {
        chunks.push(d);
        captured += d.length;
      } else {
        truncated = true;
      }
    };
    child.stdout?.on("data", onData);
    child.stderr?.on("data", onData);

    let timedOut = false;
    const timer = setTimeout(() => {
      timedOut = true;
      if (child.pid !== undefined) killProcessTree(child.pid);
    }, opts.timeoutMs);

    let settled = false;
    const finish = (exitCode: number | null, spawnError?: string): void => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      let output = Buffer.concat(chunks).toString("utf8");
      if (truncated) output += "\n[... 출력이 32MB 를 초과하여 이후는 버려졌습니다 ...]";
      const result: RunResult = {
        command: display,
        exitCode,
        timedOut,
        durationMs: Date.now() - started,
        output,
      };
      if (spawnError !== undefined) result.spawnError = spawnError;
      resolve(result);
    };

    child.on("error", (e) => finish(null, e.message));
    child.on("close", (code) => finish(code));
  });
}

/** 알려진 Windows NTSTATUS 크래시 코드 해석표. */
const NTSTATUS: Record<number, string> = {
  0xc0000005: "STATUS_ACCESS_VIOLATION (Access Violation — 잘못된 메모리 접근)",
  0xc0000409: "STATUS_STACK_BUFFER_OVERRUN (Fail Fast / Stack Buffer Overrun)",
  0xc00000fd: "STATUS_STACK_OVERFLOW (Stack Overflow)",
  0xc0000135: "STATUS_DLL_NOT_FOUND (필요한 DLL 없음)",
  0xc0000142: "STATUS_DLL_INIT_FAILED (DLL 초기화 실패)",
  0xc0000374: "STATUS_HEAP_CORRUPTION (힙 손상)",
  0xc0000017: "STATUS_NO_MEMORY (메모리 부족)",
  0xc000013a: "STATUS_CONTROL_C_EXIT (Ctrl+C / 콘솔 종료)",
  0x80000003: "STATUS_BREAKPOINT (브레이크포인트)",
};

/** exit 코드를 사람이 읽을 수 있게 서술(NTSTATUS 해석 병기). */
export function describeExitCode(code: number | null): string {
  if (code === null) return "없음(강제 종료됨)";
  const u = code < 0 ? code >>> 0 : code;
  const known = NTSTATUS[u];
  if (known !== undefined) return `${code} (0x${u.toString(16).toUpperCase()} ${known})`;
  if (u > 0xffff) return `${code} (0x${u.toString(16).toUpperCase()})`;
  return String(code);
}
