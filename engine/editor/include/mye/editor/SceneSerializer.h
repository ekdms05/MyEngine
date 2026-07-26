// mye/editor/SceneSerializer.h — 하위호환 별칭 (정본은 mye/scene/SceneSerializer.h)
//
// 씬 직렬화(World↔JSON)는 scene 계층으로 이동해 에디터·게임 런타임·세이브가 공유한다.
// 기존 에디터/테스트 코드가 mye::editor::SceneSerializer 를 계속 쓰도록 별칭만 제공한다.
#pragma once

#include "mye/scene/SceneSerializer.h"

namespace mye::editor {
using mye::scene::SceneSerializer;
}
