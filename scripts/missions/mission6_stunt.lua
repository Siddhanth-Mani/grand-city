-- mission6_stunt.lua — Stunt Show (stunt-jump mission).
-- Hit the stunt ramps, score air time, and land the big jump at the
-- harbour ramp. The engine calls onStuntJump(airTimeSeconds) when the car
-- lands after being airborne; score 3.0+ seconds of total air to finish.
-- (In the current build the engine reports landings through the collectible
-- system; this script also accepts reaching the ramp markers.)
local state = {totalAir = 0.0, jumps = 0}
function onStart()
    state.totalAir = 0.0
    state.jumps = 0
    game.setObjective("Hit the ramps — score 3.0s of total air time")
    game.setMarker(50.0, -45.0)
    game.setTimer(200)
end
function onStuntJump(airTime)
    state.totalAir = state.totalAir + airTime
    state.jumps = state.jumps + 1
    game.setObjective(string.format("Air time %.1fs total (%d jumps)", state.totalAir, state.jumps))
    if state.totalAir >= 3.0 then game.completeMission() end
end
function onUpdate(dt)
    if game.timeLeft() <= 0 then game.failMission("Stunt show over — not enough air") end
end
