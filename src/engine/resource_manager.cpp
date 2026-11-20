#include "engine/resource_manager.h"
namespace gc6 {
namespace {
void addBox(MeshData& m, float w, float h, float d) {
    float x = w / 2, y = h / 2, z = d / 2;
    Vec3 v[8] = {{-x,-y,-z},{x,-y,-z},{x,y,-z},{-x,y,-z},{-x,-y,z},{x,-y,z},{x,y,z},{-x,y,z}};
    uint32_t f[36] = {0,1,2, 0,2,3, 4,6,5, 4,7,6, 0,4,5, 0,5,1, 1,5,6, 1,6,2, 2,6,7, 2,7,3, 3,7,4, 3,4,0};
    Vec3 n[6] = {{0,0,-1},{0,0,1},{0,-1,0},{1,0,0},{0,1,0},{-1,0,0}};
    for (int i = 0; i < 8; ++i) { m.positions.push_back(v[i]); m.normals.push_back(Vec3(0,1,0)); m.uvs.push_back({0,0}); }
    for (int i = 0; i < 36; ++i) m.indices.push_back(f[i]);
    (void)n;
}
} // namespace
const MeshData& ResourceManager::box(float w, float h, float d) {
    std::string key = "box_" + std::to_string(w) + "_" + std::to_string(h) + "_" + std::to_string(d);
    auto it = meshes_.find(key);
    if (it != meshes_.end()) return it->second;
    MeshData m; addBox(m, w, h, d);
    return meshes_.emplace(key, std::move(m)).first->second;
}
const MeshData& ResourceManager::carBody() {
    auto it = meshes_.find("car");
    if (it != meshes_.end()) return it->second;
    MeshData m; addBox(m, 2.0f, 0.55f, 4.4f); // hull
    MeshData cabin; addBox(cabin, 1.7f, 0.5f, 2.2f);
    for (size_t i = 0; i < cabin.positions.size(); ++i) cabin.positions[i] += Vec3(0, 0.5f, -0.2f);
    size_t base = m.positions.size();
    m.positions.insert(m.positions.end(), cabin.positions.begin(), cabin.positions.end());
    m.normals.insert(m.normals.end(), cabin.normals.begin(), cabin.normals.end());
    m.uvs.insert(m.uvs.end(), cabin.uvs.begin(), cabin.uvs.end());
    for (auto idx : cabin.indices) m.indices.push_back(static_cast<uint32_t>(base + idx));
    return meshes_.emplace("car", std::move(m)).first->second;
}
namespace {
// Append one box (offset) into a mesh, keeping indices valid.
void addBoxAt(MeshData& m, float w, float h, float d, Vec3 offset) {
    MeshData b; addBox(b, w, h, d);
    size_t base = m.positions.size();
    for (size_t i = 0; i < b.positions.size(); ++i) b.positions[i] += offset;
    m.positions.insert(m.positions.end(), b.positions.begin(), b.positions.end());
    m.normals.insert(m.normals.end(), b.normals.begin(), b.normals.end());
    m.uvs.insert(m.uvs.end(), b.uvs.begin(), b.uvs.end());
    for (auto idx : b.indices) m.indices.push_back(static_cast<uint32_t>(base + idx));
}
} // namespace
const MeshData& ResourceManager::carBodyFor(const std::string& carClass) {
    std::string key = "car_" + carClass;
    auto it = meshes_.find(key);
    if (it != meshes_.end()) return it->second;
    MeshData m;
    if (carClass == "supercar") {
        addBoxAt(m, 2.05f, 0.42f, 4.5f, Vec3(0, 0.35f, 0));   // low wide hull
        addBoxAt(m, 1.6f, 0.38f, 1.9f, Vec3(0, 0.72f, -0.1f)); // mid cabin
        addBoxAt(m, 1.9f, 0.06f, 0.45f, Vec3(0, 0.85f, -2.1f)); // rear wing
        addBoxAt(m, 0.5f, 0.12f, 0.3f, Vec3(-0.75f, 0.62f, 2.2f)); // headlight L
        addBoxAt(m, 0.5f, 0.12f, 0.3f, Vec3(0.75f, 0.62f, 2.2f));  // headlight R
    } else if (carClass == "muscle") {
        addBoxAt(m, 2.0f, 0.6f, 4.8f, Vec3(0, 0.4f, 0));       // long hull
        addBoxAt(m, 1.7f, 0.5f, 2.0f, Vec3(0, 0.85f, -0.5f));  // cabin set back
        addBoxAt(m, 0.9f, 0.12f, 1.2f, Vec3(0, 0.78f, 0.9f));  // hood scoop
        addBoxAt(m, 1.8f, 0.08f, 0.4f, Vec3(0, 0.8f, -2.3f));  // rear spoiler lip
    } else if (carClass == "suv") {
        addBoxAt(m, 2.1f, 0.85f, 4.6f, Vec3(0, 0.55f, 0));     // tall hull
        addBoxAt(m, 1.9f, 0.75f, 3.0f, Vec3(0, 1.25f, -0.2f)); // tall cabin
        addBoxAt(m, 2.0f, 0.15f, 0.5f, Vec3(0, 1.7f, -0.2f));  // roof rails
    } else if (carClass == "pickup") {
        addBoxAt(m, 2.05f, 0.7f, 4.8f, Vec3(0, 0.5f, 0));
        addBoxAt(m, 1.8f, 0.65f, 1.6f, Vec3(0, 1.05f, 0.9f));  // cab
        addBoxAt(m, 1.9f, 0.5f, 2.2f, Vec3(0, 0.75f, -1.3f));  // open bed
        addBoxAt(m, 0.15f, 0.35f, 0.15f, Vec3(-0.95f, 1.1f, -2.3f)); // bed posts
        addBoxAt(m, 0.15f, 0.35f, 0.15f, Vec3(0.95f, 1.1f, -2.3f));
    } else if (carClass == "sports") {
        addBoxAt(m, 1.95f, 0.5f, 4.4f, Vec3(0, 0.38f, 0));
        addBoxAt(m, 1.6f, 0.45f, 2.1f, Vec3(0, 0.78f, -0.25f));
        addBoxAt(m, 1.7f, 0.06f, 0.4f, Vec3(0, 0.82f, -2.05f)); // ducktail
    } else { // sedan (default)
        addBoxAt(m, 2.0f, 0.55f, 4.4f, Vec3(0, 0.4f, 0));
        addBoxAt(m, 1.7f, 0.5f, 2.2f, Vec3(0, 0.9f, -0.2f));
    }
    return meshes_.emplace(key, std::move(m)).first->second;
}
const MeshData& ResourceManager::wheel() {
    auto it = meshes_.find("wheel");
    if (it != meshes_.end()) return it->second;
    MeshData m; addBox(m, 0.35f, 0.7f, 0.7f);
    return meshes_.emplace("wheel", std::move(m)).first->second;
}
const MeshData& ResourceManager::headlight() {
    auto it = meshes_.find("headlight");
    if (it != meshes_.end()) return it->second;
    MeshData m; addBox(m, 0.45f, 0.14f, 0.08f);
    return meshes_.emplace("headlight", std::move(m)).first->second;
}
const MeshData& ResourceManager::taillight() {
    auto it = meshes_.find("taillight");
    if (it != meshes_.end()) return it->second;
    MeshData m; addBox(m, 0.5f, 0.12f, 0.08f);
    return meshes_.emplace("taillight", std::move(m)).first->second;
}
const MeshData& ResourceManager::spoiler() {
    auto it = meshes_.find("spoiler");
    if (it != meshes_.end()) return it->second;
    MeshData m; addBox(m, 1.8f, 0.06f, 0.4f);
    return meshes_.emplace("spoiler", std::move(m)).first->second;
}
const MeshData& ResourceManager::mirror() {
    auto it = meshes_.find("mirror");
    if (it != meshes_.end()) return it->second;
    MeshData m; addBox(m, 0.18f, 0.12f, 0.1f);
    return meshes_.emplace("mirror", std::move(m)).first->second;
}
const MeshData& ResourceManager::rim() {
    auto it = meshes_.find("rim");
    if (it != meshes_.end()) return it->second;
    MeshData m; addBox(m, 0.36f, 0.5f, 0.5f);
    return meshes_.emplace("rim", std::move(m)).first->second;
}
const MeshData& ResourceManager::dashboard() {
    auto it = meshes_.find("dashboard");
    if (it != meshes_.end()) return it->second;
    MeshData m; addBox(m, 1.6f, 0.25f, 0.4f); // dash panel
    return meshes_.emplace("dashboard", std::move(m)).first->second;
}
const MeshData& ResourceManager::steeringWheel() {
    auto it = meshes_.find("steeringwheel");
    if (it != meshes_.end()) return it->second;
    MeshData m; addBox(m, 0.5f, 0.5f, 0.08f); // wheel disc (rotates with steering)
    return meshes_.emplace("steeringwheel", std::move(m)).first->second;
}
const MeshData& ResourceManager::person() {
    auto it = meshes_.find("person");
    if (it != meshes_.end()) return it->second;
    MeshData m; addBox(m, 0.5f, 1.8f, 0.35f);
    return meshes_.emplace("person", std::move(m)).first->second;
}
const MeshData& ResourceManager::groundPlane(float size) {
    auto it = meshes_.find("ground");
    if (it != meshes_.end()) return it->second;
    MeshData m;
    float s = size / 2;
    m.positions = {{-s,0,-s},{s,0,-s},{s,0,s},{-s,0,s}};
    m.normals = {Vec3(0,1,0),Vec3(0,1,0),Vec3(0,1,0),Vec3(0,1,0)};
    m.uvs = {{0,0},{1,0},{1,1},{0,1}};
    m.indices = {0,1,2, 0,2,3};
    return meshes_.emplace("ground", std::move(m)).first->second;
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
