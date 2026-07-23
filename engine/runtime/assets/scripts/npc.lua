-- npc.lua — NPC 배회·상호작용 참조 스크립트 (M6-B, docs/06 §7)
--
-- 이 파일은 M6-B NPC 시스템의 Lua 표면(mye.npc)을 시연하는 참조 예제다. 데모/게임은 이 패턴대로
--   NPC 를 등록하고 on_interact 로 대화를 기술한다. C++ NpcSystem 이 배회(웨이포인트 순회·랜덤)와
--   플레이어 접근 정지를 처리하고, 상호작용 개시 시 아래 on_interact 코루틴을 실행한다.
--
-- 배선 계약:
--   * 엔티티는 packed uint64(mye.world.entity_from_packed / :packed())로 전달한다.
--   * on_interact 는 코루틴 컨텍스트에서 실행된다 → mye.dialogue.say/choose(대기형) 사용 가능.
--   * 상호작용 중에는 NpcSystem 이 NPC 이동을 정지시키고, is_interacting()으로 플레이어 이동을
--     잠글 수 있다(데모가 플레이어 컨트롤러에서 폴링).
--   * 여러 NPC 는 독립적으로 배회한다. 한 NPC 의 on_interact 가 에러를 던져도 코루틴 스케줄러가
--     격리하므로 나머지 NPC 는 계속 동작한다.
--
-- 이 스크립트는 setup(player_packed, npc1_packed, npc2_packed) 함수를 노출한다 — 데모/테스트가
--   실제 엔티티 핸들을 넘겨 호출한다(엔티티는 C++/에디터가 만든다).

local M = {}

function M.setup(player, guard, merchant)
    mye.npc.set_player(player)

    -- 순찰병(Patrol): 광장 네 모서리를 순회. 플레이어가 가까이 오면 멈춰 바라본다.
    mye.npc.register{
        entity = guard,
        id = "guard",
        mode = "patrol",
        loop = true,
        waypoints = { {2, 2}, {6, 2}, {6, 6}, {2, 6} },
        speed = 2.5,
        wait_min = 0.5, wait_max = 1.5,
        alert_radius = 2.5,
        interact_radius = 1.5,
        on_interact = function()
            mye.dialogue.say("경비병", "멈춰라. 이 광장은 순찰 구역이다.")
            local pick = mye.dialogue.choose({ "길을 묻는다", "돌아선다" })
            if pick == 0 then
                mye.dialogue.say("경비병", "여관은 북쪽 다리를 건너면 있다.")
            else
                mye.dialogue.say("경비병", "그래, 조심히 가거라.")
            end
        end,
    }

    -- 상인(Random): 좌판 주변을 어슬렁. 상호작용하면 간단한 인사만.
    mye.npc.register{
        entity = merchant,
        id = "merchant",
        mode = "random",
        waypoints = { {10, 4}, {11, 5}, {9, 5}, {10, 6} },
        speed = 1.5,
        wait_min = 1.0, wait_max = 3.0,
        alert_radius = 2.0,
        interact_radius = 1.5,
        on_interact = function()
            mye.dialogue.say("상인", "어서 오게, 여행자. 좋은 물건 많다네!")
        end,
    }
end

-- 플레이어 컨트롤러가 매 프레임 폴링: 상호작용 키(E)가 눌렸고 대화 중이 아니면 시도.
--   반환 true = 상호작용 개시됨(플레이어 이동을 잠가야 함).
function M.try_interact_on_key(interact_pressed)
    if mye.npc.is_interacting() then return true end   -- 이미 대화 중 → 이동 잠금 유지
    if interact_pressed then
        local e = mye.npc.interact()   -- 가장 가까운 NPC 시도(없으면 0)
        return e ~= 0
    end
    return false
end

return M
