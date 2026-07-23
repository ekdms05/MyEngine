-- intro_cutscene.lua — opening cinematic for the village demo (runs once on boot).
--
-- Demonstrates the CutsceneRuntime primitives end-to-end (docs/06 §7): camera focus, a spoken line,
-- and a scripted move. The integration agent starts this as a coroutine after the scene loads (e.g.
-- mye.co.start(require("intro_cutscene").play)). It is a plain module with a `play` function, not a
-- component, so it can be launched directly and hot-reloaded.
--
-- Coordinate note: world +Y is up. The plaza (fountain/statue + chief) is at world Y ~= 3..5,
-- world X ~= -8..-6. The player spawns at (0,-3). This intro pans the camera to the plaza, has the
-- chief welcome the player, then returns the camera to the player and hands back control.

local Intro = {}

-- entityIds is an optional table the integration agent may pass in: { player=<packed>, chief=<packed> }
function Intro.play(entityIds)
    entityIds = entityIds or {}

    -- 1) Pan the camera to the village plaza (the fountain + statue landmark).
    if mye.camera and mye.camera.focus_wait then
        mye.camera.focus_wait(-7.5, 4.0, 6.0)   -- ease toward plaza center
    end

    -- 2) Chief welcomes the arriving traveler (localized).
    if mye.dialogue.say_key then
        mye.dialogue.say_key("npc.chief.name", "dialogue.chief.greet")
    else
        mye.dialogue.say("촌장", "어서 오게, 나그네여.")
    end

    -- 3) A small scripted beat: nudge the chief a step toward the road, if we were given the handle.
    if entityIds.chief and mye.cutscene and mye.cutscene.move_to then
        mye.cutscene.move_to(entityIds.chief, -6.5, 3.0, 2.0)
    end

    -- 4) Return the camera to the player and release control.
    if mye.camera and mye.camera.focus_wait then
        local px, py = 0.0, -3.0
        if _player_pos then px, py = _player_pos.x, _player_pos.y end
        mye.camera.focus_wait(px, py, 8.0)
    end

    mye.log("village.intro: cutscene complete — control returned to player")
end

return Intro
