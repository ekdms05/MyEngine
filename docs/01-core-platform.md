# 01. 코어·플랫폼

`mye_core`는 실행 파일과 엔진 모듈의 공용 기반이다. 현재 공개 API는 [include/mye/core](../engine/core/include/mye/core), 실행 조합은 [App.cpp](../engine/core/src/App.cpp)에 있다. Windows·C++20·UTF-8을 기준으로 설명한다.

## 부팅과 종료

`GuardedMain`은 로그·설정·잡·이벤트·시간·입력·창을 준비하고 앱이 등록한 모듈을 초기화한다. 서비스는 `EngineContext`로 접근하고 모듈 의존성은 `ModuleRegistry`가 정렬한다. 종료 시 틱과 서비스의 수명을 정리하고 모듈을 역순으로 해제한다.

| 계약 | 소스 |
|---|---|
| 앱 구성·모듈 등록·메인 루프 | [App.h](../engine/core/include/mye/core/App.h), [App.cpp](../engine/core/src/App.cpp) |
| 모듈 수명·서비스 등록·틱 | [Module.h](../engine/core/include/mye/core/Module.h), [Module.cpp](../engine/core/src/Module.cpp) |
| 시간·고정 스텝·보간 값 | [Time.h](../engine/core/include/mye/core/Time.h) |

```text
메시지/입력 처리 → 메인 스레드 잡 완료 처리 → PreUpdate
→ FixedUpdate × 누산된 스텝 수 → Update → PostUpdate → PreRender → PostRender
```

기본 고정 스텝은 60Hz다. 시뮬레이션 상태는 고정 틱에서 변경하고 렌더·UI는 표현 시점에 처리한다. 긴 프레임은 누산 제한으로 처리하며, 렌더 보간 값은 고정 스텝의 현재/직전 상태 사이를 표현하는 데 사용한다. 앱별 시스템 배선은 [현재 구조](13-architecture-and-features.md)를 따른다.

## 플랫폼과 입력

[Win32Window](../engine/core/src/platform/Win32Window.cpp)는 창·메시지·크기·창 모드를 처리한다. [Win32Input](../engine/core/src/platform/Win32Input.cpp)은 Raw Input·문자 입력을 코어 입력 표면으로 전달한다. XInput 패드는 InputState::PollGamepads가 프레임마다 수집한다. 메인 입력은 NewFrame → 메시지 펌프 → 패드 폴링 → 액션 캡처 순서다. Win32 경계에서 문자열을 변환하고 엔진 내부 문자열은 UTF-8로 유지한다.

main 소스의 GamepadSample/UpdateGamepad는 스틱 signed 16-bit·트리거 8-bit 원시 샘플을 보관한다. RawGamepadAxis는 데드존 전 -1~1/0~1 값을 반환해 [프로젝트 입력 액션](25-input-actions.md)이 저장한 데드존을 한 번 적용하게 한다. 기존 LeftStick/RightStick/트리거 함수의 플랫폼 필터는 조회 시 유지한다. 미연결 샘플은 버튼·축을 0으로 정리하며 패드 버튼 엣지는 슬롯별 프레임 샘플 사이에서 계산한다. 프레임 사이의 짧은 패드 탭과 직접 장치 검증은 별도다.

윈도우 메시지 훅은 ImGui와 문자/IME 처리 같은 동기 소비자가 사용한다. 게임 UI 입력 라우팅은 `engine/ui`, 게임 고유 입력 해석은 앱·게임 코드의 책임이다. 입력이 있다는 사실을 완성된 게임 채팅/IME UX로 확대하지 않는다.

## 이벤트와 잡

[EventBus](../engine/core/include/mye/core/Events.h)는 즉시 발행·구독과 큐 발행·Flush를 제공한다. 비동기 작업은 큐 경계로 결과를 전달하며 Lua·UI 소비자의 스레드 수명을 지킨다. 게임플레이 이벤트는 씬의 월드별 버스로 분리하여 편집 월드와 PlayWorld가 섞이지 않게 한다.

[JobSystem](../engine/core/include/mye/core/Jobs.h)은 Compute/IO 작업·조인·메인 스레드 실행 경로를 제공한다. GPU 업로드와 주 스레드 소유 자원 교체는 에셋 finalize에서 수행한다. work-stealing·파이버·별도 렌더 스레드는 현재 제품의 완료 기능으로 설명하지 않는다.

## 데이터·메모리·진단

| 기반 | 현재 역할 | 소스 |
|---|---|---|
| Expected·Error·ID 해시 | 실패 전달·서비스/타입 식별 | [Base.h](../engine/core/include/mye/core/Base.h) |
| 수학 | 벡터·행렬·쿼터니언·색·영역 | [Math.h](../engine/core/include/mye/core/Math.h) |
| JSON | 파싱·직렬화, 정수 64비트 보존 | [Json.h](../engine/core/include/mye/core/Json.h) |
| 설정 | 엔진·프로젝트·사용자 설정 로드/병합/저장 | [Config.h](../engine/core/include/mye/core/Config.h) |
| 메모리 | 할당자 계약·프레임 할당 | [Memory.h](../engine/core/include/mye/core/Memory.h) |
| 로그·단정 | 심각도·카테고리·오류 진단 | [Log.h](../engine/core/include/mye/core/Log.h), [Assert.h](../engine/core/include/mye/core/Assert.h) |

소유권은 값·RAII·`unique_ptr`로 표현하고 비소유 포인터의 수명을 명시한다. 외부 경계 실패를 성공처럼 진행하거나 `(void)`로 버리지 않는다. 할당자 인터페이스가 존재한다고 모든 태그 추적·풀·통계 기능이 구현된 것으로 보지 않는다.

## 검증과 한계

코어의 수학·JSON·입력·이벤트·잡·설정은 기존 `mye_tests`로 검사한다. 모듈 초기화·창·오디오 장치 같은 플랫폼 경로는 앱/샘플 실행으로 별도 확인한다. `--headless` 렌더 실행에도 DX11 디바이스가 필요할 수 있다.

설정 변경 이벤트의 부팅 연결·minidump·플랫폼 확장에는 남은 작업이 있다. 완성된 크래시 수집이나 Linux 서버 지원으로 설명하지 않는다. [개선 목록](14-development-priorities.md)의 진단·플랫폼 완료 조건을 따른다.
