#pragma once
#include <SDL.h>

namespace cfg {

constexpr int SCREEN_W = 1280;
constexpr int SCREEN_H = 720;
constexpr int TILE = 40;  // world units per tile (also pixels at zoom 1)

constexpr float PLAYER_RADIUS = 13.0f;
constexpr float PLAYER_SPEED = 200.0f;       // units / second when running
constexpr float WALK_MULTIPLIER = 0.5f;      // holding walk key
constexpr float BOT_SPEED = 165.0f;

constexpr int START_HP = 100;
constexpr int START_MONEY = 800;
constexpr int MAX_MONEY = 16000;

constexpr float ROUND_TIME = 115.0f;         // seconds of action per round
constexpr float FREEZE_TIME = 6.0f;          // buy phase
constexpr float BOMB_TIMER = 40.0f;          // seconds after plant
constexpr float PLANT_TIME = 3.2f;           // time to plant
constexpr float DEFUSE_TIME = 5.0f;          // time to defuse (no kit)
constexpr float DEFUSE_TIME_KIT = 5.0f;      // kept simple
constexpr float ROUND_END_DELAY = 4.0f;      // pause between rounds

constexpr int ROUNDS_TO_WIN = 8;             // first team to this wins the match

// Reward economy (simplified Counter-Strike rules).
constexpr int REWARD_KILL = 300;
constexpr int REWARD_WIN_ELIM = 3250;
constexpr int REWARD_WIN_BOMB = 3500;
constexpr int REWARD_PLANT = 300;
constexpr int REWARD_DEFUSE = 300;
constexpr int REWARD_LOSS_BASE = 1400;
constexpr int REWARD_LOSS_STEP = 500;

// Colors -------------------------------------------------------------------
inline SDL_Color col(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255) {
    return SDL_Color{r, g, b, a};
}

const SDL_Color C_FLOOR = col(46, 48, 54);
const SDL_Color C_FLOOR_ALT = col(40, 42, 48);
const SDL_Color C_WALL = col(96, 92, 84);
const SDL_Color C_WALL_TOP = col(120, 116, 106);
const SDL_Color C_CT = col(80, 150, 235);
const SDL_Color C_T = col(225, 175, 70);
const SDL_Color C_PLAYER = col(90, 220, 120);
const SDL_Color C_BOMBSITE = col(190, 70, 70);
const SDL_Color C_TEXT = col(235, 235, 235);
const SDL_Color C_HUD_BG = col(18, 18, 22, 210);
const SDL_Color C_HEALTH = col(90, 200, 110);
const SDL_Color C_ARMOR = col(90, 150, 235);
const SDL_Color C_BULLET = col(255, 230, 120);

}  // namespace cfg

enum class Team { CT, T };

enum class Difficulty { Easy, Normal, Hard };

// Per-difficulty bot tuning. Higher difficulty = faster reactions, tighter aim.
struct BotTuning {
    float reactMin, reactMax;  // reaction delay before opening fire (s)
    float aimError;            // random aim offset magnitude (rad)
    float spreadMul;           // extra weapon inaccuracy multiplier
    float turnSpeed;           // how fast they swing onto a target (rad/s)
    float viewRange;           // how far they can spot enemies (world units)
    float fireAlign;           // |angle error| under which they pull the trigger
};

inline BotTuning botTuning(Difficulty d) {
    switch (d) {
        case Difficulty::Easy:
            return {0.55f, 1.10f, 0.40f, 2.60f, 4.5f, 460.0f, 0.10f};
        case Difficulty::Hard:
            return {0.12f, 0.30f, 0.08f, 1.00f, 8.0f, 640.0f, 0.22f};
        case Difficulty::Normal:
        default:
            return {0.32f, 0.62f, 0.18f, 1.55f, 6.0f, 560.0f, 0.16f};
    }
}
