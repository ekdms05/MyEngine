// mye/ui/UiTypes.cpp — UI 값 타입 헬퍼
#include "mye/ui/UiTypes.h"
#include "mye/core/Base.h"

namespace mye::ui {

StyleClassId MakeStyleClass(std::string_view name) {
    return StyleClassId{HashFnv1a64(name)};
}

} // namespace mye::ui
