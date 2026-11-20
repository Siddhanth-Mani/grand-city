#include "game/overlay.h"
#include "core/logger.h"
#include "game/world.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
namespace gc6 {
namespace {
const Vec4 kPanel{0.04f, 0.05f, 0.07f, 0.82f};
const Vec4 kPanelSoft{0.04f, 0.05f, 0.07f, 0.62f};
const Vec4 kText{0.95f, 0.95f, 0.93f, 1.0f};
const Vec4 kMuted{0.62f, 0.64f, 0.66f, 1.0f};
const Vec4 kDisabled{0.40f, 0.41f, 0.43f, 1.0f};
const Vec4 kAccent{1.00f, 0.71f, 0.20f, 1.0f};
const Vec4 kInk{0.06f, 0.06f, 0.07f, 1.0f};
const Vec4 kGood{0.35f, 0.85f, 0.45f, 1.0f};
const Vec4 kBad{0.95f, 0.32f, 0.28f, 1.0f};
constexpr float kW = 1280.0f, kH = 720.0f;

std::string money(int amount) {
    std::string digits = std::to_string(amount < 0 ? -amount : amount);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (amount < 0 ? "-$" : "$") + out;
}
void centred(IRenderer& r, float y, float size, const std::string& text, const Vec4& c) {
    r.drawText2D((kW - IRenderer::textWidth(text, size)) * 0.5f, y, size, text, c);
}
void rightAligned(IRenderer& r, float right, float y, float size, const std::string& text, const Vec4& c) {
    r.drawText2D(right - IRenderer::textWidth(text, size), y, size, text, c);
}
const char* stateTitle(MenuState s) {
    switch (s) {
        case MenuState::Settings: return "SETTINGS";
        case MenuState::Garage: return "GARAGE";
        case MenuState::MissionSelect: return "MISSIONS";
        case MenuState::HowToPlay: return "HOW TO PLAY";
        case MenuState::Paused: return "PAUSED";
        default: return "GRAND CITY";
    }
}
// Left-hand menu panel shared by the title, settings, missions and pause screens.
void menuPanel(IRenderer& r, const std::string& title, const std::string& subtitle,
               const std::vector<MenuButton>& buttons, int selection,
               const std::vector<std::string>& badges = {}) {
    r.drawRect2D(0, 0, 470, kH, kPanel);
    r.drawRect2D(470, 0, 3, kH, Vec4(kAccent.x, kAccent.y, kAccent.z, 0.85f));
    r.drawText2D(56, 70, 54, title, kText);
    r.drawText2D(58, 140, 16, subtitle, kMuted);
    float y = 200;
    for (size_t i = 0; i < buttons.size(); ++i) {
        bool sel = static_cast<int>(i) == selection;
        const auto& b = buttons[i];
        if (sel) r.drawRect2D(40, y - 9, 400, 40, kAccent);
        Vec4 c = sel ? kInk : (b.enabled ? kText : kDisabled);
        r.drawText2D(58, y, 21, b.label, c);
        if (i < badges.size() && !badges[i].empty())
            rightAligned(r, 424, y + 3, 15, badges[i], sel ? kInk : kMuted);
        y += 46;
    }
    r.drawText2D(58, kH - 52, 15, "Up/Down  choose     E  confirm     Esc  back", kMuted);
}
void starsRow(IRenderer& r, float right, float y, int filled, int total, float size) {
    for (int i = 0; i < total; ++i) {
        float x = right - (total - i) * (size + 6);
        bool on = i < filled;
        r.drawRect2D(x, y, size, size, on ? kAccent : Vec4(1, 1, 1, 0.18f));
    }
}
void minimap(IRenderer& r, const HudData& hud) {
    const float x0 = 24, y0 = kH - 24 - 210, size = 210, range = 140.0f; // metres from centre to edge
    r.drawRect2D(x0 - 3, y0 - 3, size + 6, size + 6, Vec4(0, 0, 0, 0.55f));
    r.drawRect2D(x0, y0, size, size, Vec4(0.13f, 0.15f, 0.17f, 0.88f));
    const Vec3 c = hud.playerPos;
    auto toMap = [&](const Vec3& p, float& mx, float& my) {
        float dx = (p.x - c.x) / range, dz = (p.z - c.z) / range;
        mx = x0 + size * 0.5f + dx * size * 0.5f;
        my = y0 + size * 0.5f - dz * size * 0.5f;
        return std::fabs(dx) < 0.97f && std::fabs(dz) < 0.97f;
    };
    // Road grid every 40 m.
    for (float gx = std::floor((c.x - range) / 40.0f) * 40.0f; gx <= c.x + range; gx += 40.0f) {
        float mx, my;
        if (toMap(Vec3(gx, 0, c.z), mx, my)) r.drawRect2D(mx - 3, y0, 6, size, Vec4(0.30f, 0.32f, 0.35f, 0.9f));
    }
    for (float gz = std::floor((c.z - range) / 40.0f) * 40.0f; gz <= c.z + range; gz += 40.0f) {
        float mx, my;
        if (toMap(Vec3(c.x, 0, gz), mx, my)) r.drawRect2D(x0, my - 3, size, 6, Vec4(0.30f, 0.32f, 0.35f, 0.9f));
    }
    auto dot = [&](const Vec3& p, float s, const Vec4& col) {
        float mx, my;
        if (toMap(p, mx, my)) r.drawRect2D(mx - s * 0.5f, my - s * 0.5f, s, s, col);
    };
    for (const auto& p : hud.packagePositions) dot(p, 5, Vec4(1.0f, 0.85f, 0.1f, 1));
    for (const auto& p : hud.rampPositions) dot(p, 6, Vec4(0.95f, 0.45f, 0.1f, 1));
    for (const auto& p : hud.trafficPositions) dot(p, 5, Vec4(0.75f, 0.75f, 0.78f, 1));
    for (const auto& p : hud.policePositions) dot(p, 7, Vec4(0.25f, 0.45f, 1.0f, 1));
    for (const auto& p : hud.roadblockPositions) dot(p, 8, kBad);
    if (hud.missionTarget.x != 0.0f || hud.missionTarget.z != 0.0f) {
        float mx, my;
        if (toMap(hud.missionTarget, mx, my)) {
            r.drawRect2D(mx - 6, my - 6, 12, 12, kGood);
        } else {
            // Off the map: clamp an arrow-like marker to the edge.
            float dx = hud.missionTarget.x - c.x, dz = hud.missionTarget.z - c.z;
            float m = std::max(std::fabs(dx), std::fabs(dz));
            float ex = x0 + size * 0.5f + dx / m * size * 0.46f;
            float ey = y0 + size * 0.5f - dz / m * size * 0.46f;
            r.drawRect2D(ex - 5, ey - 5, 10, 10, kGood);
        }
    }
    r.drawRect2D(x0 + size * 0.5f - 5, y0 + size * 0.5f - 5, 10, 10, kText);
    r.drawText2D(x0 + 6, y0 + 6, 13, "N", kMuted);
}
void hud(IRenderer& r, World& world, const OverlayState& st) {
    const HudData& h = world.hud();
    if (h.photoMode) {
        r.drawText2D(24, 22, 15, "PHOTO MODE  -  arrows move the camera, P to exit", Vec4(1, 1, 1, 0.75f));
        return;
    }
    // Objective (top centre).
    if (!h.objective.empty()) {
        float w = std::min(IRenderer::textWidth(h.objective, 17) + 40, 1000.0f);
        r.drawRect2D((kW - w) * 0.5f, 18, w, 40, kPanelSoft);
        centred(r, 29, 17, h.objective, kText);
    }
    if (h.missionTimeLeft > 0.0f) {
        int t = static_cast<int>(std::ceil(h.missionTimeLeft));
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d:%02d", t / 60, t % 60);
        bool urgent = t <= 15;
        centred(r, 68, 30, buf, urgent && std::fmod(st.timeSeconds, 1.0f) < 0.5f ? kBad : kText);
    }
    // Money, wanted level and clock (top right).
    r.drawRect2D(kW - 260, 18, 242, 104, kPanelSoft);
    rightAligned(r, kW - 34, 28, 30, money(h.money), kGood);
    starsRow(r, kW - 34, 72, h.wantedStars, 5, 14);
    int hh = static_cast<int>(h.timeOfDay) % 24, mm = static_cast<int>((h.timeOfDay - std::floor(h.timeOfDay)) * 60);
    char clock[64];
    std::snprintf(clock, sizeof(clock), "%02d:%02d  %s", hh, mm, h.weatherName.c_str());
    r.drawText2D(kW - 244, 96, 14, clock, kMuted);
    // Speedometer (bottom right), or the enter-car hint when walking.
    if (h.onFoot) {
        r.drawRect2D(kW - 300, kH - 74, 282, 56, kPanelSoft);
        r.drawText2D(kW - 282, kH - 58, 17, "On foot  -  E to enter a car", kText);
    } else {
        r.drawRect2D(kW - 250, kH - 132, 232, 114, kPanelSoft);
        rightAligned(r, kW - 92, kH - 120, 60, std::to_string(static_cast<int>(std::round(h.speedKmh))), kText);
        r.drawText2D(kW - 84, kH - 88, 17, "km/h", kMuted);
        r.drawText2D(kW - 84, kH - 116, 22, h.gear < 0 ? "R" : (h.gear == 0 ? "N" : "G" + std::to_string(h.gear)), kAccent);
        float rpm = clampf(h.rpm / std::max(h.redlineRpm, 1000.0f), 0.0f, 1.0f);
        r.drawRect2D(kW - 236, kH - 40, 204, 8, Vec4(1, 1, 1, 0.15f));
        r.drawRect2D(kW - 236, kH - 40, 204 * rpm, 8, rpm > 0.9f ? kBad : kAccent);
        if (h.damage > 0.05f) {
            r.drawText2D(kW - 236, kH - 160, 14, "Damage", kMuted);
            r.drawRect2D(kW - 160, kH - 156, 128, 6, Vec4(1, 1, 1, 0.15f));
            r.drawRect2D(kW - 160, kH - 156, 128 * clampf(h.damage, 0, 1), 6, kBad);
        }
        r.drawText2D(kW - 250, kH - 162 - (h.damage > 0.05f ? 22 : 0), 13, h.radioStation, kMuted);
    }
    // Minimap + feed (bottom left).
    minimap(r, h);
    char packages[96];
    std::snprintf(packages, sizeof(packages), "Packages %d/%d   Best stunt %d pts", h.packagesFound,
                  h.packagesTotal, h.bestStunt);
    r.drawText2D(24, kH - 260, 14, packages, kMuted);
    // Message feed, newest at the bottom, wrapped to ~64 characters.
    std::vector<std::string> lines;
    for (const auto& m : h.messages) {
        std::string rest = m;
        while (rest.size() > 64) {
            size_t cut = rest.rfind(' ', 64);
            if (cut == std::string::npos || cut == 0) cut = 64;
            lines.push_back(rest.substr(0, cut));
            rest = rest.substr(cut + (rest[cut] == ' ' ? 1 : 0));
        }
        lines.push_back(rest);
    }
    if (lines.size() > 8) lines.erase(lines.begin(), lines.end() - 8);
    float fy = kH - 290 - 22.0f * static_cast<float>(lines.size());
    for (const auto& line : lines) {
        fy += 22;
        r.drawText2D(24, fy, 15, line, kText);
    }
}
void garage(IRenderer& r, World& world, const OverlayState& st) {
    Garage& g = world.garage();
    r.drawRect2D(0, 0, kW, kH, Vec4(0.03f, 0.035f, 0.05f, 0.55f));
    r.drawRect2D(0, 0, 520, kH, kPanel);
    r.drawText2D(56, 60, 48, "GARAGE", kText);
    r.drawText2D(58, 122, 18, "Balance  " + money(g.money()), kGood);
    float y = 180;
    for (int i = 0; i < g.carCount(); ++i) {
        const CarDef& c = g.car(i);
        bool sel = i == st.garageSelection;
        if (sel) r.drawRect2D(40, y - 9, 450, 42, kAccent);
        Vec4 col = sel ? kInk : kText;
        r.drawText2D(58, y, 21, c.params.name, col);
        std::string tag = g.currentCar() == i ? "DRIVING" : (g.owns(i) ? "OWNED" : money(c.price));
        rightAligned(r, 474, y + 3, 16, tag, sel ? kInk : (g.owns(i) ? kGood : kMuted));
        y += 50;
    }
    r.drawText2D(58, kH - 78, 15, "E  buy / drive      P  paint ($200)", kMuted);
    r.drawText2D(58, kH - 52, 15, "R  engine upgrade   F  brakes upgrade   Esc  back", kMuted);
    // Spec card for the highlighted car.
    const CarDef& c = g.car(st.garageSelection);
    const VehicleParams& p = c.params;
    float x = 600, cy = 150;
    r.drawRect2D(x - 30, cy - 40, 640, 420, kPanel);
    r.drawText2D(x, cy - 16, 36, p.name, kText);
    std::string cls = p.carClass;
    if (!cls.empty()) cls[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cls[0])));
    r.drawText2D(x, cy + 34, 17, cls, kAccent);
    const char* drive = p.drivetrain == Drivetrain::AWD ? "AWD"
                        : p.drivetrain == Drivetrain::FourWD ? "4x4"
                        : p.drivetrain == Drivetrain::FWD ? "FWD" : "RWD";
    char line[128];
    std::vector<std::pair<std::string, std::string>> rows;
    std::snprintf(line, sizeof(line), "%.0f hp  /  %.0f Nm", p.horsepower, p.peakTorqueNm);
    rows.push_back({"Power", line});
    std::snprintf(line, sizeof(line), "%.1f s", p.zeroToHundred);
    rows.push_back({"0-100 km/h", line});
    std::snprintf(line, sizeof(line), "%.0f km/h", p.topSpeed * 3.6f);
    rows.push_back({"Top speed", line});
    std::snprintf(line, sizeof(line), "%.0f kg, %.0f%% front", p.massKg, p.weightDistFront * 100.0f);
    rows.push_back({"Weight", line});
    std::snprintf(line, sizeof(line), "%s, %d gears", drive, p.gearCount);
    rows.push_back({"Drivetrain", line});
    float ry = cy + 84;
    for (const auto& row : rows) {
        r.drawText2D(x, ry, 18, row.first, kMuted);
        r.drawText2D(x + 200, ry, 18, row.second, kText);
        ry += 34;
    }
    const char* names[4] = {"Engine", "Brakes", "Tyres", "Suspension"};
    ry += 14;
    for (int k = 0; k < 4; ++k) {
        int lvl = g.upgradeLevel(st.garageSelection, static_cast<UpgradeKind>(k));
        r.drawText2D(x, ry, 16, names[k], kMuted);
        for (int s = 0; s < 3; ++s)
            r.drawRect2D(x + 200 + s * 36, ry + 2, 30, 12, s < lvl ? kAccent : Vec4(1, 1, 1, 0.15f));
        ry += 28;
    }
}
void resultCard(IRenderer& r, World& world, MenuState s) {
    GameFlow& f = world.flow();
    r.drawRect2D(0, 0, kW, kH, Vec4(0, 0, 0, 0.45f));
    r.drawRect2D(340, 200, 600, 300, kPanel);
    r.drawRect2D(340, 200, 600, 4, s == MenuState::MissionFailed ? kBad : kAccent);
    if (s == MenuState::GameComplete) {
        centred(r, 232, 34, "GAME COMPLETE", kAccent);
        float y = 296;
        std::string stats = f.finalStatsText();
        size_t start = 0;
        while (start < stats.size() && y < 450) {
            size_t end = stats.find('\n', start);
            centred(r, y, 17, stats.substr(start, end - start), kText);
            if (end == std::string::npos) break;
            start = end + 1;
            y += 26;
        }
    } else {
        bool ok = s == MenuState::MissionComplete;
        centred(r, 232, 34, ok ? "MISSION PASSED" : "MISSION FAILED", ok ? kAccent : kBad);
        centred(r, 292, 22, f.resultName(), kText);
        if (ok) {
            float total = 3 * 30.0f + 2 * 6.0f;
            starsRow(r, (kW + total) * 0.5f, 340, f.resultStars(), 3, 30);
            centred(r, 392, 22, "+" + money(f.resultMoney()), kGood);
        }
    }
    centred(r, 450, 16, "Press E to continue", kMuted);
}
} // namespace

void drawOverlay(IRenderer& r, World& world, const OverlayState& st) {
    MenuState s = world.flow().state();
    switch (s) {
        case MenuState::Playing:
            hud(r, world, st);
            break;
        case MenuState::Title:
            menuPanel(r, "GRAND CITY", "Open-world driving  -  C++17 / Direct3D 12",
                      world.flow().buttons(st.saveExists), st.menuSelection);
            break;
        case MenuState::MissionSelect: {
            std::vector<std::string> badges;
            const auto& ms = world.missions().missions();
            std::vector<bool> done(6, false);
            for (size_t i = 0; i < 6 && i < ms.size(); ++i) done[i] = ms[i].state == MissionState::Completed;
            for (int i = 0; i < 6; ++i)
                badges.push_back(done[static_cast<size_t>(i)] ? "DONE"
                                 : (world.story().isUnlocked(i, done) ? "" : "LOCKED"));
            menuPanel(r, "MISSIONS", "Finish a mission to unlock the next one",
                      world.flow().buttons(st.saveExists), st.menuSelection, badges);
            break;
        }
        case MenuState::Settings:
        case MenuState::Paused:
            if (s == MenuState::Paused) r.drawRect2D(0, 0, kW, kH, Vec4(0, 0, 0, 0.35f));
            menuPanel(r, stateTitle(s), s == MenuState::Paused ? "The city waits" : "Changes apply immediately",
                      world.flow().buttons(st.saveExists), st.menuSelection);
            break;
        case MenuState::HowToPlay: {
            r.drawRect2D(0, 0, kW, kH, Vec4(0.03f, 0.035f, 0.05f, 0.6f));
            r.drawRect2D(200, 90, 880, 540, kPanel);
            r.drawText2D(240, 120, 36, "HOW TO PLAY", kText);
            std::string text = GameFlow::howToPlayText();
            size_t nl = text.find('\n');               // skip the heading line
            r.drawText2D(240, 190, 18, nl == std::string::npos ? text : text.substr(nl + 1), kText);
            r.drawText2D(240, 590, 15, "E or Esc  back", kMuted);
            break;
        }
        case MenuState::Loading: {
            r.drawRect2D(0, 0, kW, kH, Vec4(0.03f, 0.035f, 0.05f, 0.92f));
            centred(r, 300, 40, "GRAND CITY", kText);
            r.drawRect2D(440, 372, 400, 6, Vec4(1, 1, 1, 0.15f));
            r.drawRect2D(440, 372, 400 * world.flow().loadingProgress(), 6, kAccent);
            centred(r, 400, 16, world.flow().loadingTip(), kMuted);
            break;
        }
        case MenuState::Garage:
            garage(r, world, st);
            break;
        case MenuState::MissionComplete:
        case MenuState::MissionFailed:
        case MenuState::GameComplete:
            resultCard(r, world, s);
            break;
    }
    // Toast: the newest log line for a few seconds, so menu actions
    // ("Not enough money", "Engine upgraded") are visible on screen.
    static unsigned lastCount = 0;
    static float shownAt = -100.0f;
    static std::string toast;
    static bool primed = false;
    unsigned count = Logger::infoCount();
    if (!primed) { primed = true; lastCount = count; } // start-up lines are not toasts
    if (count != lastCount) {
        lastCount = count;
        std::string latest = Logger::lastInfo();
        bool noisy = latest.rfind("NullRenderer", 0) == 0 || latest.rfind("Profiler", 0) == 0;
        if (!noisy && s != MenuState::Playing) { toast = latest; shownAt = st.timeSeconds; }
    }
    if (st.timeSeconds - shownAt < 3.0f && !toast.empty() && s != MenuState::Playing) {
        float w = IRenderer::textWidth(toast, 16) + 36;
        r.drawRect2D((kW - w) * 0.5f + 160, kH - 70, w, 38, kPanel);
        r.drawText2D((kW - w) * 0.5f + 178, kH - 60, 16, toast, kText);
    }
}
} // namespace gc6
