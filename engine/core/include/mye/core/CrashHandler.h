// mye/core/CrashHandler.h — 최소 크래시 핸들러 (docs/01 크래시 덤프 — Phase 0: 로그만)
//
// GuardedMain이 부팅 초기(Log::Init 직후)에 1회 호출한다.
// SetUnhandledExceptionFilter로 처리되지 않은 SEH 예외를 잡아 예외 코드·주소를
// 로그에 남기고 즉시 플러시한다. 디버거가 붙어 있으면 필터를 통과시켜 디버거가 잡게 한다.
// MiniDumpWriteDump(.dmp + 최근 로그 링버퍼)는 Phase 1(로드맵 M6) — 이 시그니처는 유지된다.
#pragma once

namespace mye {

void InstallCrashHandler();   // 구현: src/Assert.cpp

} // namespace mye
