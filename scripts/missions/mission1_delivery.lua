-- Mission 1: Harbour Delivery (Grand City 6)
-- Drive the package to the harbour before the timer runs out.
mission = { id = "delivery", name = "Harbour Delivery", reward = 500, timeLimit = 120 }
function onStart()
  gc6_setObjective("Drive the package to the harbour marker before time runs out.")
  gc6_log("Mission started: Harbour Delivery")
end
function onUpdate(dt)
  -- The C++ side moves the marker and checks arrival; Lua owns the rules.
end
function onArrive()
  gc6_giveMoney(mission.reward)
  gc6_log("Delivery complete! +$500")
end
