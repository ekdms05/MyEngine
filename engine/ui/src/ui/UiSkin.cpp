// mye/ui/UiSkin.cpp — 스킨/테마 (M5-A 골격; 리플렉션 등록·에셋 로드는 구현 에이전트)
#include "mye/ui/UiSkin.h"
#include "mye/core/Base.h"

namespace mye::ui {

const UiStyle* UiSkin::Find(StyleClassId cls) const {
    auto it = m_styles.find(cls.v);
    return it == m_styles.end() ? nullptr : &it->second;
}

const UiStyle* UiSkin::Find(std::string_view clsName) const {
    return Find(StyleClassId{HashFnv1a64(clsName)});
}

void UiSkin::Set(std::string_view clsName, const UiStyle& style) {
    m_styles[HashFnv1a64(clsName)] = style;
}

} // namespace mye::ui
