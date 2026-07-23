-- npc_common.lua — shared NPC interaction helpers for village_demo.
--
-- Not a component class; a plain module `require`d by each NPC script. Provides:
--   proximity(self, radius)  -> true if the player is within `radius` world units (uses _player_pos
--                               published by player.lua each frame).
--   want_interact()          -> true on the frame the interact key (E) is pressed.
--   run_dialogue(co_fn)      -> starts a cutscene coroutine running co_fn (which uses mye.dialogue.*).
--
-- Dialogue lines are addressed by localization key so the same script works in ko/en (Loc resolves
-- the current locale). This keeps ALL conversation logic in hot-reloadable Lua (demo requirement:
-- edit dialogue/logic without an engine rebuild).

local M = {}

function M.proximity(self, radius)
    if _player_pos == nil then return false end
    local e = mye.world.entity_from_packed(self.entity)
    if not e:is_valid() then return false end
    local p = e:get_position()
    local dx = _player_pos.x - p.x
    local dy = _player_pos.y - p.y
    return (dx * dx + dy * dy) <= (radius * radius)
end

function M.want_interact()
    -- E to talk. mye.input.was_pressed is edge-triggered (one frame).
    if mye.input and mye.input.was_pressed then
        return mye.input.was_pressed(mye.Key.E)
    end
    return false
end

-- Convenience: a localized `say` (speaker key + body key) with a fallback to raw say.
function M.say_key(speakerKey, bodyKey)
    if mye.dialogue.say_key then
        mye.dialogue.say_key(speakerKey, bodyKey)
    else
        local sp = mye.loc and mye.loc.text(speakerKey) or speakerKey
        local bd = mye.loc and mye.loc.text(bodyKey) or bodyKey
        mye.dialogue.say(sp, bd)
    end
end

-- Localized choose: options is a list of loc keys; returns picked index (0-based).
function M.choose_keys(keys)
    local labels = {}
    for i, k in ipairs(keys) do
        labels[i] = (mye.loc and mye.loc.text(k)) or k
    end
    return mye.dialogue.choose(labels)
end

-- Run a conversation coroutine. Prefer the engine coroutine scheduler; fall back to a bare coroutine
-- pumped by the caller's on_update if mye.co.start is unavailable.
function M.run(self, fn)
    if mye.co and mye.co.start then
        mye.co.start(fn)
    else
        self.state._co = coroutine.create(fn)
        coroutine.resume(self.state._co)
    end
end

-- Pump a fallback coroutine (only needed when mye.co.start is absent). No-op otherwise.
function M.pump(self)
    local co = self.state._co
    if co and coroutine.status(co) ~= "dead" then
        coroutine.resume(co)
    end
end

return M
