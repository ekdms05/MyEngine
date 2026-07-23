-- npc_chief.lua — village chief NPC (branching dialogue with a sub-menu loop).
--
-- Interaction: when the player is within interactRadius and presses E, face the player and run a
-- cutscene coroutine that mirrors dialogue/chief.json (greet -> ask -> [village|bridge|leave]).
-- All text is addressed by localization key so ko/en both work. Hot-reloadable: edit this file or
-- loc/ko.json and the conversation changes with no engine rebuild.

local npc = require("npc_common")
local Chief = {}

function Chief:on_init()
    self.state.busy = self.state.busy or false
    self.state.radius = 1.4
    mye.log("village.chief: on_init")
end

local function conversation(self)
    self.state.busy = true
    npc.say_key("npc.chief.name", "dialogue.chief.greet")
    while true do
        npc.say_key("npc.chief.name", "dialogue.chief.ask")
        local pick = npc.choose_keys({
            "dialogue.chief.choice.village",
            "dialogue.chief.choice.bridge",
            "dialogue.chief.choice.leave",
        })
        if pick == 0 then
            npc.say_key("npc.chief.name", "dialogue.chief.about_village")
        elseif pick == 1 then
            npc.say_key("npc.chief.name", "dialogue.chief.about_bridge")
        else
            break
        end
    end
    npc.say_key("npc.chief.name", "dialogue.chief.farewell")
    self.state.busy = false
end

function Chief:on_update(dt)
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

function Chief:on_hot_reload()
    self.state.busy = false   -- never get stuck busy across a reload
    mye.log("village.chief: hot reloaded")
end

return Chief
