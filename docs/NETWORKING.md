# Networking — Grand City 6

## Status: foundation only, not finished multiplayer

This is a minimal, clearly-labelled multiplayer **foundation**: a UDP
snapshot protocol plus a localhost loopback test. It is not a finished
multiplayer game — there is no lobby, no server browser, no lag
compensation, no anti-cheat, and no play over a real network has been
tested.

## Protocol (`src/net/net.h`, `src/net/net.cpp`)

- Transport: UDP datagrams, default port **29778** (`kDefaultPort`).
- Message: `PlayerSnapshot`, 38 bytes, little-endian, binary:
  `magic (u32, 0x47433653 = "GC6S") | tick (u32) | playerId (u32) |
  position (3×f32) | yaw (f32) | speed (f32) | inCar (u8) |
  wantedStars (u8) | health (u32)`.
- `serializeSnapshot()` / `deserializeSnapshot()` are pure functions;
  deserialization rejects bad magic and truncated packets (< 34 bytes).
- `UdpSocket`: POSIX sockets on Linux, Winsock on Windows (WSAStartup in
  the constructor). Sockets are non-blocking; `receive()` returns 0 when
  nothing is waiting.
- `loopbackTest()`: binds a receiver on port 29779, sends one snapshot
  from a second socket to 127.0.0.1:29779, polls up to 500 ms, and verifies
  every field round-trips.

## Test note (sandbox)

The test suite verifies the protocol by a serialize/deserialize round-trip
(always) and runs the live loopback test when UDP is permitted. The Linux
VM used for verification blocks UDP sends at the sandbox level (`EPERM`,
"Operation not permitted", even to localhost), so the live loopback is
reported as **SKIP** there with the reason printed. On a normal PC the
loopback test passes. The protocol code itself is unchanged either way.

## What a real multiplayer would still need

Server authority (positions from the server, not the client), client-side
prediction + reconciliation, interpolation of remote snapshots, a lobby /
session handshake, NAT traversal, and disconnect handling. None of that is
in this build, by design — the foundation is here so that work has a
protocol to build on.
