#pragma once
// Minimal multiplayer foundation: a UDP snapshot protocol. This is the
// transport + serialisation only — a finished netcode layer (prediction,
// reconciliation, lobbies, anti-cheat) is a much bigger job and is NOT
// claimed here. What exists: a fixed binary snapshot of one player's state,
// send/receive over UDP, and a localhost loopback test that proves a
// snapshot sent by one socket is received intact by another.
// See docs/NETWORKING.md for the packet layout and the road ahead.
#include "core/math.h"
#include <cstdint>
#include <string>
#include <vector>
namespace gc6 {
constexpr uint32_t kSnapshotMagic = 0x47433653; // "GC6S"
constexpr uint16_t kDefaultPort = 29777;
struct PlayerSnapshot {
    uint32_t magic = kSnapshotMagic;
    uint32_t tick = 0;
    uint32_t playerId = 0;
    Vec3 position{0, 0, 0};
    float yaw = 0.0f;
    float speed = 0.0f;
    uint8_t inCar = 0;
    uint8_t wantedStars = 0;
    uint16_t health = 100;
};
std::vector<uint8_t> serializeSnapshot(const PlayerSnapshot& s);
bool deserializeSnapshot(const uint8_t* data, size_t size, PlayerSnapshot& out);
// Minimal UDP socket wrapper (POSIX on Linux/macOS, Winsock on Windows).
class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    // loopbackOnly binds 127.0.0.1 (no firewall prompt, unreachable from the
    // network); otherwise all interfaces.
    bool bind(uint16_t port, bool loopbackOnly = false);
    bool sendTo(const std::string& host, uint16_t port, const uint8_t* data, size_t size);
    // Non-blocking receive: returns bytes read, 0 = nothing waiting, -1 = error.
    int receive(uint8_t* buffer, size_t capacity, std::string& fromHost, uint16_t& fromPort);
    bool valid() const { return fd_ >= 0; }
private:
    int fd_ = -1;
};
// Loopback self-test: bind two sockets, send a snapshot from one, receive
// it on the other, and compare every field. Returns true on an exact match.
bool loopbackTest();
} // namespace gc6
