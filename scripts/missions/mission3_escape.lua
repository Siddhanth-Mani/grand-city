-- Mission 3: Lose the Heat (Grand City 6)
-- Lose a 3-star wanted level and reach the safehouse.
mission = { id = "escape", name = "Lose the Heat", reward = 1000, timeLimit = 150 }
function onStart()
  gc6_setWanted(3)
  gc6_setObjective("Lose the police and reach the safehouse.")
  gc6_log("Mission started: Lose the Heat — wanted level 3")
end
function onUpdate(dt)
  -- C++ wanted system decays heat when no police are near.
end
function onSafehouseReached()
  gc6_giveMoney(mission.reward)
  gc6_log("You lost them. +$1000")
end
