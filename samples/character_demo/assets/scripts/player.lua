-- player.lua — M3-C character controller (docs/05, M3 완료 기준).
--
-- Contract (docs/05):
--   * file returns a class table with on_init/on_update(dt)/on_hot_reload/on_event.
--   * self.entity is a packed uint64 → mye.world.entity_from_packed(self.entity) gives a handle.
--   * self.state persists across hot reload; self.state.* is where mutable runtime data lives.
--
-- Behavior: read WASD move axis, move the entity, drive the SpriteAnimator params
--   (isMoving/speed) and facing (8-dir), keep the audio listener on the player. Footstep
--   animation events arrive via on_event("animation", {name="footstep", arg=<cue>}) and play
--   an AudioCue by name. Speed is tunable via self.state.speed so hot reload can change it live.

local Player = {}

function Player:on_init()
    -- self.state survives hot reload; initialize only if absent.
    self.state.speed = self.state.speed or 4.0          -- world units / sec
    self.state.steps = self.state.steps or 0            -- footstep counter (survives reload)
    self.state.inits = (self.state.inits or 0) + 1      -- how many fresh on_init calls
    self.state.moving = false
    mye.log("player: " .. "on_init speed=" .. tostring(self.state.speed))
end

function Player:on_update(dt)
    local e = mye.world.entity_from_packed(self.entity)
    if not e:is_valid() then return end

    -- WASD → normalized world direction (+Y up).
    -- Deterministic CLI scenarios inject a forced direction via the _demo_move global
    -- (set from C++); when present it overrides live input so dumps are reproducible.
    local dx = 0.0
    local dy = 0.0
    if _demo_move ~= nil then
        dx = _demo_move.x
        dy = _demo_move.y
    else
        if mye.input.is_down(mye.Key.D) then dx = dx + 1.0 end
        if mye.input.is_down(mye.Key.A) then dx = dx - 1.0 end
        if mye.input.is_down(mye.Key.W) then dy = dy + 1.0 end
        if mye.input.is_down(mye.Key.S) then dy = dy - 1.0 end
    end

    local moving = (dx ~= 0.0) or (dy ~= 0.0)
    self.state.moving = moving

    if moving then
        local len = math.sqrt(dx * dx + dy * dy)
        dx = dx / len
        dy = dy / len
        local p = e:get_position()
        local spd = self.state.speed
        e:set_position(mye.Vec2(p.x + dx * spd * dt, p.y + dy * spd * dt))
        e:face_move(mye.Vec2(dx, dy))   -- snap facing to nearest of 8 directions
    end

    -- Drive animator state machine: isMoving gates walk↔idle; speed scales playback.
    e:set_bool("isMoving", moving)
    e:set_float("speed", moving and self.state.speed or 0.0)

    -- Keep the audio listener on the player (so footstep spatialization is centered).
    local p2 = e:get_position()
    mye.audio.set_listener(p2.x, p2.y)
end

-- Footstep + custom events. Animation footstep markers route here as ("animation", payload).
function Player:on_event(name, payload)
    if name == "animation" and payload.name == "footstep" then
        self.state.steps = self.state.steps + 1
        local p = mye.world.entity_from_packed(self.entity):get_position()
        -- payload.arg carries the cue name from the AnimEventMarker.stringArg.
        -- The demo also wires this cue on the C++ world bus; playing here proves the
        -- Lua→audio binding path end to end (polyphony makes the double-post harmless).
        mye.audio.play_cue(payload.arg or "cue_footstep", p.x, p.y)
    end
end

function Player:on_hot_reload()
    -- Called after a live .lua edit; self.state is preserved (on_init is NOT re-run).
    self.state.reloaded = true
    self.state.reloadCount = (self.state.reloadCount or 0) + 1
    mye.log("player: " .. "hot reloaded (speed=" .. tostring(self.state.speed) .. ")")
end

return Player
