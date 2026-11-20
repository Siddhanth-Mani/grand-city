-- mission5_escape_time.lua — Four-Star Escape (police-escape time trial).
-- Survive 120 seconds at wanted level 4, then reach the hideout marker.
-- The engine ramps police pressure; this script only tracks the timer.
local state = {survived = false}
function onStart()
    state.survived = false
    game.setWanted(4)
    game.setObjective("Survive 120 seconds at 4 stars, then reach the hideout")
    game.setTimer(180)
end
function onUpdate(dt)
    local elapsed = 180 - game.timeLeft()
    if not state.survived and elapsed >= 120 then
        state.survived = true
        game.setObjective("You lost them — reach the hideout marker")
        game.setMarker(-60.0, 55.0)
    end
    if state.survived and game.distanceTo(-60.0, 55.0) < 4.0 then
        game.setWanted(0)
        game.completeMission()
        return
    end
    if game.timeLeft() <= 0 then game.failMission("Caught before the hideout") end
end
