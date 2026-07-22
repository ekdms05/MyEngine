// mye/runtime/Localization.h — 키 기반 문자열 테이블·LocalizedText (docs/06 §7 로컬라이제이션)
//
// 소유: 06 (M6-A). StringTable 에셋(locale별, key → 문자열) + LocalizedText 참조 타입.
//   UI·툴팁·대사가 소비. 포맷 자리표시자 {0}/{name} 치환. 폴백 체인 ko → en → key(문자 그대로).
//   로케일 전환 시 LocaleChangedEvent 발행 → UI 전체 텍스트 리플로우(구독은 소비자 몫).
//
// 확장 포인트(docs/06 확장 §7): 한국어 조사 처리 훅 {name:이/가} — 로케일별 후처리 함수 주입점.
//   M6-A 는 자리표시자 치환까지 구현, 조사 훅은 인터페이스 자리만(FormatterHook).
#pragma once

#include "mye/runtime/RuntimeTypes.h"
#include "mye/core/Base.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mye { class EventBus; }
namespace mye::asset { class VirtualFileSystem; }

namespace mye::runtime {

// 로케일 변경 통지 — 구독자(UI)가 표시 텍스트를 다시 조회·리플로우한다.
struct LocaleChangedEvent {
    MYE_EVENT(LocaleChangedEvent);
    uint8_t locale;   // Locale 값(struct 는 trivially copyable 유지 — Enqueue 가능)
};

// 포맷 인자(자리표시자 치환). 이름 인자 {name} 과 위치 인자 {0} 둘 다 지원.
struct FormatArg {
    std::string key;     // "name" 또는 위치 인덱스 문자열 "0"
    std::string value;   // 치환 값(이미 로컬라이즈된 최종 문자열)
};

// 로케일별 후처리 훅(조사 처리 {name:이/가} 등). loc·치환 후 문자열을 받아 변형해 반환.
//   M6-A: 인터페이스 자리만. 미설정이면 무변형(항등).
using FormatterHook = std::function<std::string(Locale loc, std::string_view formatted)>;

// ---------------------------------------------------------------------------
// LocalizedText — UI/대사가 보유하는 "키 참조" 값 타입.
//   key 로 StringTable 을 조회해 현재 로케일 문자열을 얻는다. key 가 비면 literal 을 그대로 쓴다
//   (인라인 하드코딩 텍스트 겸용 — 에디터/데모 편의).
// ---------------------------------------------------------------------------
struct LocalizedText {
    std::string key;       // "dialogue.npc1.greeting" (비면 literal 사용)
    std::string literal;   // key 없을 때 표시할 원문(폴백/인라인)

    static LocalizedText Key(std::string k) { return LocalizedText{std::move(k), {}}; }
    static LocalizedText Literal(std::string s) { return LocalizedText{{}, std::move(s)}; }
    bool IsKeyed() const { return !key.empty(); }
};

// ---------------------------------------------------------------------------
// LocalizationSystem — 로케일별 StringTable 보유·조회·로케일 전환.
//   EngineContext 서비스로 등록(MYE_SERVICE). Lua Loc.text(key,...) 바인딩(05)의 백엔드.
// ---------------------------------------------------------------------------
class LocalizationSystem {
public:
    MYE_SERVICE(mye::runtime::LocalizationSystem);

    LocalizationSystem();
    ~LocalizationSystem();
    LocalizationSystem(const LocalizationSystem&) = delete;
    LocalizationSystem& operator=(const LocalizationSystem&) = delete;

    // events: LocaleChangedEvent 발행(nullptr 허용=테스트). 비소유.
    void Initialize(EventBus* events);
    void Shutdown();

    // ---- 테이블 로드 ----
    // locale 문자열 테이블을 JSON(key→value)로 병합 로드. 같은 key 는 나중 로드가 덮어씀.
    //   포맷: { "dialogue.greeting": "안녕하세요, {name}님", ... }. 파싱 실패 시 Error.
    Expected<void, Error> LoadTableJson(Locale loc, std::string_view json);

    // VFS 경로에서 로드(편의). vfs 로 blob 읽기 → LoadTableJson.
    Expected<void, Error> LoadTableFromFile(Locale loc, asset::VirtualFileSystem& vfs,
                                            std::string_view vpath);

    // 코드로 한 항목 등록(테스트·핫패치).
    void Set(Locale loc, std::string_view key, std::string value);

    // ---- 로케일 ----
    void   SetLocale(Locale loc);           // 변경 시 LocaleChangedEvent 발행
    Locale CurrentLocale() const;

    // 조사/후처리 훅 주입(로케일별). 미설정이면 무변형.
    void SetFormatterHook(FormatterHook hook);

    // ---- 조회 ----
    // 현재 로케일로 key 조회. 없으면 폴백 체인(현재→ko→en), 최종 실패 시 key 문자열 자체 반환.
    std::string Get(std::string_view key) const;
    // 자리표시자 치환 + 후처리 훅 적용.
    std::string Format(std::string_view key, const std::vector<FormatArg>& args) const;
    // LocalizedText 해석(keyed 면 Format, literal 이면 그대로).
    std::string Resolve(const LocalizedText& text,
                        const std::vector<FormatArg>& args = {}) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::runtime
