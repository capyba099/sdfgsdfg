#include "Weapon.h"
#include <array>

namespace {
const std::array<WeaponDef, static_cast<size_t>(WeaponId::COUNT)> kDefs = {{
    // id              name        dmg  delay  mag  res  reload spread  range  falloff pellets price  auto
    {WeaponId::Knife,  "KNIFE",     55, 0.45f,   0,   0, 0.00f, 0.00f,   46.0f, 0.00f, 1,      0,    true},
    {WeaponId::Pistol, "PISTOL",    26, 0.16f,  12,  48, 1.60f, 0.030f, 700.0f, 0.30f, 1,      0,    false},
    {WeaponId::Smg,    "SMG",       21, 0.075f, 30, 120, 2.10f, 0.055f, 650.0f, 0.45f, 1,   1500,    true},
    {WeaponId::Rifle,  "RIFLE",     33, 0.095f, 30,  90, 2.40f, 0.040f, 950.0f, 0.20f, 1,   2700,    true},
    {WeaponId::Sniper, "SNIPER",    115, 1.45f,  10,  30, 3.40f, 0.010f, 1400.0f,0.05f, 1,   4750,    false},
}};
}  // namespace

const WeaponDef& weaponDef(WeaponId id) {
    return kDefs[static_cast<size_t>(id)];
}

const std::vector<WeaponId>& buyableWeapons() {
    static const std::vector<WeaponId> list = {
        WeaponId::Pistol, WeaponId::Smg, WeaponId::Rifle, WeaponId::Sniper};
    return list;
}
