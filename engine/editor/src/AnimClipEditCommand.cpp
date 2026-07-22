// AnimClipEditCommand.cpp — 애니 클립 편집 커맨드 + 값 변환 헬퍼 + 8방향 세트 구성 (docs/07)
#include "mye/editor/AnimEditing.h"

#include <algorithm>

namespace mye::editor {

AnimClipEditCommand::AnimClipEditCommand(asset::AnimationClipData* target,
                                         asset::AnimationClipData before,
                                         asset::AnimationClipData after,
                                         std::string label)
    : m_target(target),
      m_before(std::move(before)),
      m_after(std::move(after)),
      m_label(std::move(label)) {}

void AnimClipEditCommand::Execute(EditorContext&) {
    if (m_target) *m_target = m_after;
}

void AnimClipEditCommand::Undo(EditorContext&) {
    if (m_target) *m_target = m_before;
}

namespace anim_edit {

asset::AnimationClipData WithFrameAppended(const asset::AnimationClipData& c,
                                           uint32_t frameIndex, float duration) {
    asset::AnimationClipData out = c;
    out.frameIndices.push_back(frameIndex);
    out.frameDurations.push_back(duration);
    return out;
}

asset::AnimationClipData WithFrameRemoved(const asset::AnimationClipData& c, size_t i) {
    asset::AnimationClipData out = c;
    if (i >= out.frameIndices.size()) return out;
    out.frameIndices.erase(out.frameIndices.begin() + static_cast<std::ptrdiff_t>(i));
    if (i < out.frameDurations.size())
        out.frameDurations.erase(out.frameDurations.begin() + static_cast<std::ptrdiff_t>(i));
    // 제거된 프레임을 가리키던 이벤트는 폐기, 뒤 프레임을 가리키던 이벤트는 한 칸 당김.
    std::vector<asset::AnimEventMarker> evs;
    for (asset::AnimEventMarker ev : out.events) {
        if (ev.frameIndex == i) continue;
        if (ev.frameIndex > i) --ev.frameIndex;
        evs.push_back(std::move(ev));
    }
    out.events = std::move(evs);
    return out;
}

asset::AnimationClipData WithFrameMoved(const asset::AnimationClipData& c, size_t from, size_t to) {
    asset::AnimationClipData out = c;
    const size_t n = out.frameIndices.size();
    if (from >= n || to >= n || from == to) return out;
    auto rot = [&](auto& vec) {
        if (vec.size() != n) return;
        auto v = std::move(vec[from]);
        vec.erase(vec.begin() + static_cast<std::ptrdiff_t>(from));
        vec.insert(vec.begin() + static_cast<std::ptrdiff_t>(to), std::move(v));
    };
    rot(out.frameIndices);
    rot(out.frameDurations);
    return out;
}

asset::AnimationClipData WithFrameDuration(const asset::AnimationClipData& c, size_t i, float dur) {
    asset::AnimationClipData out = c;
    if (i < out.frameDurations.size()) out.frameDurations[i] = dur;
    return out;
}

asset::AnimationClipData WithEventAdded(const asset::AnimationClipData& c,
                                        const asset::AnimEventMarker& ev) {
    asset::AnimationClipData out = c;
    out.events.push_back(ev);
    // frameIndex 오름차순 유지(타임라인 표시 안정).
    std::stable_sort(out.events.begin(), out.events.end(),
                     [](const asset::AnimEventMarker& a, const asset::AnimEventMarker& b) {
                         return a.frameIndex < b.frameIndex;
                     });
    return out;
}

asset::AnimationClipData WithEventRemoved(const asset::AnimationClipData& c, size_t i) {
    asset::AnimationClipData out = c;
    if (i < out.events.size())
        out.events.erase(out.events.begin() + static_cast<std::ptrdiff_t>(i));
    return out;
}

} // namespace anim_edit

anim::DirectionalAnimSet BuildDirectionalSet(const std::string& baseName,
                                             const std::vector<asset::AnimationClipData>& clips,
                                             bool mirrorRight) {
    anim::DirectionalAnimSet set;
    set.name = baseName;
    set.mirrorRight = mirrorRight;
    for (int d = 0; d < 8; ++d) {
        const std::string want = baseName + "_" + anim::Dir8Suffix(static_cast<anim::Dir8>(d));
        for (const asset::AnimationClipData& c : clips) {
            if (c.name == want) { set.clips[static_cast<size_t>(d)] = &c; break; }
        }
    }
    return set;
}

} // namespace mye::editor
