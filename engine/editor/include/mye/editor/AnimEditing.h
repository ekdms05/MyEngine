// mye/editor/AnimEditing.h — 애니메이션 에디터 편집 모델·커맨드 (docs/07 §애니메이션 에디터)
//
// 07: .anim 클립/상태머신 편집. 타임라인(프레임·duration), 이벤트 마커(footstep 등), 8방향 세트,
//   리스트 기반 전이 편집(노드 그래프는 P2 백로그). 편집은 커맨드 경유, 값은 AnimationClipData
//   (04 스키마)를 직렬화(refl/ser)한다.
//
// 편집 대상은 에디터가 보유한 AnimationClipData 워킹 카피(비-ECS)다. 커맨드는 그 인스턴스를
//   비소유 포인터로 캡처하고, before/after를 값 스냅샷으로 저장해 Undo 1회로 되돌린다. 이는
//   PropertyEditCommand(ECS 컴포넌트 대상)와 별개의 "에셋 문서 편집" 경로다(docs/07 §4 문서별 스택).
#pragma once

#include "mye/editor/Command.h"
#include "mye/asset/SpriteSheet.h"
#include "mye/anim/SpriteAnimator.h"

#include <string>
#include <vector>

namespace mye::editor {

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
anim::DirectionalAnimSet BuildDirectionalSet(const std::string& baseName,
                                             const std::vector<asset::AnimationClipData>& clips,
                                             bool mirrorRight = false);

} // namespace mye::editor
