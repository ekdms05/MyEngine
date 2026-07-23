-- npc_merchant.lua — traveling merchant NPC (buy / rumor / leave menu loop).
--
-- Mirrors dialogue/merchant.json. Same interaction pattern as the chief: proximity + E starts a
-- cutscene coroutine of localized say/choose. Demonstrates a second distinct branching conversation
-- so the vertical slice shows more than one NPC personality.

local npc = require("npc_common")
local Merchant = {}

function Merchant:on_init()
    self.state.busy = self.state.busy or false
    self.state.radius = 1.4
    self.state.visits = self.state.visits or 0
    mye.log("village.merchant: on_init")
end

local function conversation(self)
    self.state.busy = true
    self.state.visits = self.state.visits + 1
    npc.say_key("npc.merchant.name", "dialogue.merchant.greet")
    while true do
        npc.say_key("npc.merchant.name", "dialogue.merchant.menu")
        local pick = npc.choose_keys({
            "dialogue.merchant.choice.buy",
            "dialogue.merchant.choice.rumor",
            "dialogue.merchant.choice.leave",
        })
        if pick == 0 then
            npc.say_key("npc.merchant.name", "dialogue.merchant.buy")
        elseif pick == 1 then
            npc.say_key("npc.merchant.name", "dialogue.merchant.rumor")
        else
            break
        end
    end
    npc.say_key("npc.merchant.name", "dialogue.merchant.farewell")
    self.state.busy = false
end

function Merchant:on_update(dt)
    npc.pump(self)
    if self.state.busy then return end
    if npc.proximity(self, self.state.radius) and npc.want_interact() then
        local e = mye.world.entity_from_packed(self.entity)
        if e:is_valid() and _player_pos then
            local p = e:get_position()
            e:face_move(mye.Vec2(_player_pos.x - p.x, _player_pos.y - p.y))
        end
        npc.run(self, function() conversation(self) end)
    end
end

function Merchant:on_hot_reload()
    self.state.busy = false
    mye.log("village.merchant: hot reloaded")
end

return Merchant
