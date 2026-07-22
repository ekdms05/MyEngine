// mye/script/src/bindings/EngineBindings.cpp — 표준 바인딩 묶음 팩토리 (docs/05, M3-C)
//
// MakeStandardBindings: Math/ECS/Input/Audio/Event 모듈을 생성해 소유 벡터로 반환한다.
//   데모/App 배선 편의(각 인자 nullptr 허용 — 해당 표면 안전 no-op/폴백).
#include "mye/script/bindings/EngineBindings.h"

namespace mye::script {

std::vector<std::unique_ptr<IBindingModule>> MakeStandardBindings(
    ecs::World* world, const InputState* input, audio::AudioEngine* audioEngine) {
    std::vector<std::unique_ptr<IBindingModule>> mods;
    mods.reserve(5);
    mods.push_back(std::make_unique<MathBindingModule>());
    mods.push_back(std::make_unique<EcsBindingModule>(world));
    mods.push_back(std::make_unique<InputBindingModule>(input));
    mods.push_back(std::make_unique<AudioBindingModule>(audioEngine));
    mods.push_back(std::make_unique<EventBindingModule>());
    return mods;
}

} // namespace mye::script
