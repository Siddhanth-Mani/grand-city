#pragma once
// A* pathfinding over the city road graph. Nodes are the city's road
// intersections (40 m grid); edges link cardinal neighbours. Used by
// traffic (drive to a destination), police (route to the player instead of
// steering straight through buildings), and roadblocks. Pure and
// deterministic: same city + same endpoints = same path.
#include "core/math.h"
#include "game/city.h"
#include <vector>
namespace gc6 {
class RoadGraph {
public:
    void build(const City& city);
    int nearestNode(const Vec3& pos) const;
    // A* from start to goal (world positions). Empty vector = no route.
    std::vector<Vec3> findPath(const Vec3& start, const Vec3& goal) const;
    size_t nodeCount() const { return nodes_.size(); }
    const std::vector<Vec3>& nodes() const { return nodes_; }
    const std::vector<std::vector<int>>& edges() const { return edges_; }
private:
    std::vector<Vec3> nodes_;
    std::vector<std::vector<int>> edges_;
    float spacing_ = 40.0f;
};
} // namespace gc6
