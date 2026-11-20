-- mission4_taxi.lua — City Taxi (fare mission).
-- Pick up a passenger at the marker, drop them off at the destination.
-- Each on-time drop-off earns a fare; the mission completes after 3 fares.
-- API (from the engine): game.setObjective(text), game.setMarker(x, z),
-- game.distanceTo(x, z), game.addMoney(n), game.completeMission(),
-- game.failMission(reason), game.setTimer(seconds), game.timeLeft().
local pickups = {
    {x = 40.0, z = -30.0},
    {x = -45.0, z = 25.0},
    {x = 20.0, z = 55.0},
}
local dropoffs = {
    {x = -20.0, z = -55.0},
    {x = 55.0, z = 10.0},
    {x = -60.0, z = -10.0},
}
local state = {fare = 1, hasPassenger = false}
function onStart()
    state.fare = 1
    state.hasPassenger = false
    game.setObjective("Pick up the passenger (fare 1/3)")
    game.setMarker(pickups[1].x, pickups[1].z)
    game.setTimer(240)
end
function onUpdate(dt)
    local target = state.hasPassenger and dropoffs[state.fare] or pickups[state.fare]
    if game.distanceTo(target.x, target.z) < 4.0 then
        if state.hasPassenger then
            game.addMoney(300)
            game.setObjective("Fare " .. state.fare .. " complete: $300 earned")
            state.fare = state.fare + 1
            state.hasPassenger = false
            if state.fare > 3 then
                game.completeMission()
                return
            end
            game.setObjective("Pick up the passenger (fare " .. state.fare .. "/3)")
            game.setMarker(pickups[state.fare].x, pickups[state.fare].z)
        else
            state.hasPassenger = true
            game.setObjective("Passenger aboard — drop off at the marker")
            game.setMarker(dropoffs[state.fare].x, dropoffs[state.fare].z)
        end
    end
    if game.timeLeft() <= 0 then game.failMission("Out of time — the fares went to someone else") end
end
