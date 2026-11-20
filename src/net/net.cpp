#include "net/net.h"
#include "core/logger.h"
#include <cmath>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace gc6 {
namespace {
void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
void putF32(std::vector<uint8_t>& b, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    putU32(b, v);
}
uint32_t getU32(const uint8_t* d, size_t off) {
    return static_cast<uint32_t>(d[off]) | (static_cast<uint32_t>(d[off + 1]) << 8) |
           (static_cast<uint32_t>(d[off + 2]) << 16) | (static_cast<uint32_t>(d[off + 3]) << 24);
}
float getF32(const uint8_t* d, size_t off) {
    uint32_t v = getU32(d, off);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
} // namespace
std::vector<uint8_t> serializeSnapshot(const PlayerSnapshot& s) {
    std::vector<uint8_t> b;
    b.reserve(40);
    putU32(b, s.magic);
    putU32(b, s.tick);
    putU32(b, s.playerId);
    putF32(b, s.position.x); putF32(b, s.position.y); putF32(b, s.position.z);
    putF32(b, s.yaw);
    putF32(b, s.speed);
    b.push_back(s.inCar);
    b.push_back(s.wantedStars);
    putU32(b, s.health);
    return b;
}
bool deserializeSnapshot(const uint8_t* data, size_t size, PlayerSnapshot& out) {
    if (size < 34 || !data) return false;
    if (getU32(data, 0) != kSnapshotMagic) return false;
    out.magic = getU32(data, 0);
    out.tick = getU32(data, 4);
    out.playerId = getU32(data, 8);
    out.position = Vec3(getF32(data, 12), getF32(data, 16), getF32(data, 20));
    out.yaw = getF32(data, 24);
    out.speed = getF32(data, 28);
    out.inCar = data[32];
    out.wantedStars = data[33];
    out.health = size >= 38 ? static_cast<uint16_t>(getU32(data, 34)) : 100;
    return true;
}
UdpSocket::UdpSocket() {
#ifdef _WIN32
    static bool wsaInit = false;
    if (!wsaInit) {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) wsaInit = true;
    }
#endif
    fd_ = static_cast<int>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
}
UdpSocket::~UdpSocket() {
    if (fd_ >= 0) {
#ifdef _WIN32
        closesocket(static_cast<SOCKET>(fd_));
#else
        close(fd_);
#endif
    }
}
bool UdpSocket::bind(uint16_t port, bool loopbackOnly) {
    if (fd_ < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(static_cast<SOCKET>(fd_), FIONBIO, &mode);
#else
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
#endif
    return true;
}
bool UdpSocket::sendTo(const std::string& host, uint16_t port, const uint8_t* data, size_t size) {
    if (fd_ < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) return false;
    auto sent = sendto(fd_, reinterpret_cast<const char*>(data), static_cast<int>(size), 0,
                       reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<int>(size);
}
int UdpSocket::receive(uint8_t* buffer, size_t capacity, std::string& fromHost, uint16_t& fromPort) {
    if (fd_ < 0) return -1;
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    auto n = recvfrom(fd_, reinterpret_cast<char*>(buffer), static_cast<int>(capacity), 0,
                      reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (n < 0) return 0; // nothing waiting (non-blocking) or error; treated as empty
    char ip[64]{};
    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
    fromHost = ip;
    fromPort = ntohs(from.sin_port);
    return static_cast<int>(n);
}
bool loopbackTest() {
    UdpSocket sender, receiver;
    if (!receiver.bind(kDefaultPort + 1, true)) { Logger::warn("Net: loopback bind failed"); return false; }
    PlayerSnapshot s;
    s.tick = 1234; s.playerId = 7;
    s.position = Vec3(12.5f, 0.0f, -44.0f);
    s.yaw = 1.25f; s.speed = 23.4f; s.inCar = 1; s.wantedStars = 3; s.health = 82;
    auto bytes = serializeSnapshot(s);
    if (!sender.sendTo("127.0.0.1", kDefaultPort + 1, bytes.data(), bytes.size())) return false;
    // Poll briefly for the packet (UDP on loopback is near-instant).
    for (int attempt = 0; attempt < 50; ++attempt) {
        uint8_t buf[128]{};
        std::string from;
        uint16_t port = 0;
        int n = receiver.receive(buf, sizeof(buf), from, port);
        if (n > 0) {
            PlayerSnapshot got;
            if (!deserializeSnapshot(buf, static_cast<size_t>(n), got)) return false;
            return got.tick == s.tick && got.playerId == s.playerId &&
                   std::fabs(got.position.x - s.position.x) < 0.001f &&
                   std::fabs(got.speed - s.speed) < 0.001f && got.wantedStars == s.wantedStars &&
                   got.health == s.health;
        }
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }
    return false;
}
} // namespace gc6
