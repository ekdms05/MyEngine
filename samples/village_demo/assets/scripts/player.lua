-- player.lua — village_demo player controller (docs/05 contract).
--
-- Same contract as character_demo: returns a class table with on_init/on_update(dt)/on_hot_reload/
-- on_event. self.entity is a packed uint64; mye.world.entity_from_packed gives a handle. self.state
-- persists across hot reload. Behavior: WASD move, drive the 8-dir SpriteAnimator (isMoving/speed),
-- keep the audio listener on the player, and suspend movement while a dialogue/cutscene is active so
-- the player can't walk away mid-conversation.

local Player = {}

function Player:on_init()
    self.state.speed = self.state.speed or 4.0        -- world units / sec (hot-reloadable)
    self.state.steps = self.state.steps or 0
    self.state.moving = false
    mye.log("village.player: on_init speed=" .. tostring(self.state.speed))
end

-- True while a dialogue box or cutscene is waiting for input; movement is frozen then.
local function dialogue_active()
    if mye.dialogue and mye.dialogue.is_active then
        return mye.dialogue.is_active()
    end
    return false
end

function Player:on_update(dt)
    local e = mye.world.entity_from_packed(self.entity)
    if not e:is_valid() then return end

    local dx, dy = 0.0, 0.0
    if not dialogue_active() then
        if _demo_move ~= nil then
            dx, dy = _demo_move.x, _demo_move.y
        else
            if mye.input.is_down(mye.Key.D) then dx = dx + 1.0 end
            if mye.input.is_down(mye.Key.A) then dx = dx - 1.0 end
            if mye.input.is_down(mye.Key.W) then dy = dy + 1.0 end
            if mye.input.is_down(mye.Key.S) then dy = dy - 1.0 end
        end
    end

    local moving = (dx ~= 0.0) or (dy ~= 0.0)
    self.state.moving = moving
    if moving then
        local len = math.sqrt(dx * dx + dy * dy)
        dx, dy = dx / len, dy / len
        local p = e:get_position()
        local spd = self.state.speed
        e:set_position(mye.Vec2(p.x + dx * spd * dt, p.y + dy * spd * dt))
        e:face_move(mye.Vec2(dx, dy))
    end

    e:set_bool("isMoving", moving)
    e:set_float("speed", moving and self.state.speed or 0.0)

    local p2 = e:get_position()
    mye.audio.set_listener(p2.x, p2.y)
    -- Publish player world pos for NPC proximity checks (globals bridge Lua scripts).
    _player_pos = { x = p2.x, y = p2.y }
end

function Player:on_event(name, payload)
    if name == "animation" and payload.name == "footstep" then
        self.state.steps = self.state.steps + 1
        local p = mye.world.entity_from_packed(self.entity):get_position()
        mye.audio.play_cue(payload.arg or "cue_footstep", p.x, p.y)
    end
end

function Player:on_hot_reload()
    self.state.reloaded = true
    mye.log("village.player: hot reloaded (speed=" .. tostring(self.state.speed) .. ")")
end

return Player
