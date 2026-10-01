// mye/editor/AnimEditing.h — 애니메이션 에디터 편집 모델·커맨드 (docs/07 §애니메이션 에디터)
//
// .anim 문서는 AnimationAsset(SpriteSheet + AnimationClipData)과 독립 Undo를 소유한다.
// 패널은 프레임·시간·이벤트와 이미지 미리보기를 제공하며 런타임 ClipPlayback을 재사용한다.
// 커맨드는 문서보다 짧게 살아야 한다. 씬의 컴포넌트 지정은 별도 씬 Undo에 기록한다.
#pragma once

#include "mye/editor/Command.h"
#include "mye/asset/SpriteSheet.h"
#include "mye/asset/AnimationAsset.h"
#include "mye/anim/SpriteAnimator.h"

#include <string>
#include <vector>

namespace mye::editor {

Expected<void, Error> AssignAnimationToEntity(EditorContext& ctx, ecs::Entity entity, asset::AssetRef animation);
Expected<void, Error> SetupCharacterMovement(EditorContext& ctx, ecs::Entity entity);
Expected<void, Error> AssignCharacterMotion(EditorContext& ctx, ecs::Entity entity,
                                           asset::AssetRef animation, bool walking);

class AnimAssetEditCommand final : public IEditorCommand {
public:
    AnimAssetEditCommand(asset::AnimationAsset& target, asset::AnimationAsset after, std::string label)
        : m_target(target), m_before(target), m_after(std::move(after)), m_label(std::move(label)) {}
    void Execute(EditorContext&) override { m_target = m_after; }
    void Undo(EditorContext&) override { m_target = m_before; }
    std::string_view Label() const override { return m_label; }
private:
    // ponytail: 작은 클립은 값 스냅샷으로 Undo한다. 큰 시트의 메모리가 병목이면 변경 프레임만 기록한다.
    asset::AnimationAsset& m_target; // Owning document outlives its command stack.
    asset::AnimationAsset m_before, m_after;
    std::string m_label;
};

// ---------------------------------------------------------------------------
// 클립 편집 커맨드 — 클립 전체를 before/after 스냅샷(간결·안전). 프레임/duration/이벤트 편집 공용.
// ---------------------------------------------------------------------------
// 세밀 diff 대신 클립 전체 값 스냅샷을 쓴다(클립은 작음). Execute=after, Undo=before.
class AnimClipEditCommand final : public IEditorCommand {
public:
    AnimClipEditCommand(asset::AnimationClipData* target,
                        asset::AnimationClipData before,
                        asset::AnimationClipData after,
                        std::string label = "클립 편집");

    void Execute(EditorContext& ctx) override;
    void Undo(EditorContext& ctx) override;
    std::string_view Label() const override { return m_label; }
    CommandKind Kind() const override { return CommandKind::Custom; }

private:
    asset::AnimationClipData* m_target = nullptr;
    asset::AnimationClipData  m_before;
    asset::AnimationClipData  m_after;
    std::string               m_label;
};

// ---- 클립 편집 헬퍼(순수 값 변환 — 테스트·패널 공용) ----
namespace anim_edit {

// 프레임 추가(끝에). 기본 duration(초).
asset::AnimationClipData WithFrameAppended(const asset::AnimationClipData& c,
                                           uint32_t frameIndex, float duration);
// 프레임 제거(clip index i).
asset::AnimationClipData WithFrameRemoved(const asset::AnimationClipData& c, size_t i);
// 프레임 순서 이동(from → to).
asset::AnimationClipData WithFrameMoved(const asset::AnimationClipData& c, size_t from, size_t to);
// 프레임 duration 변경.
asset::AnimationClipData WithFrameDuration(const asset::AnimationClipData& c, size_t i, float dur);
// 이벤트 마커 추가.
asset::AnimationClipData WithEventAdded(const asset::AnimationClipData& c,
                                        const asset::AnimEventMarker& ev);
// 이벤트 마커 제거(index).
asset::AnimationClipData WithEventRemoved(const asset::AnimationClipData& c, size_t i);

} // namespace anim_edit

// ---------------------------------------------------------------------------
// 8방향 세트 구성 — walk_S/SW/... 명명 규칙으로 8클립을 DirectionalAnimSet에 배선 (docs/07)
// ---------------------------------------------------------------------------
// clips: 후보 클립 목록(이름이 "<base>_<suffix>"). base와 8방향 접미사로 각 슬롯을 채운다.
//   suffix 규약은 anim::Dir8Suffix(down/down_left/...) 사용. 반환 clips[i]는 clips 원소를 가리킨다.
//
// [수명 계약 — 중요] 반환된 DirectionalAnimSet 은 clips 벡터의 원소를 비소유 raw 포인터로
//   가리킨다. 따라서 (1) clips 는 반환된 세트보다 오래 살아 있어야 하고, (2) 세트를 다 쓸
//   때까지 clips 를 재할당(push_back 등으로 capacity 변경)해서는 안 된다 — 재할당 시 모든
//   포인터가 dangling 된다. 8방향 배선 UI 를 붙일 때는 clips 를 먼저 확정(고정)한 뒤 이 함수를
//   호출하거나, 인덱스 기반 참조로 대체할 것.
anim::DirectionalAnimSet BuildDirectionalSet(const std::string& baseName,
                                             const std::vector<asset::AnimationClipData>& clips,
                                             bool mirrorRight = false);

} // namespace mye::editor
