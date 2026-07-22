// mye/runtime/DialogueData.cpp — 대사 데이터 조회·직렬화 (M6-A)
//
// IndexOf 실동작 + refl/ser JsonArchive 왕복(SaveDialogueJson/LoadDialogueJson).
//   refl 등록: LocalizedText / DialogueChoice / DialogueLine / DialogueScript. vector<struct>
//   필드는 TypeBuilder 가 자동 지원하므로 평범한 Field 등록으로 재귀 없이 왕복된다.
#include "mye/runtime/DialogueData.h"

#include "mye/refl/TypeBuilder.h"
#include "mye/ser/Serialize.h"
#include "mye/ser/JsonArchive.h"
#include "mye/core/Json.h"

// 전역 스코프 리플렉션 등록(비침투). 필드 타입 순서: 잎(LocalizedText) → 가지 → 루트.
MYE_REFLECT(mye::runtime::LocalizedText);
MYE_REFLECT(mye::runtime::DialogueChoice);
MYE_REFLECT(mye::runtime::DialogueLine);
MYE_REFLECT(mye::runtime::DialogueScript);

template <> void mye::refl::Reflect(TypeBuilder<mye::runtime::LocalizedText>& b) {
    b.Version(1)
     .Field("key", &mye::runtime::LocalizedText::key)
     .Field("literal", &mye::runtime::LocalizedText::literal);
}
template <> void mye::refl::Reflect(TypeBuilder<mye::runtime::DialogueChoice>& b) {
    b.Version(1)
     .Field("text", &mye::runtime::DialogueChoice::text)
     .Field("gotoId", &mye::runtime::DialogueChoice::gotoId)
     .Field("conditionKey", &mye::runtime::DialogueChoice::conditionKey);
}
template <> void mye::refl::Reflect(TypeBuilder<mye::runtime::DialogueLine>& b) {
    b.Version(1)
     .Field("id", &mye::runtime::DialogueLine::id)
     .Field("speaker", &mye::runtime::DialogueLine::speaker)
     .Field("portrait", &mye::runtime::DialogueLine::portrait)
     .Field("body", &mye::runtime::DialogueLine::body)
     .Field("voiceCue", &mye::runtime::DialogueLine::voiceCue)
     .Field("gotoId", &mye::runtime::DialogueLine::gotoId)
     .Field("choices", &mye::runtime::DialogueLine::choices);
}
template <> void mye::refl::Reflect(TypeBuilder<mye::runtime::DialogueScript>& b) {
    b.Version(1)
     .Field("startId", &mye::runtime::DialogueScript::startId)
     .Field("lines", &mye::runtime::DialogueScript::lines)
     .Field("version", &mye::runtime::DialogueScript::version);
}

namespace mye::runtime {

int32_t DialogueScript::IndexOf(std::string_view id) const {
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].id == id) return static_cast<int32_t>(i);
    }
    return -1;
}

Expected<std::string, Error> SaveDialogueJson(const DialogueScript& script) {
    DialogueScript copy = script;   // Serialize 는 non-const 참조.
    auto wr = ser::JsonArchive::ForWrite();
    auto r = ser::Serialize(wr, copy);
    if (!r) return r.GetError();
    return json::Stringify(wr.Root());
}

Expected<DialogueScript, Error> LoadDialogueJson(std::string_view json) {
    auto parsed = json::Parse(json);
    if (!parsed) return MakeError(RuntimeError::ParseFailed, parsed.GetError().message);
    DialogueScript script;
    auto rd = ser::JsonArchive::ForRead(parsed.Value());
    auto r = ser::Serialize(rd, script);
    if (!r) return MakeError(RuntimeError::ParseFailed, r.GetError().message);
    return script;
}

} // namespace mye::runtime
