// mye/runtime/RuntimeTypes.cpp — 공용 값 타입 헬퍼 구현 (M6-A 골격)
#include "mye/runtime/RuntimeTypes.h"

namespace mye::runtime {

Error MakeError(RuntimeError code, std::string message) {
    return Error{std::move(message), static_cast<int32_t>(code)};
}

const char* LocaleTag(Locale loc) {
    switch (loc) {
        case Locale::Korean:  return "ko";
        case Locale::English: return "en";
    }
    return "ko";
}

Locale LocaleFromTag(std::string_view tag) {
    if (tag == "en") return Locale::English;
    return Locale::Korean;
}

} // namespace mye::runtime
