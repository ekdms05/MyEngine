# 05. 스크립트·플러그인·데이터 확장

게임 동작은 `mye_script`의 Lua 5.4/sol2, 동적 데이터는 `mye_ddc`, 네이티브 확장은 `mye_plugin`이 담당한다. 이 문서는 현재 등록·실행·해제 경계를 설명한다. 실제 프로젝트 앱의 연결은 [현재 구조](13-architecture-and-features.md)를 따른다.

## Lua 실행과 수명

| 구성 | 책임 | 소스 |
|---|---|---|
| ScriptRuntime | Lua VM·보호 실행·바인딩 | [ScriptRuntime.h](../engine/script/include/mye/script/ScriptRuntime.h) |
| ScriptComponent·ScriptClass | 엔티티에 붙는 스크립트 데이터·콜백 | [ScriptComponent.h](../engine/script/include/mye/script/ScriptComponent.h), [ScriptClass.h](../engine/script/include/mye/script/ScriptClass.h) |
| ScriptSystem | 엔티티 콜백·이벤트·핫 리로드 | [ScriptSystem.h](../engine/script/include/mye/script/ScriptSystem.h) |
| CoroutineScheduler | 대기·연출 코루틴 진행 | [CoroutineScheduler.h](../engine/script/include/mye/script/CoroutineScheduler.h) |
| IBindingModule | 네이티브 바인딩 등록 | [IBindingModule.h](../engine/script/include/mye/script/IBindingModule.h) |

```text
스크립트 에셋/소스 → ScriptRuntime/ScriptClass → ScriptComponent
→ ScriptSystem 보호 콜백 → ECS·입력·오디오·이벤트 소비
```

Lua 상태는 소유 스레드에서 사용하고 외부 콜백 오류는 보호된 호출에서 격리한다. 스크립트가 참조하는 World·에셋·오디오보다 VM·콜백이 오래 살아남지 않게 종료한다. 오류를 로그만 남기고 성공한 게임 명령으로 취급하지 않는다.

핫 리로드는 스크립트 상태 보존과 재바인딩을 다룬다. Lua 코루틴·콜백 재실행과 네이티브 DLL 코드 교체는 다른 기능이다. 실행 중 DLL의 타입/vtable을 자동 이전하는 기능은 현재 제공 목록에 넣지 않는다.

## 바인딩과 DDC

[binding 소스](../engine/script/src/bindings)는 Math·ECS·Input·Audio·Event 경로를 연결한다. 리플렉션 기반 값 접근과 [DDC](../engine/ddc)의 스키마·인스턴스·엔티티 스토어·Lua 정의 시스템도 별도 구현·테스트가 있다.

게임 데이터·규칙의 소유는 `game/`에 두고 재사용 가능한 등록·실행 기반만 엔진에 둔다. 동적 스키마의 식별·필드 타입·범위와 오류를 검증한다. DDC가 있다는 이유로 엔진·RHI·파일·작업 스레드의 모든 API를 Lua에 노출하지 않는다.

## 네이티브 플러그인

[Plugin.h](../engine/plugin/include/mye/plugin/Plugin.h)와 [PluginHost](../engine/plugin/include/mye/plugin/PluginHost.h)가 등록·버전 검사·로드/틱/언로드를 담당한다. DLL 경로는 Windows 로더와 테스트 플러그인으로 검증한다. 소유 서비스·타입·콜백을 등록/해제하는 순서와 호스트 수명을 지킨다.

플러그인 등록 가능성과 모든 에디터·렌더·임포터 확장 표면의 구현 완료는 별개다. 현재 호스트 인터페이스를 기준으로 작성하고, 자동 SDK 패키징·전체 ABI 협상·에디터 Lua 전용 VM·원격 커맨드는 실제 제품 경로가 연결되기 전 지원 기능으로 쓰지 않는다.

## 사용 예와 검증

`character_demo`는 캐릭터 동작·애니메이션·발소리·Lua 핫 리로드, `village_demo`는 NPC·대화·컷신을 조합한다. script·DDC·플러그인 동작은 기존 자체 테스트로 검사한다.

현재 MyGame은 script/runtime/gameplay 라이브러리를 모두 조합한 실행 경로가 아니다. 모듈 기능을 새로 구현하기보다 해당 샘플과 기존 바인딩을 제품 앱에 연결한다. 프로젝트 단위 로드/언로드·게임 명령·에디터 플레이 시스템 통합의 완료 조건은 [14](14-development-priorities.md)에 있다.
