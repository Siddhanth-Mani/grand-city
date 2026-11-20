#include "game/pathfinding.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
namespace gc6 {
void RoadGraph::build(const City& city) {
    nodes_ = city.roadNodes;
    spacing_ = city.roadSpacing();
    edges_.assign(nodes_.size(), {});
    for (size_t i = 0; i < nodes_.size(); ++i) {
        for (size_t j = 0; j < nodes_.size(); ++j) {
            if (i == j) continue;
            Vec3 d = nodes_[j] - nodes_[i];
            float dx = std::fabs(d.x), dz = std::fabs(d.z);
            // Cardinal neighbours only (grid roads, no diagonals).
            if ((dx > spacing_ * 0.9f && dx < spacing_ * 1.1f && dz < 0.5f) ||
                (dz > spacing_ * 0.9f && dz < spacing_ * 1.1f && dx < 0.5f))
                edges_[i].push_back(static_cast<int>(j));
        }
    }
}
int RoadGraph::nearestNode(const Vec3& pos) const {
    int best = -1;
    float bestD = std::numeric_limits<float>::max();
    for (size_t i = 0; i < nodes_.size(); ++i) {
        float d = length(Vec3(nodes_[i].x - pos.x, 0, nodes_[i].z - pos.z));
        if (d < bestD) { bestD = d; best = static_cast<int>(i); }
    }
    return best;
}
std::vector<Vec3> RoadGraph::findPath(const Vec3& start, const Vec3& goal) const {
    int s = nearestNode(start), g = nearestNode(goal);
    if (s < 0 || g < 0) return {};
    if (s == g) return {nodes_[static_cast<size_t>(g)]};
    const float INF = std::numeric_limits<float>::max();
    std::vector<float> gScore(nodes_.size(), INF), fScore(nodes_.size(), INF);
    std::vector<int> cameFrom(nodes_.size(), -1);
    std::vector<bool> closed(nodes_.size(), false);
    auto heuristic = [&](int n) {
        return length(Vec3(nodes_[static_cast<size_t>(n)].x - goal.x, 0,
                           nodes_[static_cast<size_t>(n)].z - goal.z));
    };
    using Node = std::pair<float, int>; // (fScore, index)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    gScore[static_cast<size_t>(s)] = 0.0f;
    fScore[static_cast<size_t>(s)] = heuristic(s);
    open.emplace(fScore[static_cast<size_t>(s)], s);
    while (!open.empty()) {
        int cur = open.top().second;
        open.pop();
        if (closed[static_cast<size_t>(cur)]) continue;
        closed[static_cast<size_t>(cur)] = true;
        if (cur == g) break;
        for (int nb : edges_[static_cast<size_t>(cur)]) {
            float tentative = gScore[static_cast<size_t>(cur)] +
                length(nodes_[static_cast<size_t>(nb)] - nodes_[static_cast<size_t>(cur)]);
            if (tentative < gScore[static_cast<size_t>(nb)]) {
                gScore[static_cast<size_t>(nb)] = tentative;
                fScore[static_cast<size_t>(nb)] = tentative + heuristic(nb);
                cameFrom[static_cast<size_t>(nb)] = cur;
                open.emplace(fScore[static_cast<size_t>(nb)], nb);
            }
        }
    }
    if (cameFrom[static_cast<size_t>(g)] < 0) return {}; // no route
    std::vector<Vec3> path;
    for (int n = g; n >= 0; n = cameFrom[static_cast<size_t>(n)])
        path.push_back(nodes_[static_cast<size_t>(n)]);
    std::reverse(path.begin(), path.end());
    return path;
}
} // namespace gc6
