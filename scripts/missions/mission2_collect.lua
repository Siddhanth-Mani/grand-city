-- Mission 2: City Pickup (Grand City 6)
-- Collect 5 packages scattered across the city.
mission = { id = "collect", name = "City Pickup", reward = 750, timeLimit = 180 }
packages = 0
function onStart()
  packages = 0
  gc6_setObjective("Collect 5 packages scattered across the city.")
  gc6_log("Mission started: City Pickup")
end
function onPackageCollected()
  packages = packages + 1
  gc6_log("Package " .. packages .. " of 5 collected.")
  if packages >= 5 then
    gc6_giveMoney(mission.reward)
    gc6_log("All packages collected! +$750")
  end
end
