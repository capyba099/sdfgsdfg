#pragma once
#include <string>
#include <vector>

enum class WeaponId {
    Knife,
    Pistol,     // USP/Glock style sidearm (everyone spawns with this)
    Smg,        // MP5-style
    Rifle,      // AK/M4-style
    Sniper,     // AWP-style
    COUNT
};

struct WeaponDef {
    WeaponId id;
    std::string name;
    int damage;             // base damage per bullet at point blank
    float fireDelay;        // seconds between shots
    int magSize;            // rounds per magazine (0 = melee)
    int reserveAmmo;        // spare rounds carried
    float reloadTime;       // seconds
    float spread;           // radians of random cone at full inaccuracy
    float range;            // max effective range in world units
    float falloff;          // damage multiplier reduction per 1000 units
    int pellets;            // bullets fired per trigger pull
    int price;              // buy-menu cost
    bool automatic;         // hold to keep firing
};

// Central catalogue of all weapons in the game.
const WeaponDef& weaponDef(WeaponId id);

// Weapons that appear in the buy menu, in display order.
const std::vector<WeaponId>& buyableWeapons();
