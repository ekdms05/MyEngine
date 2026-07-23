-- npc_guard.lua — east-gate guard NPC (single choice, no loop).
--
-- Mirrors dialogue/guard.json. The guard stands across the bridge (east side of the creek), so
-- reaching him proves bridge traversal + hybrid depth (player walks the one-way bridge deck). A
-- short two-option branch (pass / stay) keeps it distinct from the looping merchant/chief menus.

local npc = require("npc_common")
local Guard = {}

function Guard:on_init()
    self.state.busy = self.state.busy or false
    self.state.radius = 1.4
    mye.log("village.guard: on_init")
end

local function conversation(self)
    self.state.busy = true
    local pick = 0
    -- greet carries the choice (see guard.json): pass -> allow, stay -> stay
    if mye.dialogue.say_key then mye.dialogue.say_key("npc.guard.name", "dialogue.guard.greet")
    else npc.say_key("npc.guard.name", "dialogue.guard.greet") end
    pick = npc.choose_keys({
        "dialogue.guard.choice.pass",
        "dialogue.guard.choice.stay",
    })
    if pick == 0 then
        npc.say_key("npc.guard.name", "dialogue.guard.allow")
    else
        npc.say_key("npc.guard.name", "dialogue.guard.stay")
    end
    self.state.busy = false
end

function Guard:on_update(dt)
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

function Guard:on_hot_reload()
    self.state.busy = false
    mye.log("village.guard: hot reloaded")
end

return Guard
