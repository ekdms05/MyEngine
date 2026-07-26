// mye/gameplay/Quest.cpp — 퀘스트 진행 구현 (Quest.h 참조)
#include "mye/gameplay/Quest.h"

#include <algorithm>

namespace mye::gameplay {

namespace {
bool AllComplete(const ActiveQuest& q) {
    for (const QuestObjective& o : q.objectives)
        if (o.current < o.required) return false;
    return true;
}
} // namespace

bool AcceptQuest(QuestLog& log, const QuestDef& def) {
    if (std::find(log.completed.begin(), log.completed.end(), def.id) != log.completed.end())
        return false;   // 이미 완료
    for (const ActiveQuest& q : log.active)
        if (q.id == def.id) return false;   // 이미 진행 중
    ActiveQuest aq;
    aq.id = def.id;
    aq.objectives = def.objectives;   // 진행 사본(current 포함)
    for (QuestObjective& o : aq.objectives) o.current = 0;
    log.active.push_back(std::move(aq));
    return true;
}

int32_t ReportEvent(QuestLog& log, ObjectiveType type, uint32_t targetId, int32_t amount) {
    if (amount <= 0) return 0;
    int32_t advanced = 0;
    for (ActiveQuest& q : log.active) {
        for (QuestObjective& o : q.objectives) {
            if (o.type != type || o.targetId != targetId) continue;
            if (o.current >= o.required) continue;
            const int32_t before = o.current;
            if (type == ObjectiveType::ReachLevel)
                o.current = std::max(o.current, amount);   // 레벨은 도달값(최댓값)
            else
                o.current = std::min(o.required, o.current + amount);
            o.current = std::min(o.current, o.required);
            if (o.current != before) ++advanced;
        }
    }
    return advanced;
}

bool IsQuestComplete(const QuestLog& log, QuestId id) {
    for (const ActiveQuest& q : log.active)
        if (q.id == id) return AllComplete(q);
    return false;
}

bool TurnInQuest(QuestLog& log, QuestId id) {
    auto it = std::find_if(log.active.begin(), log.active.end(),
                           [&](const ActiveQuest& q) { return q.id == id; });
    if (it == log.active.end() || !AllComplete(*it)) return false;
    log.active.erase(it);
    log.completed.push_back(id);
    return true;
}

} // namespace mye::gameplay
