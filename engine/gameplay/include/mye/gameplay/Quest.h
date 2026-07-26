// mye/gameplay/Quest.h — 퀘스트 목표·진행·보상 (docs/mmorpg/04, M8)
//
// 퀘스트는 목표(몹 처치·아이템 수집·레벨 도달) 목록. 게임 이벤트를 보고하면 매칭 목표가 진행되고,
// 전 목표 완료 시 턴인 가능. 순수 로직 — 진행 상태는 자기완결(진행에 카탈로그 불필요).
#pragma once

#include "mye/gameplay/Item.h"    // ItemStack
#include "mye/ecs/ComponentType.h"

#include <cstdint>
#include <vector>

namespace mye::gameplay {

using QuestId = uint32_t;

enum class ObjectiveType : uint8_t { KillMob, CollectItem, ReachLevel };

struct QuestObjective {
    ObjectiveType type = ObjectiveType::KillMob;
    uint32_t      targetId = 0;    // 몹 id / 아이템 id / 레벨값
    int32_t       required = 1;
    int32_t       current = 0;
};

// 퀘스트 정의(데이터) — 목표 + 보상.
struct QuestDef {
    QuestId                    id = 0;
    std::vector<QuestObjective> objectives;
    int64_t                    rewardXp = 0;
    int64_t                    rewardGold = 0;
    std::vector<ItemStack>     rewardItems;
};

// 진행 중 퀘스트(목표 진행 포함, 자기완결).
struct ActiveQuest {
    QuestId                     id = 0;
    std::vector<QuestObjective> objectives;
};

struct QuestLog {
    MYE_COMPONENT(QuestLog);
    std::vector<ActiveQuest> active;
    std::vector<QuestId>     completed;
};

// 퀘스트 수락(중복·완료 방지). 성공 true.
bool AcceptQuest(QuestLog& log, const QuestDef& def);

// 게임 이벤트 보고 → 매칭 목표 진행(required 상한). 진행된 목표 수 반환.
int32_t ReportEvent(QuestLog& log, ObjectiveType type, uint32_t targetId, int32_t amount);

// 특정 퀘스트의 전 목표 완료 여부.
bool IsQuestComplete(const QuestLog& log, QuestId id);

// 턴인: 완료 상태면 active 에서 제거하고 completed 에 기록. 성공 true(호출부가 보상 지급).
bool TurnInQuest(QuestLog& log, QuestId id);

} // namespace mye::gameplay
