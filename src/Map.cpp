#include "Map.h"
#include <algorithm>
#include <string>
#include "Config.h"

using cfg::TILE;

namespace {
// Map legend:
//   #  wall
//   .  floor
//   C  counter-terrorist spawn
//   T  terrorist spawn
//   a  bomb site A floor
//   b  bomb site B floor
// Rows may differ in length; the loader pads short rows with floor.
const std::vector<std::string> kLayout = {
    "##################################",
    "#CC........#...........#....aaaa..#",
    "#CC........#...........#....aaaa..#",
    "#CC....................#....aaaa..#",
    "#.........#...####......#.........#",
    "#.........#......#......#.........#",
    "#....######......#..........#####.#",
    "#................#..........#.....#",
    "#....#####.......#......######.....#",
    "#....#...........#..........#.....#",
    "#....#......####.............#.....#",
    "#....#......#......#####...........#",
    "#..........#..........#......#####.#",
    "#.bbbb.....#..........#...........#",
    "#.bbbb.....#####......#.....#####.#",
    "#.bbbb...........#............#...#",
    "#.bbbb...........#............#...#",
    "#........######..#......###.......#",
    "#......................#.....TTT..#",
    "#......................#.....TTT..#",
    "#......................#.....TTT..#",
    "##################################",
};
}  // namespace

Map::Map() { load(); }

void Map::load() {
    h_ = static_cast<int>(kLayout.size());
    w_ = 0;
    for (const auto& row : kLayout) w_ = std::max<int>(w_, (int)row.size());

    tiles_.assign(w_ * h_, 0);

    SDL_Rect siteARect{0, 0, 0, 0};
    SDL_Rect siteBRect{0, 0, 0, 0};
    bool haveA = false, haveB = false;

    for (int ty = 0; ty < h_; ++ty) {
        const std::string& row = kLayout[ty];
        for (int tx = 0; tx < w_; ++tx) {
            char c = tx < (int)row.size() ? row[tx] : '.';
            Vec2 center{(tx + 0.5f) * TILE, (ty + 0.5f) * TILE};
            switch (c) {
                case '#':
                    tiles_[idx(tx, ty)] = 1;
                    break;
                case 'C':
                    ctSpawns_.push_back(center);
                    break;
                case 'T':
                    tSpawns_.push_back(center);
                    break;
                case 'a': {
                    SDL_Rect r{tx * TILE, ty * TILE, TILE, TILE};
                    if (!haveA) { siteARect = r; haveA = true; }
                    else {
                        int x1 = std::min(siteARect.x, r.x);
                        int y1 = std::min(siteARect.y, r.y);
                        int x2 = std::max(siteARect.x + siteARect.w, r.x + r.w);
                        int y2 = std::max(siteARect.y + siteARect.h, r.y + r.h);
                        siteARect = {x1, y1, x2 - x1, y2 - y1};
                    }
                    break;
                }
                case 'b': {
                    SDL_Rect r{tx * TILE, ty * TILE, TILE, TILE};
                    if (!haveB) { siteBRect = r; haveB = true; }
                    else {
                        int x1 = std::min(siteBRect.x, r.x);
                        int y1 = std::min(siteBRect.y, r.y);
                        int x2 = std::max(siteBRect.x + siteBRect.w, r.x + r.w);
                        int y2 = std::max(siteBRect.y + siteBRect.h, r.y + r.h);
                        siteBRect = {x1, y1, x2 - x1, y2 - y1};
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    if (haveA) {
        sites_.push_back({'A',
                          {siteARect.x + siteARect.w / 2.0f,
                           siteARect.y + siteARect.h / 2.0f},
                          siteARect});
    }
    if (haveB) {
        sites_.push_back({'B',
                          {siteBRect.x + siteBRect.w / 2.0f,
                           siteBRect.y + siteBRect.h / 2.0f},
                          siteBRect});
    }
}

float Map::worldW() const { return w_ * (float)TILE; }
float Map::worldH() const { return h_ * (float)TILE; }

bool Map::isWallTile(int tx, int ty) const {
    if (tx < 0 || ty < 0 || tx >= w_ || ty >= h_) return true;
    return tiles_[idx(tx, ty)] != 0;
}

bool Map::isWallWorld(float x, float y) const {
    return isWallTile((int)(x / TILE), (int)(y / TILE));
}

Vec2 Map::collide(const Vec2& pos, float radius) const {
    Vec2 p = pos;
    // Sample the four cardinal extents of the circle and push it out of any
    // solid tile it overlaps. Cheap, robust for axis-aligned tile worlds.
    for (int iter = 0; iter < 3; ++iter) {
        int minTx = (int)((p.x - radius) / TILE);
        int maxTx = (int)((p.x + radius) / TILE);
        int minTy = (int)((p.y - radius) / TILE);
        int maxTy = (int)((p.y + radius) / TILE);
        for (int ty = minTy; ty <= maxTy; ++ty) {
            for (int tx = minTx; tx <= maxTx; ++tx) {
                if (!isWallTile(tx, ty)) continue;
                float left = tx * (float)TILE;
                float right = left + TILE;
                float top = ty * (float)TILE;
                float bottom = top + TILE;
                float closestX = clampf(p.x, left, right);
                float closestY = clampf(p.y, top, bottom);
                Vec2 diff = p - Vec2{closestX, closestY};
                float d = diff.length();
                if (d < radius) {
                    if (d > 1e-4f) {
                        p += diff.normalized() * (radius - d);
                    } else {
                        // Center inside the tile: push out along nearest edge.
                        float dl = p.x - left, dr = right - p.x;
                        float dt = p.y - top, db = bottom - p.y;
                        float m = std::min({dl, dr, dt, db});
                        if (m == dl) p.x = left - radius;
                        else if (m == dr) p.x = right + radius;
                        else if (m == dt) p.y = top - radius;
                        else p.y = bottom + radius;
                    }
                }
            }
        }
    }
    return p;
}

RayHit Map::raycast(const Vec2& from, const Vec2& to) const {
    RayHit result;
    Vec2 dir = to - from;
    float maxDist = dir.length();
    if (maxDist < 1e-4f) {
        result.point = to;
        return result;
    }
    dir = dir / maxDist;

    // DDA grid traversal.
    float posX = from.x, posY = from.y;
    int tx = (int)(posX / TILE);
    int ty = (int)(posY / TILE);
    int stepX = dir.x > 0 ? 1 : -1;
    int stepY = dir.y > 0 ? 1 : -1;

    float tMaxX, tMaxY, tDeltaX, tDeltaY;
    if (std::fabs(dir.x) < 1e-6f) {
        tMaxX = 1e30f; tDeltaX = 1e30f;
    } else {
        float nextX = (stepX > 0 ? (tx + 1) * TILE : tx * TILE);
        tMaxX = (nextX - posX) / dir.x;
        tDeltaX = TILE / std::fabs(dir.x);
    }
    if (std::fabs(dir.y) < 1e-6f) {
        tMaxY = 1e30f; tDeltaY = 1e30f;
    } else {
        float nextY = (stepY > 0 ? (ty + 1) * TILE : ty * TILE);
        tMaxY = (nextY - posY) / dir.y;
        tDeltaY = TILE / std::fabs(dir.y);
    }

    float traveled = 0.0f;
    if (isWallTile(tx, ty)) {
        result.hit = true;
        result.point = from;
        result.dist = 0;
        return result;
    }

    while (traveled <= maxDist) {
        if (tMaxX < tMaxY) {
            tx += stepX;
            traveled = tMaxX;
            tMaxX += tDeltaX;
        } else {
            ty += stepY;
            traveled = tMaxY;
            tMaxY += tDeltaY;
        }
        if (traveled > maxDist) break;
        if (isWallTile(tx, ty)) {
            result.hit = true;
            result.dist = traveled;
            result.point = from + dir * traveled;
            return result;
        }
    }
    result.point = to;
    result.dist = maxDist;
    return result;
}

bool Map::lineOfSight(const Vec2& a, const Vec2& b) const {
    return !raycast(a, b).hit;
}

bool Map::inBombSite(const Vec2& p, char* outLabel) const {
    for (const auto& s : sites_) {
        if (p.x >= s.rect.x && p.x <= s.rect.x + s.rect.w &&
            p.y >= s.rect.y && p.y <= s.rect.y + s.rect.h) {
            if (outLabel) *outLabel = s.label;
            return true;
        }
    }
    return false;
}

Vec2 Map::nearestFloor(const Vec2& p) const {
    int tx = clampf(p.x / TILE, 0, (float)w_ - 1);
    int ty = clampf(p.y / TILE, 0, (float)h_ - 1);
    if (!isWallTile(tx, ty))
        return {(tx + 0.5f) * TILE, (ty + 0.5f) * TILE};
    for (int radius = 1; radius < std::max(w_, h_); ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) != radius && std::abs(dy) != radius) continue;
                int nx = tx + dx, ny = ty + dy;
                if (!isWallTile(nx, ny))
                    return {(nx + 0.5f) * TILE, (ny + 0.5f) * TILE};
            }
        }
    }
    return p;
}

std::vector<Vec2> Map::findPath(const Vec2& start, const Vec2& goal) const {
    int sx = (int)(start.x / TILE), sy = (int)(start.y / TILE);
    int gx = (int)(goal.x / TILE), gy = (int)(goal.y / TILE);
    if (isWallTile(sx, sy) || isWallTile(gx, gy)) {
        Vec2 ng = nearestFloor(goal);
        gx = (int)(ng.x / TILE); gy = (int)(ng.y / TILE);
    }
    if (isWallTile(sx, sy) || isWallTile(gx, gy)) return {};

    std::vector<int> prev(w_ * h_, -2);  // -2 unvisited, -1 = start
    std::vector<int> queue;
    queue.reserve(w_ * h_);
    int startIdx = idx(sx, sy);
    prev[startIdx] = -1;
    queue.push_back(startIdx);
    int goalIdx = idx(gx, gy);
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    size_t head = 0;
    bool found = false;
    while (head < queue.size()) {
        int cur = queue[head++];
        if (cur == goalIdx) { found = true; break; }
        int cx = cur % w_, cy = cur / w_;
        for (auto& d : dirs) {
            int nx = cx + d[0], ny = cy + d[1];
            if (isWallTile(nx, ny)) continue;
            int ni = idx(nx, ny);
            if (prev[ni] != -2) continue;
            prev[ni] = cur;
            queue.push_back(ni);
        }
    }
    if (!found) return {};

    std::vector<Vec2> path;
    int cur = goalIdx;
    while (cur != -1) {
        int cx = cur % w_, cy = cur / w_;
        path.push_back({(cx + 0.5f) * TILE, (cy + 0.5f) * TILE});
        cur = prev[cur];
    }
    std::reverse(path.begin(), path.end());
    return path;
}

void Map::render(SDL_Renderer* r, const Vec2& cam) const {
    // Floor + walls. Only draw tiles within the viewport for efficiency.
    int x0 = std::max(0, (int)(cam.x / TILE));
    int y0 = std::max(0, (int)(cam.y / TILE));
    int x1 = std::min(w_ - 1, (int)((cam.x + cfg::SCREEN_W) / TILE));
    int y1 = std::min(h_ - 1, (int)((cam.y + cfg::SCREEN_H) / TILE));

    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            SDL_Rect dst{(int)(tx * TILE - cam.x), (int)(ty * TILE - cam.y),
                         TILE, TILE};
            if (isWallTile(tx, ty)) {
                SDL_SetRenderDrawColor(r, cfg::C_WALL.r, cfg::C_WALL.g,
                                       cfg::C_WALL.b, 255);
                SDL_RenderFillRect(r, &dst);
                SDL_Rect top = dst;
                top.h = 6;
                SDL_SetRenderDrawColor(r, cfg::C_WALL_TOP.r, cfg::C_WALL_TOP.g,
                                       cfg::C_WALL_TOP.b, 255);
                SDL_RenderFillRect(r, &top);
            } else {
                const SDL_Color& f =
                    ((tx + ty) & 1) ? cfg::C_FLOOR : cfg::C_FLOOR_ALT;
                SDL_SetRenderDrawColor(r, f.r, f.g, f.b, 255);
                SDL_RenderFillRect(r, &dst);
            }
        }
    }

    // Bomb sites: translucent marker + label.
    for (const auto& s : sites_) {
        SDL_Rect dst{(int)(s.rect.x - cam.x), (int)(s.rect.y - cam.y),
                     s.rect.w, s.rect.h};
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, cfg::C_BOMBSITE.r, cfg::C_BOMBSITE.g,
                               cfg::C_BOMBSITE.b, 55);
        SDL_RenderFillRect(r, &dst);
        SDL_SetRenderDrawColor(r, cfg::C_BOMBSITE.r, cfg::C_BOMBSITE.g,
                               cfg::C_BOMBSITE.b, 180);
        SDL_RenderDrawRect(r, &dst);
    }
}
