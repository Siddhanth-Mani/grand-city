# Vehicle physics

The model is a bicycle model with an engine curve, drift, body roll/pitch,
and per-wheel spring suspension. It is arcade-realistic: believable at the
wheel, forgiving enough to be fun, and fully deterministic at a fixed 60 Hz
step. All formulas live in `src/physics/vehicle.cpp`.

## Longitudinal

```
engineCurve = clamp(1 - speed / topSpeed, 0, 1)
F_engine  = throttle * engineForce * (0.35 + 0.65 * engineCurve)
F_brake   = brake * brakeForce
F_drag    = dragCoeff * speed * |speed| * 10
F_roll    = rollingResistance (sign of speed)
a         = (F_engine - F_brake - F_drag - F_roll) / mass
speed    += a * dt          (clamped to topSpeed, reverse cap -12 m/s)
```

The engine curve means the car pulls hard off the line and tapers near top
speed, like a real car. Drag is quadratic, so top speed is self-limiting.

## Steering and yaw (bicycle model)

```
steerTarget = maxSteer * steerInput / (1 + |speed| * 0.045)
steer      -> steerTarget (smoothed, 8/s)
yaw       += (speed / wheelBase) * tan(steer) * dt
```

Steering angle falls with speed, so the car is nimble in a car park and
stable on a highway.

## Drift

Lateral velocity decays exponentially with the grip rate, except when the
steering angle is large at high speed, where grip drops to 35% and the car
slides: `lateralVel *= exp(-grip * dt)`. Handbrake (Space) is wired as full
brake here; a dedicated handbrake that cuts rear grip is the next step.

## Body roll and pitch

```
targetRoll  = -steer * speed * 0.02
targetPitch = -accel * 0.004
```
Both are smoothed (5/s) — the body leans into corners and dives under
braking, which is most of what "feels real" at the wheel.

## Suspension

Per wheel, a spring-damper against a procedural road surface:

```
compression = restLength - (wheelY - roadHeight(x, z))
springForce = stiffness * compression
```

Travel is exposed per wheel (`suspensionTravel()`), so the renderer can
visually compress the wheels. The road surface is a gentle sine field; a
heightmap is the next step.

## Collision

Cars and buildings are AABBs. On overlap, the car is pushed out along the
smallest penetration axis, speed is cut to 35%, and the crash is flagged
(`crashed()`), which feeds the wanted system. Pedestrians are spheres
against the same AABBs.

## Per-car tuning

See `data/cars.json`. The Comet GT (sports) has more force, more grip and a
higher top speed; the Bison 4x4 (SUV) is heavier with softer handling; the
Meridian (sedan) sits in between. Tune `engineForce`, `grip`, `topSpeed`
and `suspensionStiffness` to change the feel — no code changes needed for
the numbers, but the JSON is currently documentation; wiring the loader into
`World::init` is the next step.

## v2: full drivetrain

Engine torque by RPM, gearbox with real gears, clutch slip on launch:

```
torque(rpm)   = peak * (0.72 + 0.28 * (rpm-idle)/(peakRpm-idle))   below peak
              = peak * (1 - 0.45 * (rpm-peakRpm)/(redline-peakRpm)) above peak
rpm(speed)    = 60 * (|speed| / (2π * wheelRadius)) * gearRatio[gear] * finalDrive
wheelTorque   = torque * gearRatio[gear] * finalDrive * clutch
clutch        = 0.55 + 0.45 * clamp(speed / 8) on launch, 0.25 while shifting
shift         = up when rpm > 0.92 * redline, down when rpm < 1400
```

Traction: the driven axle can only push `maxDrive = drivenLoad * grip`,
where drivenLoad is 95% of the weight for AWD/4x4 and ~55% for RWD/FWD,
plus/minus weight transfer (`driveForce * cgHeight / wheelBase`, helps RWD,
hurts FWD). Traction control caps the force at 1.05× the limit; without it
the wheels spin up to 1.25×. Wheelspin also feeds the skid sound and the
tire model. Per-drivetrain behaviour: AWD grips (fastest wet launch), RWD
slides on power (oversteer factor 1.15 when spinning), FWD understeers
(yaw factor 0.85).

## v2: Pacejka tyres

Simplified magic formula (`src/physics/tires.cpp`):

```
F = D * sin(C * atan(B*slip - E*(B*slip - atan(B*slip))))
```

`B/C/D/E` per surface (tarmac D=1.0, wet D=0.62, gravel D=0.45); peak force
scales with vertical load, and a friction circle caps the combined
lateral+longitudinal force at the peak. Surface grip also multiplies the
lateral decay rate in the vehicle model, and rain (weather wetness) maps
tarmac to the wet parameters.

## v2: raycast suspension (revised)

Per wheel, a ray from the mount down to the procedural road surface:

```
compression = clamp(staticSag + (wheelGround - centerGround), 0, 0.3)
springForce = stiffness * compression
damperForce = damping * d(compression)/dt
```

`staticSag` is 0.15 m (the car sits on its springs at rest); a bump under a
wheel pushes it up, a dip unloads it. Travel is exposed per wheel.

## v2: weight transfer, aero, brake fade, damage

```
longTransfer = mass * accel * cgHeight / wheelBase     (front <-> rear)
latTransfer  = mass * latAccel * cgHeight / trackWidth  (side to side)
drag         = 0.5 * 1.225 * Cd * frontalArea * speed * |speed|
downforce    = downforceCoeff * speed^2   (adds grip load at speed)
brakeFade    = clamp(1 - (tempC - 350) / 1000, 0.70, 1.0)
               tempC rises with braking energy, cools toward 60 C
damagePower  = clamp(1 - 0.45 * damage, 0.55, 1.0)
damageSteer  = clamp(1 - 0.35 * damage, 0.65, 1.0)
```

ABS: hard braking on low grip pulses the brakes (~8 Hz) instead of
locking; measured stopping distance on wet road is shorter with ABS than
without. Damage comes from crashes (impact speed × 0.008); above 0.6 the
engine smokes and steering degrades.

## v2: OBB + swept collision

Cars are oriented boxes (OBB, yaw-aware). `obbOverlap` uses the separating
axis theorem on the two yaw axes; `collideWith` separates the cars along
the minimum translation vector and exchanges speed. Fast cars move in
sub-steps (max 0.5 m per step) so they cannot tunnel through walls at
33 ms steps — tested at 95 m/s top speed.

## Per-car tuning (v2)

`data/cars.json` now holds 6 cars with full specs (horsepower, torque
curve, weight, weight distribution, drivetrain, top speed, 0–100, gear
ratios, price). The loader (`src/game/car_data.cpp`) feeds them into the
garage and the player's car; upgrades multiply the numbers (see
`src/game/economy.cpp`). The old note that the JSON was "documentation
only" no longer applies — it is wired in.
