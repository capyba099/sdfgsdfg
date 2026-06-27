#pragma once
#include <SDL.h>
#include <vector>
#include "Vec2.h"

struct RayHit {
    bool hit = false;
    Vec2 point;     // world position where the ray stopped
    float dist = 0; // distance from origin
};

struct BombSite {
    char label;     // 'A' or 'B'
    Vec2 center;
    SDL_Rect rect;  // world-space rectangle
};

class Map {
public:
    Map();

    int width() const { return w_; }
    int height() const { return h_; }
    float worldW() const;
    float worldH() const;

    bool isWallTile(int tx, int ty) const;
    bool isWallWorld(float x, float y) const;

    // Resolve a moving circle against the walls. Returns adjusted position.
    Vec2 collide(const Vec2& pos, float radius) const;

    // Cast a ray from `from` toward `to`. Stops at first wall. Used for both
    // bullets and line-of-sight checks.
    RayHit raycast(const Vec2& from, const Vec2& to) const;

    // True if there is an unobstructed straight line between two points.
    bool lineOfSight(const Vec2& a, const Vec2& b) const;

    const std::vector<Vec2>& ctSpawns() const { return ctSpawns_; }
    const std::vector<Vec2>& tSpawns() const { return tSpawns_; }
    const std::vector<BombSite>& bombSites() const { return sites_; }

    // Random walkable point near a tile area (used for bot navigation goals).
    bool inBombSite(const Vec2& p, char* outLabel = nullptr) const;

    // Grid BFS pathfinding. Returns a list of world-space waypoints
    // (tile centers) from `start` to `goal`, or empty if unreachable.
    std::vector<Vec2> findPath(const Vec2& start, const Vec2& goal) const;

    // Nearest walkable tile center to an arbitrary world point.
    Vec2 nearestFloor(const Vec2& p) const;

    void render(SDL_Renderer* r, const Vec2& camera) const;

private:
    int idx(int tx, int ty) const { return ty * w_ + tx; }
    void load();

    int w_ = 0;
    int h_ = 0;
    std::vector<uint8_t> tiles_;  // 1 = wall, 0 = floor
    std::vector<Vec2> ctSpawns_;
    std::vector<Vec2> tSpawns_;
    std::vector<BombSite> sites_;
};
