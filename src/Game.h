#pragma once
#include <SDL.h>
#include <array>
#include <random>
#include <string>
#include <vector>
#include "Config.h"
#include "Map.h"
#include "Vec2.h"
#include "Weapon.h"

enum class Phase { MainMenu, Freeze, Live, RoundEnd, MatchEnd };

enum class BotState { Idle, MoveToObjective, Engage, PlantOrDefuse };

struct Actor {
    Team team = Team::CT;
    bool isPlayer = false;
    bool alive = true;
    std::string name;

    Vec2 pos;
    Vec2 vel;
    float aim = 0.0f;     // facing angle in radians
    int hp = cfg::START_HP;
    int armor = 0;
    bool helmet = false;
    int money = cfg::START_MONEY;

    // Weapon state, indexed by WeaponId.
    std::array<bool, (size_t)WeaponId::COUNT> owned{};
    std::array<int, (size_t)WeaponId::COUNT> mag{};
    std::array<int, (size_t)WeaponId::COUNT> reserve{};
    WeaponId weapon = WeaponId::Pistol;

    float fireCooldown = 0.0f;
    float reloadTimer = 0.0f;
    bool reloading = false;
    float muzzleFlash = 0.0f;

    // Objective state.
    bool hasBomb = false;
    bool hasDefuseKit = false;

    // Stats.
    int kills = 0;
    int deaths = 0;

    // AI fields.
    BotState botState = BotState::Idle;
    std::vector<Vec2> path;
    size_t pathIdx = 0;
    float repathTimer = 0.0f;
    int targetIdx = -1;
    float reactionTimer = 0.0f;
    float aimError = 0.0f;
    float strafeDir = 1.0f;
    float strafeTimer = 0.0f;

    const WeaponDef& curDef() const { return weaponDef(weapon); }
};

struct Tracer {
    Vec2 a, b;
    float life;
    SDL_Color color;
};

struct FloatText {
    std::string text;
    Vec2 pos;
    float life;
    SDL_Color color;
};

class Game {
public:
    Game();
    ~Game();

    bool init();
    int run();

private:
    // Lifecycle ------------------------------------------------------------
    void handleEvents();
    void update(float dt);
    void render();

    // Round / match flow ---------------------------------------------------
    void startMatch();
    void startRound();
    void endRound(Team winner, const std::string& reason);
    void giveLoadout(Actor& a);
    void resetActorsForRound();

    // Player ---------------------------------------------------------------
    void updatePlayer(float dt);
    void doBuy(WeaponId id);
    void buyArmor();

    // Bots -----------------------------------------------------------------
    void updateBot(Actor& a, float dt, int selfIdx);
    int nearestVisibleEnemy(const Actor& a) const;
    Vec2 chooseObjective(const Actor& a);

    // Combat ---------------------------------------------------------------
    void tryFire(Actor& a, int selfIdx);
    void startReload(Actor& a);
    void applyDamage(Actor& target, int dmg, int attackerIdx);
    void switchWeapon(Actor& a, WeaponId id);

    // Bomb -----------------------------------------------------------------
    void updateBomb(float dt);

    // Rendering helpers ----------------------------------------------------
    void renderWorld();
    void renderActor(const Actor& a);
    void renderHUD();
    void renderBuyMenu();
    void renderMainMenu();
    void renderRoundBanner();
    void renderScoreboard();
    void renderMinimap();
    void drawFilledCircle(int cx, int cy, int radius, SDL_Color c);
    void drawRectWorld(const SDL_Rect& worldRect, SDL_Color c, bool fill);
    Vec2 worldToScreen(const Vec2& w) const { return w - camera_; }

    // Helpers --------------------------------------------------------------
    int teamAlive(Team t) const;
    Actor& player() { return actors_[0]; }
    float frand(float lo, float hi);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool running_ = false;
    bool headless_ = false;

    Map map_;
    std::vector<Actor> actors_;
    std::vector<Tracer> tracers_;
    std::vector<FloatText> floatTexts_;
    Vec2 camera_;

    Phase phase_ = Phase::MainMenu;
    float phaseTimer_ = 0.0f;
    int round_ = 0;
    int ctScore_ = 0;
    int tScore_ = 0;
    int ctLossStreak_ = 0;
    int tLossStreak_ = 0;
    Team lastWinner_ = Team::CT;
    std::string roundMessage_;

    // Bomb state.
    bool bombCarried_ = true;     // bomb is on a living T until planted
    bool bombPlanted_ = false;
    bool bombDefused_ = false;
    Vec2 bombPos_;
    char bombSite_ = '?';
    float bombTimer_ = 0.0f;
    float plantProgress_ = 0.0f;
    float defuseProgress_ = 0.0f;
    bool showScoreboard_ = false;
    bool buyMenuOpen_ = false;

    // Input snapshot.
    bool mouseDown_ = false;
    bool mousePrev_ = false;
    int mouseX_ = 0, mouseY_ = 0;

    std::mt19937 rng_;
};
