#include "engine/resource_manager.h"
#include <cmath>
#include <functional>
namespace gc6 {
namespace {
constexpr float kPi = 3.14159265f;

// ---------------------------------------------------------------- builders
void append(MeshData& dst, const MeshData& src, const Mat4& xf = Mat4(1.0f)) {
    uint32_t base = static_cast<uint32_t>(dst.positions.size());
    glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(xf)));
    for (size_t i = 0; i < src.positions.size(); ++i) {
        dst.positions.push_back(Vec3(xf * Vec4(src.positions[i], 1.0f)));
        Vec3 n = i < src.normals.size() ? src.normals[i] : Vec3(0, 1, 0);
        Vec3 tn = nm * n;
        dst.normals.push_back(glm::length(tn) > 1e-6f ? glm::normalize(tn) : Vec3(0, 1, 0));
        dst.uvs.push_back(i < src.uvs.size() ? src.uvs[i] : Vec2(0, 0));
    }
    for (auto idx : src.indices) dst.indices.push_back(base + idx);
}
// Axis-aligned box centred on the origin, 24 vertices with face normals.
MeshData boxMesh(float w, float h, float d) {
    MeshData m;
    Vec3 half(w / 2, h / 2, d / 2);
    const Vec3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const Vec3 u[6] = {{0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    for (int f = 0; f < 6; ++f) {
        Vec3 v = glm::cross(n[f], u[f]);
        uint32_t base = static_cast<uint32_t>(m.positions.size());
        const float s[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (auto& c : s) {
            m.positions.push_back((n[f] + u[f] * c[0] + v * c[1]) * half);
            m.normals.push_back(n[f]);
            m.uvs.push_back(Vec2((c[0] + 1) / 2, (c[1] + 1) / 2));
        }
        for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) m.indices.push_back(base + k);
    }
    return m;
}
MeshData boxAt(float w, float h, float d, Vec3 at, float yaw = 0.0f, float pitch = 0.0f) {
    MeshData out;
    Mat4 xf = translate(Mat4(1.0f), at);
    if (yaw != 0.0f) xf = rotateY(xf, yaw);
    if (pitch != 0.0f) xf = rotateX(xf, pitch);
    append(out, boxMesh(w, h, d), xf);
    return out;
}
// Surface of revolution around +y from a (radius, y) profile; smooth normals.
MeshData lathe(const std::vector<Vec2>& prof, int segs, bool capTop, bool capBottom) {
    MeshData m;
    size_t n = prof.size();
    for (int s = 0; s <= segs; ++s) {
        float a = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(segs);
        float ca = std::cos(a), sa = std::sin(a);
        for (size_t i = 0; i < n; ++i) {
            Vec2 t = (i + 1 < n ? prof[i + 1] : prof[i]) - (i > 0 ? prof[i - 1] : prof[i]);
            Vec2 pn = glm::normalize(Vec2(t.y, -t.x));
            m.positions.push_back(Vec3(prof[i].x * ca, prof[i].y, prof[i].x * sa));
            m.normals.push_back(glm::normalize(Vec3(pn.x * ca, pn.y, pn.x * sa)));
        }
    }
    for (int s = 0; s < segs; ++s)
        for (size_t i = 0; i + 1 < n; ++i) {
            uint32_t a = static_cast<uint32_t>(s * n + i), b = static_cast<uint32_t>((s + 1) * n + i);
            m.indices.insert(m.indices.end(), {a, b, b + 1, a, b + 1, a + 1});
        }
    auto cap = [&](size_t i, float ny) {
        uint32_t centre = static_cast<uint32_t>(m.positions.size());
        m.positions.push_back(Vec3(0, prof[i].y, 0));
        m.normals.push_back(Vec3(0, ny, 0));
        for (int s = 0; s <= segs; ++s) {
            float a = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(segs);
            m.positions.push_back(Vec3(prof[i].x * std::cos(a), prof[i].y, prof[i].x * std::sin(a)));
            m.normals.push_back(Vec3(0, ny, 0));
        }
        for (int s = 0; s < segs; ++s)
            m.indices.insert(m.indices.end(), {centre, centre + 1 + s, centre + 2 + s});
    };
    if (capTop) cap(n - 1, 1.0f);
    if (capBottom) cap(0, -1.0f);
    m.uvs.assign(m.positions.size(), Vec2(0, 0));
    return m;
}
MeshData cylinderMesh(float r0, float r1, float h, int segs, bool caps = true) {
    return lathe({{r0, 0.0f}, {r1, h}}, segs, caps, caps);
}
MeshData sphereMesh(float r, int segs, int stacks, float bottomCut = -1.0f) {
    std::vector<Vec2> prof;
    for (int i = 0; i <= stacks; ++i) {
        float a = -kPi / 2 + kPi * static_cast<float>(i) / static_cast<float>(stacks);
        if (std::sin(a) < bottomCut - 1e-4f) continue;
        prof.push_back(Vec2(std::max(r * std::cos(a), 0.0f), r * std::sin(a)));
    }
    return lathe(prof, segs, false, bottomCut > -1.0f);
}
// Capsule hanging down from y = 0 to y = -len (radius r).
MeshData limbMesh(float len, float r, float rEnd) {
    std::vector<Vec2> prof;
    for (int i = 0; i <= 4; ++i) {                   // bottom hemisphere
        float a = -kPi / 2 + kPi / 2 * static_cast<float>(i) / 4.0f;
        prof.push_back(Vec2(rEnd * std::cos(a), -len + rEnd + rEnd * std::sin(a)));
    }
    for (int i = 0; i <= 4; ++i) {                   // top hemisphere
        float a = kPi / 2 * static_cast<float>(i) / 4.0f;
        prof.push_back(Vec2(r * std::cos(a), -r + r * std::sin(a)));
    }
    prof.front().x = 0.0f;
    prof.back().x = 0.0f;
    return lathe(prof, 10, false, false);
}

// Ear-clipping triangulation of a simple polygon (any winding).
std::vector<uint32_t> triangulate(const std::vector<Vec2>& p) {
    std::vector<uint32_t> out, idx;
    size_t n = p.size();
    float area = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const Vec2& a = p[i]; const Vec2& b = p[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    float sign = area >= 0.0f ? 1.0f : -1.0f;
    for (size_t i = 0; i < n; ++i) idx.push_back(static_cast<uint32_t>(i));
    auto crossz = [](Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    size_t guard = 0;
    while (idx.size() > 3 && guard++ < n * n) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); ++i) {
            uint32_t ia = idx[(i + idx.size() - 1) % idx.size()], ib = idx[i], ic = idx[(i + 1) % idx.size()];
            Vec2 a = p[ia], b = p[ib], c = p[ic];
            if (crossz(a, b, c) * sign <= 1e-7f) continue;          // reflex
            bool inside = false;
            for (uint32_t j : idx) {
                if (j == ia || j == ib || j == ic) continue;
                Vec2 q = p[j];
                if (crossz(a, b, q) * sign > 0 && crossz(b, c, q) * sign > 0 && crossz(c, a, q) * sign > 0) {
                    inside = true;
                    break;
                }
            }
            if (inside) continue;
            out.insert(out.end(), {ia, ib, ic});
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) out.insert(out.end(), {idx[0], idx[1], idx[2]});
    return out;
}

// Extrudes a side profile (z, y) across the car's width. halfWidth(z, y)
// gives the half width at each profile point; edges are bevelled so the
// panels catch the light like pressed metal.
MeshData extrude(std::vector<Vec2> poly, const std::function<float(float, float)>& halfWidth, float bevel) {
    // Drop near-duplicate points (they would give zero-length edges).
    std::vector<Vec2> clean;
    for (const Vec2& q : poly)
        if (clean.empty() || glm::length(q - clean.back()) > 0.004f) clean.push_back(q);
    while (clean.size() > 3 && glm::length(clean.front() - clean.back()) < 0.004f) clean.pop_back();
    poly = clean;
    size_t n = poly.size();
    MeshData m;
    if (n < 3) return m;
    float area = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const Vec2& a = poly[i]; const Vec2& b = poly[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    float sgn = area >= 0.0f ? 1.0f : -1.0f;
    std::vector<Vec2> en(n);
    std::vector<float> el(n);
    for (size_t i = 0; i < n; ++i) {
        Vec2 e = poly[(i + 1) % n] - poly[i];
        el[i] = glm::length(e);
        en[i] = glm::normalize(Vec2(e.y, -e.x)) * sgn;           // outward edge normal
    }
    std::vector<Vec2> vn(n);
    for (size_t i = 0; i < n; ++i) {
        size_t pi = (i + n - 1) % n;
        // Length-weighted so a long panel keeps its own normal near a corner.
        vn[i] = glm::normalize(en[pi] * el[pi] + en[i] * el[i]);
    }
    const float side[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    for (int ring = 0; ring < 4; ++ring) {
        bool capEdge = ring == 0 || ring == 3;
        for (size_t i = 0; i < n; ++i) {
            Vec2 p = poly[i];
            float hw = halfWidth(p.x, p.y);
            Vec2 q = capEdge ? p - vn[i] * bevel : p;
            float x = capEdge ? hw : hw - bevel;
            m.positions.push_back(Vec3(side[ring] * x, q.y, q.x));
            float sx = capEdge ? 0.75f : 0.2f;
            m.normals.push_back(glm::normalize(Vec3(side[ring] * sx, vn[i].y, vn[i].x)));
        }
    }
    for (uint32_t ring = 0; ring < 3; ++ring)
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t j = static_cast<uint32_t>((i + 1) % n);
            uint32_t a = ring * static_cast<uint32_t>(n) + i, b = ring * static_cast<uint32_t>(n) + j;
            uint32_t c = (ring + 1) * static_cast<uint32_t>(n) + j, d = (ring + 1) * static_cast<uint32_t>(n) + i;
            m.indices.insert(m.indices.end(), {a, b, c, a, c, d});
        }
    std::vector<uint32_t> tris = triangulate(poly);
    for (int s = -1; s <= 1; s += 2) {
        uint32_t base = static_cast<uint32_t>(m.positions.size());
        for (size_t i = 0; i < n; ++i) {
            Vec2 q = poly[i] - vn[i] * bevel;
            m.positions.push_back(Vec3(static_cast<float>(s) * halfWidth(poly[i].x, poly[i].y), q.y, q.x));
            m.normals.push_back(Vec3(static_cast<float>(s), 0, 0));
        }
        for (uint32_t t : tris) m.indices.push_back(base + t);
    }
    m.uvs.assign(m.positions.size(), Vec2(0, 0));
    return m;
}

// Plan-view taper: the corners of the car are rounded off.
float planTaper(const CarShape& s, float z) {
    float t = std::fabs(z) / (s.length * 0.5f);
    return 1.0f - 0.09f * std::pow(clampf(t, 0.0f, 1.0f), 5.0f) - 0.02f * t * t;
}

std::vector<Vec2> bodyProfile(const CarShape& s) {
    std::vector<Vec2> p;
    float h = s.length * 0.5f;
    float ar = s.wheelRadius + 0.07f, cy = s.wheelRadius;
    auto arch = [&](float cz) {
        p.push_back({cz - ar, s.sill});
        for (int i = 0; i <= 10; ++i) {
            float t = kPi - kPi * static_cast<float>(i) / 10.0f;
            p.push_back({cz + ar * std::cos(t), std::max(cy + ar * std::sin(t), s.sill)});
        }
        p.push_back({cz + ar, s.sill});
    };
    p.push_back({-h + 0.12f, s.sill + 0.05f});
    arch(s.rearAxleZ());
    arch(s.frontAxleZ());
    p.push_back({h - 0.10f, s.noseBottom + 0.03f});
    p.push_back({h - 0.01f, s.noseBottom + 0.14f});
    p.push_back({h, (s.noseBottom + s.noseTop) * 0.5f});
    p.push_back({h - 0.03f, s.noseTop});
    p.push_back({h - 0.14f, s.noseTop + (s.hoodFront - s.noseTop) * 0.75f});
    p.push_back({h - 0.32f, s.hoodFront});
    // Hood rises gently to the windscreen base.
    for (int i = 1; i <= 3; ++i) {
        float t = static_cast<float>(i) / 3.0f;
        float z = (h - 0.32f) + (s.zA - (h - 0.32f)) * t;
        p.push_back({z, s.hoodFront + (s.beltY - s.hoodFront) * std::sqrt(t)});
    }
    p.push_back({(s.zA + s.zC) * 0.5f, s.beltY + 0.01f});
    p.push_back({s.zC, s.beltY});
    float deckEnd = -h + 0.30f;
    if (s.zC - 0.05f > deckEnd) {
        p.push_back({(s.zC + deckEnd) * 0.5f, s.deckY + 0.01f});
        p.push_back({deckEnd, s.deckY});
    }
    p.push_back({-h + 0.08f, s.tailTop - 0.02f});
    p.push_back({-h, s.tailTop - 0.14f});
    p.push_back({-h + 0.02f, s.sill + 0.22f});
    return p;
}

MeshData buildBody(const CarShape& s) {
    auto hw = [&](float z, float y) {
        float top = clampf((y - s.beltY + 0.25f) / 0.25f, 0.0f, 1.0f); // shoulder tuck
        return s.width * 0.5f * planTaper(s, z) - 0.03f * top;
    };
    MeshData m = extrude(bodyProfile(s), hw, 0.06f);
    // Painted roof panel over the glasshouse.
    float rw = s.width * 0.5f - 0.235f;
    std::vector<Vec2> roof;
    roof.push_back({s.zW - 0.06f, s.roofY - 0.05f});
    roof.push_back({s.zW - 0.16f, s.roofY + 0.012f});
    for (int i = 1; i < 6; ++i) {
        float t = static_cast<float>(i) / 6.0f;
        roof.push_back({(s.zW - 0.16f) + ((s.zR + 0.16f) - (s.zW - 0.16f)) * t,
                        s.roofY + 0.012f + 0.035f * std::sin(kPi * t)});
    }
    roof.push_back({s.zR + 0.16f, s.roofY + 0.012f});
    roof.push_back({s.zR + 0.06f, s.roofY - 0.05f});
    append(m, extrude(roof, [&](float z, float) { return rw * planTaper(s, z); }, 0.025f));
    return m;
}

MeshData buildGlass(const CarShape& s) {
    std::vector<Vec2> g;
    g.push_back({s.zA + 0.02f, s.beltY - 0.03f});
    g.push_back({s.zW, s.roofY - 0.015f});
    for (int i = 1; i < 6; ++i) {
        float t = static_cast<float>(i) / 6.0f;
        g.push_back({s.zW + (s.zR - s.zW) * t, s.roofY - 0.015f + 0.035f * std::sin(kPi * t)});
    }
    g.push_back({s.zR, s.roofY - 0.015f});
    g.push_back({s.zC - 0.02f, s.beltY - 0.03f});
    auto hw = [&](float z, float y) {
        float t = clampf((y - s.beltY) / (s.roofY - s.beltY), 0.0f, 1.0f);
        return (s.width * 0.5f - 0.06f - 0.19f * t) * planTaper(s, z);
    };
    return extrude(g, hw, 0.05f);
}

MeshData buildTrim(const CarShape& s) {
    MeshData m;
    float h = s.length * 0.5f, W = s.width;
    float noseH = s.noseTop - s.noseBottom;
    bool tall = s.sill > 0.38f;
    // Grille and lower intake.
    append(m, boxAt(W * (tall ? 0.56f : 0.46f), noseH * (tall ? 0.55f : 0.38f), 0.05f,
                    Vec3(0, s.noseBottom + noseH * 0.52f, h - 0.005f)));
    append(m, boxAt(W * 0.72f, 0.07f, 0.05f, Vec3(0, s.noseBottom + 0.07f, h - 0.04f)));
    // Side skirts between the arches.
    float ar = s.wheelRadius + 0.07f;
    float z0 = s.rearAxleZ() + ar + 0.02f, z1 = s.frontAxleZ() - ar - 0.02f;
    for (int sd = -1; sd <= 1; sd += 2)
        append(m, boxAt(0.05f, 0.09f, z1 - z0, Vec3(sd * (W * 0.5f - 0.01f), s.sill + 0.03f, (z0 + z1) * 0.5f)));
    // Wheel-arch lips (tall cars get chunkier cladding).
    for (float cz : {s.rearAxleZ(), s.frontAxleZ()})
        for (int sd = -1; sd <= 1; sd += 2)
            for (int i = 0; i < 7; ++i) {
                float a = kPi * (0.08f + 0.84f * static_cast<float>(i) / 6.0f);
                Vec3 at(sd * (W * 0.5f * planTaper(s, cz) - 0.005f), s.wheelRadius + ar * std::sin(a),
                        cz + ar * std::cos(a));
                if (at.y < s.sill) continue;
                append(m, boxAt(0.04f, tall ? 0.07f : 0.035f, ar * 0.5f, at, 0.0f, -(a - kPi / 2)));
            }
    // Rear diffuser.
    append(m, boxAt(W * 0.7f, 0.09f, 0.08f, Vec3(0, s.sill + 0.05f, -h + 0.04f)));
    // Mirrors on short stalks.
    for (int sd = -1; sd <= 1; sd += 2) {
        append(m, boxAt(0.16f, 0.10f, 0.20f, Vec3(sd * (W * 0.5f + 0.07f), s.beltY + 0.10f, s.zA - 0.18f), sd * 0.12f));
        append(m, boxAt(0.10f, 0.03f, 0.05f, Vec3(sd * (W * 0.5f - 0.01f), s.beltY + 0.05f, s.zA - 0.2f)));
    }
    // Door handles and a B-pillar line on each side of the glass.
    float zMid = (s.zA + s.zC) * 0.5f;
    for (int sd = -1; sd <= 1; sd += 2) {
        append(m, boxAt(0.02f, 0.025f, 0.16f, Vec3(sd * (W * 0.5f * planTaper(s, zMid) + 0.003f), s.beltY - 0.12f, zMid + 0.45f)));
        append(m, boxAt(0.02f, 0.025f, 0.16f, Vec3(sd * (W * 0.5f * planTaper(s, zMid) + 0.003f), s.beltY - 0.12f, zMid - 0.55f)));
    }
    if (s.bed) {
        float zb0 = s.zC - 0.08f, zb1 = -h + 0.14f;
        append(m, boxAt(W - 0.16f, 0.02f, zb0 - zb1, Vec3(0, s.deckY + 0.004f, (zb0 + zb1) * 0.5f)));
    }
    return m;
}

MeshData buildPlates(const CarShape& s) {
    MeshData m;
    float h = s.length * 0.5f;
    append(m, boxAt(0.50f, 0.11f, 0.02f, Vec3(0, s.noseBottom + 0.17f, h + 0.012f)));
    append(m, boxAt(0.50f, 0.11f, 0.02f, Vec3(0, s.sill + 0.33f, -h - 0.004f)));
    return m;
}
MeshData buildHeadlights(const CarShape& s) {
    MeshData m;
    float h = s.length * 0.5f, W = s.width;
    float y = s.noseTop - 0.07f;
    for (int sd = -1; sd <= 1; sd += 2) {
        float x = sd * (W * 0.5f - 0.26f);
        append(m, boxAt(0.36f, 0.065f, 0.20f, Vec3(x, y, h - 0.10f), sd * -0.18f, 0.25f));
    }
    return m;
}
MeshData buildTaillights(const CarShape& s) {
    MeshData m;
    float h = s.length * 0.5f, W = s.width;
    float y = s.tailTop - 0.15f;
    append(m, boxAt(W - 0.34f, 0.045f, 0.04f, Vec3(0, y, -h + 0.005f)));  // full-width light bar
    for (int sd = -1; sd <= 1; sd += 2)
        append(m, boxAt(0.22f, 0.10f, 0.12f, Vec3(sd * (W * 0.5f - 0.16f), y, -h + 0.06f)));
    return m;
}

MeshData buildTyre(float r) {
    float w = 0.245f;
    std::vector<Vec2> prof = {{r * 0.66f, -w / 2}, {r * 0.86f, -w / 2}, {r - 0.02f, -w / 2 + 0.025f},
                              {r, -w / 2 + 0.06f}, {r, w / 2 - 0.06f}, {r - 0.02f, w / 2 - 0.025f},
                              {r * 0.86f, w / 2}, {r * 0.72f, w / 2 - 0.01f}};
    MeshData lat = lathe(prof, 30, false, false);
    MeshData m;
    // lathe is around +y; turn it so the axle is +x (outer face at +x).
    append(m, lat, rotateZ(Mat4(1.0f), -kPi / 2));
    // Dark inner wheel and brake disc behind the spokes.
    append(m, cylinderMesh(r * 0.70f, r * 0.70f, 0.02f, 24), translate(rotateZ(Mat4(1.0f), -kPi / 2), Vec3(0, w / 2 - 0.09f, 0)));
    return m;
}
MeshData buildRim(float r) {
    MeshData m;
    float w = 0.245f;
    float x = w / 2 - 0.035f;
    Mat4 toX = rotateZ(Mat4(1.0f), -kPi / 2);
    // Rim lip (a ring) and hub.
    append(m, lathe({{r * 0.72f, -0.03f}, {r * 0.70f, 0.012f}, {r * 0.64f, 0.016f}}, 28, false, false),
           translate(Mat4(1.0f), Vec3(x, 0, 0)) * toX);
    append(m, cylinderMesh(r * 0.17f, r * 0.13f, 0.035f, 14), translate(Mat4(1.0f), Vec3(x - 0.012f, 0, 0)) * toX);
    for (int k = 0; k < 5; ++k) {
        float a = 2.0f * kPi * static_cast<float>(k) / 5.0f;
        Mat4 xf = translate(Mat4(1.0f), Vec3(x, 0, 0));
        xf = rotateX(xf, a);
        xf = translate(xf, Vec3(0, r * 0.42f, 0));
        append(m, boxMesh(0.035f, r * 0.52f, 0.075f), xf);
        Mat4 xf2 = translate(Mat4(1.0f), Vec3(x - 0.005f, 0, 0));
        xf2 = rotateX(xf2, a + 0.18f);
        xf2 = translate(xf2, Vec3(0, r * 0.40f, 0));
        append(m, boxMesh(0.03f, r * 0.48f, 0.05f), xf2);
    }
    return m;
}

// Lumpy foliage: three overlapping spheres with a hashed radial jitter.
float jitter(Vec3 p, int seed) {
    float v = std::sin(p.x * 12.9898f + p.y * 78.233f + p.z * 37.719f + static_cast<float>(seed) * 4.1f) * 43758.5453f;
    return v - std::floor(v);
}
MeshData buildCanopy(int variant) {
    MeshData m;
    struct Blob { Vec3 c; float r; };
    std::vector<Blob> blobs = variant == 0
        ? std::vector<Blob>{{{0, 4.5f, 0}, 1.9f}, {{0.9f, 5.1f, 0.4f}, 1.35f}, {{-0.8f, 4.1f, -0.5f}, 1.3f}, {{0.1f, 5.8f, -0.2f}, 1.1f}}
        : std::vector<Blob>{{{0, 5.0f, 0}, 1.5f}, {{0, 6.2f, 0}, 1.15f}, {{0.4f, 3.9f, 0.3f}, 1.35f}};
    int seed = 0;
    for (const Blob& b : blobs) {
        MeshData s = sphereMesh(b.r, 12, 8);
        for (size_t i = 0; i < s.positions.size(); ++i) {
            float j = 0.85f + 0.3f * jitter(glm::floor(s.positions[i] * 3.0f), seed);
            s.positions[i] *= j;
        }
        append(m, s, translate(Mat4(1.0f), b.c));
        ++seed;
    }
    return m;
}
} // namespace

// ---------------------------------------------------------------- shapes
const CarShape& ResourceManager::carShape(const std::string& c) {
    static const std::unordered_map<std::string, CarShape> shapes = [] {
        std::unordered_map<std::string, CarShape> t;
        CarShape s;  // sedan: the defaults
        t["sedan"] = s;
        CarShape hatch = s;
        hatch.length = 4.25f; hatch.width = 1.80f; hatch.wheelBase = 2.65f; hatch.wheelRadius = 0.32f;
        hatch.sill = 0.28f; hatch.noseBottom = 0.30f; hatch.noseTop = 0.66f; hatch.hoodFront = 0.80f;
        hatch.beltY = 0.98f; hatch.roofY = 1.50f; hatch.zA = 0.85f; hatch.zW = 0.10f; hatch.zR = -1.55f;
        hatch.zC = -1.98f; hatch.deckY = 0.98f; hatch.tailTop = 0.96f;
        t["hatch"] = hatch;
        CarShape sp = s;
        sp.length = 4.50f; sp.width = 1.90f; sp.wheelBase = 2.70f; sp.axleOffset = -0.05f; sp.wheelRadius = 0.35f;
        sp.sill = 0.25f; sp.noseBottom = 0.24f; sp.noseTop = 0.55f; sp.hoodFront = 0.66f; sp.beltY = 0.88f;
        sp.roofY = 1.28f; sp.zA = 0.80f; sp.zW = -0.15f; sp.zR = -0.75f; sp.zC = -1.85f; sp.deckY = 0.88f;
        sp.tailTop = 0.86f;
        t["sports"] = sp;
        CarShape su = s;
        su.length = 4.62f; su.width = 2.00f; su.wheelBase = 2.70f; su.axleOffset = -0.10f; su.wheelRadius = 0.36f;
        su.sill = 0.20f; su.noseBottom = 0.18f; su.noseTop = 0.44f; su.hoodFront = 0.56f; su.beltY = 0.80f;
        su.roofY = 1.14f; su.zA = 1.05f; su.zW = 0.05f; su.zR = -0.55f; su.zC = -1.95f; su.deckY = 0.84f;
        su.tailTop = 0.82f;
        t["supercar"] = su;
        CarShape mu = s;
        mu.length = 4.85f; mu.width = 1.92f; mu.wheelBase = 2.85f; mu.axleOffset = -0.05f; mu.wheelRadius = 0.36f;
        mu.sill = 0.30f; mu.noseBottom = 0.32f; mu.noseTop = 0.78f; mu.hoodFront = 0.92f; mu.beltY = 1.00f;
        mu.roofY = 1.38f; mu.zA = 0.55f; mu.zW = -0.25f; mu.zR = -1.00f; mu.zC = -1.60f; mu.deckY = 1.00f;
        mu.tailTop = 0.98f;
        t["muscle"] = mu;
        CarShape suv = s;
        suv.length = 4.80f; suv.width = 1.96f; suv.wheelBase = 2.90f; suv.wheelRadius = 0.40f; suv.sill = 0.45f;
        suv.noseBottom = 0.45f; suv.noseTop = 0.92f; suv.hoodFront = 1.08f; suv.beltY = 1.18f; suv.roofY = 1.78f;
        suv.zA = 1.05f; suv.zW = 0.50f; suv.zR = -2.10f; suv.zC = -2.28f; suv.deckY = 1.18f; suv.tailTop = 1.15f;
        t["suv"] = suv;
        CarShape pk = s;
        pk.length = 5.40f; pk.width = 2.00f; pk.wheelBase = 3.30f; pk.axleOffset = 0.15f; pk.wheelRadius = 0.42f;
        pk.sill = 0.50f; pk.noseBottom = 0.50f; pk.noseTop = 1.00f; pk.hoodFront = 1.18f; pk.beltY = 1.22f;
        pk.roofY = 1.90f; pk.zA = 1.30f; pk.zW = 0.80f; pk.zR = -0.45f; pk.zC = -0.55f; pk.deckY = 1.22f;
        pk.tailTop = 1.20f; pk.bed = true;
        t["pickup"] = pk;
        CarShape van = s;
        van.length = 5.10f; van.width = 2.00f; van.wheelBase = 3.20f; van.axleOffset = 0.05f; van.wheelRadius = 0.38f;
        van.sill = 0.40f; van.noseBottom = 0.40f; van.noseTop = 0.85f; van.hoodFront = 1.08f; van.beltY = 1.15f;
        van.roofY = 2.25f; van.zA = 1.70f; van.zW = 1.05f; van.zR = -2.47f; van.zC = -2.50f; van.deckY = 1.15f;
        van.tailTop = 1.12f;
        t["van"] = van;
        return t;
    }();
    auto it = shapes.find(c);
    return it != shapes.end() ? it->second : shapes.at("sedan");
}

// ---------------------------------------------------------------- meshes
const MeshData& ResourceManager::box(float w, float h, float d) {
    std::string key = "box_" + std::to_string(w) + "_" + std::to_string(h) + "_" + std::to_string(d);
    return cached(key, [&] { return boxMesh(w, h, d); });
}
const MeshData& ResourceManager::carBody() { return carBodyFor("sedan"); }
const MeshData& ResourceManager::carBodyFor(const std::string& c) {
    return cached("car_body_" + c, [&] { return buildBody(carShape(c)); });
}
const MeshData& ResourceManager::carGlassFor(const std::string& c) {
    return cached("car_glass_" + c, [&] { return buildGlass(carShape(c)); });
}
const MeshData& ResourceManager::carTrimFor(const std::string& c) {
    return cached("car_trim_" + c, [&] { return buildTrim(carShape(c)); });
}
const MeshData& ResourceManager::carPlatesFor(const std::string& c) {
    return cached("car_plates_" + c, [&] { return buildPlates(carShape(c)); });
}
const MeshData& ResourceManager::carHeadlightsFor(const std::string& c) {
    return cached("car_head_" + c, [&] { return buildHeadlights(carShape(c)); });
}
const MeshData& ResourceManager::carTaillightsFor(const std::string& c) {
    return cached("car_tail_" + c, [&] { return buildTaillights(carShape(c)); });
}
const MeshData& ResourceManager::tyre(float r) {
    return cached("tyre_" + std::to_string(r), [&] { return buildTyre(r); });
}
const MeshData& ResourceManager::rimFor(float r) {
    return cached("rim_" + std::to_string(r), [&] { return buildRim(r); });
}
const MeshData& ResourceManager::wheel() { return tyre(0.34f); }
const MeshData& ResourceManager::rim() { return rimFor(0.34f); }
const MeshData& ResourceManager::headlight() { return cached("headlight", [] { return boxMesh(0.45f, 0.14f, 0.08f); }); }
const MeshData& ResourceManager::taillight() { return cached("taillight", [] { return boxMesh(0.5f, 0.12f, 0.08f); }); }
const MeshData& ResourceManager::spoiler() { return cached("spoiler", [] { return boxMesh(1.8f, 0.06f, 0.4f); }); }
const MeshData& ResourceManager::mirror() { return cached("mirror", [] { return boxMesh(0.18f, 0.12f, 0.1f); }); }
const MeshData& ResourceManager::dashboard() {
    return cached("dashboard", [] {
        MeshData m;
        append(m, boxAt(1.6f, 0.22f, 0.45f, Vec3(0, 0, 0)));
        append(m, boxAt(0.5f, 0.16f, 0.05f, Vec3(-0.35f, 0.14f, 0.08f), 0.0f, -0.4f)); // instrument binnacle
        append(m, boxAt(0.35f, 0.22f, 0.04f, Vec3(0.15f, 0.16f, 0.05f), 0.0f, -0.3f)); // centre screen
        return m;
    });
}
const MeshData& ResourceManager::steeringWheel() {
    return cached("steeringwheel", [] {
        MeshData m;
        // Rim as a ring of short segments in the x-y plane, plus two spokes.
        for (int i = 0; i < 16; ++i) {
            float a = 2.0f * kPi * static_cast<float>(i) / 16.0f;
            Mat4 xf = rotateZ(Mat4(1.0f), a);
            xf = translate(xf, Vec3(0, 0.18f, 0));
            append(m, boxMesh(0.075f, 0.03f, 0.03f), xf);
        }
        append(m, boxAt(0.34f, 0.04f, 0.02f, Vec3(0, -0.02f, 0)));
        append(m, cylinderMesh(0.06f, 0.06f, 0.04f, 12), rotateX(Mat4(1.0f), kPi / 2));
        return m;
    });
}
const MeshData& ResourceManager::personTorso() {
    return cached("p_torso", [] {
        MeshData m;
        // Hips to shoulders: a tapered, slightly flattened capsule.
        std::vector<Vec2> prof = {{0.0f, -0.02f}, {0.13f, 0.0f}, {0.155f, 0.10f}, {0.145f, 0.28f},
                                  {0.17f, 0.45f}, {0.18f, 0.55f}, {0.12f, 0.63f}, {0.0f, 0.65f}};
        append(m, lathe(prof, 12, false, false), scale(Mat4(1.0f), Vec3(1.12f, 1.0f, 0.68f)));
        return m;
    });
}
const MeshData& ResourceManager::personHead() {
    return cached("p_head", [] {
        MeshData m;
        append(m, cylinderMesh(0.05f, 0.045f, 0.09f, 8));
        append(m, sphereMesh(0.105f, 12, 8), translate(Mat4(1.0f), Vec3(0, 0.16f, 0.005f)) *
                                                  scale(Mat4(1.0f), Vec3(0.88f, 1.08f, 1.0f)));
        append(m, boxAt(0.03f, 0.04f, 0.03f, Vec3(0, 0.15f, 0.1f)));  // nose
        return m;
    });
}
const MeshData& ResourceManager::personHair() {
    return cached("p_hair", [] {
        MeshData m;
        append(m, sphereMesh(0.112f, 12, 8, 0.0f), translate(Mat4(1.0f), Vec3(0, 0.175f, -0.008f)) *
                                                        scale(Mat4(1.0f), Vec3(0.92f, 1.0f, 1.04f)));
        return m;
    });
}
const MeshData& ResourceManager::personLimb(float len, float r) {
    return cached("p_limb_" + std::to_string(len) + "_" + std::to_string(r),
                  [&] { return limbMesh(len, r, r * 0.8f); });
}
const MeshData& ResourceManager::personShoe() {
    return cached("p_shoe", [] { return boxAt(0.10f, 0.07f, 0.25f, Vec3(0, -0.03f, 0.05f)); });
}
const MeshData& ResourceManager::personHand() {
    return cached("p_hand", [] {
        MeshData m;
        append(m, sphereMesh(0.045f, 8, 6), scale(translate(Mat4(1.0f), Vec3(0, -0.05f, 0)), Vec3(0.8f, 1.3f, 1.0f)));
        return m;
    });
}
const MeshData& ResourceManager::person() {
    return cached("person", [&] {
        MeshData m;
        append(m, personTorso(), translate(Mat4(1.0f), Vec3(0, 0.92f, 0)));
        append(m, personHead(), translate(Mat4(1.0f), Vec3(0, 1.55f, 0)));
        for (int s = -1; s <= 1; s += 2) {
            append(m, limbMesh(0.92f, 0.08f, 0.06f), translate(Mat4(1.0f), Vec3(s * 0.09f, 0.94f, 0)));
            append(m, limbMesh(0.62f, 0.055f, 0.045f), translate(Mat4(1.0f), Vec3(s * 0.22f, 1.48f, 0)));
        }
        return m;
    });
}
const MeshData& ResourceManager::treeTrunk() {
    return cached("tree_trunk", [] {
        MeshData m;
        append(m, cylinderMesh(0.17f, 0.10f, 3.6f, 10));
        // Two boughs.
        Mat4 a = translate(Mat4(1.0f), Vec3(0, 2.6f, 0));
        append(m, cylinderMesh(0.07f, 0.04f, 1.4f, 6), rotateZ(a, 0.6f));
        append(m, cylinderMesh(0.07f, 0.04f, 1.3f, 6), rotateX(a, -0.55f));
        return m;
    });
}
const MeshData& ResourceManager::treeCanopy(int variant) {
    return cached("tree_canopy_" + std::to_string(variant), [&] { return buildCanopy(variant); });
}
const MeshData& ResourceManager::lampPost() {
    return cached("lamp_post", [] {
        MeshData m;
        append(m, cylinderMesh(0.15f, 0.13f, 0.5f, 10));                 // base
        append(m, cylinderMesh(0.075f, 0.055f, 7.4f, 10));               // pole
        Mat4 arm = translate(Mat4(1.0f), Vec3(0, 7.2f, 0));
        arm = rotateZ(arm, -kPi / 2 + 0.12f);
        append(m, cylinderMesh(0.045f, 0.04f, 1.65f, 8), arm);           // arm reaching +x
        return m;
    });
}
const MeshData& ResourceManager::lampHead() {
    return cached("lamp_head", [] {
        MeshData m;
        append(m, boxAt(0.62f, 0.10f, 0.26f, Vec3(0, 0, 0)));
        return m;
    });
}
const MeshData& ResourceManager::signalPole() {
    return cached("signal_pole", [] {
        MeshData m;
        append(m, cylinderMesh(0.16f, 0.14f, 0.4f, 10));
        append(m, cylinderMesh(0.10f, 0.085f, 6.0f, 10));
        Mat4 arm = translate(Mat4(1.0f), Vec3(0, 5.7f, 0));
        arm = rotateZ(arm, -kPi / 2);
        append(m, cylinderMesh(0.075f, 0.06f, 5.2f, 8), arm);
        return m;
    });
}
const MeshData& ResourceManager::signalHead() {
    return cached("signal_head", [] {
        MeshData m;
        append(m, boxAt(0.34f, 0.95f, 0.26f, Vec3(0, 0, 0)));
        for (int i = -1; i <= 1; ++i)                                   // visors
            append(m, boxAt(0.30f, 0.025f, 0.16f, Vec3(0, i * 0.29f + 0.14f, 0.2f)));
        return m;
    });
}
const MeshData& ResourceManager::sphere(float r) {
    return cached("sphere_" + std::to_string(r), [&] { return sphereMesh(r, 14, 10); });
}
const MeshData& ResourceManager::cylinder(float r, float h) {
    return cached("cyl_" + std::to_string(r) + "_" + std::to_string(h), [&] { return cylinderMesh(r, r, h, 16); });
}
const MeshData& ResourceManager::groundPlane(float size) {
    return cached("ground", [&] {
        MeshData m;
        float s = size / 2;
        m.positions = {{-s, 0, -s}, {s, 0, -s}, {s, 0, s}, {-s, 0, s}};
        m.normals = {Vec3(0, 1, 0), Vec3(0, 1, 0), Vec3(0, 1, 0), Vec3(0, 1, 0)};
        m.uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        m.indices = {0, 1, 2, 0, 2, 3};
        return m;
    });
}
TextureData ResourceManager::checkerboard(int size, unsigned int c0, unsigned int c1) {
    TextureData t; t.width = t.height = size; t.channels = 4;
    t.pixels.resize(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        unsigned int c = ((x / 8) + (y / 8)) % 2 == 0 ? c0 : c1;
        size_t i = (static_cast<size_t>(y) * size + x) * 4;
        t.pixels[i] = (c >> 16) & 0xFF; t.pixels[i+1] = (c >> 8) & 0xFF;
        t.pixels[i+2] = c & 0xFF; t.pixels[i+3] = 0xFF;
    }
    return t;
}
void ResourceManager::clear() { meshes_.clear(); }
} // namespace gc6
