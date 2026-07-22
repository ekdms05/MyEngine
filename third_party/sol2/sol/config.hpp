// sol2 v3.2.3 (single-header release) — sol/config.hpp
//
// 이 파일은 sol2 단일 헤더 배포판이 sol/sol.hpp 초입에서 #include <sol/config.hpp> 로 요구하는
// "사용자 구성 지점"이다. 배포판은 이 파일을 sol.hpp·forward.hpp 와 함께 벤더링하도록 의도한다.
// 여기에 SOL_* 구성 매크로를 정의해 컴파일 타임 동작을 조정한다. 정의하지 않으면 sol2 는
// 안전한 기본값으로 폴백한다(빈 config.hpp 도 유효 — 이 파일은 확장 지점일 뿐이다).
//
// MyEngine 규약(third_party/CMakeLists.txt 의 mye_thirdparty_sol2 가 컴파일 정의로 켠다):
//   SOL_ALL_SAFETIES_ON=1            — 모든 스택 접근·함수 호출 검사(디버그 안전)
//   SOL_EXCEPTIONS_SAFE_PROPAGATION=1 — Lua↔C++ 경계 예외 안전 전파(/EHsc)
// 위 두 매크로는 타깃 컴파일 정의로 주입되므로 이 파일에서 중복 정의하지 않는다.
//
// 필요 시 이 파일에 추가 구성(예: SOL_NO_EXCEPTIONS, SOL_PRINT_ERRORS 등)을 정의한다.
#ifndef SOL_CONFIG_HPP
#define SOL_CONFIG_HPP

// (의도적으로 비어 있음 — CMake 컴파일 정의가 안전·예외 옵션을 담당한다.)

#endif // SOL_CONFIG_HPP
