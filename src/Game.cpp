#include "Game.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "Font.h"

namespace {
constexpr float PI = 3.14159265358979323846f;
constexpr float BOT_VIEW_RANGE = 620.0f;

const char* kCtNames[] = {"YOU", "VOLK", "SEAL", "GIGN", "SAS"};
const char* kTNames[] = {"PHOENIX", "LEET", "ARCTIC", "GUERILLA", "SEPARAT"};

float angleDiff(float a, float b) {
    float d = a - b;
    while (d > PI) d -= 2 * PI;
    while (d < -PI) d += 2 * PI;
    return d;
}
}  // namespace

Game::Game() : rng_(std::random_device{}()) {}

Game::~Game() {
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool Game::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    window_ = SDL_CreateWindow("CS 3 - Tactical Strike", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, cfg::SCREEN_W,
                               cfg::SCREEN_H, SDL_WINDOW_SHOWN);
    if (!window_) {
        std::fprintf(stderr, "CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    renderer_ = SDL_CreateRenderer(
        window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer_) {
        std::fprintf(stderr, "CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    startMatch();
    phase_ = Phase::MainMenu;
    return true;
}

float Game::frand(float lo, float hi) {
    std::uniform_real_distribution<float> d(lo, hi);
    return d(rng_);
}

// ---------------------------------------------------------------------------
// Match / round flow
// ---------------------------------------------------------------------------
void Game::startMatch() {
    ctScore_ = tScore_ = 0;
    round_ = 0;
    ctLossStreak_ = tLossStreak_ = 0;
    actors_.clear();

    const int perTeam = 5;
    for (int i = 0; i < perTeam; ++i) {
        Actor a;
        a.team = Team::CT;
        a.isPlayer = (i == 0);
        a.name = kCtNames[i % 5];
        a.money = cfg::START_MONEY;
        actors_.push_back(a);
    }
    for (int i = 0; i < perTeam; ++i) {
        Actor a;
        a.team = Team::T;
        a.name = kTNames[i % 5];
        a.money = cfg::START_MONEY;
        actors_.push_back(a);
    }
    startRound();
}

void Game::giveLoadout(Actor& a) {
    a.owned.fill(false);
    a.mag.fill(0);
    a.reserve.fill(0);
    a.owned[(size_t)WeaponId::Knife] = true;
    a.owned[(size_t)WeaponId::Pistol] = true;
    a.mag[(size_t)WeaponId::Pistol] = weaponDef(WeaponId::Pistol).magSize;
    a.reserve[(size_t)WeaponId::Pistol] = weaponDef(WeaponId::Pistol).reserveAmmo;
    a.weapon = WeaponId::Pistol;
}

void Game::resetActorsForRound() {
    const auto& ctSpawns = map_.ctSpawns();
    const auto& tSpawns = map_.tSpawns();
    int ctI = 0, tI = 0;

    for (auto& a : actors_) {
        a.alive = true;
        a.hp = cfg::START_HP;
        a.armor = 0;
        a.helmet = false;
        a.hasBomb = false;
        a.hasDefuseKit = (a.team == Team::CT);
        a.vel = {0, 0};
        a.fireCooldown = 0;
        a.reloading = false;
        a.reloadTimer = 0;
        a.muzzleFlash = 0;
        a.botState = BotState::Idle;
        a.path.clear();
        a.pathIdx = 0;
        a.repathTimer = 0;
        a.targetIdx = -1;
        a.reactionTimer = 0;
        giveLoadout(a);

        if (a.team == Team::CT) {
            Vec2 base = ctSpawns.empty() ? Vec2{100, 100}
                                         : ctSpawns[ctI % ctSpawns.size()];
            a.pos = base + Vec2{frand(-8, 8), frand(-8, 8)};
            a.aim = 0.0f;
            ctI++;
        } else {
            Vec2 base = tSpawns.empty() ? Vec2{600, 600}
                                        : tSpawns[tI % tSpawns.size()];
            a.pos = base + Vec2{frand(-8, 8), frand(-8, 8)};
            a.aim = PI;
            tI++;
        }
    }

    // Give the bomb to the first living terrorist.
    for (auto& a : actors_)
        if (a.team == Team::T) { a.hasBomb = true; break; }

    // Bots auto-buy according to their wallet.
    for (auto& a : actors_) {
        if (a.isPlayer) continue;
        if (a.money >= weaponDef(WeaponId::Rifle).price + 200) {
            a.owned[(size_t)WeaponId::Rifle] = true;
            a.mag[(size_t)WeaponId::Rifle] = weaponDef(WeaponId::Rifle).magSize;
            a.reserve[(size_t)WeaponId::Rifle] =
                weaponDef(WeaponId::Rifle).reserveAmmo;
            a.weapon = WeaponId::Rifle;
            a.money -= weaponDef(WeaponId::Rifle).price;
            a.armor = 100;
            a.helmet = true;
            a.money -= 350;
        } else if (a.money >= weaponDef(WeaponId::Smg).price) {
            a.owned[(size_t)WeaponId::Smg] = true;
            a.mag[(size_t)WeaponId::Smg] = weaponDef(WeaponId::Smg).magSize;
            a.reserve[(size_t)WeaponId::Smg] =
                weaponDef(WeaponId::Smg).reserveAmmo;
            a.weapon = WeaponId::Smg;
            a.money -= weaponDef(WeaponId::Smg).price;
        }
        if (a.money < 0) a.money = 0;
    }
}

void Game::startRound() {
    round_++;
    bombPlanted_ = false;
    bombDefused_ = false;
    bombCarried_ = true;
    bombSite_ = '?';
    bombTimer_ = 0;
    plantProgress_ = 0;
    defuseProgress_ = 0;
    tracers_.clear();
    floatTexts_.clear();
    resetActorsForRound();
    phase_ = Phase::Freeze;
    phaseTimer_ = cfg::FREEZE_TIME;
    roundMessage_ = "BUY PHASE";
    buyMenuOpen_ = true;  // open buy menu automatically for the player
}

void Game::endRound(Team winner, const std::string& reason) {
    if (phase_ == Phase::RoundEnd || phase_ == Phase::MatchEnd) return;
    lastWinner_ = winner;
    if (winner == Team::CT) ctScore_++; else tScore_++;

    // Economy: reward winners, give loss bonus to losers.
    int winReward = (reason.find("BOMB") != std::string::npos ||
                     reason.find("DEFUS") != std::string::npos)
                        ? cfg::REWARD_WIN_BOMB
                        : cfg::REWARD_WIN_ELIM;
    int& winStreak = (winner == Team::CT) ? ctLossStreak_ : tLossStreak_;
    int& loseStreak = (winner == Team::CT) ? tLossStreak_ : ctLossStreak_;
    winStreak = 0;
    int lossBonus = std::min(cfg::REWARD_LOSS_BASE + loseStreak * cfg::REWARD_LOSS_STEP,
                             cfg::REWARD_LOSS_BASE + 4 * cfg::REWARD_LOSS_STEP);
    loseStreak++;

    for (auto& a : actors_) {
        if (a.team == winner)
            a.money = std::min(cfg::MAX_MONEY, a.money + winReward);
        else
            a.money = std::min(cfg::MAX_MONEY, a.money + lossBonus);
    }

    roundMessage_ = (winner == Team::CT ? "COUNTER-TERRORISTS WIN" :
                                          "TERRORISTS WIN");
    roundMessage_ += " - " + reason;

    if (ctScore_ >= cfg::ROUNDS_TO_WIN || tScore_ >= cfg::ROUNDS_TO_WIN) {
        phase_ = Phase::MatchEnd;
        phaseTimer_ = 9999.0f;
    } else {
        phase_ = Phase::RoundEnd;
        phaseTimer_ = cfg::ROUND_END_DELAY;
    }
}

int Game::teamAlive(Team t) const {
    int n = 0;
    for (const auto& a : actors_)
        if (a.team == t && a.alive) n++;
    return n;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
int Game::run() {
    running_ = true;

    // Headless simulation mode: drive the full game logic with a fixed time
    // step and no rendering window interaction. Used for automated testing of
    // AI, combat, the bomb objective and round/economy transitions.
    if (const char* hv = std::getenv("CS3_HEADLESS")) {
        int maxFrames = std::atoi(hv);
        if (maxFrames <= 0) maxFrames = 6000;  // ~100s at 60 fps
        headless_ = true;
        startMatch();
        phase_ = Phase::Freeze;
        phaseTimer_ = cfg::FREEZE_TIME;
        const float dt = 1.0f / 60.0f;
        int lastReported = -1;
        for (int f = 0; f < maxFrames && running_; ++f) {
            update(dt);
            if (ctScore_ + tScore_ != lastReported) {
                lastReported = ctScore_ + tScore_;
                std::printf("[sim] round=%d ctAlive=%d tAlive=%d score CT %d : %d T  %s\n",
                            round_, teamAlive(Team::CT), teamAlive(Team::T),
                            ctScore_, tScore_, roundMessage_.c_str());
                std::fflush(stdout);
            }
            if (phase_ == Phase::MatchEnd) {
                std::printf("[sim] MATCH END  CT %d : %d T\n", ctScore_, tScore_);
                break;
            }
        }
        std::printf("[sim] completed: rounds played=%d, final CT %d : %d T\n",
                    round_, ctScore_, tScore_);
        return 0;
    }

    Uint64 prev = SDL_GetPerformanceCounter();
    const double freq = (double)SDL_GetPerformanceFrequency();

    while (running_) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - prev) / freq);
        prev = now;
        if (dt > 0.05f) dt = 0.05f;  // clamp huge frame spikes

        handleEvents();
        update(dt);
        render();
    }
    return 0;
}

void Game::handleEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) running_ = false;
        else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
            mouseDown_ = true;
        else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
            mouseDown_ = false;
        else if (e.type == SDL_MOUSEWHEEL) {
            // Cycle owned weapons.
            Actor& p = player();
            if (p.alive) {
                int dir = e.wheel.y > 0 ? 1 : -1;
                int cur = (int)p.weapon;
                for (int i = 0; i < (int)WeaponId::COUNT; ++i) {
                    cur = (cur + dir + (int)WeaponId::COUNT) % (int)WeaponId::COUNT;
                    if (p.owned[cur]) { switchWeapon(p, (WeaponId)cur); break; }
                }
            }
        } else if (e.type == SDL_KEYDOWN) {
            SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_ESCAPE) {
                if (phase_ == Phase::MainMenu) running_ = false;
                else buyMenuOpen_ = false;
            }
            if (phase_ == Phase::MainMenu) {
                if (k == SDLK_RETURN || k == SDLK_SPACE) {
                    startMatch();
                }
                continue;
            }
            if (phase_ == Phase::MatchEnd) {
                if (k == SDLK_RETURN || k == SDLK_SPACE) {
                    startMatch();
                    phase_ = Phase::MainMenu;
                }
                continue;
            }

            if (k == SDLK_TAB) showScoreboard_ = true;
            if (k == SDLK_b && phase_ == Phase::Freeze) buyMenuOpen_ = !buyMenuOpen_;

            Actor& p = player();
            // Buying (only in freeze phase with menu open).
            if (phase_ == Phase::Freeze && buyMenuOpen_) {
                if (k == SDLK_1) doBuy(WeaponId::Pistol);
                else if (k == SDLK_2) doBuy(WeaponId::Smg);
                else if (k == SDLK_3) doBuy(WeaponId::Rifle);
                else if (k == SDLK_4) doBuy(WeaponId::Sniper);
                else if (k == SDLK_5) buyArmor();
            } else if (p.alive) {
                // Weapon selection by slot.
                auto sel = [&](WeaponId w) { if (p.owned[(size_t)w]) switchWeapon(p, w); };
                if (k == SDLK_1) sel(WeaponId::Knife);
                else if (k == SDLK_2) sel(WeaponId::Pistol);
                else if (k == SDLK_3) sel(WeaponId::Smg);
                else if (k == SDLK_4) sel(WeaponId::Rifle);
                else if (k == SDLK_5) sel(WeaponId::Sniper);
                else if (k == SDLK_r) startReload(p);
            }
        } else if (e.type == SDL_KEYUP) {
            if (e.key.keysym.sym == SDLK_TAB) showScoreboard_ = false;
        }
    }
    SDL_GetMouseState(&mouseX_, &mouseY_);
}

void Game::switchWeapon(Actor& a, WeaponId id) {
    if (!a.owned[(size_t)id]) return;
    a.weapon = id;
    a.reloading = false;
    a.reloadTimer = 0;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void Game::update(float dt) {
    if (phase_ == Phase::MainMenu || phase_ == Phase::MatchEnd) return;

    phaseTimer_ -= dt;

    if (phase_ == Phase::Freeze && phaseTimer_ <= 0) {
        phase_ = Phase::Live;
        phaseTimer_ = cfg::ROUND_TIME;
        buyMenuOpen_ = false;
        roundMessage_.clear();
    }
    if (phase_ == Phase::RoundEnd && phaseTimer_ <= 0) {
        startRound();
        return;
    }

    bool actionPhase = (phase_ == Phase::Live || phase_ == Phase::Freeze);
    if (actionPhase) {
        if (!headless_) updatePlayer(dt);
        for (size_t i = 0; i < actors_.size(); ++i) {
            if (headless_ || !actors_[i].isPlayer) updateBot(actors_[i], dt, (int)i);
        }
        for (auto& a : actors_) {
            if (a.fireCooldown > 0) a.fireCooldown -= dt;
            if (a.muzzleFlash > 0) a.muzzleFlash -= dt;
            if (a.reloading) {
                a.reloadTimer -= dt;
                if (a.reloadTimer <= 0) {
                    WeaponId w = a.weapon;
                    int need = weaponDef(w).magSize - a.mag[(size_t)w];
                    int take = std::min(need, a.reserve[(size_t)w]);
                    a.mag[(size_t)w] += take;
                    a.reserve[(size_t)w] -= take;
                    a.reloading = false;
                }
            }
        }
    }

    if (phase_ == Phase::Live) updateBomb(dt);

    // Tracer / float-text lifetimes.
    for (auto& t : tracers_) t.life -= dt;
    tracers_.erase(std::remove_if(tracers_.begin(), tracers_.end(),
                                  [](const Tracer& t) { return t.life <= 0; }),
                   tracers_.end());
    for (auto& f : floatTexts_) { f.life -= dt; f.pos.y -= 20 * dt; }
    floatTexts_.erase(std::remove_if(floatTexts_.begin(), floatTexts_.end(),
                                     [](const FloatText& f) { return f.life <= 0; }),
                      floatTexts_.end());

    // Camera follows the player (or spectates if dead).
    Vec2 focus = player().pos;
    camera_ = focus - Vec2{cfg::SCREEN_W / 2.0f, cfg::SCREEN_H / 2.0f};
    camera_.x = clampf(camera_.x, 0, std::max(0.0f, map_.worldW() - cfg::SCREEN_W));
    camera_.y = clampf(camera_.y, 0, std::max(0.0f, map_.worldH() - cfg::SCREEN_H));

    // Round-end checks during live play.
    if (phase_ == Phase::Live) {
        int ctA = teamAlive(Team::CT);
        int tA = teamAlive(Team::T);
        if (bombPlanted_) {
            if (ctA == 0) endRound(Team::T, "TARGET PROTECTED");
        } else {
            if (tA == 0) endRound(Team::CT, "TERRORISTS ELIMINATED");
            else if (ctA == 0) endRound(Team::T, "CT ELIMINATED");
            else if (phaseTimer_ <= 0) endRound(Team::CT, "TIME EXPIRED");
        }
    }

    mousePrev_ = mouseDown_;
}

// ---------------------------------------------------------------------------
// Player control
// ---------------------------------------------------------------------------
void Game::updatePlayer(float dt) {
    Actor& p = player();
    if (!p.alive) return;

    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    Vec2 move{0, 0};
    if (ks[SDL_SCANCODE_W]) move.y -= 1;
    if (ks[SDL_SCANCODE_S]) move.y += 1;
    if (ks[SDL_SCANCODE_A]) move.x -= 1;
    if (ks[SDL_SCANCODE_D]) move.x += 1;
    bool walking = ks[SDL_SCANCODE_LSHIFT];

    float speed = cfg::PLAYER_SPEED * (walking ? cfg::WALK_MULTIPLIER : 1.0f);
    if (move.lengthSq() > 0) {
        p.pos += move.normalized() * speed * dt;
        p.pos = map_.collide(p.pos, cfg::PLAYER_RADIUS);
    }

    // Aim toward the mouse cursor.
    Vec2 worldMouse = Vec2{(float)mouseX_, (float)mouseY_} + camera_;
    Vec2 toMouse = worldMouse - p.pos;
    if (toMouse.lengthSq() > 1) p.aim = std::atan2(toMouse.y, toMouse.x);

    if (phase_ != Phase::Live) return;  // no shooting / planting in freeze

    // Fire.
    bool wantFire = mouseDown_ && (p.curDef().automatic || !mousePrev_);
    if (wantFire) tryFire(p, 0);

    // Plant / defuse with E.
    bool useKey = ks[SDL_SCANCODE_E];
    if (useKey && p.hasBomb && !bombPlanted_) {
        char label;
        if (map_.inBombSite(p.pos, &label) && move.lengthSq() == 0) {
            plantProgress_ += dt;
            if (plantProgress_ >= cfg::PLANT_TIME) {
                bombPlanted_ = true;
                bombCarried_ = false;
                p.hasBomb = false;
                bombPos_ = p.pos;
                bombSite_ = label;
                bombTimer_ = cfg::BOMB_TIMER;
                p.money = std::min(cfg::MAX_MONEY, p.money + cfg::REWARD_PLANT);
                floatTexts_.push_back({"BOMB PLANTED", p.pos, 2.5f, cfg::C_BOMBSITE});
            }
        } else {
            plantProgress_ = 0;
        }
    } else if (useKey && p.team == Team::CT && bombPlanted_ && !bombDefused_) {
        if (distance(p.pos, bombPos_) < 40 && move.lengthSq() == 0) {
            defuseProgress_ += dt;
            if (defuseProgress_ >= cfg::DEFUSE_TIME) {
                bombDefused_ = true;
                p.money = std::min(cfg::MAX_MONEY, p.money + cfg::REWARD_DEFUSE);
                endRound(Team::CT, "BOMB DEFUSED");
            }
        } else {
            defuseProgress_ = 0;
        }
    } else {
        if (p.hasBomb) plantProgress_ = 0;
    }
}

void Game::doBuy(WeaponId id) {
    Actor& p = player();
    const WeaponDef& def = weaponDef(id);
    if (p.owned[(size_t)id]) {
        // Refill ammo instead if already owned.
        p.reserve[(size_t)id] = def.reserveAmmo;
        p.mag[(size_t)id] = def.magSize;
        switchWeapon(p, id);
        return;
    }
    if (p.money < def.price) return;
    p.money -= def.price;
    p.owned[(size_t)id] = true;
    p.mag[(size_t)id] = def.magSize;
    p.reserve[(size_t)id] = def.reserveAmmo;
    switchWeapon(p, id);
}

void Game::buyArmor() {
    Actor& p = player();
    if (p.armor >= 100 && p.helmet) return;
    int cost = 1000;
    if (p.money < cost) return;
    p.money -= cost;
    p.armor = 100;
    p.helmet = true;
}

// ---------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------
void Game::startReload(Actor& a) {
    WeaponId w = a.weapon;
    const WeaponDef& def = weaponDef(w);
    if (def.magSize == 0) return;
    if (a.reloading) return;
    if (a.mag[(size_t)w] >= def.magSize) return;
    if (a.reserve[(size_t)w] <= 0) return;
    a.reloading = true;
    a.reloadTimer = def.reloadTime;
}

void Game::tryFire(Actor& a, int selfIdx) {
    if (!a.alive || a.reloading || a.fireCooldown > 0) return;
    const WeaponDef& def = a.curDef();
    WeaponId w = a.weapon;

    if (def.magSize == 0) {
        // Knife: short-range melee swing.
        a.fireCooldown = def.fireDelay;
        for (size_t i = 0; i < actors_.size(); ++i) {
            if ((int)i == selfIdx) continue;
            Actor& t = actors_[i];
            if (!t.alive || t.team == a.team) continue;
            if (distance(a.pos, t.pos) <= def.range) {
                Vec2 dir{std::cos(a.aim), std::sin(a.aim)};
                if (dot(dir, (t.pos - a.pos).normalized()) > 0.3f)
                    applyDamage(t, def.damage, selfIdx);
            }
        }
        return;
    }

    if (a.mag[(size_t)w] <= 0) { startReload(a); return; }

    a.mag[(size_t)w]--;
    a.fireCooldown = def.fireDelay;
    a.muzzleFlash = 0.05f;

    bool moving = a.vel.lengthSq() > 1.0f;
    float inacc = def.spread * (moving ? 1.4f : 0.6f);

    for (int pellet = 0; pellet < std::max(1, def.pellets); ++pellet) {
        float ang = a.aim + frand(-inacc, inacc);
        Vec2 dir{std::cos(ang), std::sin(ang)};
        Vec2 origin = a.pos + dir * (cfg::PLAYER_RADIUS + 2);
        Vec2 far = origin + dir * def.range;

        RayHit wall = map_.raycast(origin, far);
        float maxD = wall.hit ? wall.dist : def.range;
        Vec2 endPoint = wall.hit ? wall.point : far;

        // Find the closest enemy intersected by this ray.
        int bestIdx = -1;
        float bestD = maxD;
        for (size_t i = 0; i < actors_.size(); ++i) {
            if ((int)i == selfIdx) continue;
            Actor& t = actors_[i];
            if (!t.alive || t.team == a.team) continue;
            Vec2 rel = t.pos - origin;
            float proj = dot(rel, dir);
            if (proj < 0 || proj > bestD) continue;
            float perp = (rel - dir * proj).length();
            if (perp <= cfg::PLAYER_RADIUS) {
                bestD = proj;
                bestIdx = (int)i;
            }
        }

        if (bestIdx >= 0) {
            endPoint = origin + dir * bestD;
            float dmgF = def.damage *
                         std::max(0.3f, 1.0f - def.falloff * (bestD / 1000.0f));
            applyDamage(actors_[bestIdx], (int)dmgF, selfIdx);
        }

        tracers_.push_back({origin, endPoint, 0.06f, cfg::C_BULLET});
    }

    if (a.mag[(size_t)w] <= 0) startReload(a);
}

void Game::applyDamage(Actor& target, int dmg, int attackerIdx) {
    if (!target.alive) return;
    int remaining = dmg;
    if (target.armor > 0) {
        // Kevlar absorbs part of the damage.
        int absorbed = std::min(target.armor, (int)(dmg * 0.5f));
        target.armor -= absorbed;
        remaining = (int)(dmg * 0.66f);
    }
    target.hp -= remaining;

    if (target.hp <= 0) {
        target.hp = 0;
        target.alive = false;
        target.deaths++;
        floatTexts_.push_back({"X", target.pos, 1.2f, cfg::col(230, 80, 80)});

        if (attackerIdx >= 0 && attackerIdx < (int)actors_.size()) {
            Actor& killer = actors_[attackerIdx];
            if (killer.team != target.team) {
                killer.kills++;
                killer.money = std::min(cfg::MAX_MONEY,
                                        killer.money + cfg::REWARD_KILL);
            }
        }

        // Hand off the bomb if the carrier dies before planting.
        if (target.hasBomb && !bombPlanted_) {
            target.hasBomb = false;
            float best = 1e9f;
            Actor* recv = nullptr;
            for (auto& a : actors_)
                if (a.team == Team::T && a.alive) {
                    float d = distance(a.pos, target.pos);
                    if (d < best) { best = d; recv = &a; }
                }
            if (recv) recv->hasBomb = true;
        }
    }
}

// ---------------------------------------------------------------------------
// Bomb
// ---------------------------------------------------------------------------
void Game::updateBomb(float dt) {
    if (!bombPlanted_ || bombDefused_) return;
    bombTimer_ -= dt;
    if (bombTimer_ <= 0) {
        // Detonation: terrorists win, splash damage for flavor.
        for (auto& a : actors_) {
            if (a.alive && distance(a.pos, bombPos_) < 120)
                applyDamage(a, 200, -1);
        }
        endRound(Team::T, "BOMB DETONATED");
    }
}

// ---------------------------------------------------------------------------
// Bot AI
// ---------------------------------------------------------------------------
int Game::nearestVisibleEnemy(const Actor& a) const {
    int best = -1;
    float bestD = BOT_VIEW_RANGE;
    for (size_t i = 0; i < actors_.size(); ++i) {
        const Actor& t = actors_[i];
        if (!t.alive || t.team == a.team) continue;
        float d = distance(a.pos, t.pos);
        if (d > bestD) continue;
        if (map_.lineOfSight(a.pos, t.pos)) {
            bestD = d;
            best = (int)i;
        }
    }
    return best;
}

Vec2 Game::chooseObjective(const Actor& a) {
    const auto& sites = map_.bombSites();
    if (a.team == Team::T) {
        if (bombPlanted_) return bombPos_;  // defend the plant
        // Head to a bomb site (carrier plants, others support).
        if (!sites.empty()) {
            // Prefer site A early, mix it up by name hash.
            const BombSite& s = sites[(std::hash<std::string>{}(a.name)) % sites.size()];
            return s.center;
        }
    } else {
        if (bombPlanted_) return bombPos_;  // rush to defuse
        if (!sites.empty()) {
            const BombSite& s = sites[(std::hash<std::string>{}(a.name) + round_) % sites.size()];
            return s.center;
        }
    }
    return a.pos;
}

void Game::updateBot(Actor& a, float dt, int selfIdx) {
    if (!a.alive) return;
    if (a.reactionTimer > 0) a.reactionTimer -= dt;
    if (a.strafeTimer > 0) a.strafeTimer -= dt;
    a.repathTimer -= dt;

    int enemy = nearestVisibleEnemy(a);

    if (enemy >= 0) {
        if (a.targetIdx != enemy) {
            a.targetIdx = enemy;
            a.reactionTimer = frand(0.12f, 0.32f);  // human-like delay
            a.aimError = frand(-0.10f, 0.10f);
        }
        a.botState = BotState::Engage;
    } else if (a.botState == BotState::Engage) {
        a.botState = BotState::MoveToObjective;
        a.targetIdx = -1;
    }

    Vec2 desiredMove{0, 0};

    if (a.botState == BotState::Engage && a.targetIdx >= 0) {
        const Actor& tgt = actors_[a.targetIdx];
        Vec2 toT = tgt.pos - a.pos;
        float dist = toT.length();
        float wantAim = std::atan2(toT.y, toT.x) + a.aimError;
        // Smoothly turn toward the target.
        float diff = angleDiff(wantAim, a.aim);
        float turn = clampf(diff, -8.0f * dt, 8.0f * dt);
        a.aim += turn;

        // Maintain a comfortable engagement distance and strafe.
        if (a.strafeTimer <= 0) {
            a.strafeTimer = frand(0.4f, 1.1f);
            a.strafeDir = (frand(0, 1) < 0.5f) ? -1.0f : 1.0f;
        }
        Vec2 dir = toT.normalized();
        Vec2 perp{-dir.y, dir.x};
        float ideal = 220.0f;
        if (dist > ideal + 40) desiredMove += dir;
        else if (dist < ideal - 40) desiredMove -= dir;
        desiredMove += perp * a.strafeDir * 0.7f;

        // Make sure ammo is available, then fire when aligned.
        const WeaponDef& def = a.curDef();
        if (def.magSize > 0 && a.mag[(size_t)a.weapon] <= 0) {
            startReload(a);
        } else if (a.reactionTimer <= 0 && std::fabs(diff) < 0.22f) {
            tryFire(a, selfIdx);
        }
    } else {
        // Objective movement using grid pathfinding.
        Vec2 goal = chooseObjective(a);

        bool carrierPlant = (a.team == Team::T && a.hasBomb && !bombPlanted_);
        bool ctDefuse = (a.team == Team::CT && bombPlanted_ && !bombDefused_);

        if (carrierPlant) {
            char label;
            if (map_.inBombSite(a.pos, &label)) {
                plantProgress_ += dt;
                a.botState = BotState::PlantOrDefuse;
                if (plantProgress_ >= cfg::PLANT_TIME) {
                    bombPlanted_ = true;
                    bombCarried_ = false;
                    a.hasBomb = false;
                    bombPos_ = a.pos;
                    bombSite_ = label;
                    bombTimer_ = cfg::BOMB_TIMER;
                    a.money = std::min(cfg::MAX_MONEY, a.money + cfg::REWARD_PLANT);
                    floatTexts_.push_back({"BOMB PLANTED", a.pos, 2.5f, cfg::C_BOMBSITE});
                }
                desiredMove = {0, 0};
            }
        }
        if (ctDefuse && distance(a.pos, bombPos_) < 38) {
            defuseProgress_ += dt;
            a.botState = BotState::PlantOrDefuse;
            desiredMove = {0, 0};
            if (defuseProgress_ >= cfg::DEFUSE_TIME) {
                bombDefused_ = true;
                a.money = std::min(cfg::MAX_MONEY, a.money + cfg::REWARD_DEFUSE);
                endRound(Team::CT, "BOMB DEFUSED");
                return;
            }
        }

        if (a.botState != BotState::PlantOrDefuse ||
            (!map_.inBombSite(a.pos) && !(ctDefuse && distance(a.pos, bombPos_) < 38))) {
            // Recompute path periodically or when none.
            if (a.repathTimer <= 0 || a.path.empty()) {
                a.path = map_.findPath(a.pos, goal);
                a.pathIdx = 0;
                a.repathTimer = frand(0.6f, 1.0f);
            }
            // Follow the path.
            while (a.pathIdx < a.path.size() &&
                   distance(a.pos, a.path[a.pathIdx]) < cfg::TILE * 0.6f)
                a.pathIdx++;
            if (a.pathIdx < a.path.size()) {
                Vec2 wp = a.path[a.pathIdx];
                Vec2 d = (wp - a.pos);
                if (d.lengthSq() > 1) {
                    desiredMove = d.normalized();
                    a.aim = std::atan2(d.y, d.x);
                }
            }
        }
    }

    if (desiredMove.lengthSq() > 0) {
        Vec2 nv = desiredMove.normalized();
        Vec2 newPos = a.pos + nv * cfg::BOT_SPEED * dt;
        Vec2 resolved = map_.collide(newPos, cfg::PLAYER_RADIUS);
        a.vel = (resolved - a.pos) / std::max(dt, 1e-4f);
        a.pos = resolved;
    } else {
        a.vel = {0, 0};
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void Game::render() {
    SDL_SetRenderDrawColor(renderer_, 20, 20, 24, 255);
    SDL_RenderClear(renderer_);

    if (phase_ == Phase::MainMenu) {
        renderMainMenu();
    } else {
        renderWorld();
        renderHUD();
        renderMinimap();
        if (phase_ == Phase::Freeze && buyMenuOpen_) renderBuyMenu();
        if (phase_ == Phase::Freeze || phase_ == Phase::RoundEnd)
            renderRoundBanner();
        if (showScoreboard_ || phase_ == Phase::MatchEnd) renderScoreboard();
        if (phase_ == Phase::MatchEnd) renderRoundBanner();
    }

    SDL_RenderPresent(renderer_);
}

void Game::drawFilledCircle(int cx, int cy, int radius, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    for (int dy = -radius; dy <= radius; ++dy) {
        int dx = (int)std::sqrt((float)radius * radius - dy * dy);
        SDL_RenderDrawLine(renderer_, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

void Game::renderWorld() {
    map_.render(renderer_, camera_);

    // Planted bomb marker (blinking).
    if (bombPlanted_ && !bombDefused_) {
        Vec2 s = worldToScreen(bombPos_);
        bool blink = ((int)(bombTimer_ * 4) & 1) == 0;
        SDL_Color c = blink ? cfg::col(255, 60, 60) : cfg::col(120, 30, 30);
        SDL_Rect br{(int)s.x - 7, (int)s.y - 5, 14, 10};
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, 255);
        SDL_RenderFillRect(renderer_, &br);
    } else if (bombCarried_) {
        // Show the bomb carrier marker handled in renderActor.
    }

    // Tracers.
    for (const auto& t : tracers_) {
        float alpha = clampf(t.life / 0.06f, 0, 1) * 255;
        SDL_SetRenderDrawColor(renderer_, t.color.r, t.color.g, t.color.b,
                               (Uint8)alpha);
        Vec2 a = worldToScreen(t.a);
        Vec2 b = worldToScreen(t.b);
        SDL_RenderDrawLine(renderer_, (int)a.x, (int)a.y, (int)b.x, (int)b.y);
    }

    for (const auto& a : actors_) renderActor(a);

    // Floating text.
    for (const auto& f : floatTexts_) {
        Vec2 s = worldToScreen(f.pos);
        SDL_Color c = f.color;
        c.a = (Uint8)(clampf(f.life, 0, 1) * 255);
        Font::drawCentered(renderer_, f.text, (int)s.x, (int)s.y - 20, 2, c);
    }
}

void Game::renderActor(const Actor& a) {
    if (!a.alive) {
        // Draw a dim corpse marker.
        Vec2 s = worldToScreen(a.pos);
        if (s.x < -40 || s.y < -40 || s.x > cfg::SCREEN_W + 40 ||
            s.y > cfg::SCREEN_H + 40)
            return;
        drawFilledCircle((int)s.x, (int)s.y, (int)cfg::PLAYER_RADIUS,
                         cfg::col(70, 60, 60, 160));
        return;
    }
    Vec2 s = worldToScreen(a.pos);
    if (s.x < -40 || s.y < -40 || s.x > cfg::SCREEN_W + 40 ||
        s.y > cfg::SCREEN_H + 40)
        return;

    SDL_Color body = (a.team == Team::CT) ? cfg::C_CT : cfg::C_T;
    drawFilledCircle((int)s.x, (int)s.y, (int)cfg::PLAYER_RADIUS, body);

    // Player highlight ring.
    if (a.isPlayer) {
        SDL_SetRenderDrawColor(renderer_, cfg::C_PLAYER.r, cfg::C_PLAYER.g,
                               cfg::C_PLAYER.b, 255);
        for (int k = 0; k < 360; k += 12) {
            float r1 = k * PI / 180.0f;
            int px = (int)(s.x + std::cos(r1) * (cfg::PLAYER_RADIUS + 3));
            int py = (int)(s.y + std::sin(r1) * (cfg::PLAYER_RADIUS + 3));
            SDL_RenderDrawPoint(renderer_, px, py);
        }
    }

    // Gun barrel direction.
    Vec2 dir{std::cos(a.aim), std::sin(a.aim)};
    Vec2 muzzle = s + dir * (cfg::PLAYER_RADIUS + 10);
    SDL_SetRenderDrawColor(renderer_, 20, 20, 20, 255);
    SDL_RenderDrawLine(renderer_, (int)s.x, (int)s.y, (int)muzzle.x,
                       (int)muzzle.y);

    if (a.muzzleFlash > 0)
        drawFilledCircle((int)muzzle.x, (int)muzzle.y, 4,
                         cfg::col(255, 220, 120, 230));

    // Bomb carrier indicator.
    if (a.hasBomb) {
        SDL_Rect b{(int)s.x - 4, (int)s.y - (int)cfg::PLAYER_RADIUS - 10, 8, 6};
        SDL_SetRenderDrawColor(renderer_, 230, 60, 60, 255);
        SDL_RenderFillRect(renderer_, &b);
    }

    // Health bar for non-player actors.
    if (!a.isPlayer) {
        int bw = 26;
        int bx = (int)s.x - bw / 2;
        int by = (int)s.y - (int)cfg::PLAYER_RADIUS - 8;
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
        SDL_Rect bg{bx - 1, by - 1, bw + 2, 5};
        SDL_RenderFillRect(renderer_, &bg);
        SDL_Color hc = (a.team == player().team) ? cfg::C_HEALTH
                                                  : cfg::col(220, 80, 80);
        SDL_SetRenderDrawColor(renderer_, hc.r, hc.g, hc.b, 255);
        SDL_Rect hb{bx, by, bw * a.hp / cfg::START_HP, 3};
        SDL_RenderFillRect(renderer_, &hb);
    }
}

void Game::renderHUD() {
    const Actor& p = player();

    // Bottom bar background.
    SDL_SetRenderDrawColor(renderer_, cfg::C_HUD_BG.r, cfg::C_HUD_BG.g,
                           cfg::C_HUD_BG.b, cfg::C_HUD_BG.a);
    SDL_Rect bar{0, cfg::SCREEN_H - 64, cfg::SCREEN_W, 64};
    SDL_RenderFillRect(renderer_, &bar);

    char buf[128];

    // Health & armor (bottom-left).
    if (p.alive) {
        std::snprintf(buf, sizeof(buf), "HP %d", p.hp);
        Font::draw(renderer_, buf, 24, cfg::SCREEN_H - 50, 3, cfg::C_HEALTH);
        std::snprintf(buf, sizeof(buf), "ARMOR %d", p.armor);
        Font::draw(renderer_, buf, 24, cfg::SCREEN_H - 26, 2, cfg::C_ARMOR);
    } else {
        Font::draw(renderer_, "DEAD - SPECTATING", 24, cfg::SCREEN_H - 44, 3,
                   cfg::col(220, 80, 80));
    }

    // Money (bottom-left, above bar).
    std::snprintf(buf, sizeof(buf), "$%d", p.money);
    Font::draw(renderer_, buf, 24, cfg::SCREEN_H - 92, 3, cfg::col(120, 220, 120));

    // Weapon & ammo (bottom-right).
    const WeaponDef& def = p.curDef();
    Font::draw(renderer_, def.name, cfg::SCREEN_W - 360, cfg::SCREEN_H - 50, 3,
               cfg::C_TEXT);
    if (def.magSize > 0) {
        std::snprintf(buf, sizeof(buf), "%d / %d", p.mag[(size_t)p.weapon],
                      p.reserve[(size_t)p.weapon]);
        Font::draw(renderer_, buf, cfg::SCREEN_W - 360, cfg::SCREEN_H - 26, 2,
                   p.reloading ? cfg::col(230, 180, 80) : cfg::C_TEXT);
        if (p.reloading)
            Font::draw(renderer_, "RELOADING", cfg::SCREEN_W - 200,
                       cfg::SCREEN_H - 26, 2, cfg::col(230, 180, 80));
    }

    // Top-center: score + timer.
    std::snprintf(buf, sizeof(buf), "CT %d", ctScore_);
    Font::draw(renderer_, buf, cfg::SCREEN_W / 2 - 140, 16, 3, cfg::C_CT);
    std::snprintf(buf, sizeof(buf), "%d T", tScore_);
    Font::draw(renderer_, buf, cfg::SCREEN_W / 2 + 70, 16, 3, cfg::C_T);

    float t = phaseTimer_;
    if (bombPlanted_ && phase_ == Phase::Live) t = bombTimer_;
    if (t < 0) t = 0;
    int sec = (int)t;
    std::snprintf(buf, sizeof(buf), "%d:%02d", sec / 60, sec % 60);
    SDL_Color tc = (bombPlanted_ && phase_ == Phase::Live) ? cfg::col(255, 80, 80)
                                                           : cfg::C_TEXT;
    Font::drawCentered(renderer_, buf, cfg::SCREEN_W / 2, 14, 4, tc);

    // Plant / defuse progress bar.
    auto progressBar = [&](float frac, const std::string& label, SDL_Color c) {
        int w = 300, h = 22;
        int x = cfg::SCREEN_W / 2 - w / 2;
        int y = cfg::SCREEN_H / 2 + 80;
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 200);
        SDL_Rect bg{x - 2, y - 2, w + 4, h + 4};
        SDL_RenderFillRect(renderer_, &bg);
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, 255);
        SDL_Rect fg{x, y, (int)(w * clampf(frac, 0, 1)), h};
        SDL_RenderFillRect(renderer_, &fg);
        Font::drawCentered(renderer_, label, cfg::SCREEN_W / 2, y - 24, 2,
                           cfg::C_TEXT);
    };
    if (p.alive && p.hasBomb && plantProgress_ > 0 && !bombPlanted_)
        progressBar(plantProgress_ / cfg::PLANT_TIME, "PLANTING",
                    cfg::col(230, 120, 60));
    if (p.alive && p.team == Team::CT && bombPlanted_ && defuseProgress_ > 0)
        progressBar(defuseProgress_ / cfg::DEFUSE_TIME, "DEFUSING",
                    cfg::col(80, 160, 230));

    // Hint line.
    if (phase_ == Phase::Live && p.alive) {
        if (p.hasBomb)
            Font::drawCentered(renderer_, "HOLD E ON A BOMB SITE TO PLANT",
                               cfg::SCREEN_W / 2, cfg::SCREEN_H - 92, 2,
                               cfg::col(200, 200, 120));
        else if (bombPlanted_ && p.team == Team::CT)
            Font::drawCentered(renderer_, "HOLD E ON THE BOMB TO DEFUSE",
                               cfg::SCREEN_W / 2, cfg::SCREEN_H - 92, 2,
                               cfg::col(120, 180, 230));
    }
}

void Game::renderBuyMenu() {
    int w = 460, h = 360;
    int x = cfg::SCREEN_W / 2 - w / 2;
    int y = cfg::SCREEN_H / 2 - h / 2;
    SDL_SetRenderDrawColor(renderer_, 12, 14, 18, 235);
    SDL_Rect bg{x, y, w, h};
    SDL_RenderFillRect(renderer_, &bg);
    SDL_SetRenderDrawColor(renderer_, 90, 150, 235, 255);
    SDL_RenderDrawRect(renderer_, &bg);

    Font::drawCentered(renderer_, "BUY MENU", cfg::SCREEN_W / 2, y + 16, 3,
                       cfg::C_TEXT);
    const Actor& p = player();
    char buf[128];
    std::snprintf(buf, sizeof(buf), "MONEY $%d", p.money);
    Font::drawCentered(renderer_, buf, cfg::SCREEN_W / 2, y + 48, 2,
                       cfg::col(120, 220, 120));

    struct Row { const char* key; WeaponId id; };
    Row rows[] = {{"1", WeaponId::Pistol}, {"2", WeaponId::Smg},
                  {"3", WeaponId::Rifle}, {"4", WeaponId::Sniper}};
    int ry = y + 90;
    for (auto& row : rows) {
        const WeaponDef& d = weaponDef(row.id);
        SDL_Color c = p.owned[(size_t)row.id]
                          ? cfg::col(120, 220, 120)
                          : (p.money >= d.price ? cfg::C_TEXT
                                                : cfg::col(120, 120, 120));
        std::snprintf(buf, sizeof(buf), "%s  %s", row.key, d.name.c_str());
        Font::draw(renderer_, buf, x + 30, ry, 2, c);
        std::snprintf(buf, sizeof(buf), "$%d", d.price);
        Font::draw(renderer_, buf, x + w - 130, ry, 2, c);
        ry += 40;
    }
    SDL_Color ac = (p.armor >= 100 && p.helmet) ? cfg::col(120, 220, 120)
                                                : cfg::C_TEXT;
    Font::draw(renderer_, "5  KEVLAR + HELMET", x + 30, ry, 2, ac);
    Font::draw(renderer_, "$1000", x + w - 130, ry, 2, ac);

    Font::drawCentered(renderer_, "PRESS B TO CLOSE", cfg::SCREEN_W / 2,
                       y + h - 28, 2, cfg::col(160, 160, 160));
}

void Game::renderMainMenu() {
    Font::drawCentered(renderer_, "C S  3", cfg::SCREEN_W / 2, 150, 12,
                       cfg::col(90, 150, 235));
    Font::drawCentered(renderer_, "TACTICAL STRIKE", cfg::SCREEN_W / 2, 280, 4,
                       cfg::C_TEXT);

    const char* lines[] = {
        "WASD - MOVE        MOUSE - AIM        LEFT CLICK - SHOOT",
        "R - RELOAD     1-5 - WEAPONS     SHIFT - WALK     E - PLANT / DEFUSE",
        "B - BUY MENU (FREEZE TIME)     TAB - SCOREBOARD     ESC - QUIT",
        "",
        "DEFEAT THE ENEMY TEAM OR COMPLETE THE BOMB OBJECTIVE",
        "FIRST TO 8 ROUNDS WINS THE MATCH",
    };
    int y = 380;
    for (auto* l : lines) {
        Font::drawCentered(renderer_, l, cfg::SCREEN_W / 2, y, 2,
                           cfg::col(200, 200, 210));
        y += 30;
    }
    bool blink = ((int)(SDL_GetTicks() / 500) & 1) == 0;
    if (blink)
        Font::drawCentered(renderer_, "PRESS ENTER TO START", cfg::SCREEN_W / 2,
                           y + 30, 3, cfg::col(120, 220, 120));
}

void Game::renderRoundBanner() {
    if (roundMessage_.empty() && phase_ != Phase::MatchEnd) return;
    int boxH = 70;
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 170);
    SDL_Rect box{0, cfg::SCREEN_H / 2 - boxH / 2 - 40, cfg::SCREEN_W, boxH};
    SDL_RenderFillRect(renderer_, &box);

    if (phase_ == Phase::MatchEnd) {
        bool ctWon = ctScore_ > tScore_;
        std::string msg = ctWon ? "COUNTER-TERRORISTS WIN THE MATCH"
                                : "TERRORISTS WIN THE MATCH";
        Font::drawCentered(renderer_, msg, cfg::SCREEN_W / 2,
                           cfg::SCREEN_H / 2 - 56, 3,
                           ctWon ? cfg::C_CT : cfg::C_T);
        Font::drawCentered(renderer_, "PRESS ENTER FOR MAIN MENU",
                           cfg::SCREEN_W / 2, cfg::SCREEN_H / 2 + 60, 2,
                           cfg::col(180, 180, 180));
        return;
    }

    SDL_Color c = cfg::C_TEXT;
    if (phase_ == Phase::RoundEnd)
        c = (lastWinner_ == Team::CT) ? cfg::C_CT : cfg::C_T;
    Font::drawCentered(renderer_, roundMessage_, cfg::SCREEN_W / 2,
                       cfg::SCREEN_H / 2 - 56, 3, c);

    if (phase_ == Phase::Freeze) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "ROUND %d", round_);
        Font::drawCentered(renderer_, buf, cfg::SCREEN_W / 2,
                           cfg::SCREEN_H / 2 + 20, 2, cfg::col(200, 200, 200));
    }
}

void Game::renderScoreboard() {
    int w = 720, h = 420;
    int x = cfg::SCREEN_W / 2 - w / 2;
    int y = cfg::SCREEN_H / 2 - h / 2;
    SDL_SetRenderDrawColor(renderer_, 10, 12, 16, 235);
    SDL_Rect bg{x, y, w, h};
    SDL_RenderFillRect(renderer_, &bg);
    SDL_SetRenderDrawColor(renderer_, 80, 80, 90, 255);
    SDL_RenderDrawRect(renderer_, &bg);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "CT %d   -   %d T", ctScore_, tScore_);
    Font::drawCentered(renderer_, buf, cfg::SCREEN_W / 2, y + 16, 3, cfg::C_TEXT);

    auto drawTeam = [&](Team team, int colX, SDL_Color tc) {
        Font::draw(renderer_, team == Team::CT ? "COUNTER-TERRORISTS"
                                               : "TERRORISTS",
                   colX, y + 60, 2, tc);
        Font::draw(renderer_, "NAME", colX, y + 92, 2, cfg::col(150, 150, 150));
        Font::draw(renderer_, "K", colX + 200, y + 92, 2, cfg::col(150, 150, 150));
        Font::draw(renderer_, "D", colX + 240, y + 92, 2, cfg::col(150, 150, 150));
        Font::draw(renderer_, "$", colX + 280, y + 92, 2, cfg::col(150, 150, 150));
        int row = y + 120;
        for (const auto& a : actors_) {
            if (a.team != team) continue;
            SDL_Color nc = a.isPlayer ? cfg::col(120, 220, 120)
                                      : (a.alive ? cfg::C_TEXT
                                                 : cfg::col(120, 120, 120));
            Font::draw(renderer_, a.name, colX, row, 2, nc);
            std::snprintf(buf, sizeof(buf), "%d", a.kills);
            Font::draw(renderer_, buf, colX + 200, row, 2, nc);
            std::snprintf(buf, sizeof(buf), "%d", a.deaths);
            Font::draw(renderer_, buf, colX + 240, row, 2, nc);
            std::snprintf(buf, sizeof(buf), "%d", a.money);
            Font::draw(renderer_, buf, colX + 280, row, 2, nc);
            row += 30;
        }
    };
    drawTeam(Team::CT, x + 30, cfg::C_CT);
    drawTeam(Team::T, x + w / 2 + 10, cfg::C_T);
}

void Game::renderMinimap() {
    int mmW = 200, mmH = (int)(mmW * map_.worldH() / map_.worldW());
    int ox = cfg::SCREEN_W - mmW - 16;
    int oy = 16;
    float sx = mmW / map_.worldW();
    float sy = mmH / map_.worldH();

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 170);
    SDL_Rect bg{ox - 4, oy - 4, mmW + 8, mmH + 8};
    SDL_RenderFillRect(renderer_, &bg);

    // Walls.
    SDL_SetRenderDrawColor(renderer_, 80, 80, 90, 200);
    for (int ty = 0; ty < map_.height(); ++ty)
        for (int tx = 0; tx < map_.width(); ++tx)
            if (map_.isWallTile(tx, ty)) {
                SDL_Rect r{ox + (int)(tx * cfg::TILE * sx),
                           oy + (int)(ty * cfg::TILE * sy),
                           std::max(1, (int)(cfg::TILE * sx)),
                           std::max(1, (int)(cfg::TILE * sy))};
                SDL_RenderFillRect(renderer_, &r);
            }

    // Bomb sites.
    for (const auto& s : map_.bombSites()) {
        SDL_SetRenderDrawColor(renderer_, 190, 70, 70, 160);
        SDL_Rect r{ox + (int)(s.rect.x * sx), oy + (int)(s.rect.y * sy),
                   (int)(s.rect.w * sx), (int)(s.rect.h * sy)};
        SDL_RenderDrawRect(renderer_, &r);
    }

    // Actors (only show enemies the player can currently see + all allies).
    const Actor& me = player();
    for (const auto& a : actors_) {
        if (!a.alive) continue;
        bool ally = (a.team == me.team);
        bool visible = ally || (me.alive && map_.lineOfSight(me.pos, a.pos) &&
                                distance(me.pos, a.pos) < BOT_VIEW_RANGE);
        if (!visible) continue;
        SDL_Color c = a.isPlayer ? cfg::C_PLAYER
                                 : (a.team == Team::CT ? cfg::C_CT : cfg::C_T);
        int px = ox + (int)(a.pos.x * sx);
        int py = oy + (int)(a.pos.y * sy);
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, 255);
        SDL_Rect r{px - 2, py - 2, 4, 4};
        SDL_RenderFillRect(renderer_, &r);
    }

    if (bombPlanted_ && !bombDefused_) {
        int px = ox + (int)(bombPos_.x * sx);
        int py = oy + (int)(bombPos_.y * sy);
        SDL_SetRenderDrawColor(renderer_, 255, 60, 60, 255);
        SDL_Rect r{px - 2, py - 2, 5, 5};
        SDL_RenderFillRect(renderer_, &r);
    }
}

void Game::drawRectWorld(const SDL_Rect& worldRect, SDL_Color c, bool fill) {
    SDL_Rect r{worldRect.x - (int)camera_.x, worldRect.y - (int)camera_.y,
               worldRect.w, worldRect.h};
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    if (fill) SDL_RenderFillRect(renderer_, &r);
    else SDL_RenderDrawRect(renderer_, &r);
}
