// ZOMBIE SURVIVAL ARENA - Phase 7 build (SFML 3, C++17)
// Phase 7 adds the SAVE SYSTEM on top of the Phase 5 build below:
//   * zsa::SaveManager (saves/player_data.dat + saves/settings.dat): statistics,
//     personal bests, discovered weapons, saved settings and a resumable run.
//   * CONTINUE on the main menu (progress is checkpointed at the start of every
//     wave), PROFILE screen, fullscreen option (also F11), personal bests on the
//     menu and the game-over screen.
// Compile SaveManager.cpp together with this file (see the README notes).
//
// ZOMBIE SURVIVAL ARENA - Phase 5 build (SFML 3, C++17)
// Adds: AudioManager (sfx + music + saved volumes), pause menu, settings and
// controls screens, polished main menu / HUD / game-over screen, hit markers,
// damage numbers, screen shake/flash and fades - on top of the Phase 4 game and
// the zombie death animation (assets/zombie_death.png, 2176x181), unchanged.
// Link sfml-audio. Sounds go in assets/audio/ (see AudioManager).
//
#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <iostream>
#include <optional>
#include <string>
#include <random>
#include <utility>
#include <vector>

// Phase 7: the save system (works with include/systems/ or a flat folder).
#if __has_include("systems/SaveManager.h")
#include "systems/SaveManager.h"
#else
#include "systems/SaveManager.h"
#endif

// ==================================================
// DATA STRUCTURES
// ==================================================

struct Bullet
{
    sf::Vector2f position;
    sf::Vector2f velocity;
    float life = 1.5f;

    // Per-weapon properties (filled in when the bullet is fired).
    float damage = 1.f;
    int pierceLeft = 0;                        // Extra zombies it may pass through.
    float radius = 3.5f;
    float tailLength = 0.f;                    // >0 draws a tracer streak.
    sf::Color color = sf::Color(255, 220, 125);
    sf::Color outline = sf::Color(255, 150, 55);
    std::vector<int> hitZombieIds;             // Piercing bullets never hit a zombie twice.

    sf::Vector2f previousGround;               // Last ground-plane position (wall tests).
    bool hitSolid = false;                     // Stopped by a wall/prop this frame.
};

struct Particle
{
    sf::Vector2f position;
    sf::Vector2f velocity;
    float life = 0.45f;
    float maxLife = 0.45f;
    float size = 2.f;
    sf::Color color = sf::Color(190, 35, 35);
};

struct Zombie
{
    int id = 0;   // Unique id (lets piercing bullets remember who they already hit).
    sf::Vector2f position;
    float health = 3.f;
    float maxHealth = 3.f;
    float speed = 55.f;

    // Walking animation: 8 frames in one horizontal row.
    int animationFrame = 0;
    float animationTimer = 0.f;
    bool facingLeft = false;
    float hitFlashTimer = 0.f;
    float spawnShieldTimer = 3.f;

    // Attack animation and timing.
    bool isAttacking = false;
    int attackFrame = 0;
    float attackAnimationTimer = 0.f;
    float attackCooldownTimer = 0.f;
    bool attackDamageApplied = false;

    // Death animation: 8 frames in one horizontal row.
    bool isDying = false;
    int deathFrame = 0;
    float deathAnimationTimer = 0.f;
    float corpseTimer = 0.f;      // Time spent on the final frame.
    bool deathFinished = false;   // True when the zombie can be removed.
};

// ==================================================
// DIFFICULTY SETTINGS  (Easy / Normal / Hard)
// ==================================================
// Every value here scales the wave system, so tuning a difficulty
// only means editing one row of the table below.

struct DifficultySettings
{
    const char* name;
    const char* description;
    float zombieCountMultiplier;     // Zombies per wave.
    float zombieSpeedMultiplier;     // Chase speed.
    float spawnIntervalMultiplier;   // >1 = slower spawning.
    float zombieDamage;              // Damage per zombie hit.
    float intermissionHeal;          // Health restored between waves.
    float maxAliveMultiplier;        // Scales how many zombies may be alive at once.
    sf::Color accent;                // UI colour for this difficulty.
};

// The old HARD values are now EASY; NORMAL and HARD are tougher.
const std::array<DifficultySettings, 3> DIFFICULTIES = {{
    { "EASY",   "MORE ZOMBIES  FASTER  HARDER HITS",
      1.35f, 1.15f, 0.80f, 15.f, 15.f, 1.00f, sf::Color(110, 210, 110) },
    { "NORMAL", "A REAL CHALLENGE  LESS HEALING",
      1.90f, 1.38f, 0.58f, 22.f,  8.f, 1.15f, sf::Color(255, 205,  95) },
    { "HARD",   "RELENTLESS HORDES  DEADLY HITS",
      2.30f, 1.50f, 0.45f, 30.f,  3.f, 1.30f, sf::Color(245,  75,  65) }
}};

// ==================================================
// WAVE CONFIGURATION
// ==================================================

namespace WaveConfig
{
    constexpr int   BASE_ZOMBIES            = 6;     // Zombies in wave 1 (before difficulty).
    constexpr int   ZOMBIES_PER_WAVE        = 3;     // Extra zombies each wave.
    constexpr int   MAX_ZOMBIES_PER_WAVE    = 120;

    constexpr float BASE_SPAWN_INTERVAL     = 2.0f;
    constexpr float MIN_SPAWN_INTERVAL      = 0.40f;
    constexpr float SPAWN_INTERVAL_DECAY    = 0.92f; // Per wave.
    constexpr float FIRST_SPAWN_DELAY       = 0.8f;  // Pause after a wave starts.

    constexpr std::size_t BASE_MAX_ALIVE    = 8;     // Zombies alive at once in wave 1.
    constexpr std::size_t MAX_ALIVE_PER_WAVE = 2;
    constexpr std::size_t MAX_ALIVE_CAP     = 40;

    constexpr float SPEED_BONUS_PER_WAVE    = 3.f;
    constexpr float MAX_SPEED_BONUS         = 30.f;

    constexpr float INTERMISSION_DURATION   = 5.f;   // Break between waves.
    constexpr float WAVE_BANNER_DURATION    = 2.f;

    // Spawn balancing.
    constexpr int   SPAWN_CANDIDATES        = 12;    // Edge points scored per spawn.
    constexpr float MIN_SPAWN_PLAYER_DIST   = 260.f; // Never spawn right on top of the player.
}

int waveZombieCount(int wave, const DifficultySettings& difficulty)
{
    const int raw = WaveConfig::BASE_ZOMBIES +
                    WaveConfig::ZOMBIES_PER_WAVE * (wave - 1);
    const int scaled = static_cast<int>(
        std::round(static_cast<float>(raw) * difficulty.zombieCountMultiplier)
    );
    return std::clamp(scaled, 3, WaveConfig::MAX_ZOMBIES_PER_WAVE);
}

float waveSpawnInterval(int wave, const DifficultySettings& difficulty)
{
    const float interval =
        WaveConfig::BASE_SPAWN_INTERVAL *
        std::pow(WaveConfig::SPAWN_INTERVAL_DECAY, static_cast<float>(wave - 1)) *
        difficulty.spawnIntervalMultiplier;

    return std::max(WaveConfig::MIN_SPAWN_INTERVAL, interval);
}

float waveSpeedBonus(int wave)
{
    return std::min(
        WaveConfig::MAX_SPEED_BONUS,
        WaveConfig::SPEED_BONUS_PER_WAVE * static_cast<float>(wave - 1)
    );
}

std::size_t waveMaxAlive(int wave)
{
    return std::min(
        WaveConfig::MAX_ALIVE_CAP,
        WaveConfig::BASE_MAX_ALIVE +
            static_cast<std::size_t>(wave - 1) * WaveConfig::MAX_ALIVE_PER_WAVE
    );
}

// ==================================================
// MENU LAYOUT
// ==================================================

constexpr float MENU_BUTTON_X = 250.f;
constexpr float MENU_BUTTON_W = 300.f;
constexpr float MENU_BUTTON_H = 56.f;
constexpr float MENU_BUTTON_Y0 = 240.f;
constexpr float MENU_BUTTON_STEP = 72.f;

// Returns the difficulty button under the point, or -1.
int menuButtonAt(const sf::Vector2f& point)
{
    for (int i = 0; i < 3; ++i)
    {
        const float top = MENU_BUTTON_Y0 + static_cast<float>(i) * MENU_BUTTON_STEP;

        if (point.x >= MENU_BUTTON_X && point.x <= MENU_BUTTON_X + MENU_BUTTON_W &&
            point.y >= top && point.y <= top + MENU_BUTTON_H)
            return i;
    }
    return -1;
}

constexpr float PI = 3.14159265358979323846f;

// Best score / best wave PER DIFFICULTY (saves/difficulty_bests.txt).
// The SaveManager file only stores one overall best, so this small companion
// file remembers which difficulty each record was set on.
struct DifficultyBests
{
    std::array<std::uint32_t, 3> score{};
    std::array<std::uint32_t, 3> wave{};
    bool loaded = false;
};

DifficultyBests loadDifficultyBests(const std::string& directory)
{
    DifficultyBests bests;
    std::ifstream in((std::filesystem::path(directory) / "difficulty_bests.txt").string());
    if (!in)
        return bests;

    std::array<std::uint64_t, 6> values{};
    for (std::uint64_t& value : values)
        if (!(in >> value) || value > 1000000000ull)
            return DifficultyBests{};   // Corrupted: ignore the file.

    for (std::size_t i = 0; i < 3; ++i)
    {
        bests.score[i] = static_cast<std::uint32_t>(values[i]);
        bests.wave[i] = static_cast<std::uint32_t>(values[3 + i]);
    }
    bests.loaded = true;
    return bests;
}

void saveDifficultyBests(const DifficultyBests& bests, const std::string& directory)
{
    try
    {
        std::filesystem::create_directories(directory);
        std::ofstream out((std::filesystem::path(directory) / "difficulty_bests.txt").string(),
                          std::ios::trunc);
        for (std::uint32_t value : bests.score) out << value << ' ';
        for (std::uint32_t value : bests.wave) out << value << ' ';
        out << '\n';
    }
    catch (const std::exception&)
    {
        // Not fatal: the overall records are saved by the SaveManager.
    }
}

// 75 -> "1:15", 3725 -> "1:02:05"   (Phase 7: statistics screens)
std::string formatDuration(std::uint64_t totalSeconds)
{
    const std::uint64_t hours = totalSeconds / 3600u;
    const std::uint64_t minutes = (totalSeconds / 60u) % 60u;
    const std::uint64_t seconds = totalSeconds % 60u;

    std::string text;
    if (hours > 0u)
    {
        text += std::to_string(hours) + ":";
        if (minutes < 10u) text += "0";
    }
    text += std::to_string(minutes) + ":";
    if (seconds < 10u) text += "0";
    text += std::to_string(seconds);
    return text;
}
constexpr float RAD_TO_DEG = 180.f / PI;

float vectorLength(const sf::Vector2f& value)
{
    return std::sqrt(value.x * value.x + value.y * value.y);
}

sf::Vector2f normalized(const sf::Vector2f& value)
{
    const float length = vectorLength(value);

    if (length <= 0.0001f)
        return {0.f, 0.f};

    return value / length;
}


sf::Vector2f rotateVector(const sf::Vector2f& value, float degrees)
{
    const float radians = degrees / RAD_TO_DEG;
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    return {value.x * c - value.y * s, value.x * s + value.y * c};
}

// ==================================================
// WEAPON SYSTEM  (Phase 2)
// ==================================================
// Every weapon is one row in the WEAPONS table below, so balancing a
// weapon only means editing that row. The runtime state of each weapon
// (magazine / reserve) lives in WeaponState inside main().
//
// AMMUNITION AND RELOAD RULES
//   * Each weapon has its own magazine and its own reserve.
//   * The trigger does nothing on an empty magazine (no automatic reload;
//     the HUD tells the player to press R).
//   * R reloads only the MISSING bullets: min(missing, reserve).
//   * R is ignored when the magazine is full or the reserve is empty.
//   * You cannot shoot while a magazine reload is in progress.
//   * Switching weapons CANCELS a reload (nothing is transferred).
//   * Shotgun: loads ONE shell at a time. Every shell is kept immediately,
//     and pulling the trigger interrupts the reload (if a shell is loaded).
//   * A reload also ends when the player dies or a new game starts.

enum class WeaponId : int
{
    Pistol = 0,
    Shotgun,
    AssaultRifle,
    SniperRifle,
    Count
};

constexpr int WEAPON_COUNT = static_cast<int>(WeaponId::Count);

enum class FireMode
{
    SemiAuto,   // One shot per click.
    FullAuto    // Keeps firing while the mouse button is held.
};

struct WeaponDef
{
    const char* name;
    const char* shortName;        // Used in the HUD if the icon is missing.
    const char* textureFile;      // Weapon sprite (looked up in assets/).

    // Ammunition.
    int magazineSize;
    int startReserve;
    int maxReserve;

    // Firing behaviour.
    FireMode mode;
    float fireInterval;           // Seconds between shots.
    int pellets;                  // Bullets per shot.
    float damage;                 // Damage per bullet (zombie HP is 3..8).
    float spreadDegrees;          // Random cone half-angle (smaller = more accurate).
    float bulletSpeed;
    float bulletLife;             // Seconds in the air = effective range.
    int pierce;                   // Extra zombies one bullet can pass through.
    float bulletRadius;
    float bulletTail;             // Tracer length in pixels (0 = none).
    sf::Color bulletColor;
    sf::Color bulletOutline;

    // Reloading and switching.
    float reloadTime;             // Whole magazine, or PER SHELL if shellReload.
    bool shellReload;
    float swapTime;               // Time to bring the weapon up.

    // Feel.
    float recoil;                 // Gun kick in pixels per shot.
    float flashScale;             // Muzzle flash size multiplier.

    // Visuals.
    bool useSheetGun;             // true = use the gun drawn in player_hands_gun_aligned.png.
    float muzzleYRatio;           // Barrel height inside the weapon PNG (0 = top, 1 = bottom).
};

//                                name            short  texture             mag res  max  mode                    rate   pel dmg   spread speed  life  prc  rad   tail
const std::array<WeaponDef, WEAPON_COUNT> WEAPONS = {{
    { "PISTOL",        "PST", "pistol.png",        12,  60, 120, FireMode::SemiAuto,  0.26f, 1, 1.5f,  1.2f,  850.f, 1.25f, 0, 3.5f,  0.f,
      sf::Color(255, 220, 125), sf::Color(255, 150,  55),
      1.10f, false, 0.28f,  5.f, 0.85f, false, 0.28f },

    { "SHOTGUN",       "SHG", "shotgun.png",        6,  30,  60, FireMode::SemiAuto,  0.85f, 6, 1.0f,  7.5f,  780.f, 0.42f, 0, 2.8f,  0.f,
      sf::Color(255, 205, 110), sf::Color(235, 125,  40),
      0.50f, true,  0.40f, 13.f, 1.60f, false, 0.30f },

    { "ASSAULT RIFLE", "AR",  "assault_rifle.png", 30, 120, 240, FireMode::FullAuto,  0.095f, 1, 1.25f, 2.2f,  900.f, 1.40f, 0, 3.0f,  0.f,
      sf::Color(255, 220, 125), sf::Color(255, 150,  55),
      1.90f, false, 0.35f,  4.f, 1.00f, true,  0.30f },

    { "SNIPER RIFLE",  "SNP", "sniper_rifle.png",   5,  25,  50, FireMode::SemiAuto,  1.35f, 1, 4.5f,  0.1f, 1500.f, 1.00f, 2, 3.0f, 26.f,
      sf::Color(205, 235, 255), sf::Color(110, 180, 255),
      2.60f, false, 0.50f, 16.f, 1.75f, false, 0.30f }
}};

namespace WeaponConfig
{
    constexpr int   STARTING_WEAPON      = static_cast<int>(WeaponId::Pistol);
    constexpr float FIRE_INPUT_BUFFER    = 0.12f;  // A click shortly before the gun is ready still fires.
    constexpr float RESTOCK_FRACTION     = 0.40f;  // Reserve ammo refilled after every wave (of maxReserve).
    constexpr bool  AUTO_RELOAD_ON_EMPTY = false;  // true = trigger pull on an empty magazine reloads.

    // Sprite alignment, in frame pixels (the 512 x 320 frame of the player sheets).
    // The back of every separate weapon sprite sits at GUN_BACK_X and its muzzle
    // lands at GUN_BACK_X + sprite width. For the 168 px assault rifle that is
    // 438 = the existing muzzleRight, so the layout stays consistent.
    constexpr float GUN_BACK_X           = 270.f;

    // When a separate weapon sprite replaces the rifle that is painted into
    // player_hands_gun_aligned.png, the sheet is clipped so only the hands/arm
    // area is drawn (frame x < CUT_RIGHT when facing right, mirrored when left).
    constexpr float SHEET_CUT_RIGHT      = 300.f;
}

// ==================================================
// PICKUP SYSTEM  (Phase 3)
// ==================================================
// Zombies drop items, the arena occasionally spawns supplies, and the
// player collects everything by walking over it. Every item type is one
// row of the PICKUPS table, and every tunable number lives in PickupConfig.
//
// COLLECTION RULES
//   * Health / ammo that would be wasted (full HP, full reserve) is NOT
//     consumed: it stays on the ground and shows "HEALTH FULL" / "AMMO FULL".
//   * Health never exceeds the maximum, ammo never exceeds the weapon's
//     configured maxReserve.
//   * Power-ups are always collected. Picking up one that is already active
//     refreshes its timer (no stacking). Each power-up has its own timer, so
//     they can overlap freely.
//   * Everything is cleared when a new game starts.

enum class PickupType : int
{
    HealthSmall = 0,
    HealthLarge,
    HealthHeart,
    HealthSpray,
    AmmoPistol,
    AmmoShotgun,
    AmmoRifle,
    AmmoGeneric,
    PowerShield,
    PowerSpeed,
    PowerDamage,
    PowerRapidFire,
    CoinSmall,
    CoinLarge,
    WeaponShotgun,        // Weapons are found, not given: dropped by zombies.
    WeaponRifle,
    WeaponSniper,
    Count
};

constexpr int PICKUP_TYPE_COUNT = static_cast<int>(PickupType::Count);

enum class PickupCategory
{
    Health,
    Ammo,
    PowerUp,
    Coin,
    Weapon
};

enum class PowerUpKind : int
{
    Shield = 0,
    Speed,
    Damage,
    RapidFire,
    Count
};

constexpr int POWERUP_KIND_COUNT = static_cast<int>(PowerUpKind::Count);

struct PickupDef
{
    const char* name;           // Shown in notifications.
    const char* textureFile;    // 128 x 128 icon in assets/.
    PickupCategory category;
    int amount;                 // HP / bullets / % of reserve / seconds / score.
    sf::Color glow;             // Glow, ring and particle colour.
    float lifetime;             // Seconds on the ground before it disappears.
    float displaySize;          // On-screen size in pixels.
};

//                                   name                 texture                  category                  amount glow                          life  size
const std::array<PickupDef, PICKUP_TYPE_COUNT> PICKUPS = {{
    { "SMALL HEALTH PACK", "health_pack.png",         PickupCategory::Health,  25, sf::Color( 90, 230, 110), 18.f, 36.f },
    { "LARGE HEALTH PACK", "large_health_pack.png",   PickupCategory::Health,  50, sf::Color( 90, 230, 110), 18.f, 44.f },
    { "HEALTH HEART",      "health_heart.png",        PickupCategory::Health,  15, sf::Color(255,  95, 115), 18.f, 34.f },
    { "HEALTH SPRAY",      "health_spray.png",        PickupCategory::Health,  35, sf::Color(120, 230, 205), 18.f, 36.f },

    { "PISTOL AMMO",       "ammo_pistol_rifle.png",   PickupCategory::Ammo,    24, sf::Color(255, 200,  90), 20.f, 36.f },
    { "SHOTGUN SHELLS",    "ammo_shotgun_shells.png", PickupCategory::Ammo,     6, sf::Color(255, 140,  80), 20.f, 36.f },
    { "RIFLE AMMO",        "ammo_rifle_crate.png",    PickupCategory::Ammo,    60, sf::Color(255, 200,  90), 20.f, 42.f },
    { "ALL AMMO",          "ammo_generic.png",        PickupCategory::Ammo,    15, sf::Color(255, 225, 120), 20.f, 38.f },

    { "SHIELD",            "powerup_shield.png",      PickupCategory::PowerUp, 10, sf::Color( 90, 200, 255), 16.f, 40.f },
    { "SPEED BOOST",       "powerup_speed.png",       PickupCategory::PowerUp,  8, sf::Color(120, 255, 190), 16.f, 40.f },
    { "DAMAGE BOOST",      "powerup_damage.png",      PickupCategory::PowerUp, 10, sf::Color(255,  95,  70), 16.f, 40.f },
    { "RAPID FIRE",        "powerup_rapid_fire.png",  PickupCategory::PowerUp,  8, sf::Color(255, 225,  80), 16.f, 40.f },

    { "COIN",              "coin.png",                PickupCategory::Coin,    25, sf::Color(255, 210,  70), 14.f, 28.f },
    { "LARGE COIN",        "coin.png",                PickupCategory::Coin,   100, sf::Color(255, 160,  40), 14.f, 40.f },

    // Weapon drops (the icon is the weapon's own sprite).
    { "SHOTGUN",           "shotgun.png",             PickupCategory::Weapon,   0, sf::Color(255, 170,  80), 40.f, 62.f },
    { "ASSAULT RIFLE",     "assault_rifle.png",       PickupCategory::Weapon,   0, sf::Color(255, 225, 100), 40.f, 66.f },
    { "SNIPER RIFLE",      "sniper_rifle.png",        PickupCategory::Weapon,   0, sf::Color(150, 205, 255), 40.f, 70.f }
}};

struct Pickup
{
    PickupType type = PickupType::HealthSmall;
    sf::Vector2f position;          // Point on the ground.
    float age = 0.f;
    float lifetime = 15.f;
    float bobPhase = 0.f;
    bool collecting = false;        // Playing the collection animation.
    float collectTimer = 0.f;
    float noticeCooldown = 0.f;     // Stops "FULL" messages from spamming.
};

struct FloatingText
{
    std::string text;
    sf::Vector2f position;
    float life = 1.2f;
    float maxLife = 1.2f;
    sf::Color color = sf::Color::White;
    float scale = 2.f;
};

namespace PickupConfig
{
    constexpr std::size_t MAX_ACTIVE_PICKUPS = 10;
    constexpr float PLAYER_MAX_HEALTH = 100.f;

    constexpr float COLLECT_RADIUS     = 38.f;   // Distance from the player's feet.
    constexpr float LIFETIME_SCALE     = 1.6f;   // The world is bigger: items last longer.
    constexpr float COLLECT_ANIM_TIME  = 0.28f;

    // Zombie drop table (percent per kill, must add up to 100).
    constexpr float DROP_NONE    = 65.f;
    constexpr float DROP_HEALTH  = 15.f;
    constexpr float DROP_AMMO    = 12.f;
    constexpr float DROP_POWERUP =  6.f;
    constexpr float DROP_COIN    =  2.f;

    // Random supply drops that appear in the arena by themselves.
    constexpr float SUPPLY_INTERVAL_MIN = 20.f;
    constexpr float SUPPLY_INTERVAL_MAX = 32.f;

    // Power-up strength.
    constexpr float SHIELD_DAMAGE_MULTIPLIER = 0.5f;   // 50% less incoming damage.
    constexpr float SPEED_MULTIPLIER         = 1.5f;
    constexpr float DAMAGE_MULTIPLIER        = 2.0f;
    constexpr float RAPID_FIRE_MULTIPLIER    = 1.5f;   // Shots per second.

    // Weapon progression: the game starts with the pistol only. Each other
    // weapon drops from zombies once its wave is reached (in this order).
    constexpr std::array<int, 4> WEAPON_MIN_WAVE = {0, 2, 4, 6};   // Pistol, Shotgun, Rifle, Sniper.
    constexpr float WEAPON_DROP_CHANCE = 0.07f;     // Per kill once the weapon is eligible.
    constexpr int   WEAPON_DROP_PITY_KILLS = 12;    // Guaranteed after this many eligible kills.

    // F2 spawns every pickup type in turn next to the player (for testing).
    constexpr bool  ENABLE_DEBUG_KEY = false;   // Cheat key: set true only while testing.
}

// ==================================================
// WORLD / MAP SYSTEM  (Phase 4)
// ==================================================
// The arena is now a 2560 x 1920 world made of 32 x 32 tiles (80 x 60).
// Walls, buildings, fences and props are "obstacles" with a collision
// rectangle. The player, zombies, bullets, explosions and pickups all use
// the same obstacle list, so everything interacts with the map correctly.
//
// COLLISION RULES
//   * Player and zombies are circles standing on the ground (their feet).
//     They are pushed out of every solid rectangle, which makes them slide
//     along walls instead of sticking to them.
//   * Bullets are tested on the ground plane: the screen position of a bullet
//     is projected down by BULLET_PLANE_OFFSET (the gun is held above the
//     feet). Walls and fences use their footprint; small props use their
//     visible shape, so "aim at it" means "hit it".
//   * Chain-link fences block movement but NOT bullets.
//   * Crates take damage and break, explosive barrels blow up, walls,
//     cars, barricades and sandbags are indestructible.
//   * Zombies follow a shared navigation field (see "Navigation") so they
//     walk around buildings through doors instead of pushing into walls.

namespace WorldConfig
{
    constexpr int   TILE  = 32;
    constexpr int   COLS  = 80;
    constexpr int   ROWS  = 60;
    constexpr float WORLD_W = static_cast<float>(COLS * TILE);   // 2560
    constexpr float WORLD_H = static_cast<float>(ROWS * TILE);   // 1920

    constexpr float VIEW_W = 800.f;
    constexpr float VIEW_H = 600.f;

    constexpr float PLAYER_SPAWN_X = 1280.f;   // Centre of the main crossroads.
    constexpr float PLAYER_SPAWN_Y = 960.f;

    constexpr float PLAYER_RADIUS = 14.f;      // Collision circle at the feet.
    constexpr float ZOMBIE_RADIUS = 13.f;
    constexpr float NAV_MARGIN    = 13.f;      // Keeps zombies off wall edges.

    constexpr float CAMERA_SMOOTHING   = 7.f;  // Higher = tighter follow.
    constexpr float CAMERA_HEAD_OFFSET = 34.f; // Camera looks at the torso.

    constexpr float BULLET_PLANE_OFFSET = 28.f;  // Gun height above the feet.

    constexpr float MIN_ZOMBIE_SPAWN_DISTANCE = 560.f;  // Always off-screen.

    // Destructible props.
    constexpr float CRATE_SMALL_HP   = 3.f;
    constexpr float CRATE_LARGE_HP   = 6.f;
    constexpr float CRATE_DROP_CHANCE = 0.25f;   // Chance a broken crate drops loot.

    // Explosive barrels.
    constexpr float EXPLOSION_RADIUS             = 130.f;
    constexpr float EXPLOSION_ZOMBIE_MAX_DAMAGE  = 10.f;   // At the centre.
    constexpr float EXPLOSION_ZOMBIE_MIN_DAMAGE  = 3.f;    // At the edge.
    constexpr float EXPLOSION_PLAYER_MAX_DAMAGE  = 28.f;   // Barrels hurt you too.
    constexpr float EXPLOSION_PROP_DAMAGE        = 4.f;    // Crates nearby.
    constexpr float EXPLOSION_CHAIN_DELAY        = 0.15f;  // Barrel chain reactions.
    constexpr float EXPLOSION_SHAKE              = 11.f;   // Camera shake strength.
}

// Ground layout of Map 1 - Abandoned City (same grid as
// maps/abandoned_city_ground.csv).  g = grass, s = sidewalk, a = asphalt,
// c = concrete, i = interior floor, d = dirt.
constexpr const char* GROUND_MAP[WorldConfig::ROWS] = {
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccccccccccccccccccgggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccccccccccccccccccgggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccccccccccccccccccgggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccciiiiiiiiiiiiiccgggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccciiiiiiiiiiiiiccgggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccciiiiiiiiiiiiiccgggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccciiiiiiiiiiiiiccgggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccciiiiiiiiiiiiiccgggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccciiiiiiiiiiiiiccgggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccciiiiiiiiiiiiiccgggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccciiiiiiiiiiiiiccgggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccciiiiiiiiiiiiiccgggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccccccccccccccccccgggggg",
    "gggggcccccccccccccccccggggggggggssaaaaaaaaaaaassggggggggccccccccccccccccccgggggg",
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss",
    "ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaassaaaaaaaaaaaassaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaassaaaaaaaaaaaassaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaassaaaaaaaaaaaassaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaassaaaaaaaaaaaassaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaassaaaaaaaaaaaassaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaassaaaaaaaaaaaassaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaassaaaaaaaaaaaassaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaassaaaaaaaaaaaassaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaassaaaaaaaaaaaassaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaassaaaaaaaaaaaassaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss",
    "ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss",
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggaaaaaaasaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggaaaaaaasaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggaaaaaaasaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "gggggcccccccccccccccccggggaaaaaaasaaaaaaaaaaaassgggggggggcccccccccccccccccgggggg",
    "gggggcccddddddddddddccggggaaaaaaasaaaaaaaaaaaassgggggggggcccccccccccccccccgggggg",
    "gggggcccddddddddddddccggggaaaaaaasaaaaaaaaaaaassgggggggggcccccccccccccccccgggggg",
    "gggggcccddddddddddddccggggaaaaaaasaaaaaaaaaaaassgggggggggcccccccccccccccccgggggg",
    "gggggcccddddddddddddccggggaaaaaaasaaaaaaaaaaaassgggggggggcccccccccccccccccgggggg",
    "gggggcccddddddddddddccggggaaaaaaasaaaaaaaaaaaassgggggggggcccccccccccccccccgggggg",
    "gggggcccddddddddddddccggggaaaaaaasaaaaaaaaaaaassgggggggggcccccccccccccccccgggggg",
    "gggggcccddddddddddddccggggaaaaaaasaaaaaaaaaaaassgggggggggcccccccccccccccccgggggg",
    "gggggcccddddddddddddccggggaaaaaaasaaaaaaaaaaaassgggggggggcccccccccccccccccgggggg",
    "gggggcccddddddddddddccggggaaaaaaasaaaaaaaaaaaassgggggggggcccccccccccccccccgggggg",
    "gggggcccddddddddddddccggggaaaaaaasaaaaaaaaaaaassgggggggggcccccccccccccccccgggggg",
    "gggggcccccccccccccccccggggaaaaaaasaaaaaaaaaaaassgggggggggcccccccccccccccccgggggg",
    "gggggcccccccccccccccccggggaaaaaaasaaaaaaaaaaaassgggggggggcccccccccccccccccgggggg",
    "ggggggggggggggggggggggggggaaaaaaasaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggaaaaaaasaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg",
    "ggggggggggggggggggggggggggggggggssaaaaaaaaaaaassgggggggggggggggggggggggggggggggg"
};

enum class ObstacleKind
{
    Wall,
    Fence,
    CrateSmall,
    CrateLarge,
    BarrelExplosive,
    BarrelSolid,
    Vehicle,
    Barricade,
    Sandbags,
    UtilityBox
};

struct Obstacle
{
    ObstacleKind kind = ObstacleKind::Wall;

    sf::FloatRect moveRect;        // Footprint that blocks walking.
    sf::FloatRect bulletRect;      // Shape that stops bullets (ground plane).
    bool blocksMovement = true;
    bool blocksBullets = true;

    float hp = -1.f;               // < 0 means indestructible.
    float maxHp = -1.f;
    bool explosive = false;
    bool alive = true;
    float fuse = -1.f;             // >= 0: counting down to an explosion.
    float hitFlash = 0.f;

    // Drawing (only for props that are drawn one by one; walls are batched).
    const sf::Texture* texture = nullptr;
    const sf::Texture* damagedTexture = nullptr;
    sf::Vector2f spriteCenter;
    sf::Vector2f spriteSize;
    float rotation = 0.f;
};

// One big vertex array per texture: thousands of tiles in a few draw calls.
struct TileBatch
{
    const sf::Texture* texture = nullptr;
    sf::Color fallback = sf::Color(80, 80, 80);   // Used if the PNG is missing.
    sf::VertexArray vertices = sf::VertexArray(sf::PrimitiveType::Triangles);
};

struct DynamicDecal
{
    const sf::Texture* texture = nullptr;
    sf::Vector2f position;
    sf::Vector2f size;
    float rotation = 0.f;
    float alpha = 255.f;
    float scorchRadius = 0.f;      // > 0 draws a dark scorch mark instead.
    sf::Color tint = sf::Color(12, 10, 8);   // Colour of the scorch / stain.
    bool blood = false;            // Blood stain left by a dead zombie.
};

struct Decoration
{
    const sf::Texture* texture = nullptr;
    sf::Vector2f position;         // Centre.
    sf::Vector2f size;
    float rotation = 0.f;
    bool overhead = false;         // Drawn above characters (lamps, signs).
};

struct EffectSprite
{
    const sf::Texture* texture = nullptr;
    sf::Vector2f position;
    float age = 0.f;
    float life = 0.5f;
    float startSize = 32.f;
    float endSize = 48.f;
    float startAlpha = 255.f;
    float rotation = 0.f;
    float rise = 0.f;              // Pixels per second (negative = up).
};

struct GapRange
{
    int from;
    int to;
};

// --------------------------------------------------
// Geometry helpers
// --------------------------------------------------
inline bool circleOverlapsRect(const sf::Vector2f& center, float radius,
                               const sf::FloatRect& rect)
{
    const float nearestX = std::clamp(center.x, rect.position.x, rect.position.x + rect.size.x);
    const float nearestY = std::clamp(center.y, rect.position.y, rect.position.y + rect.size.y);
    const float dx = center.x - nearestX;
    const float dy = center.y - nearestY;

    return dx * dx + dy * dy < radius * radius;
}

// Moves the circle out of the rectangle (along the shortest way). Returns true if it moved.
inline bool pushCircleOutOfRect(sf::Vector2f& center, float radius,
                                const sf::FloatRect& rect)
{
    const float left = rect.position.x;
    const float top = rect.position.y;
    const float right = rect.position.x + rect.size.x;
    const float bottom = rect.position.y + rect.size.y;

    const float nearestX = std::clamp(center.x, left, right);
    const float nearestY = std::clamp(center.y, top, bottom);
    const float dx = center.x - nearestX;
    const float dy = center.y - nearestY;
    const float distanceSquared = dx * dx + dy * dy;

    if (distanceSquared >= radius * radius)
        return false;

    if (distanceSquared > 1.0e-6f)
    {
        const float distance = std::sqrt(distanceSquared);
        const float push = (radius - distance) / distance;
        center.x += dx * push;
        center.y += dy * push;
        return true;
    }

    // The centre is inside the rectangle: leave through the nearest side.
    const float toLeft = center.x - left;
    const float toRight = right - center.x;
    const float toTop = center.y - top;
    const float toBottom = bottom - center.y;
    const float smallest = std::min({toLeft, toRight, toTop, toBottom});

    if (smallest == toLeft)
        center.x = left - radius;
    else if (smallest == toRight)
        center.x = right + radius;
    else if (smallest == toTop)
        center.y = top - radius;
    else
        center.y = bottom + radius;

    return true;
}

// Segment versus rectangle (Liang-Barsky). tHit = 0..1 along a -> b.
inline bool segmentIntersectsRect(const sf::Vector2f& a, const sf::Vector2f& b,
                                  const sf::FloatRect& rect, float& tHit)
{
    const sf::Vector2f d = b - a;
    float t0 = 0.f;
    float t1 = 1.f;

    const float p[4] = {-d.x, d.x, -d.y, d.y};
    const float q[4] = {
        a.x - rect.position.x,
        rect.position.x + rect.size.x - a.x,
        a.y - rect.position.y,
        rect.position.y + rect.size.y - a.y
    };

    for (int i = 0; i < 4; ++i)
    {
        if (std::abs(p[i]) < 1.0e-8f)
        {
            if (q[i] < 0.f)
                return false;
        }
        else
        {
            const float t = q[i] / p[i];

            if (p[i] < 0.f)
            {
                if (t > t1)
                    return false;
                t0 = std::max(t0, t);
            }
            else
            {
                if (t < t0)
                    return false;
                t1 = std::min(t1, t);
            }
        }
    }

    tHit = t0;
    return true;
}

inline sf::FloatRect inflatedRect(const sf::FloatRect& rect, float amount)
{
    return sf::FloatRect(
        {rect.position.x - amount, rect.position.y - amount},
        {rect.size.x + amount * 2.f, rect.size.y + amount * 2.f}
    );
}

inline unsigned int tileHash(int column, int row, unsigned int salt)
{
    unsigned int h = static_cast<unsigned int>(column) * 73856093u ^
                     static_cast<unsigned int>(row) * 19349663u ^
                     salt * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

// --------------------------------------------------
// Tiny built-in 5x7 pixel font.
// This avoids needing an extra .ttf font file.
// --------------------------------------------------
std::array<const char*, 7> pixelGlyph(char character)
{
    switch (character)
    {
        case 'A': return {"01110","10001","10001","11111","10001","10001","10001"};
        case 'B': return {"11110","10001","10001","11110","10001","10001","11110"};
        case 'C': return {"01111","10000","10000","10000","10000","10000","01111"};
        case 'D': return {"11110","10001","10001","10001","10001","10001","11110"};
        case 'F': return {"11111","10000","10000","11110","10000","10000","10000"};
        case 'H': return {"10001","10001","10001","11111","10001","10001","10001"};
        case 'I': return {"01110","00100","00100","00100","00100","00100","01110"};
        case 'K': return {"10001","10010","10100","11000","10100","10010","10001"};
        case 'L': return {"10000","10000","10000","10000","10000","10000","11111"};
        case 'N': return {"10001","11001","11001","10101","10011","10011","10001"};
        case 'U': return {"10001","10001","10001","10001","10001","10001","01110"};
        case 'V': return {"10001","10001","10001","10001","10001","01010","00100"};
        case 'W': return {"10001","10001","10001","10101","10101","11011","10001"};
        case 'X': return {"10001","10001","01010","00100","01010","10001","10001"};
        case 'Y': return {"10001","10001","01010","00100","00100","00100","00100"};
        case 'Z': return {"11111","00001","00010","00100","01000","10000","11111"};
        case 'E': return {"11111","10000","10000","11110","10000","10000","11111"};
        case 'G': return {"01111","10000","10000","10111","10001","10001","01111"};
        case 'M': return {"10001","11011","10101","10101","10001","10001","10001"};
        case 'O': return {"01110","10001","10001","10001","10001","10001","01110"};
        case 'P': return {"11110","10001","10001","11110","10000","10000","10000"};
        case 'R': return {"11110","10001","10001","11110","10100","10010","10001"};
        case 'S': return {"01111","10000","10000","01110","00001","00001","11110"};
        case 'T': return {"11111","00100","00100","00100","00100","00100","00100"};
        case 'J': return {"00111","00010","00010","00010","00010","10010","01100"};
        case 'Q': return {"01110","10001","10001","10001","10101","10010","01101"};
        case '?': return {"01110","10001","00001","00010","00100","00000","00100"};
        case '!': return {"00100","00100","00100","00100","00100","00000","00100"};
        case ',': return {"00000","00000","00000","00000","01100","00100","01000"};
        case ' ': return {"00000","00000","00000","00000","00000","00000","00000"};
        case ':': return {"00000","00100","00100","00000","00100","00100","00000"};
        case '.': return {"00000","00000","00000","00000","00000","01100","01100"};
        case '/': return {"00001","00010","00010","00100","01000","01000","10000"};
        case '+': return {"00000","00100","00100","11111","00100","00100","00000"};
        case '%': return {"11001","11010","00010","00100","01000","01011","10011"};
        case '-': return {"00000","00000","00000","11111","00000","00000","00000"};
        case '0': return {"01110","10001","10011","10101","11001","10001","01110"};
        case '1': return {"00100","01100","00100","00100","00100","00100","01110"};
        case '2': return {"01110","10001","00001","00010","00100","01000","11111"};
        case '3': return {"11110","00001","00001","01110","00001","00001","11110"};
        case '4': return {"00010","00110","01010","10010","11111","00010","00010"};
        case '5': return {"11111","10000","10000","11110","00001","00001","11110"};
        case '6': return {"01110","10000","10000","11110","10001","10001","01110"};
        case '7': return {"11111","00001","00010","00100","01000","01000","01000"};
        case '8': return {"01110","10001","10001","01110","10001","10001","01110"};
        case '9': return {"01110","10001","10001","01111","00001","00001","01110"};
        default:  return {"00000","00000","00000","00000","00000","00000","00000"};
    }
}

void drawPixelText(
    sf::RenderWindow& window,
    const std::string& text,
    float x,
    float y,
    float pixelSize,
    sf::Color color)
{
    sf::RectangleShape pixel({pixelSize, pixelSize});
    pixel.setFillColor(color);

    float cursorX = x;

    for (char character : text)
    {
        const auto glyph = pixelGlyph(character);

        for (int row = 0; row < 7; ++row)
        {
            for (int column = 0; column < 5; ++column)
            {
                if (glyph[row][column] == '1')
                {
                    pixel.setPosition({
                        cursorX + column * pixelSize,
                        y + row * pixelSize
                    });
                    window.draw(pixel);
                }
            }
        }

        cursorX += 6.f * pixelSize;
    }
}

float pixelTextWidth(const std::string& text, float pixelSize)
{
    if (text.empty())
        return 0.f;

    return (text.size() * 6.f - 1.f) * pixelSize;
}

// Text with a soft drop shadow so it stays readable on any background.
void drawShadowText(
    sf::RenderWindow& window,
    const std::string& text,
    float x,
    float y,
    float pixelSize,
    sf::Color color)
{
    const float offset = std::max(1.f, pixelSize * 0.5f);

    drawPixelText(
        window, text, x + offset, y + offset, pixelSize,
        sf::Color(0, 0, 0, static_cast<std::uint8_t>(color.a * 0.65f))
    );
    drawPixelText(window, text, x, y, pixelSize, color);
}

// Horizontally centred text (with shadow) around centerX.
void drawCenteredText(
    sf::RenderWindow& window,
    const std::string& text,
    float centerX,
    float y,
    float pixelSize,
    sf::Color color)
{
    drawShadowText(
        window, text,
        centerX - pixelTextWidth(text, pixelSize) / 2.f,
        y, pixelSize, color
    );
}

// ==================================================
// AUDIO SYSTEM  (Phase 5)
// ==================================================
// One AudioManager owns every sound and every music track.
//
//   * Sound effects: sf::SoundBuffer + a small pool of sf::Sound "voices" per
//     effect, created ONCE at start-up (nothing is created per frame).
//   * Music: four sf::Music tracks with smooth cross-fading.
//   * Settings: master / music / sfx volume + mute, saved in config/audio.ini.
//   * Missing files never crash the game: the effect is synthesised instead
//     (and a note is printed), so the game is always audible.
//
// Files are searched in (first match wins):
//   assets/audio/sfx/<name>   assets/audio/<name>   assets/sfx/<name>   assets/<name>
//   assets/audio/music/<name> assets/audio/<name>   assets/music/<name> assets/<name>

enum class SoundId : int
{
    PistolShot = 0,
    ShotgunShot,
    RifleShot,
    SniperShot,
    BulletImpact,
    ZombieGrowl,
    ZombieAttack,
    ZombieDeath,
    PlayerHurt,
    PlayerDeath,
    Reload,
    ShellLoad,
    EmptyClick,
    Pickup,
    PowerUp,
    WaveStart,
    Explosion,
    MenuClick,
    MenuHover,
    Count
};

enum class MusicId : int
{
    Menu = 0,
    Gameplay,
    Intense,
    GameOver,
    Count,
    None
};

constexpr int SOUND_COUNT = static_cast<int>(SoundId::Count);
constexpr int MUSIC_COUNT = static_cast<int>(MusicId::Count);

struct AudioSettings
{
    int master = 80;     // 0 - 100
    int music  = 60;
    int sfx    = 85;
    bool muted = false;
};

class AudioManager
{
public:
    AudioManager() = default;
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // ---------------- loading ----------------
    void load()
    {
        std::mt19937 synthRandom(1234u);

        for (int i = 0; i < SOUND_COUNT; ++i)
        {
            const SfxDef& def = SFX[i];
            bool ok = false;

            for (const std::string& path : candidates(def.file, false))
            {
                if (std::filesystem::exists(path) && buffers_[i].loadFromFile(path))
                {
                    ok = true;
                    break;
                }
            }

            if (!ok)
            {
                std::cerr << "Audio note: " << def.file
                          << " not found - using a built-in sound instead.\n";
                synthesize(static_cast<SoundId>(i), synthRandom);
            }

            voices_[i].reserve(static_cast<std::size_t>(def.voices));
            for (int v = 0; v < def.voices; ++v)
                voices_[i].push_back(std::make_unique<sf::Sound>(buffers_[i]));
        }

        for (int m = 0; m < MUSIC_COUNT; ++m)
        {
            auto track = std::make_unique<sf::Music>();
            bool ok = false;

            for (const std::string& path : candidates(MUSIC_FILES[m], true))
            {
                if (std::filesystem::exists(path) && track->openFromFile(path))
                {
                    ok = true;
                    break;
                }
            }

            if (ok)
            {
                track->setLooping(true);
                music_[m] = std::move(track);
            }
            else
            {
                std::cerr << "Audio note: music " << MUSIC_FILES[m] << " not found.\n";
            }
        }

        applyVolumes();
    }

    // ---------------- settings ----------------
    static constexpr const char* SETTINGS_FILE = "config/audio.ini";

    void loadSettings()
    {
        std::ifstream in(SETTINGS_FILE);
        if (!in)
        {
            applyVolumes();
            return;
        }

        std::string line;
        while (std::getline(in, line))
        {
            const auto hash = line.find('#');
            if (hash != std::string::npos)
                line.erase(hash);

            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            auto trim = [](std::string s)
            {
                const char* ws = " \t\r\n";
                s.erase(0, s.find_first_not_of(ws) == std::string::npos ? s.size() : s.find_first_not_of(ws));
                const auto last = s.find_last_not_of(ws);
                s.erase(last == std::string::npos ? 0 : last + 1);
                return s;
            };

            const std::string key = trim(line.substr(0, eq));
            const std::string value = trim(line.substr(eq + 1));
            if (value.empty())
                continue;

            int number = 0;
            try { number = std::stoi(value); }
            catch (...) { continue; }

            if (key == "master")      settings_.master = std::clamp(number, 0, 100);
            else if (key == "music")  settings_.music  = std::clamp(number, 0, 100);
            else if (key == "sfx")    settings_.sfx    = std::clamp(number, 0, 100);
            else if (key == "muted")  settings_.muted  = (number != 0);
        }

        applyVolumes();
    }

    void saveSettings() const
    {
        std::error_code ignored;
        std::filesystem::create_directories("config", ignored);

        std::ofstream out(SETTINGS_FILE);
        if (!out)
        {
            std::cerr << "Could not write " << SETTINGS_FILE << "\n";
            return;
        }

        out << "# Zombie Survival Arena - audio settings\n"
            << "master=" << settings_.master << "\n"
            << "music="  << settings_.music  << "\n"
            << "sfx="    << settings_.sfx    << "\n"
            << "muted="  << (settings_.muted ? 1 : 0) << "\n";
    }

    const AudioSettings& settings() const { return settings_; }

    void setMaster(int v) { settings_.master = std::clamp(v, 0, 100); applyVolumes(); }
    void setMusicVolume(int v) { settings_.music = std::clamp(v, 0, 100); applyVolumes(); }
    void setSfxVolume(int v) { settings_.sfx = std::clamp(v, 0, 100); applyVolumes(); }
    void setMuted(bool m) { settings_.muted = m; applyVolumes(); }
    void toggleMute() { setMuted(!settings_.muted); }

    // ---------------- sound effects ----------------
    // volumeScale: 0..1 extra scaling. pitchJitter: random +/- pitch change.
    void play(SoundId id, float volumeScale = 1.f)
    {
        const int index = static_cast<int>(id);
        const SfxDef& def = SFX[index];

        if (sfxMasterVolume() <= 0.f)
            return;

        // Anti-spam: the same effect may not retrigger faster than minInterval.
        if (sinceLast_[index] < def.minInterval)
            return;

        sf::Sound* chosen = nullptr;
        for (auto& voice : voices_[index])
        {
            if (voice->getStatus() != sf::SoundSource::Status::Playing)
            {
                chosen = voice.get();
                break;
            }
        }

        // All voices busy: reuse the one that started first (round robin).
        if (!chosen)
        {
            chosen = voices_[index][static_cast<std::size_t>(nextSteal_[index])].get();
            nextSteal_[index] = (nextSteal_[index] + 1) % static_cast<int>(voices_[index].size());
        }

        const float jitter = def.pitchJitter > 0.f
            ? 1.f + (jitterDist_(rng_) * def.pitchJitter)
            : 1.f;

        chosen->setPitch(jitter);
        chosen->setVolume(std::clamp(sfxMasterVolume() * def.baseVolume * volumeScale, 0.f, 100.f));
        chosen->play();
        sinceLast_[index] = 0.f;
    }

    void playWeaponShot(int weaponIndex)
    {
        switch (weaponIndex)
        {
            case 0:  play(SoundId::PistolShot); break;
            case 1:  play(SoundId::ShotgunShot); break;
            case 2:  play(SoundId::RifleShot); break;
            default: play(SoundId::SniperShot); break;
        }
    }

    void stopSound(SoundId id)
    {
        for (auto& voice : voices_[static_cast<int>(id)])
            voice->stop();
    }

    void stopAllSfx()
    {
        for (auto& pool : voices_)
            for (auto& voice : pool)
                voice->stop();
    }

    // Pause menu: freeze / resume every sound that is currently playing.
    void pauseAllSfx()
    {
        for (auto& pool : voices_)
            for (auto& voice : pool)
                if (voice->getStatus() == sf::SoundSource::Status::Playing)
                    voice->pause();
    }

    void resumeAllSfx()
    {
        for (auto& pool : voices_)
            for (auto& voice : pool)
                if (voice->getStatus() == sf::SoundSource::Status::Paused)
                    voice->play();
    }

    // Random zombie groans while zombies are alive. Call once per frame
    // with the (frozen-while-paused) delta time.
    void updateZombieAmbient(float dt, std::size_t livingZombies)
    {
        if (livingZombies == 0 || dt <= 0.f)
        {
            return;
        }

        growlTimer_ -= dt;
        if (growlTimer_ > 0.f)
            return;

        // More zombies = more frequent groans.
        const float crowd = std::min(1.f, static_cast<float>(livingZombies) / 20.f);
        const float base = 7.5f - 4.5f * crowd;
        growlTimer_ = base * (0.6f + 0.8f * (jitterDist_(rng_) * 0.5f + 0.5f));

        play(SoundId::ZombieGrowl, 0.55f + 0.35f * crowd);
    }

    // ---------------- music ----------------
    void playMusic(MusicId id)
    {
        if (id == currentMusic_)
            return;

        currentMusic_ = id;

        for (int m = 0; m < MUSIC_COUNT; ++m)
            musicTarget_[m] = (m == static_cast<int>(id)) ? 1.f : 0.f;

        if (id != MusicId::None)
        {
            const int index = static_cast<int>(id);
            if (music_[index] && music_[index]->getStatus() != sf::SoundSource::Status::Playing)
            {
                music_[index]->setVolume(0.f);
                musicLevel_[index] = 0.f;
                music_[index]->play();
            }
        }
    }

    MusicId currentMusic() const { return currentMusic_; }

    // 1 = normal, <1 = quieter (used behind the pause menu).
    void setMusicDuck(float duck) { duckTarget_ = std::clamp(duck, 0.f, 1.f); }

    // Must be called every frame (cross-fades, overlap timers).
    void update(float dt)
    {
        for (float& t : sinceLast_)
            t += dt;

        duck_ += (duckTarget_ - duck_) * (1.f - std::exp(-8.f * dt));

        for (int m = 0; m < MUSIC_COUNT; ++m)
        {
            if (!music_[m])
                continue;

            const float step = dt / MUSIC_FADE_SECONDS;
            if (musicLevel_[m] < musicTarget_[m])
                musicLevel_[m] = std::min(musicTarget_[m], musicLevel_[m] + step);
            else if (musicLevel_[m] > musicTarget_[m])
                musicLevel_[m] = std::max(musicTarget_[m], musicLevel_[m] - step);

            if (musicLevel_[m] <= 0.f && musicTarget_[m] <= 0.f &&
                music_[m]->getStatus() == sf::SoundSource::Status::Playing)
            {
                music_[m]->stop();
            }

            music_[m]->setVolume(musicLevel_[m] * musicMasterVolume() * duck_);
        }
    }

private:
    struct SfxDef
    {
        const char* file;
        float baseVolume;     // 0..1 relative loudness of this effect.
        int voices;           // How many copies may overlap.
        float minInterval;    // Seconds before it may retrigger.
        float pitchJitter;    // Random pitch variation (0 = none).
    };

    // Tweak loudness / overlap per sound here.
    static constexpr SfxDef SFX[SOUND_COUNT] = {
        {"pistol_shot.wav",   0.80f, 3, 0.04f, 0.03f},
        {"shotgun_shot.wav",  0.95f, 3, 0.04f, 0.03f},
        {"rifle_shot.wav",    0.70f, 5, 0.03f, 0.04f},
        {"sniper_shot.wav",   1.00f, 2, 0.04f, 0.02f},
        {"bullet_impact.wav", 0.45f, 4, 0.045f, 0.12f},
        {"zombie_growl.wav",  0.60f, 3, 0.60f, 0.12f},
        {"zombie_attack.wav", 0.75f, 3, 0.18f, 0.06f},
        {"zombie_death.wav",  0.80f, 4, 0.08f, 0.08f},
        {"player_hurt.wav",   0.95f, 2, 0.20f, 0.03f},
        {"player_death.wav",  1.00f, 1, 0.50f, 0.00f},
        {"reload.wav",        0.80f, 2, 0.10f, 0.00f},
        {"shell_load.wav",    0.60f, 3, 0.10f, 0.05f},
        {"empty_click.wav",   0.55f, 2, 0.15f, 0.03f},
        {"pickup.wav",        0.70f, 3, 0.05f, 0.04f},
        {"powerup.wav",       0.90f, 2, 0.20f, 0.00f},
        {"wave_start.wav",    0.90f, 1, 1.00f, 0.00f},
        {"explosion.wav",     1.00f, 3, 0.10f, 0.05f},
        {"menu_click.wav",    0.70f, 2, 0.04f, 0.00f},
        {"menu_hover.wav",    0.35f, 2, 0.05f, 0.00f}
    };

    static constexpr const char* MUSIC_FILES[MUSIC_COUNT] = {
        "menu_theme.ogg", "gameplay_theme.ogg", "intense_wave.ogg", "game_over.ogg"
    };

    static constexpr float MUSIC_FADE_SECONDS = 1.2f;

    static std::vector<std::string> candidates(const std::string& file, bool isMusic)
    {
        std::vector<std::string> list;
        const char* sub = isMusic ? "music" : "sfx";

        list.push_back(std::string("assets/audio/") + sub + "/" + file);
        list.push_back("assets/audio/" + file);
        list.push_back(std::string("assets/") + sub + "/" + file);
        list.push_back("assets/" + file);
        list.push_back(file);

        // Also accept the other common extension (wav <-> ogg).
        const auto dot = file.rfind('.');
        if (dot != std::string::npos)
        {
            const std::string stem = file.substr(0, dot);
            const std::string ext = file.substr(dot);
            const std::string other = (ext == ".wav") ? ".ogg" : ".wav";
            list.push_back(std::string("assets/audio/") + sub + "/" + stem + other);
            list.push_back("assets/audio/" + stem + other);
        }

        return list;
    }

    float sfxMasterVolume() const
    {
        if (settings_.muted)
            return 0.f;
        return static_cast<float>(settings_.master) * static_cast<float>(settings_.sfx) / 100.f;
    }

    float musicMasterVolume() const
    {
        if (settings_.muted)
            return 0.f;
        return static_cast<float>(settings_.master) * static_cast<float>(settings_.music) / 100.f;
    }

    void applyVolumes()
    {
        // Sound effects read the settings every time they start; music is
        // refreshed in update(), but do it right now so sliders feel instant.
        for (int m = 0; m < MUSIC_COUNT; ++m)
            if (music_[m])
                music_[m]->setVolume(musicLevel_[m] * musicMasterVolume() * duck_);

        if (settings_.muted)
        {
            for (auto& pool : voices_)
                for (auto& voice : pool)
                    voice->setVolume(0.f);
        }
    }

    // ---- Built-in sounds (used only when a file is missing) ----
    void synthesize(SoundId id, std::mt19937& rng)
    {
        constexpr unsigned RATE = 44100;
        std::uniform_real_distribution<float> noise(-1.f, 1.f);
        std::vector<std::int16_t> samples;

        auto push = [&](float v)
        {
            samples.push_back(static_cast<std::int16_t>(std::clamp(v, -1.f, 1.f) * 30000.f));
        };

        auto tone = [&](float seconds, float f0, float f1, float amp, float decay, float noiseMix)
        {
            const int n = static_cast<int>(seconds * RATE);
            float phase = 0.f;
            float lp = 0.f;

            for (int i = 0; i < n; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(n);
                const float f = f0 + (f1 - f0) * t;
                phase += 2.f * PI * f / static_cast<float>(RATE);
                const float env = std::exp(-decay * t) * std::min(1.f, static_cast<float>(i) / 40.f);
                lp += (noise(rng) - lp) * 0.35f;
                push(amp * env * ((1.f - noiseMix) * std::sin(phase) + noiseMix * lp));
            }
        };

        switch (id)
        {
            case SoundId::MenuClick:   tone(0.07f, 900.f, 700.f, 0.55f, 5.f, 0.f); break;
            case SoundId::MenuHover:   tone(0.04f, 620.f, 640.f, 0.35f, 4.f, 0.f); break;
            case SoundId::Pickup:
                tone(0.07f, 660.f, 660.f, 0.5f, 2.f, 0.f);
                tone(0.12f, 990.f, 1100.f, 0.5f, 4.f, 0.f);
                break;
            case SoundId::BulletImpact: tone(0.07f, 180.f, 90.f, 0.8f, 6.f, 0.8f); break;
            case SoundId::PlayerDeath:  tone(1.1f, 260.f, 45.f, 0.8f, 3.f, 0.25f); break;
            case SoundId::Explosion:    tone(0.9f, 90.f, 30.f, 1.0f, 3.5f, 0.85f); break;
            case SoundId::ShellLoad:
                tone(0.03f, 1400.f, 900.f, 0.6f, 5.f, 0.6f);
                tone(0.06f, 320.f, 200.f, 0.7f, 5.f, 0.2f);
                break;
            case SoundId::EmptyClick:   tone(0.04f, 1800.f, 1200.f, 0.5f, 6.f, 0.5f); break;
            default:                    tone(0.15f, 440.f, 440.f, 0.4f, 4.f, 0.f); break;
        }

        if (!buffers_[static_cast<int>(id)].loadFromSamples(
                samples.data(), samples.size(), 1, RATE, {sf::SoundChannel::Mono}))
        {
            std::cerr << "Audio: could not build fallback sound.\n";
        }
    }

    AudioSettings settings_;

    // NOTE: buffers must be declared BEFORE the voices that use them, so the
    // voices are destroyed first.
    std::array<sf::SoundBuffer, SOUND_COUNT> buffers_;
    std::array<std::vector<std::unique_ptr<sf::Sound>>, SOUND_COUNT> voices_;
    std::array<float, SOUND_COUNT> sinceLast_ = [] {
        std::array<float, SOUND_COUNT> a{};
        a.fill(10.f);
        return a;
    }();
    std::array<int, SOUND_COUNT> nextSteal_{};

    std::array<std::unique_ptr<sf::Music>, MUSIC_COUNT> music_;
    std::array<float, MUSIC_COUNT> musicLevel_{};
    std::array<float, MUSIC_COUNT> musicTarget_{};
    MusicId currentMusic_ = MusicId::None;
    float duck_ = 1.f;
    float duckTarget_ = 1.f;

    float growlTimer_ = 4.f;
    std::mt19937 rng_{std::random_device{}()};
    std::uniform_real_distribution<float> jitterDist_{-1.f, 1.f};
};

// ==================================================
// UI SYSTEM  (Phase 5): menus, buttons and screen effects
// ==================================================
// Everything is drawn with the game's own pixel font, so no extra images
// are needed. All layouts are in the fixed 800 x 600 screen space.

struct UiRect
{
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;

    bool contains(const sf::Vector2f& p) const
    {
        return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }
};

// Main menu: 0-2 difficulty (= PLAY), 3 settings, 4 controls, 5 exit,
// 6 continue (Phase 7), 7 profile (Phase 7).
constexpr int MAIN_MENU_ITEMS = 9;   // Phase 8: 8 = credits.

UiRect uiMainRect(int i)
{
    if (i == 6)
        return {200.f, 156.f, 400.f, 50.f};

    if (i < 3)
        return {250.f, 238.f + static_cast<float>(i) * 58.f, 300.f, 50.f};

    // Bottom row, left to right: settings, controls, profile, credits, exit.
    const int slot = (i == 3) ? 0 : (i == 4) ? 1 : (i == 7) ? 2 : (i == 8) ? 3 : 4;
    return {40.f + static_cast<float>(slot) * 146.f, 440.f, 136.f, 40.f};
}

// Pause menu: resume, restart, settings, main menu, exit.
constexpr int PAUSE_ITEMS = 5;

UiRect uiPauseRect(int i)
{
    return {250.f, 168.f + static_cast<float>(i) * 62.f, 300.f, 50.f};
}

// Settings: 0 master, 1 music, 2 sfx, 3 mute, 4 fullscreen (Phase 7), 5 back.
constexpr int SETTINGS_ITEMS = 6;

UiRect uiSettingsRect(int i)
{
    if (i < 3)
        return {250.f, 168.f + static_cast<float>(i) * 74.f, 300.f, 58.f};

    return {250.f, 390.f + static_cast<float>(i - 3) * 52.f, 300.f, 44.f};
}

// The draggable bar of one slider row.
UiRect uiSliderBar(int i)
{
    const UiRect row = uiSettingsRect(i);
    return {row.x, row.y + 28.f, row.w, 14.f};
}

// Game over: restart, main menu, exit.
constexpr int GAME_OVER_ITEMS = 3;

UiRect uiGameOverRect(int i)
{
    return {250.f, 356.f + static_cast<float>(i) * 58.f, 300.f, 46.f};
}

UiRect uiControlsBackRect()
{
    return {250.f, 524.f, 300.f, 46.f};
}

sf::Color lerpColor(const sf::Color& a, const sf::Color& b, float t)
{
    t = std::clamp(t, 0.f, 1.f);
    auto mix = [&](std::uint8_t x, std::uint8_t y)
    {
        return static_cast<std::uint8_t>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t);
    };
    return sf::Color(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a));
}

// hover: 0..1 smooth hover amount, press: 0..1 click flash.
void drawUiButton(
    sf::RenderWindow& window,
    const UiRect& r,
    const std::string& label,
    float labelScale,
    sf::Color accent,
    float hover,
    float press)
{
    const float grow = 4.f * hover - 3.f * press;

    sf::RectangleShape box({r.w + 2.f * grow, r.h + 2.f * grow});
    box.setPosition({r.x - grow, r.y - grow});
    box.setFillColor(lerpColor(sf::Color(30, 40, 32, 235), sf::Color(66, 94, 70, 245), hover));
    box.setOutlineThickness(2.f + 2.f * hover);
    box.setOutlineColor(lerpColor(
        sf::Color(accent.r, accent.g, accent.b, 100), accent, hover));
    window.draw(box);

    if (press > 0.f)
    {
        sf::RectangleShape flash({r.w + 2.f * grow, r.h + 2.f * grow});
        flash.setPosition({r.x - grow, r.y - grow});
        flash.setFillColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(press * 110.f)));
        window.draw(flash);
    }

    drawCenteredText(
        window, label,
        r.x + r.w / 2.f,
        r.y + (r.h - 7.f * labelScale) / 2.f,
        labelScale,
        lerpColor(sf::Color(200, 215, 200), sf::Color(255, 255, 240), hover)
    );
}

// Dark zombie-apocalypse backdrop: gradient sky, moon, drifting fog,
// shambling silhouettes and rising embers. Pure shapes, drawn from time t.
void drawMenuBackground(sf::RenderWindow& window, float t)
{
    // Gradient.
    sf::VertexArray sky(sf::PrimitiveType::TriangleStrip, 4);
    const sf::Color top(8, 12, 12), bottom(40, 18, 16);
    sky[0] = sf::Vertex{{0.f, 0.f}, top, {}};
    sky[1] = sf::Vertex{{800.f, 0.f}, top, {}};
    sky[2] = sf::Vertex{{0.f, 600.f}, bottom, {}};
    sky[3] = sf::Vertex{{800.f, 600.f}, bottom, {}};
    window.draw(sky);

    // Moon with a soft halo.
    for (int i = 3; i >= 0; --i)
    {
        const float radius = 44.f + static_cast<float>(i) * 16.f;
        sf::CircleShape halo(radius);
        halo.setOrigin({radius, radius});
        halo.setPosition({650.f, 110.f});
        halo.setFillColor(i == 0 ? sf::Color(205, 215, 190, 150)
                                 : sf::Color(160, 190, 160, 12));
        window.draw(halo);
    }

    // Drifting fog banks.
    for (int k = 0; k < 8; ++k)
    {
        const float fk = static_cast<float>(k);
        const float radius = 90.f + fk * 11.f;
        const float x = std::fmod(fk * 143.f + t * (7.f + fk * 2.5f), 1000.f) - 100.f;
        const float y = 150.f + fk * 56.f + std::sin(t * 0.35f + fk) * 18.f;

        sf::CircleShape fog(radius);
        fog.setOrigin({radius, radius});
        fog.setScale({1.8f, 0.55f});
        fog.setPosition({x, y});
        fog.setFillColor(sf::Color(70, 95, 72, 20));
        window.draw(fog);
    }

    // Ground.
    sf::RectangleShape ground({800.f, 70.f});
    ground.setPosition({0.f, 548.f});
    ground.setFillColor(sf::Color(9, 10, 9));
    window.draw(ground);

    // Zombie silhouettes shambling across.
    for (int k = 0; k < 6; ++k)
    {
        const float fk = static_cast<float>(k);
        const float speed = 13.f + fk * 4.f;
        const float x = std::fmod(fk * 163.f + t * speed, 960.f) - 80.f;
        const float bob = std::sin(t * (4.f + fk * 0.4f) + fk) * 2.f;
        const float baseY = 556.f + (k % 2) * 6.f;
        const float s = 0.85f + 0.07f * static_cast<float>(k % 3);
        const sf::Color shade(6, 8, 6, 235);

        sf::RectangleShape body({13.f * s, 30.f * s});
        body.setPosition({x - 6.f * s, baseY - 30.f * s + bob});
        body.setFillColor(shade);
        window.draw(body);

        sf::CircleShape head(7.f * s);
        head.setOrigin({7.f * s, 7.f * s});
        head.setPosition({x + 1.f, baseY - 36.f * s + bob});
        head.setFillColor(shade);
        window.draw(head);

        sf::RectangleShape arms({24.f * s, 5.f * s});
        arms.setPosition({x + 2.f, baseY - 27.f * s + bob});
        arms.setFillColor(shade);
        window.draw(arms);

        sf::RectangleShape legs({4.f * s, 10.f * s});
        legs.setFillColor(shade);
        legs.setPosition({x - 5.f * s + std::sin(t * 6.f + fk) * 2.f, baseY - 2.f});
        window.draw(legs);
        legs.setPosition({x + 2.f * s - std::sin(t * 6.f + fk) * 2.f, baseY - 2.f});
        window.draw(legs);
    }

    // Rising embers.
    sf::RectangleShape ember({2.f, 2.f});
    for (int k = 0; k < 30; ++k)
    {
        const float fk = static_cast<float>(k);
        const float x = std::fmod(fk * 97.3f + t * (5.f + static_cast<float>(k % 5) * 2.f) + std::sin(t + fk) * 12.f, 800.f);
        const float y = 600.f - std::fmod(fk * 53.7f + t * (22.f + static_cast<float>(k % 7) * 5.f), 620.f);
        const float twinkle = 0.5f + 0.5f * std::sin(t * 3.f + fk * 1.7f);

        ember.setPosition({x, y});
        ember.setFillColor(sf::Color(255, 120, 45, static_cast<std::uint8_t>(60.f + 150.f * twinkle)));
        window.draw(ember);
    }

    // Edge vignette.
    sf::VertexArray vignette(sf::PrimitiveType::Triangles);
    const sf::Color edge(0, 0, 0, 150), clear(0, 0, 0, 0);
    auto quad = [&](sf::Vector2f p0, sf::Vector2f p1, sf::Vector2f p2, sf::Vector2f p3,
                    sf::Color c0, sf::Color c1, sf::Color c2, sf::Color c3)
    {
        const sf::Vertex a{p0, c0, {}}, b{p1, c1, {}}, c{p2, c2, {}}, d{p3, c3, {}};
        vignette.append(a); vignette.append(b); vignette.append(c);
        vignette.append(a); vignette.append(c); vignette.append(d);
    };
    const float depth = 120.f;
    quad({0.f, 0.f}, {depth, 0.f}, {depth, 600.f}, {0.f, 600.f}, edge, clear, clear, edge);
    quad({800.f - depth, 0.f}, {800.f, 0.f}, {800.f, 600.f}, {800.f - depth, 600.f}, clear, edge, edge, clear);
    quad({0.f, 0.f}, {800.f, 0.f}, {800.f, depth}, {0.f, depth}, edge, edge, clear, clear);
    window.draw(vignette);
}

// Soft coloured glow around the screen edges (damage / low health).
void drawEdgeGlow(sf::RenderWindow& window, sf::Color color, float depth)
{
    if (color.a == 0)
        return;

    sf::VertexArray glow(sf::PrimitiveType::Triangles);
    const sf::Color clear(color.r, color.g, color.b, 0);

    auto quad = [&](sf::Vector2f p0, sf::Vector2f p1, sf::Vector2f p2, sf::Vector2f p3,
                    sf::Color c0, sf::Color c1, sf::Color c2, sf::Color c3)
    {
        const sf::Vertex a{p0, c0, {}}, b{p1, c1, {}}, c{p2, c2, {}}, d{p3, c3, {}};
        glow.append(a); glow.append(b); glow.append(c);
        glow.append(a); glow.append(c); glow.append(d);
    };

    quad({0.f, 0.f}, {depth, 0.f}, {depth, 600.f}, {0.f, 600.f}, color, clear, clear, color);
    quad({800.f - depth, 0.f}, {800.f, 0.f}, {800.f, 600.f}, {800.f - depth, 600.f}, clear, color, color, clear);
    quad({0.f, 0.f}, {800.f, 0.f}, {800.f, depth}, {0.f, depth}, color, color, clear, clear);
    quad({0.f, 600.f - depth}, {800.f, 600.f - depth}, {800.f, 600.f}, {0.f, 600.f}, clear, clear, color, color);
    window.draw(glow);
}

// One settings slider (label, percentage, bar and knob).
void drawUiSlider(
    sf::RenderWindow& window,
    int index,
    const std::string& label,
    int value,
    bool muted,
    float hover)
{
    const UiRect row = uiSettingsRect(index);
    const UiRect bar = uiSliderBar(index);
    const sf::Color accent(120, 205, 95);

    drawShadowText(window, label, row.x, row.y + 4.f, 2.f,
                   lerpColor(sf::Color(200, 215, 200), sf::Color(255, 235, 150), hover));

    const std::string percent = std::to_string(value) + "%";
    drawShadowText(window, percent,
                   row.x + row.w - pixelTextWidth(percent, 2.f), row.y + 4.f, 2.f,
                   sf::Color(255, 205, 95));

    sf::RectangleShape back({bar.w, bar.h});
    back.setPosition({bar.x, bar.y});
    back.setFillColor(sf::Color(26, 32, 27, 230));
    back.setOutlineThickness(2.f);
    back.setOutlineColor(lerpColor(sf::Color(75, 92, 76), accent, hover));
    window.draw(back);

    sf::RectangleShape fill({bar.w * static_cast<float>(value) / 100.f, bar.h});
    fill.setPosition({bar.x, bar.y});
    fill.setFillColor(muted ? sf::Color(95, 95, 90) : accent);
    window.draw(fill);

    sf::RectangleShape knob({8.f, bar.h + 10.f});
    knob.setOrigin({4.f, 5.f});
    knob.setPosition({bar.x + bar.w * static_cast<float>(value) / 100.f, bar.y});
    knob.setFillColor(sf::Color(250, 245, 225));
    window.draw(knob);
}

// ==================================================
// MAIN
// ==================================================

int main()
{
    // --------------------------------------------------
    // 1. WINDOW
    // --------------------------------------------------
    // Phase 7: progress and settings are loaded BEFORE the window exists, so
    // the saved fullscreen / window-size preference can be applied at start-up.
    zsa::SaveManager saveManager("saves");
    zsa::PlayerData playerData;
    zsa::GameSettings gameSettings;
    bool settingsFileLoaded = false;

    {
        std::string error;

        if (saveManager.loadPlayerData(playerData, &error))
            std::cout << "Save: progress loaded (" << playerData.totalGamesPlayed
                      << " games played).\n";
        else
            std::cout << "Save: " << error << " (starting fresh)\n";

        settingsFileLoaded = saveManager.loadSettings(gameSettings, &error);
        if (!settingsFileLoaded)
            std::cout << "Settings: " << error << " (using defaults)\n";
    }

    DifficultyBests difficultyBests = loadDifficultyBests(saveManager.getSaveDirectory());

    // Saves from before this file existed: credit the overall best to the
    // difficulty that was played last.
    if (!difficultyBests.loaded && playerData.bestScore > 0u)
    {
        const std::size_t last = std::min<std::size_t>(gameSettings.difficulty, 2u);
        difficultyBests.score[last] = playerData.bestScore;
        difficultyBests.wave[last] = playerData.bestWave;
    }

    bool isFullscreen = settingsFileLoaded && gameSettings.fullscreen;
    sf::Vector2u windowedSize(800u, 600u);

    if (settingsFileLoaded)
    {
        const sf::Vector2u desktop = sf::VideoMode::getDesktopMode().size;
        windowedSize = {
            std::clamp(gameSettings.windowWidth, 640u, std::max(640u, desktop.x)),
            std::clamp(gameSettings.windowHeight, 480u, std::max(480u, desktop.y))
        };
    }

    // Window icon (assets/icons/game_icon.png). Missing file = default icon.
    sf::Image gameIcon;
    bool gameIconLoaded = false;
    for (const char* path : {"assets/icons/game_icon.png", "assets/game_icon.png", "game_icon.png"})
    {
        if (gameIcon.loadFromFile(path))
        {
            gameIconLoaded = true;
            break;
        }
    }
    if (!gameIconLoaded)
        std::cerr << "Note: assets/icons/game_icon.png not found - using the default window icon.\n";
    else if (gameIcon.getSize().x > 256u || gameIcon.getSize().y > 256u)
    {
        // The icon is large (e.g. 1254 x 1254). Window icons look best at
        // about 256 px, so shrink it once here with a simple box filter.
        const sf::Vector2u src = gameIcon.getSize();
        const unsigned int side = 256u;
        sf::Image small(sf::Vector2u(side, side), sf::Color::Transparent);
        for (unsigned int y = 0; y < side; ++y)
        {
            for (unsigned int x = 0; x < side; ++x)
            {
                const unsigned int x0 = x * src.x / side, x1 = std::max(x0 + 1u, (x + 1u) * src.x / side);
                const unsigned int y0 = y * src.y / side, y1 = std::max(y0 + 1u, (y + 1u) * src.y / side);
                unsigned long r = 0, g = 0, b = 0, a = 0, n = 0;
                for (unsigned int sy = y0; sy < y1 && sy < src.y; ++sy)
                    for (unsigned int sx = x0; sx < x1 && sx < src.x; ++sx)
                    {
                        const sf::Color c = gameIcon.getPixel({sx, sy});
                        r += c.r; g += c.g; b += c.b; a += c.a; ++n;
                    }
                if (n > 0)
                    small.setPixel({x, y}, sf::Color(static_cast<std::uint8_t>(r / n), static_cast<std::uint8_t>(g / n),
                                                     static_cast<std::uint8_t>(b / n), static_cast<std::uint8_t>(a / n)));
            }
        }
        gameIcon = small;
    }

    sf::RenderWindow window;

    auto createWindow = [&](bool fullscreen)
    {
        if (fullscreen)
            window.create(sf::VideoMode::getDesktopMode(), "Zombie Survival Arena",
                          sf::Style::Default, sf::State::Fullscreen);
        else
            window.create(sf::VideoMode(windowedSize), "Zombie Survival Arena",
                          sf::Style::Default, sf::State::Windowed);

        window.setFramerateLimit(60);
        window.setKeyRepeatEnabled(false);   // One key press = one action (pause, menus).
        if (gameIconLoaded)
            window.setIcon(gameIcon.getSize(), gameIcon.getPixelsPtr());
    };

    createWindow(isFullscreen);

    // The fixed 800 x 600 screen used by every menu and the HUD. Its viewport
    // is letterboxed (see applyLetterbox) so fullscreen never stretches it.
    sf::View screenView(sf::FloatRect({0.f, 0.f}, {800.f, 600.f}));

    // ==================================================
    // VISUAL IDENTITY: LOADING SCREEN AND CREDITS BACKGROUND
    // ==================================================
    // assets/backgrounds/loading_screen.png and credits_background.png are
    // drawn "fit" style (whole picture visible, bars above/below on the 4:3
    // screen), so they are never stretched or cropped. A missing file only falls back to plain text.
    sf::Texture loadingArt;
    sf::Texture creditsArt;
    bool loadingArtLoaded = false;
    bool creditsArtLoaded = false;

    auto loadArt = [&](sf::Texture& texture, const char* file) -> bool
    {
        const std::array<std::string, 4> roots = {
            "assets/backgrounds/", "assets/", "assets/ui/", ""
        };

        for (const std::string& root : roots)
        {
            if (texture.loadFromFile(root + file))
            {
                texture.setSmooth(true);
                return true;
            }
        }

        std::cerr << "Note: " << file << " not found (using a plain fallback).\n";
        return false;
    };

    loadingArtLoaded = loadArt(loadingArt, "loading_screen.png");
    creditsArtLoaded = loadArt(creditsArt, "credits_background.png");

    // Keeps the 800 x 600 picture undistorted (needed before the main letterbox exists).
    auto letterboxScreenView = [&]()
    {
        const sf::Vector2u size = window.getSize();
        if (size.x == 0u || size.y == 0u)
            return;

        const float windowRatio = static_cast<float>(size.x) / static_cast<float>(size.y);
        const float targetRatio = 800.f / 600.f;

        sf::FloatRect viewport({0.f, 0.f}, {1.f, 1.f});
        if (windowRatio > targetRatio)
        {
            viewport.size.x = targetRatio / windowRatio;
            viewport.position.x = (1.f - viewport.size.x) * 0.5f;
        }
        else
        {
            viewport.size.y = windowRatio / targetRatio;
            viewport.position.y = (1.f - viewport.size.y) * 0.5f;
        }
        screenView.setViewport(viewport);
    };

    struct CoverInfo
    {
        float scale = 1.f;
        sf::Vector2f origin;   // Top-left of the visible part, in texture pixels.
    };

    // Draws a texture fully visible inside the 800 x 600 screen ("fit"):
    // nothing is cropped, so the 16:9 artwork keeps its whole title and text.
    // The unused strips above and below are filled with near-black.
    auto drawCover = [&](const sf::Texture& texture) -> CoverInfo
    {
        const sf::Vector2u size = texture.getSize();
        CoverInfo info;
        if (size.x == 0u || size.y == 0u)
            return info;

        info.scale = std::min(800.f / static_cast<float>(size.x), 600.f / static_cast<float>(size.y));
        const float drawW = static_cast<float>(size.x) * info.scale;
        const float drawH = static_cast<float>(size.y) * info.scale;
        const float offX = (800.f - drawW) * 0.5f;
        const float offY = (600.f - drawH) * 0.5f;
        // Texture pixel that lands on the screen's top-left (negative = empty margin).
        info.origin = {-offX / info.scale, -offY / info.scale};

        sf::RectangleShape backdrop({800.f, 600.f});
        backdrop.setFillColor(sf::Color(6, 8, 14));
        window.draw(backdrop);

        sf::Sprite sprite(texture);
        sprite.setPosition({offX, offY});
        sprite.setScale({info.scale, info.scale});
        window.draw(sprite);
        return info;
    };

    // Shows the loading screen with a REAL progress bar (0..1). The artwork's
    // painted bar is covered by a bar that fills as the game loads.
    auto showLoading = [&](float progress)
    {
        while (const std::optional<sf::Event> loadEvent = window.pollEvent())
            if (loadEvent->is<sf::Event::Closed>())
                window.close();

        if (!window.isOpen())
            return;

        progress = std::clamp(progress, 0.f, 1.f);
        letterboxScreenView();
        window.setView(screenView);
        window.clear(sf::Color(6, 8, 14));

        if (loadingArtLoaded)
        {
            const CoverInfo cover = drawCover(loadingArt);
            const sf::Vector2f textureSize(loadingArt.getSize());

            // The painted bar's inside, in pixels of the 1672 x 941 artwork.
            // If the bar in loading_screen.png sits elsewhere, only these four
            // numbers need changing (open the image, hover the bar's inner
            // top-left and bottom-right corners, and read the pixel position).
            constexpr float ART_W = 1672.f, ART_H = 941.f;
            // These cover the bar painted in the artwork (including its baked-in
            // "72%" text), so the real progress bar replaces it.
            constexpr float BAR_LEFT = 399.f, BAR_TOP = 751.f;      // inner top-left
            constexpr float BAR_RIGHT = 1277.f, BAR_BOTTOM = 797.f; // inner bottom-right
            auto toScreen = [&](float px, float py)
            {
                return sf::Vector2f(
                    ((px / ART_W) * textureSize.x - cover.origin.x) * cover.scale,
                    ((py / ART_H) * textureSize.y - cover.origin.y) * cover.scale);
            };
            const sf::Vector2f a = toScreen(BAR_LEFT, BAR_TOP);
            const sf::Vector2f b = toScreen(BAR_RIGHT, BAR_BOTTOM);

            sf::RectangleShape well(b - a);
            well.setPosition(a);
            well.setFillColor(sf::Color(4, 2, 4));
            window.draw(well);

            sf::RectangleShape fill({(b.x - a.x) * progress, b.y - a.y});
            fill.setPosition(a);
            fill.setFillColor(sf::Color(219, 30, 31));
            window.draw(fill);

            sf::RectangleShape shine({(b.x - a.x) * progress, (b.y - a.y) * 0.35f});
            shine.setPosition(a);
            shine.setFillColor(sf::Color(255, 130, 120, 110));
            window.draw(shine);

            // Live percentage drawn by the game (the artwork's own is covered).
            const int percent = static_cast<int>(std::round(progress * 100.f));
            drawCenteredText(window, std::to_string(percent) + "%",
                             (a.x + b.x) * 0.5f, a.y + ((b.y - a.y) - 7.f * 2.f) * 0.5f,
                             2.f, sf::Color(245, 245, 235));
        }
        else
        {
            drawCenteredText(window, "ZOMBIE SURVIVAL ARENA", 400.f, 230.f, 4.f, sf::Color(220, 58, 45));
            drawCenteredText(window, "LOADING", 400.f, 300.f, 3.f, sf::Color(245, 245, 230));

            sf::RectangleShape back({400.f, 14.f});
            back.setPosition({200.f, 350.f});
            back.setFillColor(sf::Color(30, 36, 32));
            back.setOutlineThickness(2.f);
            back.setOutlineColor(sf::Color(95, 110, 95));
            window.draw(back);

            sf::RectangleShape fill({400.f * progress, 14.f});
            fill.setPosition({200.f, 350.f});
            fill.setFillColor(sf::Color(219, 30, 31));
            window.draw(fill);
        }

        window.display();
    };

    showLoading(0.05f);

    // Audio: sound effects, music and the saved volume settings.
    AudioManager audio;
    audio.load();
    audio.loadSettings();

    // Phase 7: settings.dat (SaveManager) is the master copy of the music and
    // sound-effect volumes. (audio.ini still keeps master volume and mute.)
    if (settingsFileLoaded)
    {
        audio.setMusicVolume(static_cast<int>(std::lround(gameSettings.musicVolume)));
        audio.setSfxVolume(static_cast<int>(std::lround(gameSettings.sfxVolume)));
    }

    // --------------------------------------------------
    // 2. LOAD CHARACTER SPRITE SHEETS
    // --------------------------------------------------
    showLoading(0.20f);

    sf::Texture bodyTexture;
    sf::Texture armsGunTexture;

    if (!bodyTexture.loadFromFile("assets/player_body_aligned.png"))
    {
        std::cerr << "Could not load assets/player_body_aligned.png\n";
        return 1;
    }

    if (!armsGunTexture.loadFromFile("assets/player_hands_gun_aligned.png"))
    {
        std::cerr << "Could not load assets/player_hands_gun_aligned.png\n";
        return 1;
    }

    bodyTexture.setSmooth(false);
    armsGunTexture.setSmooth(false);

    // Both sheets: 2048 x 640.
    // Four columns x two rows. Each frame: 512 x 320.
    constexpr int FRAME_W = 512;
    constexpr int FRAME_H = 320;
    constexpr int COLUMNS = 4;

    if (bodyTexture.getSize() != sf::Vector2u(2048u, 640u) ||
        armsGunTexture.getSize() != sf::Vector2u(2048u, 640u))
    {
        std::cerr
            << "Incorrect sprite sheet dimensions.\n"
            << "Both aligned sheets must be 2048 x 640.\n";
        return 1;
    }

    // --------------------------------------------------
    // 3. PLAYER SPRITES
    // --------------------------------------------------
    showLoading(0.30f);
    sf::Sprite body(bodyTexture);
    sf::Sprite armsGun(armsGunTexture);

    constexpr float PLAYER_SCALE = 0.22f;

    body.setOrigin({256.f, 300.f});
    body.setScale({PLAYER_SCALE, PLAYER_SCALE});

    constexpr float SHOULDER_X = 256.f;
    constexpr float SHOULDER_Y = 185.f;

    armsGun.setOrigin({SHOULDER_X, SHOULDER_Y});
    armsGun.setScale({PLAYER_SCALE, PLAYER_SCALE});

    sf::Vector2f playerPosition(WorldConfig::PLAYER_SPAWN_X, WorldConfig::PLAYER_SPAWN_Y);
    body.setPosition(playerPosition);

    const sf::Vector2f shoulderOffset(
        (SHOULDER_X - 256.f) * PLAYER_SCALE,
        (SHOULDER_Y - 300.f) * PLAYER_SCALE
    );

    const sf::Vector2f muzzleRight(438.f, 175.f);
    const sf::Vector2f muzzleLeft(82.f, 175.f);

    // --------------------------------------------------
    // 4. PLAYER MOVEMENT AND ANIMATION
    // --------------------------------------------------
    constexpr float PLAYER_SPEED = 250.f;
    constexpr float FRAME_TIME = 0.12f;

    float animationTimer = 0.f;
    int currentFrame = 0;
    bool facingLeft = false;

    // --------------------------------------------------
    // 5. AIMING
    // --------------------------------------------------
    constexpr float MAX_AIM_ANGLE = 90.f;   // 90 up/down + left/right flip = full 360 degrees
    constexpr float AIM_SMOOTHING = 10.f;

    float currentAimAngle = 0.f;

    // --------------------------------------------------
    // 6. BULLETS
    // --------------------------------------------------
    std::vector<Bullet> bullets;

    constexpr float BULLET_RADIUS = 3.5f;

    sf::CircleShape bulletShape(BULLET_RADIUS);
    bulletShape.setOrigin({BULLET_RADIUS, BULLET_RADIUS});
    bulletShape.setFillColor(sf::Color(255, 220, 125));
    bulletShape.setOutlineThickness(1.f);
    bulletShape.setOutlineColor(sf::Color(255, 150, 55));

    // --------------------------------------------------
    // 7. MUZZLE FLASH
    // --------------------------------------------------
    constexpr float FLASH_DURATION = 0.075f;

    float muzzleFlashTimer = 0.f;
    sf::Vector2f muzzleFlashPosition(0.f, 0.f);
    sf::Vector2f muzzleFlashDirection(1.f, 0.f);
    float muzzleFlashScale = 1.f;   // Set per weapon when firing.

    sf::CircleShape flashOuter(9.f);
    flashOuter.setOrigin({9.f, 9.f});

    sf::CircleShape flashInner(5.f);
    flashInner.setOrigin({5.f, 5.f});

    sf::RectangleShape flashRay({19.f, 5.f});
    flashRay.setOrigin({0.f, 2.5f});

    // --------------------------------------------------
    // 8. RIFLE RECOIL
    // --------------------------------------------------
    float recoilAmount = 0.f;

    constexpr float MAX_RECOIL = 13.f;
    constexpr float RECOIL_RECOVERY_SPEED = 48.f;

    // --------------------------------------------------
    // 9. ZOMBIE SYSTEM
    // --------------------------------------------------
    sf::Texture zombieWalkTexture;

    if (!zombieWalkTexture.loadFromFile("assets/zombie_walk.png"))
    {
        std::cerr << "Could not load assets/zombie_walk.png\n";
        return 1;
    }

    zombieWalkTexture.setSmooth(false);

    sf::Texture zombieAttackTexture;

    if (!zombieAttackTexture.loadFromFile("assets/zombie_attack.png"))
    {
        std::cerr << "Could not load assets/zombie_attack.png\n";
        return 1;
    }

    zombieAttackTexture.setSmooth(false);

    if (zombieAttackTexture.getSize() != sf::Vector2u(2176u, 320u))
    {
        std::cerr
            << "Incorrect zombie_attack.png dimensions. Expected 2176 x 320.\n";
        return 1;
    }

    // Death animation sheet: 2176 x 181 (its own custom frame layout).
    sf::Texture zombieDeathTexture;

    if (!zombieDeathTexture.loadFromFile("assets/zombie_death.png"))
    {
        std::cerr << "Could not load assets/zombie_death.png\n";
        return 1;
    }

    zombieDeathTexture.setSmooth(false);

    if (zombieDeathTexture.getSize() != sf::Vector2u(2176u, 181u))
    {
        std::cerr
            << "Incorrect zombie_death.png dimensions. Expected 2176 x 181.\n";
        return 1;
    }

    // Walk and attack sheets are 2176 x 320:
    // 8 columns, 1 row, each frame is 272 x 320.
    // The death sheet is handled separately below.
    constexpr int ZOMBIE_FRAME_W = 272;
    constexpr int ZOMBIE_FRAME_H = 320;
    constexpr int ZOMBIE_FRAME_COUNT = 8;
    constexpr float ZOMBIE_ANIMATION_TIME = 0.13f;
    constexpr float ZOMBIE_SPRITE_SCALE = 0.27f;

    if (zombieWalkTexture.getSize() != sf::Vector2u(2176u, 320u))
    {
        std::cerr
            << "Incorrect zombie_walk.png dimensions. Expected 2176 x 320.\n";
        return 1;
    }

    showLoading(0.40f);
    sf::Sprite zombieSprite(zombieWalkTexture);
    zombieSprite.setOrigin({136.f, 308.f});
    zombieSprite.setScale({ZOMBIE_SPRITE_SCALE, ZOMBIE_SPRITE_SCALE});

    constexpr int ZOMBIE_ATTACK_FRAME_COUNT = 8;
    constexpr float ZOMBIE_ATTACK_FRAME_TIME = 0.11f;
    constexpr float ZOMBIE_ATTACK_COOLDOWN = 0.85f;
    constexpr float ZOMBIE_ATTACK_RANGE = 54.f;
    constexpr int ZOMBIE_ATTACK_DAMAGE_FRAME = 4;

    sf::Sprite zombieAttackSprite(zombieAttackTexture);
    zombieAttackSprite.setOrigin({136.f, 308.f});
    zombieAttackSprite.setScale({ZOMBIE_SPRITE_SCALE, ZOMBIE_SPRITE_SCALE});

    // Death animation settings.
    constexpr int ZOMBIE_DEATH_FRAME_COUNT = 8;
    constexpr float ZOMBIE_DEATH_FRAME_TIME = 0.10f;
    // How long the final frame (blood pool) stays and fades out before the
    // zombie is removed. Set to 0.f to disappear the instant the animation ends.
    constexpr float ZOMBIE_CORPSE_LINGER = 0.8f;

    // The death sheet is 2176 x 181 (8 frames of 272 x 181). Its zombie is
    // drawn smaller inside the frame than in the 320-tall walk/attack sheets,
    // so it needs its own scale and origin.
    //   ZOMBIE_DEATH_SPRITE_SCALE: raise if the dying zombie looks smaller
    //                              than the walking one, lower if bigger.
    //   ZOMBIE_DEATH_ORIGIN_Y:     the feet/ground line inside the frame.
    //                              Raise to move the corpse up, lower to move it down.
    constexpr float ZOMBIE_DEATH_SPRITE_SCALE = 0.54f;
    // Nudge corpses up (negative) or down (positive) relative to the zombie's feet.
    constexpr float ZOMBIE_DEATH_GROUND_OFFSET_Y = 0.f;
    // Ignore stray pixels along the very top edge of the sheet.
    constexpr unsigned DEATH_SHEET_IGNORE_TOP_PIXELS = 6u;
    // Empty columns allowed inside one pose (e.g. blood splatter gaps).
    constexpr unsigned DEATH_SHEET_MAX_GAP = 10u;

    // The 8 poses on the death sheet are NOT on an even 272 px grid, so
    // fixed slicing makes the animation jump around. Instead, find each
    // pose automatically from the transparent gaps between them, and anchor
    // every pose at the bottom-centre of its own pixels (the ground).
    struct DeathFrame
    {
        sf::IntRect rect;
        sf::Vector2f origin;
    };

    std::array<DeathFrame, ZOMBIE_DEATH_FRAME_COUNT> deathFrames;

    {
        const sf::Image deathImage = zombieDeathTexture.copyToImage();
        const unsigned sheetW = deathImage.getSize().x;
        const unsigned sheetH = deathImage.getSize().y;

        std::vector<bool> columnHasPixels(sheetW, false);

        for (unsigned x = 0; x < sheetW; ++x)
        {
            for (unsigned y = DEATH_SHEET_IGNORE_TOP_PIXELS; y < sheetH; ++y)
            {
                if (deathImage.getPixel({x, y}).a > 20)
                {
                    columnHasPixels[x] = true;
                    break;
                }
            }
        }

        struct Run { unsigned start; unsigned end; };
        std::vector<Run> runs;

        bool inRun = false;
        unsigned gap = 0;

        for (unsigned x = 0; x < sheetW; ++x)
        {
            if (columnHasPixels[x])
            {
                if (!inRun)
                {
                    runs.push_back({x, x});
                    inRun = true;
                }
                runs.back().end = x;
                gap = 0;
            }
            else if (inRun)
            {
                if (++gap > DEATH_SHEET_MAX_GAP)
                    inRun = false;
            }
        }

        // Drop tiny specks, then keep the widest 8 poses in left-to-right order.
        runs.erase(
            std::remove_if(runs.begin(), runs.end(),
                [](const Run& r) { return r.end - r.start < 8u; }),
            runs.end()
        );

        if (runs.size() > static_cast<std::size_t>(ZOMBIE_DEATH_FRAME_COUNT))
        {
            std::sort(runs.begin(), runs.end(),
                [](const Run& a, const Run& b)
                { return (a.end - a.start) > (b.end - b.start); });
            runs.resize(ZOMBIE_DEATH_FRAME_COUNT);
            std::sort(runs.begin(), runs.end(),
                [](const Run& a, const Run& b) { return a.start < b.start; });
        }

        if (runs.size() == static_cast<std::size_t>(ZOMBIE_DEATH_FRAME_COUNT))
        {
            for (int i = 0; i < ZOMBIE_DEATH_FRAME_COUNT; ++i)
            {
                unsigned top = sheetH;
                unsigned bottom = 0u;

                for (unsigned y = DEATH_SHEET_IGNORE_TOP_PIXELS; y < sheetH; ++y)
                {
                    for (unsigned x = runs[i].start; x <= runs[i].end; ++x)
                    {
                        if (deathImage.getPixel({x, y}).a > 20)
                        {
                            top = std::min(top, y);
                            bottom = std::max(bottom, y);
                            break;
                        }
                    }
                }

                const int w = static_cast<int>(runs[i].end - runs[i].start + 1u);
                const int h = static_cast<int>(bottom - top + 1u);

                deathFrames[i].rect = sf::IntRect(
                    {static_cast<int>(runs[i].start), static_cast<int>(top)},
                    {w, h}
                );
                deathFrames[i].origin = {w / 2.f, static_cast<float>(h)};
            }
        }
        else
        {
            std::cerr
                << "Warning: found " << runs.size()
                << " poses in zombie_death.png (expected "
                << ZOMBIE_DEATH_FRAME_COUNT
                << "). Falling back to equal slicing.\n";

            const int equalW = static_cast<int>(sheetW) / ZOMBIE_DEATH_FRAME_COUNT;

            for (int i = 0; i < ZOMBIE_DEATH_FRAME_COUNT; ++i)
            {
                deathFrames[i].rect = sf::IntRect(
                    {i * equalW, 0},
                    {equalW, static_cast<int>(sheetH)}
                );
                deathFrames[i].origin = {
                    equalW / 2.f,
                    static_cast<float>(sheetH) - 13.f
                };
            }
        }
    }

    showLoading(0.50f);
    sf::Sprite zombieDeathSprite(zombieDeathTexture);
    zombieDeathSprite.setScale({ZOMBIE_DEATH_SPRITE_SCALE, ZOMBIE_DEATH_SPRITE_SCALE});

    // Visible blue aura while a newly spawned zombie is protected.
    sf::CircleShape zombieShield(39.f);
    zombieShield.setOrigin({39.f, 39.f});
    zombieShield.setFillColor(sf::Color(35, 165, 255, 24));
    zombieShield.setOutlineThickness(3.f);

    std::vector<Zombie> zombies;

    // One reusable soft shadow ellipse for characters (no per-frame allocation).
    sf::CircleShape groundShadow(15.f);
    groundShadow.setOrigin({15.f, 15.f});
    auto drawGroundShadow = [&](sf::Vector2f feet, float widthScale, std::uint8_t alpha)
    {
        groundShadow.setScale({widthScale, widthScale * 0.32f});
        groundShadow.setPosition(feet + sf::Vector2f(0.f, 2.f));
        groundShadow.setFillColor(sf::Color(0, 0, 0, alpha));
        window.draw(groundShadow);
    };

    std::mt19937 randomEngine(
        static_cast<unsigned int>(
            std::random_device{}()
        )
    );

    // --------------------------------------------------
    // BLOOD PARTICLE SYSTEM
    // --------------------------------------------------
    std::vector<Particle> particles;
    std::uniform_real_distribution<float> particleAngle(0.f, 6.2831853f);
    std::uniform_real_distribution<float> particleSpeed(45.f, 190.f);
    std::uniform_real_distribution<float> particleLife(0.22f, 0.55f);
    std::uniform_real_distribution<float> particleSize(1.5f, 3.2f);

    auto spawnBloodBurst = [&](const sf::Vector2f& position, int count, bool largeBurst)
    {
        for (int i = 0; i < count; ++i)
        {
            const float angle = particleAngle(randomEngine);
            const float speed = particleSpeed(randomEngine) *
                                (largeBurst ? 1.15f : 0.75f);

            Particle particle;
            particle.position = position;
            particle.velocity = {
                std::cos(angle) * speed,
                std::sin(angle) * speed
            };
            particle.life = particleLife(randomEngine) *
                            (largeBurst ? 1.25f : 0.8f);
            particle.maxLife = particle.life;
            particle.size = particleSize(randomEngine) *
                            (largeBurst ? 1.35f : 1.f);

            // A few dark-red particles give the burst some depth.
            const int shade = (i % 4 == 0) ? 105 : 175;
            particle.color = sf::Color(
                static_cast<std::uint8_t>(shade + (i % 3) * 18),
                static_cast<std::uint8_t>(18 + (i % 3) * 5),
                static_cast<std::uint8_t>(22 + (i % 2) * 8),
                245
            );

            particles.push_back(particle);
        }
    };

    sf::RectangleShape particlePixel;
    particlePixel.setOutlineThickness(0.f);

    std::uniform_int_distribution<int> sideDistribution(0, 3);
    std::uniform_real_distribution<float> xDistribution(30.f, 770.f);
    std::uniform_real_distribution<float> yDistribution(30.f, 570.f);
    std::uniform_real_distribution<float> speedDistribution(48.f, 68.f);
    std::uniform_real_distribution<float> spawnJitter(0.f, 40.f);

    float spawnTimer = 0.f;

    sf::RectangleShape zombieHealthBackground({28.f, 4.f});
    zombieHealthBackground.setFillColor(sf::Color(65, 25, 25));

    sf::RectangleShape zombieHealthForeground({28.f, 4.f});
    zombieHealthForeground.setFillColor(sf::Color(90, 210, 85));

    // --------------------------------------------------
    // 9b. WAVE SYSTEM STATE
    // --------------------------------------------------
    int currentWave = 1;
    int waveZombieTotal = 0;      // Zombies this wave will spawn in total.
    int waveZombiesSpawned = 0;   // Already spawned this wave.
    int waveZombiesKilled = 0;    // Killed this wave.

    bool intermission = false;    // True during the break between waves.
    float intermissionTimer = 0.f;
    float waveBannerTimer = 0.f;

    int lastSpawnSide = -1;       // Used to rotate spawn sides.

    // --------------------------------------------------
    // 8b. WORLD: TEXTURES, TILES, WALLS, NAVIGATION  (Phase 4)
    // --------------------------------------------------
    constexpr int   MAP_COLS = WorldConfig::COLS;
    constexpr int   MAP_ROWS = WorldConfig::ROWS;
    constexpr float TILE_F   = static_cast<float>(WorldConfig::TILE);
    constexpr float WORLD_W  = WorldConfig::WORLD_W;
    constexpr float WORLD_H  = WorldConfig::WORLD_H;

    // Environment PNGs are looked up under several folder names, so the
    // Phase 4 ZIP can simply be unpacked into the assets folder.
    std::map<std::string, sf::Texture> envTextures;
    std::set<std::string> envMissing;
    const std::array<std::string, 5> envRoots = {
        "assets/", "assets/environment/", "assets/phase4/", "assets/env/", ""
    };

    auto envTexture = [&](const std::string& relativePath) -> const sf::Texture*
    {
        const auto found = envTextures.find(relativePath);
        if (found != envTextures.end())
            return &found->second;

        if (envMissing.count(relativePath) > 0)
            return nullptr;

        for (const std::string& root : envRoots)
        {
            sf::Texture texture;

            if (texture.loadFromFile(root + relativePath))
            {
                texture.setSmooth(false);      // Crisp pixel art.
                texture.setRepeated(true);     // Long walls repeat their tile.
                return &envTextures.emplace(relativePath, std::move(texture)).first->second;
            }
        }

        envMissing.insert(relativePath);
        std::cerr << "Note: environment image " << relativePath
                  << " not found - using a flat colour instead.\n";
        return nullptr;
    };

    std::map<std::string, TileBatch> groundBatches;   // Floor tiles.
    std::map<std::string, TileBatch> decalBatches;    // Road markings, potholes...
    std::map<std::string, TileBatch> wallBatches;     // Walls and fences.
    TileBatch shadowBatch;                            // Soft wall shadows.

    auto batchFor = [&](std::map<std::string, TileBatch>& group,
                        const std::string& relativePath,
                        sf::Color fallback) -> TileBatch&
    {
        auto it = group.find(relativePath);

        if (it == group.end())
        {
            TileBatch batch;
            batch.texture = envTexture(relativePath);
            batch.fallback = fallback;
            it = group.emplace(relativePath, std::move(batch)).first;
        }

        return it->second;
    };

    // Axis-aligned quad; the texture repeats if the quad is larger than the tile.
    auto addTexturedQuad = [](TileBatch& batch, float x, float y, float w, float h,
                              float u, float v)
    {
        const sf::Color color = batch.texture ? sf::Color::White : batch.fallback;

        const sf::Vertex a{{x, y},         color, {0.f, 0.f}};
        const sf::Vertex b{{x + w, y},     color, {u,   0.f}};
        const sf::Vertex c{{x + w, y + h}, color, {u,   v}};
        const sf::Vertex d{{x, y + h},     color, {0.f, v}};

        batch.vertices.append(a); batch.vertices.append(b); batch.vertices.append(c);
        batch.vertices.append(a); batch.vertices.append(c); batch.vertices.append(d);
    };

    // Rotated sprite-sized quad for decals.
    auto addRotatedQuad = [](TileBatch& batch, sf::Vector2f center, float w, float h,
                             float angleDegrees, float texW, float texH)
    {
        if (!batch.texture)
            return;   // Missing decals are simply skipped.

        const float radians = angleDegrees / RAD_TO_DEG;
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        const float hw = w * 0.5f;
        const float hh = h * 0.5f;

        const std::array<sf::Vector2f, 4> corners = {{{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}}};
        const std::array<sf::Vector2f, 4> uvs = {{{0.f, 0.f}, {texW, 0.f}, {texW, texH}, {0.f, texH}}};
        std::array<sf::Vertex, 4> verts;

        for (int i = 0; i < 4; ++i)
        {
            verts[i].position = {
                center.x + corners[i].x * c - corners[i].y * s,
                center.y + corners[i].x * s + corners[i].y * c
            };
            verts[i].color = sf::Color::White;
            verts[i].texCoords = uvs[i];
        }

        batch.vertices.append(verts[0]); batch.vertices.append(verts[1]); batch.vertices.append(verts[2]);
        batch.vertices.append(verts[0]); batch.vertices.append(verts[2]); batch.vertices.append(verts[3]);
    };

    // ---- Ground tiles ----
    sf::Image minimapImage(sf::Vector2u(MAP_COLS, MAP_ROWS), sf::Color(20, 20, 20));

    for (int row = 0; row < MAP_ROWS; ++row)
    {
        for (int column = 0; column < MAP_COLS; ++column)
        {
            const char kind = GROUND_MAP[row][column];
            const unsigned int cluster = tileHash(column / 2, row / 2, 7u) % 100u;
            const unsigned int single = tileHash(column, row, 3u) % 100u;

            std::string file;
            sf::Color fallback;
            sf::Color mapColor;

            switch (kind)
            {
                case 'g':
                    file = (cluster < 22u) ? "ground_tiles/grass_dark.png" : "ground_tiles/grass.png";
                    fallback = (cluster < 22u) ? sf::Color(40, 85, 45) : sf::Color(55, 105, 55);
                    mapColor = sf::Color(48, 92, 50);
                    break;
                case 's':
                    file = "ground_tiles/sidewalk.png";
                    fallback = sf::Color(139, 135, 122);
                    mapColor = sf::Color(120, 116, 104);
                    break;
                case 'a':
                    file = "ground_tiles/asphalt.png";
                    fallback = sf::Color(48, 51, 54);
                    mapColor = sf::Color(54, 57, 60);
                    break;
                case 'c':
                    if (single < 6u)       { file = "ground_tiles/damaged_concrete.png";       fallback = sf::Color(95, 95, 90); }
                    else if (single < 8u)  { file = "ground_tiles/blood_stained_concrete.png"; fallback = sf::Color(110, 70, 70); }
                    else                   { file = "ground_tiles/concrete.png";               fallback = sf::Color(104, 105, 101); }
                    mapColor = sf::Color(96, 98, 94);
                    break;
                case 'i':
                    file = "ground_tiles/interior_tile.png";
                    fallback = sf::Color(90, 92, 95);
                    mapColor = sf::Color(86, 88, 92);
                    break;
                default:   // dirt
                    if (single < 70u)      { file = "ground_tiles/dirt.png";   fallback = sf::Color(110, 85, 55); }
                    else if (single < 86u) { file = "ground_tiles/mud.png";    fallback = sf::Color(80, 62, 45); }
                    else                   { file = "ground_tiles/gravel.png"; fallback = sf::Color(110, 108, 100); }
                    mapColor = sf::Color(100, 78, 52);
                    break;
            }

            TileBatch& batch = batchFor(groundBatches, file, fallback);
            addTexturedQuad(
                batch,
                static_cast<float>(column) * TILE_F, static_cast<float>(row) * TILE_F,
                TILE_F, TILE_F, TILE_F, TILE_F
            );

            minimapImage.setPixel(
                sf::Vector2u(static_cast<unsigned>(column), static_cast<unsigned>(row)),
                mapColor
            );
        }
    }

    showLoading(0.62f);

    // ---- Obstacle containers ----
    std::vector<Obstacle> obstacles;
    std::size_t staticObstacleCount = 0;     // Walls etc.; props are rebuilt per game.
    std::vector<Decoration> decorations;
    std::vector<DynamicDecal> dynamicDecals;
    std::vector<EffectSprite> effectSprites;

    auto tileRect = [](int c0, int r0, int c1, int r1) -> sf::FloatRect
    {
        return sf::FloatRect(
            {static_cast<float>(c0) * TILE_F, static_cast<float>(r0) * TILE_F},
            {static_cast<float>(c1 - c0 + 1) * TILE_F, static_cast<float>(r1 - r0 + 1) * TILE_F}
        );
    };

    auto markMinimap = [&](const sf::FloatRect& rect, sf::Color color)
    {
        const int c0 = std::max(0, static_cast<int>(rect.position.x / TILE_F));
        const int r0 = std::max(0, static_cast<int>(rect.position.y / TILE_F));
        const int c1 = std::min(MAP_COLS - 1, static_cast<int>((rect.position.x + rect.size.x - 1.f) / TILE_F));
        const int r1 = std::min(MAP_ROWS - 1, static_cast<int>((rect.position.y + rect.size.y - 1.f) / TILE_F));

        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
                minimapImage.setPixel(
                    sf::Vector2u(static_cast<unsigned>(c), static_cast<unsigned>(r)), color
                );
    };

    // Solid wall made of one or more tiles (c0..c1, r0..r1 inclusive).
    auto addWallRun = [&](int c0, int r0, int c1, int r1, bool brick)
    {
        const sf::FloatRect rect = tileRect(c0, r0, c1, r1);

        Obstacle wall;
        wall.kind = ObstacleKind::Wall;
        wall.moveRect = rect;
        wall.bulletRect = rect;
        obstacles.push_back(wall);

        const bool horizontal = rect.size.x >= rect.size.y;
        const std::string file = std::string("walls_fences/") +
            (brick ? "brick_" : "concrete_") +
            (horizontal ? "horizontal.png" : "vertical.png");

        TileBatch& batch = batchFor(
            wallBatches, file, brick ? sf::Color(112, 55, 43) : sf::Color(111, 112, 108)
        );
        addTexturedQuad(batch, rect.position.x, rect.position.y,
                        rect.size.x, rect.size.y, rect.size.x, rect.size.y);

        // Soft shadow cast to the lower right.
        const sf::Color shadow(0, 0, 0, 70);
        const sf::Vertex a{{rect.position.x + 6.f, rect.position.y + 9.f}, shadow, {}};
        const sf::Vertex b{{rect.position.x + rect.size.x + 6.f, rect.position.y + 9.f}, shadow, {}};
        const sf::Vertex c{{rect.position.x + rect.size.x + 6.f, rect.position.y + rect.size.y + 9.f}, shadow, {}};
        const sf::Vertex d{{rect.position.x + 6.f, rect.position.y + rect.size.y + 9.f}, shadow, {}};
        shadowBatch.vertices.append(a); shadowBatch.vertices.append(b); shadowBatch.vertices.append(c);
        shadowBatch.vertices.append(a); shadowBatch.vertices.append(c); shadowBatch.vertices.append(d);

        markMinimap(rect, brick ? sf::Color(150, 80, 60) : sf::Color(170, 172, 168));
    };

    // Thin fence: blocks walking, and (wood only) bullets.
    auto addFenceRun = [&](int c0, int r0, int c1, int r1, bool chainlink)
    {
        const sf::FloatRect tiles = tileRect(c0, r0, c1, r1);
        const bool horizontal = tiles.size.x >= tiles.size.y;

        sf::FloatRect thin = tiles;
        if (horizontal)
        {
            thin.position.y += 9.f;
            thin.size.y = 14.f;
        }
        else
        {
            thin.position.x += 9.f;
            thin.size.x = 14.f;
        }

        Obstacle fence;
        fence.kind = ObstacleKind::Fence;
        fence.moveRect = thin;
        fence.bulletRect = thin;
        fence.blocksBullets = !chainlink;     // You can shoot through chain-link.
        obstacles.push_back(fence);

        const std::string file = std::string("walls_fences/") +
            (chainlink ? "chainlink_" : "wood_fence_") +
            (horizontal ? "horizontal.png" : "vertical.png");

        TileBatch& batch = batchFor(
            wallBatches, file, chainlink ? sf::Color(100, 111, 107) : sf::Color(160, 110, 60)
        );
        addTexturedQuad(batch, tiles.position.x, tiles.position.y,
                        tiles.size.x, tiles.size.y, tiles.size.x, tiles.size.y);
    };

    // A straight wall with door gaps cut out of it.
    auto addWallLine = [&](bool horizontal, int fixedIndex, int from, int to,
                           std::vector<GapRange> gaps, bool brick)
    {
        std::sort(gaps.begin(), gaps.end(),
                  [](const GapRange& a, const GapRange& b) { return a.from < b.from; });

        auto emit = [&](int a, int b)
        {
            if (horizontal)
                addWallRun(a, fixedIndex, b, fixedIndex, brick);
            else
                addWallRun(fixedIndex, a, fixedIndex, b, brick);
        };

        int cursor = from;
        for (const GapRange& gap : gaps)
        {
            if (gap.from > cursor)
                emit(cursor, gap.from - 1);
            cursor = std::max(cursor, gap.to + 1);
        }
        if (cursor <= to)
            emit(cursor, to);
    };

    // Hollow building: one-tile walls with doorways on any side.
    auto addBuilding = [&](int c0, int r0, int c1, int r1, bool brick,
                           const std::vector<GapRange>& north,
                           const std::vector<GapRange>& south,
                           const std::vector<GapRange>& west,
                           const std::vector<GapRange>& east)
    {
        addWallLine(true,  r0, c0, c1, north, brick);
        addWallLine(true,  r1, c0, c1, south, brick);
        addWallLine(false, c0, r0 + 1, r1 - 1, west, brick);
        addWallLine(false, c1, r0 + 1, r1 - 1, east, brick);
    };

    // ---- Static decals (road markings, potholes, stains) ----
    // Decals keep their native texture size; this variant takes it from the PNG.
    auto addNativeDecal = [&](const std::string& file, sf::Vector2f center, float angle)
    {
        TileBatch& batch = batchFor(decalBatches, file, sf::Color(0, 0, 0, 0));
        if (!batch.texture)
            return;

        const sf::Vector2u size = batch.texture->getSize();
        addRotatedQuad(batch, center, static_cast<float>(size.x), static_cast<float>(size.y),
                       angle, static_cast<float>(size.x), static_cast<float>(size.y));
    };

    auto addDecoration = [&](const std::string& file, sf::Vector2f center, bool overhead,
                             float rotation = 0.f)
    {
        const sf::Texture* texture = envTexture(file);
        if (!texture)
            return;

        Decoration decoration;
        decoration.texture = texture;
        decoration.position = center;
        decoration.size = sf::Vector2f(texture->getSize());
        decoration.rotation = rotation;
        decoration.overhead = overhead;
        decorations.push_back(decoration);
    };

    // ==================================================
    // MAP 1 - ABANDONED CITY: STATIC LAYOUT
    // ==================================================
    {
        // Outer boundary walls.
        addWallRun(0, 0, MAP_COLS - 1, 0, false);
        addWallRun(0, MAP_ROWS - 1, MAP_COLS - 1, MAP_ROWS - 1, false);
        addWallRun(0, 1, 0, MAP_ROWS - 2, false);
        addWallRun(MAP_COLS - 1, 1, MAP_COLS - 1, MAP_ROWS - 2, false);

        // Four buildings, each with doorways facing the streets.
        addBuilding(5, 5, 21, 18, true,    {},            {{12, 14}}, {},          {{11, 13}});   // NW
        addBuilding(56, 5, 73, 18, true,   {},            {{63, 65}}, {{11, 13}},  {});           // NE
        addBuilding(5, 42, 21, 54, false,  {{12, 14}},    {},         {},          {{47, 49}});   // SW
        addBuilding(57, 42, 73, 54, false, {{64, 66}},    {},         {{47, 49}},  {});           // SE

        // Interior walls and pillars.
        addWallRun(6, 11, 10, 11, false);     // NW: room divider
        addWallRun(14, 6, 14, 9, false);      // NW: room divider
        addWallRun(61, 10, 62, 11, false);    // NE: pillars
        addWallRun(67, 10, 68, 11, false);
        addWallRun(61, 14, 62, 15, false);
        addWallRun(67, 14, 68, 15, false);
        addWallRun(9, 47, 10, 48, false);     // SW: ruined walls around the courtyard
        addWallRun(60, 51, 63, 51, false);    // SE: wall hiding the supply corner

        // Chain-link and wooden fences with gaps to run through.
        addFenceRun(31, 1, 31, 9, true);
        addFenceRun(31, 13, 31, 21, true);
        addFenceRun(48, 1, 48, 9, true);
        addFenceRun(48, 13, 48, 21, true);
        addFenceRun(1, 20, 8, 20, false);
        addFenceRun(26, 38, 31, 38, true);
        addFenceRun(50, 38, 56, 38, false);
        addFenceRun(22, 57, 31, 57, true);

        // ---- Road markings ----
        // Centre lines. (lane_dashes shows a vertical yellow dash.)
        for (int r = 1; r <= 22; ++r)
            addNativeDecal("roads_markings/lane_dashes.png",
                           {40.f * TILE_F, static_cast<float>(r) * TILE_F + 16.f}, 0.f);
        for (int r = 37; r <= 58; ++r)
            addNativeDecal("roads_markings/lane_dashes.png",
                           {40.f * TILE_F, static_cast<float>(r) * TILE_F + 16.f}, 0.f);
        for (int c = 1; c <= 31; ++c)
            addNativeDecal("roads_markings/lane_dashes.png",
                           {static_cast<float>(c) * TILE_F + 16.f, 30.f * TILE_F}, 90.f);
        for (int c = 48; c <= 78; ++c)
            addNativeDecal("roads_markings/lane_dashes.png",
                           {static_cast<float>(c) * TILE_F + 16.f, 30.f * TILE_F}, 90.f);

        // Crosswalks at the crossroads.
        for (int r = 25; r <= 34; ++r)
        {
            addNativeDecal("roads_markings/crosswalk.png", {28.f * TILE_F + 16.f, static_cast<float>(r) * TILE_F + 16.f}, 0.f);
            addNativeDecal("roads_markings/crosswalk.png", {51.f * TILE_F + 16.f, static_cast<float>(r) * TILE_F + 16.f}, 0.f);
        }
        for (int c = 34; c <= 45; ++c)
        {
            addNativeDecal("roads_markings/crosswalk.png", {static_cast<float>(c) * TILE_F + 16.f, 20.f * TILE_F + 16.f}, 90.f);
            addNativeDecal("roads_markings/crosswalk.png", {static_cast<float>(c) * TILE_F + 16.f, 39.f * TILE_F + 16.f}, 90.f);
        }

        // Arrows, manholes and potholes.
        addNativeDecal("roads_markings/arrow_straight.png", {36.f * TILE_F + 16.f, 14.f * TILE_F + 16.f}, 180.f);
        addNativeDecal("roads_markings/arrow_straight.png", {43.f * TILE_F + 16.f, 14.f * TILE_F + 16.f}, 0.f);
        addNativeDecal("roads_markings/arrow_straight.png", {36.f * TILE_F + 16.f, 46.f * TILE_F + 16.f}, 180.f);
        addNativeDecal("roads_markings/arrow_straight.png", {43.f * TILE_F + 16.f, 46.f * TILE_F + 16.f}, 0.f);
        addNativeDecal("roads_markings/manhole.png", {1230.f, 900.f}, 0.f);
        addNativeDecal("roads_markings/manhole.png", {1340.f, 1040.f}, 0.f);
        addNativeDecal("roads_markings/manhole.png", {600.f, 1060.f}, 0.f);
        addNativeDecal("roads_markings/manhole.png", {1960.f, 860.f}, 0.f);

        std::mt19937 mapRandom(20260519u);
        auto mapUnit = [&]() { return std::uniform_real_distribution<float>(0.f, 1.f)(mapRandom); };

        auto groundAt = [&](float x, float y) -> char
        {
            const int c = std::clamp(static_cast<int>(x / TILE_F), 0, MAP_COLS - 1);
            const int r = std::clamp(static_cast<int>(y / TILE_F), 0, MAP_ROWS - 1);
            return GROUND_MAP[r][c];
        };

        auto freeOfWalls = [&](sf::Vector2f p, float radius) -> bool
        {
            for (const Obstacle& o : obstacles)
                if (circleOverlapsRect(p, radius, o.moveRect))
                    return false;
            return true;
        };

        // Scatter: potholes and glass on roads, bushes on grass, stains and debris.
        for (int i = 0; i < 26; ++i)
        {
            const sf::Vector2f p(80.f + mapUnit() * (WORLD_W - 160.f), 80.f + mapUnit() * (WORLD_H - 160.f));
            const char ground = groundAt(p.x, p.y);

            if (!freeOfWalls(p, 22.f))
                continue;

            if (ground == 'a')
                addNativeDecal(mapUnit() < 0.65f ? "decorations/pothole.png" : "decorations/broken_glass.png",
                               p, mapUnit() * 360.f);
            else if (ground == 's' || ground == 'c')
                addNativeDecal(mapUnit() < 0.5f ? "decorations/blood_pool.png" : "decorations/broken_glass.png",
                               p, mapUnit() * 360.f);
        }

        for (int i = 0; i < 70; ++i)
        {
            const sf::Vector2f p(80.f + mapUnit() * (WORLD_W - 160.f), 80.f + mapUnit() * (WORLD_H - 160.f));

            if (groundAt(p.x, p.y) == 'g' && freeOfWalls(p, 24.f))
                addDecoration("decorations/dead_bush.png", p, false);
        }

        // Street lamps along the sidewalks.
        for (int r : {3, 10, 17, 40, 47, 54})
        {
            addDecoration("decorations/street_lamp.png", {32.f * TILE_F - 6.f, static_cast<float>(r) * TILE_F}, true);
            addDecoration("decorations/street_lamp.png", {48.f * TILE_F + 6.f, static_cast<float>(r) * TILE_F}, true);
        }
        for (int c : {10, 20, 27, 52, 60, 70})
        {
            addDecoration("decorations/street_lamp.png", {static_cast<float>(c) * TILE_F, 24.f * TILE_F - 4.f}, true);
            addDecoration("decorations/street_lamp.png", {static_cast<float>(c) * TILE_F, 36.f * TILE_F + 4.f}, true);
        }

        // Details around doors, roadblocks and corners.
        addDecoration("decorations/trash_bin.png", {24.f * TILE_F, 19.f * TILE_F + 6.f}, false);
        addDecoration("decorations/trash_bin.png", {54.f * TILE_F, 19.f * TILE_F + 6.f}, false);
        addDecoration("decorations/trash_bin.png", {24.f * TILE_F, 41.f * TILE_F}, false);
        addDecoration("decorations/trash_bin.png", {75.f * TILE_F, 41.f * TILE_F}, false);
        addDecoration("decorations/street_sign.png", {33.f * TILE_F, 23.f * TILE_F + 10.f}, true);
        addDecoration("decorations/street_sign.png", {46.f * TILE_F, 36.f * TILE_F + 10.f}, true);
        addDecoration("decorations/warning_sign.png", {1100.f, 372.f}, true);
        addDecoration("decorations/warning_sign.png", {1460.f, 372.f}, true);
        addDecoration("decorations/debris_pile.png", {7.f * TILE_F, 22.f * TILE_F}, false);
        addDecoration("decorations/debris_pile.png", {70.f * TILE_F, 22.f * TILE_F}, false);
        addDecoration("decorations/debris_pile.png", {20.f * TILE_F, 36.f * TILE_F + 20.f}, false);
        addDecoration("decorations/debris_pile.png", {1500.f, 1100.f}, false);
        for (const sf::Vector2f p : {sf::Vector2f(1235.f, 392.f), sf::Vector2f(1325.f, 392.f),
                                      sf::Vector2f(1235.f, 1656.f), sf::Vector2f(1325.f, 1656.f)})
            addDecoration("decorations/traffic_cone.png", p, false);
    }

    staticObstacleCount = obstacles.size();
    showLoading(0.78f);

    // ---- Minimap texture (one pixel per tile) ----
    sf::Texture minimapTexture;
    if (!minimapTexture.loadFromImage(minimapImage))
        std::cerr << "Could not create the minimap texture\n";
    minimapTexture.setSmooth(false);

    // ---- Zombie and supply zones ----
    // The four building zones come from the map data; four more are the road
    // entrances, so waves also arrive down the streets.
    const std::vector<sf::FloatRect> zombieZones = {
        sf::FloatRect({160.f, 160.f},   {480.f, 320.f}),    // NW building
        sf::FloatRect({1840.f, 160.f},  {480.f, 320.f}),    // NE building
        sf::FloatRect({160.f, 1400.f},  {480.f, 320.f}),    // SW courtyard
        sf::FloatRect({1840.f, 1400.f}, {480.f, 320.f}),    // SE building
        sf::FloatRect({1120.f, 80.f},   {320.f, 150.f}),    // North road
        sf::FloatRect({1120.f, 1700.f}, {320.f, 150.f}),    // South road
        sf::FloatRect({80.f, 830.f},    {150.f, 260.f}),    // West road
        sf::FloatRect({2330.f, 830.f},  {150.f, 260.f})     // East road
    };
    const sf::FloatRect supplyZone({1800.f, 1360.f}, {320.f, 256.f});

    // ---- Camera ----
    const sf::Vector2f viewSize(WorldConfig::VIEW_W, WorldConfig::VIEW_H);
    sf::View worldView(viewSize * 0.5f, viewSize);   // Used for drawing (with shake).
    sf::View aimView(viewSize * 0.5f, viewSize);     // Used for the mouse (no shake).

    // Keeps the 4:3 picture undistorted in any window / fullscreen size.
    auto applyLetterbox = [&]()
    {
        const sf::Vector2u size = window.getSize();
        if (size.x == 0u || size.y == 0u)
            return;

        const float windowRatio = static_cast<float>(size.x) / static_cast<float>(size.y);
        const float targetRatio = 800.f / 600.f;

        sf::FloatRect viewport({0.f, 0.f}, {1.f, 1.f});
        if (windowRatio > targetRatio)
        {
            viewport.size.x = targetRatio / windowRatio;
            viewport.position.x = (1.f - viewport.size.x) * 0.5f;
        }
        else
        {
            viewport.size.y = windowRatio / targetRatio;
            viewport.position.y = (1.f - viewport.size.y) * 0.5f;
        }

        screenView.setViewport(viewport);
        worldView.setViewport(viewport);
        aimView.setViewport(viewport);
    };
    applyLetterbox();
    sf::Vector2f cameraCenter(WorldConfig::PLAYER_SPAWN_X, WorldConfig::PLAYER_SPAWN_Y);
    float cameraShake = 0.f;

    // ---- Collision queries ----
    auto isPositionFree = [&](sf::Vector2f position, float radius) -> bool
    {
        if (position.x < radius || position.y < radius ||
            position.x > WORLD_W - radius || position.y > WORLD_H - radius)
            return false;

        for (const Obstacle& o : obstacles)
            if (o.alive && o.blocksMovement && circleOverlapsRect(position, radius, o.moveRect))
                return false;

        return true;
    };

    // Moves a circle by `delta`, sliding along anything solid.
    auto moveActor = [&](sf::Vector2f position, sf::Vector2f delta, float radius) -> sf::Vector2f
    {
        const float length = vectorLength(delta);
        const int steps = std::max(1, static_cast<int>(std::ceil(length / (radius * 0.8f))));
        const sf::Vector2f step = delta / static_cast<float>(steps);

        for (int s = 0; s < steps; ++s)
        {
            position += step;

            for (int iteration = 0; iteration < 3; ++iteration)
            {
                bool pushed = false;

                for (const Obstacle& o : obstacles)
                {
                    if (!o.alive || !o.blocksMovement)
                        continue;

                    if (position.x < o.moveRect.position.x - radius ||
                        position.x > o.moveRect.position.x + o.moveRect.size.x + radius ||
                        position.y < o.moveRect.position.y - radius ||
                        position.y > o.moveRect.position.y + o.moveRect.size.y + radius)
                        continue;

                    if (pushCircleOutOfRect(position, radius, o.moveRect))
                        pushed = true;
                }

                if (!pushed)
                    break;
            }

            position.x = std::clamp(position.x, radius, WORLD_W - radius);
            position.y = std::clamp(position.y, radius, WORLD_H - radius);
        }

        return position;
    };

    // True if nothing solid stands between a and b (walking line of sight).
    auto lineClearMove = [&](sf::Vector2f a, sf::Vector2f b, float radius) -> bool
    {
        for (const Obstacle& o : obstacles)
        {
            if (!o.alive || !o.blocksMovement)
                continue;

            float t = 0.f;
            if (segmentIntersectsRect(a, b, inflatedRect(o.moveRect, radius), t))
                return false;
        }

        return true;
    };

    // ---- Navigation: a distance field that flows towards the player ----
    // Cells blocked by walls and props (plus a safety margin) are marked, then
    // one Dijkstra search from the player's cell gives every zombie the
    // shortest walkable route. One search serves all zombies and is rebuilt
    // whenever the player changes cell or a crate/barrel is destroyed.
    constexpr float NAV_INFINITY = 1.0e9f;
    std::vector<unsigned char> navBlocked(static_cast<std::size_t>(MAP_COLS * MAP_ROWS), 0);
    std::vector<float> navDistance(static_cast<std::size_t>(MAP_COLS * MAP_ROWS), NAV_INFINITY);
    bool navDirty = true;
    int navLastPlayerCell = -1;
    float navCooldown = 0.f;

    auto navColumn = [&](float x) { return std::clamp(static_cast<int>(x / TILE_F), 0, MAP_COLS - 1); };
    auto navRow    = [&](float y) { return std::clamp(static_cast<int>(y / TILE_F), 0, MAP_ROWS - 1); };
    auto navIndex  = [&](int c, int r) { return r * MAP_COLS + c; };

    auto rebuildNavGrid = [&]()
    {
        std::fill(navBlocked.begin(), navBlocked.end(), static_cast<unsigned char>(0));

        for (const Obstacle& o : obstacles)
        {
            if (!o.alive || !o.blocksMovement)
                continue;

            const sf::FloatRect rect = inflatedRect(o.moveRect, WorldConfig::NAV_MARGIN);
            const int c0 = navColumn(rect.position.x);
            const int c1 = navColumn(rect.position.x + rect.size.x - 0.01f);
            const int r0 = navRow(rect.position.y);
            const int r1 = navRow(rect.position.y + rect.size.y - 0.01f);

            for (int r = r0; r <= r1; ++r)
                for (int c = c0; c <= c1; ++c)
                    navBlocked[static_cast<std::size_t>(navIndex(c, r))] = 1;
        }
    };

    auto computeFlowField = [&]()
    {
        std::fill(navDistance.begin(), navDistance.end(), NAV_INFINITY);

        int startC = navColumn(playerPosition.x);
        int startR = navRow(playerPosition.y);

        // The player may stand in a "blocked" margin cell: use the nearest free one.
        if (navBlocked[static_cast<std::size_t>(navIndex(startC, startR))])
        {
            bool found = false;
            for (int radius = 1; radius <= 4 && !found; ++radius)
            {
                for (int dr = -radius; dr <= radius && !found; ++dr)
                {
                    for (int dc = -radius; dc <= radius && !found; ++dc)
                    {
                        const int c = startC + dc;
                        const int r = startR + dr;
                        if (c < 0 || r < 0 || c >= MAP_COLS || r >= MAP_ROWS)
                            continue;
                        if (!navBlocked[static_cast<std::size_t>(navIndex(c, r))])
                        {
                            startC = c;
                            startR = r;
                            found = true;
                        }
                    }
                }
            }

            if (!found)
                return;
        }

        using QueueEntry = std::pair<float, int>;
        std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> open;

        navDistance[static_cast<std::size_t>(navIndex(startC, startR))] = 0.f;
        open.push({0.f, navIndex(startC, startR)});

        while (!open.empty())
        {
            const auto [distance, index] = open.top();
            open.pop();

            if (distance > navDistance[static_cast<std::size_t>(index)])
                continue;

            const int c = index % MAP_COLS;
            const int r = index / MAP_COLS;

            for (int dr = -1; dr <= 1; ++dr)
            {
                for (int dc = -1; dc <= 1; ++dc)
                {
                    if (dc == 0 && dr == 0)
                        continue;

                    const int nc = c + dc;
                    const int nr = r + dr;
                    if (nc < 0 || nr < 0 || nc >= MAP_COLS || nr >= MAP_ROWS)
                        continue;
                    if (navBlocked[static_cast<std::size_t>(navIndex(nc, nr))])
                        continue;

                    // No cutting corners through wall edges.
                    if (dc != 0 && dr != 0 &&
                        (navBlocked[static_cast<std::size_t>(navIndex(c + dc, r))] ||
                         navBlocked[static_cast<std::size_t>(navIndex(c, r + dr))]))
                        continue;

                    const float stepCost = (dc != 0 && dr != 0) ? 1.4142f : 1.f;
                    const float candidate = distance + stepCost;
                    const int neighbour = navIndex(nc, nr);

                    if (candidate < navDistance[static_cast<std::size_t>(neighbour)])
                    {
                        navDistance[static_cast<std::size_t>(neighbour)] = candidate;
                        open.push({candidate, neighbour});
                    }
                }
            }
        }
    };

    // Called every frame: keeps the field up to date (cheap when nothing changed).
    auto updateNavigation = [&](float dt)
    {
        navCooldown = std::max(0.f, navCooldown - dt);

        const int playerCell = navIndex(navColumn(playerPosition.x), navRow(playerPosition.y));

        if (navDirty)
        {
            rebuildNavGrid();
            navDirty = false;
            navLastPlayerCell = -1;   // Force a new search.
        }

        if (playerCell != navLastPlayerCell && (navCooldown <= 0.f || navLastPlayerCell < 0))
        {
            computeFlowField();
            navLastPlayerCell = playerCell;
            navCooldown = 0.08f;
        }
    };

    // Can a zombie standing here eventually walk to the player?
    auto isReachable = [&](sf::Vector2f position) -> bool
    {
        if (navLastPlayerCell < 0)
            return true;   // Field not built yet.

        return navDistance[static_cast<std::size_t>(
            navIndex(navColumn(position.x), navRow(position.y)))] < NAV_INFINITY;
    };

    // Direction a zombie should walk to reach the player around obstacles.
    auto zombieChaseDirection = [&](const sf::Vector2f& position) -> sf::Vector2f
    {
        const sf::Vector2f toPlayer = playerPosition - position;
        const float walkRadius = WorldConfig::ZOMBIE_RADIUS * 0.85f;

        // Clear path: walk straight at the player.
        if (lineClearMove(position, playerPosition, walkRadius))
            return normalized(toPlayer);

        int c = navColumn(position.x);
        int r = navRow(position.y);
        sf::Vector2f target = playerPosition;
        bool haveTarget = false;

        // Follow the downhill path up to six cells and head for the farthest
        // waypoint that can be walked to in a straight line (smooth movement).
        for (int hop = 0; hop < 6; ++hop)
        {
            const float here = navDistance[static_cast<std::size_t>(navIndex(c, r))];
            float bestDistance = here;
            int bestC = -1;
            int bestR = -1;

            const int reach = (here >= NAV_INFINITY) ? 2 : 1;   // Blocked cell: look further.

            for (int dr = -reach; dr <= reach; ++dr)
            {
                for (int dc = -reach; dc <= reach; ++dc)
                {
                    if (dc == 0 && dr == 0)
                        continue;

                    const int nc = c + dc;
                    const int nr = r + dr;
                    if (nc < 0 || nr < 0 || nc >= MAP_COLS || nr >= MAP_ROWS)
                        continue;

                    const float candidate = navDistance[static_cast<std::size_t>(navIndex(nc, nr))];

                    if (candidate < bestDistance)
                    {
                        if (reach == 1 && dc != 0 && dr != 0 &&
                            (navBlocked[static_cast<std::size_t>(navIndex(c + dc, r))] ||
                             navBlocked[static_cast<std::size_t>(navIndex(c, r + dr))]))
                            continue;

                        bestDistance = candidate;
                        bestC = nc;
                        bestR = nr;
                    }
                }
            }

            if (bestC < 0)
                break;

            c = bestC;
            r = bestR;

            const sf::Vector2f waypoint(
                (static_cast<float>(c) + 0.5f) * TILE_F,
                (static_cast<float>(r) + 0.5f) * TILE_F
            );

            if (!haveTarget || lineClearMove(position, waypoint, walkRadius))
            {
                target = waypoint;
                haveTarget = true;
            }
            else
                break;

            if (bestDistance <= 0.f)
                break;   // Reached the player's cell.
        }

        if (!haveTarget)
            return normalized(toPlayer);

        // Heading for a waypoint that is the final (player) cell: go to the player.
        if (vectorLength(target - position) < 6.f)
            return normalized(toPlayer);

        return normalized(target - position);
    };

    // A free, reachable spot at or near `wanted` (used by pickups).
    auto findFreeSpot = [&](sf::Vector2f wanted, float radius, sf::Vector2f& result) -> bool
    {
        auto acceptable = [&](sf::Vector2f p)
        {
            return isPositionFree(p, radius) && isReachable(p);
        };

        if (acceptable(wanted))
        {
            result = wanted;
            return true;
        }

        for (float ring = 20.f; ring <= 120.f; ring += 20.f)
        {
            for (int k = 0; k < 10; ++k)
            {
                const float angle = (static_cast<float>(k) / 10.f) * 2.f * PI;
                const sf::Vector2f p = wanted + sf::Vector2f(std::cos(angle), std::sin(angle)) * ring;

                if (acceptable(p))
                {
                    result = p;
                    return true;
                }
            }
        }

        return false;
    };

    // ---- Props: crates, barrels, cars, barricades (rebuilt every game) ----
    auto makeProp = [&](ObstacleKind kind, const std::string& file, sf::Vector2f spriteCenter,
                        float rotation, sf::FloatRect footprint, sf::FloatRect visual) -> Obstacle&
    {
        Obstacle prop;
        prop.kind = kind;
        prop.texture = envTexture(file);
        prop.spriteCenter = spriteCenter;
        prop.rotation = rotation;
        prop.spriteSize = prop.texture ? sf::Vector2f(prop.texture->getSize()) : visual.size;
        prop.moveRect = footprint;

        // Small props stop bullets where they LOOK like they are: the visible
        // rectangle moved down onto the ground plane the bullets fly on.
        prop.bulletRect = sf::FloatRect(
            {visual.position.x, visual.position.y + WorldConfig::BULLET_PLANE_OFFSET},
            visual.size
        );

        obstacles.push_back(prop);
        return obstacles.back();
    };

    auto addCrateSmall = [&](float cx, float cy)
    {
        Obstacle& o = makeProp(ObstacleKind::CrateSmall, "obstacles/crate_small.png", {cx, cy}, 0.f,
            sf::FloatRect({cx - 15.f, cy - 15.f}, {30.f, 30.f}),
            sf::FloatRect({cx - 16.f, cy - 16.f}, {32.f, 32.f}));
        o.hp = o.maxHp = WorldConfig::CRATE_SMALL_HP;
        o.damagedTexture = envTexture("obstacles/crate_damaged.png");
    };

    auto addCrateLarge = [&](float cx, float cy)
    {
        Obstacle& o = makeProp(ObstacleKind::CrateLarge, "obstacles/crate_large.png", {cx, cy}, 0.f,
            sf::FloatRect({cx - 29.f, cy - 29.f}, {58.f, 58.f}),
            sf::FloatRect({cx - 30.f, cy - 30.f}, {60.f, 60.f}));
        o.hp = o.maxHp = WorldConfig::CRATE_LARGE_HP;
    };

    // (cx, cy) is the point on the ground the barrel stands on.
    auto addBarrel = [&](float cx, float cy, bool explosive, const std::string& file)
    {
        Obstacle& o = makeProp(
            explosive ? ObstacleKind::BarrelExplosive : ObstacleKind::BarrelSolid,
            file, {cx, cy - 12.f}, 0.f,
            sf::FloatRect({cx - 12.f, cy - 4.f}, {24.f, 15.f}),
            sf::FloatRect({cx - 15.f, cy - 34.f}, {30.f, 44.f}));
        if (explosive)
        {
            o.hp = o.maxHp = 1.f;
            o.explosive = true;
        }
    };

    auto addCar = [&](float cx, float cy, const std::string& file, bool vertical)
    {
        const sf::FloatRect hull = vertical
            ? sf::FloatRect({cx - 21.f, cy - 41.f}, {42.f, 82.f})
            : sf::FloatRect({cx - 41.f, cy - 21.f}, {82.f, 42.f});
        makeProp(ObstacleKind::Vehicle, file, {cx, cy}, vertical ? 90.f : 0.f, hull, hull);
    };

    auto addBarricade = [&](float cx, float cy)
    {
        const sf::FloatRect shape({cx - 31.f, cy - 11.f}, {62.f, 22.f});
        makeProp(ObstacleKind::Barricade, "obstacles/road_barricade.png", {cx, cy}, 0.f, shape, shape);
    };

    auto addSandbags = [&](float cx, float cy)
    {
        const sf::FloatRect shape({cx - 28.f, cy - 22.f}, {56.f, 44.f});
        makeProp(ObstacleKind::Sandbags, "obstacles/sandbag_cover.png", {cx, cy}, 0.f, shape, shape);
    };

    auto addUtilityBox = [&](float cx, float cy)
    {
        const sf::FloatRect shape({cx - 16.f, cy - 12.f}, {32.f, 26.f});
        makeProp(ObstacleKind::UtilityBox, "decorations/utility_box.png", {cx, cy}, 0.f, shape, shape);
    };

    auto buildDynamicProps = [&]()
    {
        const float T = TILE_F;

        // ---- NW building: storage hall ----
        addCrateLarge(8.f * T, 8.f * T);
        addCrateSmall(10.f * T, 7.f * T);
        addCrateSmall(10.f * T, 8.f * T);
        addBarrel(6.5f * T, 16.f * T, true, "obstacles/barrel_red.png");
        addBarrel(7.5f * T, 16.5f * T, true, "obstacles/barrel_red.png");
        addBarrel(19.f * T, 6.5f * T, false, "obstacles/barrel_blue.png");
        addCrateLarge(17.f * T, 15.f * T);
        addCrateSmall(19.f * T, 15.5f * T);

        // ---- NE building: pillar hall ----
        addBarrel(64.f * T, 7.5f * T, true, "obstacles/barrel_red.png");
        addBarrel(65.f * T, 8.f * T, true, "obstacles/barrel_red.png");
        addCrateSmall(58.f * T, 7.f * T);
        addCrateSmall(70.f * T, 16.f * T);
        addCrateLarge(70.f * T, 8.f * T);
        addBarrel(58.f * T, 16.f * T, false, "obstacles/barrel_rusted.png");

        // ---- SW building: ruined courtyard (keep the doors and a central lane clear) ----
        addCar(9.f * T, 51.f * T, "vehicles/car_wreck.png", false);
        addCar(18.f * T, 50.f * T, "vehicles/car_sedan.png", true);
        addSandbags(17.f * T, 45.f * T);
        addBarricade(13.f * T, 52.5f * T);
        addBarrel(7.f * T, 44.5f * T, true, "obstacles/barrel_red.png");
        addCrateSmall(20.f * T, 44.f * T);

        // ---- SE building: supply depot (the clear area is the supply corner) ----
        addCrateLarge(69.f * T, 44.f * T);
        addCrateLarge(71.f * T + 8.f, 46.f * T);
        addCrateSmall(69.f * T, 47.f * T);
        addSandbags(62.f * T, 45.f * T);
        addBarrel(71.f * T, 52.f * T, true, "obstacles/barrel_red.png");
        addBarrel(72.f * T, 51.5f * T, true, "obstacles/barrel_red.png");
        addBarrel(68.f * T, 52.f * T, false, "obstacles/barrel_blue.png");
        addUtilityBox(59.f * T, 53.f * T);

        // ---- Parking lot (west of the southern road) ----
        addCar(880.f, 1330.f, "vehicles/car_sedan.png", true);
        addCar(880.f, 1490.f, "vehicles/car_red.png", true);
        addCar(880.f, 1670.f, "vehicles/car_wreck.png", true);
        addCar(976.f, 1400.f, "vehicles/car_wreck.png", true);
        addCar(976.f, 1580.f, "vehicles/car_sedan.png", true);
        addCar(976.f, 1730.f, "vehicles/car_red.png", true);

        // ---- Roadblocks with a gap in the middle ----
        addBarricade(1136.f, 416.f);
        addBarricade(1200.f, 416.f);
        addBarricade(1360.f, 416.f);
        addBarricade(1424.f, 416.f);
        addSandbags(1180.f, 1680.f);
        addSandbags(1380.f, 1680.f);

        // ---- The open square at the crossroads: cover, not clutter ----
        addCar(1100.f, 880.f, "vehicles/car_wreck.png", false);
        addCar(1450.f, 1060.f, "vehicles/car_red.png", true);
        addSandbags(1180.f, 1030.f);
        addSandbags(1390.f, 900.f);
        addBarrel(1320.f, 840.f, true, "obstacles/barrel_red.png");
        addCrateSmall(1220.f, 1100.f);

        // ---- West and east streets ----
        addCar(520.f, 880.f, "vehicles/car_sedan.png", false);
        addCar(760.f, 1040.f, "vehicles/car_wreck.png", false);
        addCar(380.f, 1030.f, "vehicles/car_red.png", false);
        addCar(1820.f, 890.f, "vehicles/car_sedan.png", false);
        addCar(2080.f, 1030.f, "vehicles/car_wreck.png", false);
        addCar(1950.f, 850.f, "vehicles/car_red.png", false);
        addBarrel(2250.f, 900.f, true, "obstacles/barrel_red.png");
        addCrateLarge(640.f, 1100.f);

        // ---- Sidewalk and lawn clutter ----
        addUtilityBox(5.f * T + 16.f, 22.f * T);
        addUtilityBox(74.f * T, 20.f * T);
        addCrateSmall(26.f * T, 21.f * T);
        addCrateSmall(53.f * T, 21.f * T);
        addBarrel(36.f * T, 40.f * T, true, "obstacles/barrel_red.png");
        addBarrel(43.f * T, 55.f * T, false, "obstacles/barrel_rusted.png");
    };

    auto resetDynamicWorld = [&]()
    {
        obstacles.resize(staticObstacleCount);
        buildDynamicProps();
        dynamicDecals.clear();
        effectSprites.clear();
        cameraShake = 0.f;
        navDirty = true;
        navLastPlayerCell = -1;
    };

    resetDynamicWorld();

    // --------------------------------------------------
    // 9c. SMART SPAWN LOCATIONS
    // --------------------------------------------------
    // Random points inside the map's zombie zones are scored and the best wins:
    //   + valid: on free ground, able to reach the player, OFF-SCREEN
    //   + about 750 px away: close enough to keep the pressure on
    //   + far from other living zombies (no clumping)
    //   - same zone as the previous spawn (zombies come from all directions)
    //   + a little random jitter so it never feels predictable
    struct SpawnPoint
    {
        sf::Vector2f position;
        int side = 0;
    };

    auto chooseSpawnPoint = [&]() -> SpawnPoint
    {
        SpawnPoint best;
        best.position = zombieZones[4].position + zombieZones[4].size * 0.5f;
        best.side = 4;
        float bestScore = -1.0e9f;
        bool foundSafePoint = false;

        std::uniform_real_distribution<float> zoneRoll(0.f, 1.f);
        const float minDistance = std::max(
            WaveConfig::MIN_SPAWN_PLAYER_DIST, WorldConfig::MIN_ZOMBIE_SPAWN_DISTANCE
        );

        for (int attempt = 0; attempt < WaveConfig::SPAWN_CANDIDATES * 4; ++attempt)
        {
            SpawnPoint candidate;
            candidate.side = static_cast<int>(randomEngine() % zombieZones.size());

            const sf::FloatRect& area = zombieZones[static_cast<std::size_t>(candidate.side)];
            candidate.position = {
                area.position.x + zoneRoll(randomEngine) * area.size.x,
                area.position.y + zoneRoll(randomEngine) * area.size.y
            };

            // Never inside walls or props, and the player must be reachable.
            if (!isPositionFree(candidate.position, WorldConfig::ZOMBIE_RADIUS + 4.f) ||
                !isReachable(candidate.position))
                continue;

            const float distanceToPlayer =
                vectorLength(candidate.position - playerPosition);

            // Real restriction: spawn out of sight, never next to the player.
            if (distanceToPlayer < minDistance)
                continue;

            float nearestZombie = 250.f;
            for (const Zombie& other : zombies)
            {
                if (other.isDying)
                    continue;

                nearestZombie = std::min(
                    nearestZombie,
                    vectorLength(candidate.position - other.position)
                );
            }

            float candidateScore =
                -0.6f * std::abs(distanceToPlayer - 750.f) +
                0.6f * nearestZombie +
                spawnJitter(randomEngine);

            if (candidate.side == lastSpawnSide)
                candidateScore -= 90.f;

            if (!foundSafePoint || candidateScore > bestScore)
            {
                bestScore = candidateScore;
                best = candidate;
                foundSafePoint = true;
            }
        }

        // Fallback: any free, reachable point somewhere on the map.
        for (int attempt = 0; attempt < 300 && !foundSafePoint; ++attempt)
        {
            const sf::Vector2f point(
                80.f + zoneRoll(randomEngine) * (WORLD_W - 160.f),
                80.f + zoneRoll(randomEngine) * (WORLD_H - 160.f)
            );

            if (isPositionFree(point, WorldConfig::ZOMBIE_RADIUS + 4.f) &&
                isReachable(point) &&
                vectorLength(point - playerPosition) >= 420.f)
            {
                best.position = point;
                best.side = 0;
                foundSafePoint = true;
            }
        }

        return best;
    };

    auto countLivingZombies = [&]() -> std::size_t
    {
        // Dying zombies don't count toward the live-zombie cap.
        return static_cast<std::size_t>(
            std::count_if(
                zombies.begin(),
                zombies.end(),
                [](const Zombie& z) { return !z.isDying; }
            )
        );
    };

    // --------------------------------------------------
    // 10. PLAYER HEALTH AND SCORE
    // --------------------------------------------------
    float playerHealth = 100.f;
    float playerDamageCooldown = 0.f;
    bool gameOver = false;

    int score = 0;
    std::array<int, 3> highScores = {   // Best score per difficulty (saved between sessions).
        static_cast<int>(std::min<std::uint32_t>(difficultyBests.score[0], 2000000000u)),
        static_cast<int>(std::min<std::uint32_t>(difficultyBests.score[1], 2000000000u)),
        static_cast<int>(std::min<std::uint32_t>(difficultyBests.score[2], 2000000000u))
    };

    // Difficulty selection / main menu.
    bool inMenu = true;
    int difficultyIndex = std::clamp<int>(gameSettings.difficulty, 0, 2);   // Saved preference.
    int menuSelection = playerData.hasSavedRun ? 6 : difficultyIndex;

    // Phase 5 UI state.
    enum class SubPage { Root, Settings, Controls, Profile, Credits };
    SubPage subPage = SubPage::Root;       // Root = main menu / pause menu itself.
    bool paused = false;
    int pauseSelection = 0;
    int settingsSelection = 0;
    int gameOverSelection = 0;
    int draggingSlider = -1;               // Settings slider being dragged by the mouse.
    std::array<float, 10> buttonHover{};    // Smooth 0..1 hover amount per button.
    std::array<float, 10> buttonPress{};    // Click flash per button.
    float uiTime = 0.f;                    // Always runs (menus, pause) - for UI animation.
    float screenFade = 1.f;                // 1 = black, fades to 0 after every state change.
    float gameOverTimer = 0.f;
    float damageFlash = 0.f;               // Red flash when the player is hit.
    float displayHealth = 100.f;           // Lagging "damage trail" on the health bar.
    float hitMarkerTimer = 0.f;
    bool hitMarkerKill = false;
    float crosshairKick = 0.f;             // Crosshair opens up after a shot.
    int totalZombiesKilled = 0;
    int runCoins = 0;                      // Phase 7: coins collected this run.
    double runSurvivalSeconds = 0.0;       // Phase 7: time alive this run.
    bool cursorHidden = false;
    sf::Vector2f lastCameraShake(0.f, 0.f);
    std::vector<FloatingText> damageNumbers;

    struct Notice
    {
        std::string text;
        sf::Color color;
        float life;
    };
    std::vector<Notice> notices;           // Kill / power-up feed.

    sf::RectangleShape playerHealthBackground({160.f, 12.f});
    playerHealthBackground.setPosition({20.f, 20.f});
    playerHealthBackground.setFillColor(sf::Color(75, 30, 30));

    sf::RectangleShape playerHealthForeground({160.f, 12.f});
    playerHealthForeground.setPosition({20.f, 20.f});
    playerHealthForeground.setFillColor(sf::Color(75, 205, 90));

    // --------------------------------------------------
    // 9d. WEAPON SYSTEM STATE
    // --------------------------------------------------
    struct WeaponState
    {
        int magazine = 0;
        int reserve = 0;
    };

    std::array<WeaponState, WEAPON_COUNT> weaponStates;
    std::array<bool, WEAPON_COUNT> weaponOwned{};   // Weapons the player has found.
    int killsSinceWeaponEligible = 0;                // Pity counter for weapon drops.
    int currentWeapon = WeaponConfig::STARTING_WEAPON;

    float fireCooldown = 0.f;        // Time until the weapon may fire again.
    float fireBufferTimer = 0.f;     // Remembers a click made slightly too early.
    float switchTimer = 0.f;         // Time left while the new weapon is being raised.
    float emptyClickTimer = 0.f;     // HUD hint: magazine is empty.
    float noReserveTimer = 0.f;      // HUD hint: R pressed with no reserve ammo.
    float controlsHintTimer = 0.f;   // Shows the key hints at the start of a game.

    bool reloading = false;
    float reloadTimer = 0.f;
    int reloadTarget = 0;            // Bullets this reload will load.
    int reloadDone = 0;              // Shells already loaded (shotgun).

    bool requireMouseRelease = true; // Stops a menu click from firing the first shot.
    bool showWeaponDebug = false;    // F1: draws muzzle / sprite alignment markers.
    int nextZombieId = 1;

    std::uniform_real_distribution<float> signedUnit(-1.f, 1.f);

    auto pushNotice = [&](const std::string& text, sf::Color color)
    {
        notices.push_back({text, color, 2.4f});
        if (notices.size() > 5)
            notices.erase(notices.begin());
    };

    // Damage dealt to a zombie floats up from its head and fades out.
    auto spawnDamageNumber = [&](sf::Vector2f position, float damage, bool killed)
    {
        if (damageNumbers.size() > 40)
            damageNumbers.erase(damageNumbers.begin());

        FloatingText number;
        number.text = std::to_string(std::max(1, static_cast<int>(std::round(damage))));
        number.position = position + sf::Vector2f(signedUnit(randomEngine) * 12.f, 0.f);
        number.life = number.maxLife = 0.75f;
        number.scale = killed ? 3.f : 2.f;
        number.color = killed ? sf::Color(255, 205, 80) : sf::Color(255, 245, 225);
        damageNumbers.push_back(number);
    };

    // Waves 3, 6, 9... and everything from wave 10 on use the intense music.
    auto isIntenseWave = [](int wave) { return wave % 3 == 0 || wave >= 10; };

    // A dark blood stain that stays on the ground after a zombie has died.
    auto spawnBloodStain = [&](sf::Vector2f position)
    {
        int count = 0;
        for (const DynamicDecal& d : dynamicDecals)
            if (d.blood)
                ++count;

        if (count >= 36)
        {
            const auto oldest = std::find_if(
                dynamicDecals.begin(), dynamicDecals.end(),
                [](const DynamicDecal& d) { return d.blood; });
            if (oldest != dynamicDecals.end())
                dynamicDecals.erase(oldest);
        }

        DynamicDecal stain;
        stain.position = position + sf::Vector2f(0.f, 3.f);
        stain.scorchRadius = 15.f + 9.f * (signedUnit(randomEngine) * 0.5f + 0.5f);
        stain.alpha = 120.f + 40.f * (signedUnit(randomEngine) * 0.5f + 0.5f);
        stain.tint = sf::Color(92, 10, 14);
        stain.blood = true;
        dynamicDecals.push_back(stain);
    };

    showLoading(0.88f);

    // Load the separate weapon sprites (soft failure: falls back to the
    // gun painted into player_hands_gun_aligned.png).
    std::array<sf::Texture, WEAPON_COUNT> weaponTextures;
    std::array<bool, WEAPON_COUNT> weaponTextureLoaded{};

    for (int i = 0; i < WEAPON_COUNT; ++i)
    {
        const std::string file = WEAPONS[i].textureFile;
        const std::array<std::string, 3> candidates = {
            "assets/" + file,
            "assets/weapons/" + file,
            file
        };

        for (const std::string& path : candidates)
        {
            if (weaponTextures[i].loadFromFile(path))
            {
                weaponTextures[i].setSmooth(false);
                weaponTextureLoaded[i] = true;
                break;
            }
        }

        if (!weaponTextureLoaded[i])
        {
            std::cerr << "Note: weapon sprite " << file
                      << " not found - using the sheet gun for "
                      << WEAPONS[i].name << ".\n";
        }
    }

    auto cancelReload = [&]()
    {
        reloading = false;
        reloadTimer = 0.f;
        reloadTarget = 0;
        reloadDone = 0;
    };

    // Returns true if a reload actually started.
    auto startReload = [&]() -> bool
    {
        const WeaponDef& def = WEAPONS[currentWeapon];
        const WeaponState& state = weaponStates[currentWeapon];

        if (reloading || switchTimer > 0.f)
            return false;

        const int missing = def.magazineSize - state.magazine;

        if (missing <= 0)
            return false;                    // Magazine already full.

        if (state.reserve <= 0)
        {
            noReserveTimer = 1.2f;           // Nothing left to load.
            return false;
        }

        reloading = true;
        reloadTimer = 0.f;
        reloadDone = 0;
        reloadTarget = std::min(missing, state.reserve);

        // Magazine reloads play the reload sound once; the shotgun clacks per shell.
        if (!def.shellReload)
            audio.play(SoundId::Reload);

        return true;
    };

    auto switchWeapon = [&](int index)
    {
        if (index < 0 || index >= WEAPON_COUNT || index == currentWeapon ||
            !weaponOwned[static_cast<std::size_t>(index)])
            return;                          // Locked weapons cannot be equipped.

        audio.stopSound(SoundId::Reload);
        cancelReload();                      // Rule: switching cancels a reload.
        currentWeapon = index;
        switchTimer = WEAPONS[index].swapTime;
        fireBufferTimer = 0.f;
        emptyClickTimer = 0.f;
        muzzleFlashTimer = 0.f;
        recoilAmount = 0.f;
    };

    auto resetWeapons = [&]()
    {
        for (int i = 0; i < WEAPON_COUNT; ++i)
        {
            weaponStates[i].magazine = WEAPONS[i].magazineSize;
            weaponStates[i].reserve = WEAPONS[i].startReserve;
        }

        // Only the pistol at the start; the rest must be found.
        weaponOwned.fill(false);
        weaponOwned[static_cast<std::size_t>(WeaponConfig::STARTING_WEAPON)] = true;
        killsSinceWeaponEligible = 0;

        currentWeapon = WeaponConfig::STARTING_WEAPON;
        fireCooldown = 0.f;
        fireBufferTimer = 0.f;
        switchTimer = 0.f;
        emptyClickTimer = 0.f;
        noReserveTimer = 0.f;
        controlsHintTimer = 8.f;
        requireMouseRelease = true;
        audio.stopSound(SoundId::Reload);
        cancelReload();
    };

    // Every wave clear refills part of each weapon's reserve.
    auto restockAmmo = [&]()
    {
        for (int i = 0; i < WEAPON_COUNT; ++i)
        {
            const int bonus = static_cast<int>(std::ceil(
                static_cast<float>(WEAPONS[i].maxReserve) *
                WeaponConfig::RESTOCK_FRACTION
            ));

            weaponStates[i].reserve = std::min(
                WEAPONS[i].maxReserve,
                weaponStates[i].reserve + bonus
            );
        }
    };

    // True when the current weapon is drawn from its own PNG.
    auto weaponUsesOverlay = [&]() -> bool
    {
        return !WEAPONS[currentWeapon].useSheetGun &&
               weaponTextureLoaded[currentWeapon];
    };

    // Muzzle position in the local coordinates of the player frame.
    auto weaponMuzzleLocal = [&]() -> sf::Vector2f
    {
        if (!weaponUsesOverlay())
            return facingLeft ? muzzleLeft : muzzleRight;

        const float width = static_cast<float>(
            weaponTextures[currentWeapon].getSize().x
        );
        const float rightX = WeaponConfig::GUN_BACK_X + width;
        const float mirrorShift =
            muzzleLeft.x - (static_cast<float>(FRAME_W) - muzzleRight.x);
        const float leftX = static_cast<float>(FRAME_W) - rightX + mirrorShift;

        return {facingLeft ? leftX : rightX, muzzleRight.y};
    };

    // Ejected brass / shotgun shell.
    auto spawnCasing = [&](const sf::Vector2f& position,
                           const sf::Vector2f& shotDirection,
                           bool shell)
    {
        sf::Vector2f side(-shotDirection.y, shotDirection.x);
        if (side.y > 0.f)
            side = -side;

        const float kick = 60.f + 40.f * (signedUnit(randomEngine) * 0.5f + 0.5f);

        Particle casing;
        casing.position = position;
        casing.velocity =
            side * kick - shotDirection * 25.f + sf::Vector2f(0.f, -25.f);
        casing.life = 0.55f;
        casing.maxLife = 0.55f;
        casing.size = shell ? 4.f : 2.6f;
        casing.color = shell ? sf::Color(205, 60, 45) : sf::Color(235, 190, 80);
        particles.push_back(casing);
    };

    resetWeapons();

    // --------------------------------------------------
    // 9e. PICKUP SYSTEM STATE (Phase 3)
    // --------------------------------------------------
    std::vector<Pickup> pickups;
    std::vector<FloatingText> floatingTexts;

    // Remaining seconds of every power-up (0 = inactive).
    std::array<float, POWERUP_KIND_COUNT> powerTimers{};
    std::array<float, POWERUP_KIND_COUNT> powerDurations{};
    float supplyTimer = 20.f;
    float speedTrailTimer = 0.f;
    int debugPickupIndex = 0;

    std::uniform_real_distribution<float> unit01(0.f, 1.f);

    showLoading(0.95f);

    // Load the 128 x 128 pickup icons (soft failure: a coloured orb is drawn instead).
    std::array<sf::Texture, PICKUP_TYPE_COUNT> pickupTextures;
    std::array<bool, PICKUP_TYPE_COUNT> pickupTextureLoaded{};

    for (int i = 0; i < PICKUP_TYPE_COUNT; ++i)
    {
        if (PICKUPS[i].category == PickupCategory::Weapon)
            continue;   // Weapon drops are drawn with the weapon sprites.

        const std::string file = PICKUPS[i].textureFile;
        const std::array<std::string, 4> candidates = {
            "assets/" + file,
            "assets/pickups/" + file,
            "assets/items/" + file,
            file
        };

        for (const std::string& path : candidates)
        {
            if (pickupTextures[i].loadFromFile(path))
            {
                pickupTextures[i].setSmooth(true);
                pickupTextureLoaded[i] = true;
                break;
            }
        }

        if (!pickupTextureLoaded[i])
        {
            std::cerr << "Note: pickup icon " << file
                      << " not found - drawing a placeholder orb for "
                      << PICKUPS[i].name << ".\n";
        }
    }

    auto weaponIndexOf = [](PickupType type) -> int
    {
        switch (type)
        {
            case PickupType::WeaponShotgun: return static_cast<int>(WeaponId::Shotgun);
            case PickupType::WeaponRifle:   return static_cast<int>(WeaponId::AssaultRifle);
            case PickupType::WeaponSniper:  return static_cast<int>(WeaponId::SniperRifle);
            default:                        return -1;
        }
    };

    auto powerUpKindOf = [](PickupType type) -> int
    {
        switch (type)
        {
            case PickupType::PowerShield:    return static_cast<int>(PowerUpKind::Shield);
            case PickupType::PowerSpeed:     return static_cast<int>(PowerUpKind::Speed);
            case PickupType::PowerDamage:    return static_cast<int>(PowerUpKind::Damage);
            case PickupType::PowerRapidFire: return static_cast<int>(PowerUpKind::RapidFire);
            default:                         return -1;
        }
    };

    auto powerUpIconOf = [](int kind) -> int
    {
        switch (kind)
        {
            case static_cast<int>(PowerUpKind::Shield):    return static_cast<int>(PickupType::PowerShield);
            case static_cast<int>(PowerUpKind::Speed):     return static_cast<int>(PickupType::PowerSpeed);
            case static_cast<int>(PowerUpKind::Damage):    return static_cast<int>(PickupType::PowerDamage);
            default:                                       return static_cast<int>(PickupType::PowerRapidFire);
        }
    };

    // Current multipliers (1.0 when the power-up is not active).
    auto shieldMultiplier = [&]() -> float
    {
        return powerTimers[static_cast<int>(PowerUpKind::Shield)] > 0.f
            ? PickupConfig::SHIELD_DAMAGE_MULTIPLIER : 1.f;
    };
    auto speedMultiplier = [&]() -> float
    {
        return powerTimers[static_cast<int>(PowerUpKind::Speed)] > 0.f
            ? PickupConfig::SPEED_MULTIPLIER : 1.f;
    };
    auto damageMultiplier = [&]() -> float
    {
        return powerTimers[static_cast<int>(PowerUpKind::Damage)] > 0.f
            ? PickupConfig::DAMAGE_MULTIPLIER : 1.f;
    };
    auto rapidFireMultiplier = [&]() -> float
    {
        return powerTimers[static_cast<int>(PowerUpKind::RapidFire)] > 0.f
            ? PickupConfig::RAPID_FIRE_MULTIPLIER : 1.f;
    };

    auto spawnFloatingText = [&](const std::string& text,
                                 sf::Vector2f position,
                                 sf::Color color,
                                 float scale = 2.f)
    {
        // Stack new messages above recent ones so they never overlap.
        int recent = 0;
        for (const FloatingText& other : floatingTexts)
            if (other.life > other.maxLife * 0.6f)
                ++recent;

        FloatingText note;
        note.text = text;
        note.position = position + sf::Vector2f(0.f, -16.f * static_cast<float>(recent));
        note.color = color;
        note.scale = scale;
        floatingTexts.push_back(note);
    };

    // Sparkle burst in a pickup's colour (spawn pop and collection effect).
    auto spawnPickupBurst = [&](sf::Vector2f position,
                                sf::Color color,
                                int count,
                                float speedScale)
    {
        for (int i = 0; i < count; ++i)
        {
            const float angle =
                (static_cast<float>(i) / static_cast<float>(count)) * 2.f * PI +
                signedUnit(randomEngine) * 0.3f;
            const float speed =
                (70.f + 60.f * unit01(randomEngine)) * speedScale;

            Particle spark;
            spark.position = position;
            spark.velocity = {std::cos(angle) * speed, std::sin(angle) * speed};
            spark.life = 0.45f + 0.2f * unit01(randomEngine);
            spark.maxLife = spark.life;
            spark.size = 3.f;
            spark.color = color;
            particles.push_back(spark);
        }
    };

    auto spawnPickup = [&](PickupType type, sf::Vector2f position, bool force = false) -> bool
    {
        if (pickups.size() >= PickupConfig::MAX_ACTIVE_PICKUPS)
        {
            if (!force)
                return false;   // Too many items on the ground already.

            // A weapon drop always gets room: remove the oldest ordinary item.
            const auto removable = std::find_if(
                pickups.begin(), pickups.end(),
                [](const Pickup& p)
                { return PICKUPS[static_cast<int>(p.type)].category != PickupCategory::Weapon; });
            if (removable == pickups.end())
                return false;
            pickups.erase(removable);
        }

        const PickupDef& def = PICKUPS[static_cast<int>(type)];

        // Items only appear on free ground that the player can walk to:
        // never inside walls, buildings, props or outside the map.
        sf::Vector2f spot;
        if (!findFreeSpot(position, 18.f, spot))
            return false;

        Pickup pickup;
        pickup.type = type;
        pickup.position = spot;
        pickup.lifetime = def.lifetime * PickupConfig::LIFETIME_SCALE;
        pickup.bobPhase = unit01(randomEngine) * 2.f * PI;
        pickups.push_back(pickup);

        spawnPickupBurst(
            pickup.position + sf::Vector2f(0.f, -18.f), def.glow, 8, 0.6f
        );
        return true;
    };

    auto pickWeighted = [&](const std::vector<std::pair<PickupType, float>>& table)
        -> PickupType
    {
        float total = 0.f;
        for (const auto& entry : table)
            total += entry.second;

        float roll = unit01(randomEngine) * total;
        for (const auto& entry : table)
        {
            if (roll < entry.second)
                return entry.first;
            roll -= entry.second;
        }
        return table.back().first;
    };

    // Which exact item a category produces.
    auto rollTypeInCategory = [&](PickupCategory category) -> PickupType
    {
        switch (category)
        {
            case PickupCategory::Health:
                return pickWeighted({
                    {PickupType::HealthSmall, 40.f}, {PickupType::HealthHeart, 30.f},
                    {PickupType::HealthSpray, 15.f}, {PickupType::HealthLarge, 15.f}
                });
            case PickupCategory::Ammo:
                // Only ammo for weapons the player owns (plus the generic box).
                return pickWeighted({
                    {PickupType::AmmoPistol, 28.f},
                    {PickupType::AmmoShotgun,
                        weaponOwned[static_cast<std::size_t>(WeaponId::Shotgun)] ? 28.f : 0.f},
                    {PickupType::AmmoRifle,
                        weaponOwned[static_cast<std::size_t>(WeaponId::AssaultRifle)] ? 28.f : 0.f},
                    {PickupType::AmmoGeneric, 16.f}
                });
            case PickupCategory::PowerUp:
                return pickWeighted({
                    {PickupType::PowerShield, 25.f}, {PickupType::PowerSpeed, 25.f},
                    {PickupType::PowerDamage, 25.f}, {PickupType::PowerRapidFire, 25.f}
                });
            default:
                return pickWeighted({
                    {PickupType::CoinSmall, 75.f}, {PickupType::CoinLarge, 25.f}
                });
        }
    };

    // Called when a zombie dies: 65% nothing, otherwise one item.
    auto rollZombieDrop = [&](sf::Vector2f zombiePosition)
    {
        // Weapons: the next locked weapon (shotgun, rifle, sniper) can drop
        // once its wave is reached. A pity counter guarantees it eventually.
        {
            int nextWeapon = -1;
            for (int w = 1; w < WEAPON_COUNT; ++w)
            {
                if (!weaponOwned[static_cast<std::size_t>(w)])
                {
                    nextWeapon = w;
                    break;
                }
            }

            if (nextWeapon >= 0 &&
                currentWave >= PickupConfig::WEAPON_MIN_WAVE[static_cast<std::size_t>(nextWeapon)])
            {
                ++killsSinceWeaponEligible;

                bool alreadyOnGround = false;
                for (const Pickup& p : pickups)
                    if (weaponIndexOf(p.type) == nextWeapon)
                        alreadyOnGround = true;

                if (!alreadyOnGround &&
                    (unit01(randomEngine) < PickupConfig::WEAPON_DROP_CHANCE ||
                     killsSinceWeaponEligible >= PickupConfig::WEAPON_DROP_PITY_KILLS))
                {
                    const PickupType dropType =
                        nextWeapon == static_cast<int>(WeaponId::Shotgun)      ? PickupType::WeaponShotgun :
                        nextWeapon == static_cast<int>(WeaponId::AssaultRifle) ? PickupType::WeaponRifle :
                                                                                  PickupType::WeaponSniper;

                    if (spawnPickup(dropType, zombiePosition, true))
                    {
                        killsSinceWeaponEligible = 0;
                        pushNotice(std::string("WEAPON DROPPED  ") + WEAPONS[nextWeapon].name,
                                   PICKUPS[static_cast<int>(dropType)].glow);
                        return;
                    }
                }
            }
        }

        float roll = unit01(randomEngine) * 100.f;

        if (roll < PickupConfig::DROP_NONE)
            return;
        roll -= PickupConfig::DROP_NONE;

        PickupCategory category;
        if (roll < PickupConfig::DROP_HEALTH)
            category = PickupCategory::Health;
        else if ((roll -= PickupConfig::DROP_HEALTH) < PickupConfig::DROP_AMMO)
            category = PickupCategory::Ammo;
        else if ((roll -= PickupConfig::DROP_AMMO) < PickupConfig::DROP_POWERUP)
            category = PickupCategory::PowerUp;
        else
            category = PickupCategory::Coin;

        spawnPickup(
            rollTypeInCategory(category),
            zombiePosition + sf::Vector2f(
                signedUnit(randomEngine) * 14.f,
                signedUnit(randomEngine) * 8.f
            )
        );
    };

    // Applies a pickup. Returns false (and leaves it on the ground) when
    // collecting it would be a waste.
    auto applyPickup = [&](Pickup& pickup) -> bool
    {
        const PickupDef& def = PICKUPS[static_cast<int>(pickup.type)];
        const sf::Vector2f noteAt = playerPosition + sf::Vector2f(0.f, -85.f);

        auto deny = [&](const char* message)
        {
            if (pickup.noticeCooldown <= 0.f)
            {
                spawnFloatingText(message, noteAt, sf::Color(255, 175, 60));
                pickup.noticeCooldown = 2.0f;
            }
            return false;
        };

        switch (def.category)
        {
            case PickupCategory::Health:
            {
                if (playerHealth >= PickupConfig::PLAYER_MAX_HEALTH - 0.01f)
                    return deny("HEALTH FULL");

                const float before = playerHealth;
                playerHealth = std::min(
                    PickupConfig::PLAYER_MAX_HEALTH,
                    playerHealth + static_cast<float>(def.amount)
                );

                spawnFloatingText(
                    "+" + std::to_string(static_cast<int>(
                        std::round(playerHealth - before))) + " HP",
                    noteAt, sf::Color(120, 240, 130), 3.f
                );
                return true;
            }

            case PickupCategory::Ammo:
            {
                auto addReserve = [&](int weaponIndex, int amount) -> int
                {
                    int& reserve = weaponStates[weaponIndex].reserve;
                    const int room = WEAPONS[weaponIndex].maxReserve - reserve;
                    const int gained = std::clamp(amount, 0, std::max(0, room));
                    reserve += gained;
                    return gained;
                };

                int gained = 0;

                switch (pickup.type)
                {
                    case PickupType::AmmoPistol:
                        gained = addReserve(static_cast<int>(WeaponId::Pistol), def.amount);
                        break;
                    case PickupType::AmmoShotgun:
                        gained = addReserve(static_cast<int>(WeaponId::Shotgun), def.amount);
                        break;
                    case PickupType::AmmoRifle:
                        gained = addReserve(static_cast<int>(WeaponId::AssaultRifle), def.amount);
                        break;
                    default:
                        // Generic: a percentage of every weapon's reserve capacity.
                        for (int w = 0; w < WEAPON_COUNT; ++w)
                        {
                            const int share = static_cast<int>(std::ceil(
                                static_cast<float>(WEAPONS[w].maxReserve) *
                                static_cast<float>(def.amount) / 100.f
                            ));
                            gained += addReserve(w, share);
                        }
                        break;
                }

                if (gained <= 0)
                    return deny("AMMO FULL");

                const std::string label =
                    (pickup.type == PickupType::AmmoGeneric)
                        ? std::string("ALL AMMO")
                        : std::string(def.name);

                spawnFloatingText(
                    (pickup.type == PickupType::AmmoGeneric)
                        ? "+" + label
                        : "+" + std::to_string(gained) + " " + label,
                    noteAt, sf::Color(255, 215, 120), 2.f
                );
                return true;
            }

            case PickupCategory::Weapon:
            {
                const int w = weaponIndexOf(pickup.type);
                if (w < 0)
                    return false;

                const std::size_t wi = static_cast<std::size_t>(w);

                if (!weaponOwned[wi])
                {
                    // First time: the weapon is unlocked with a full magazine.
                    weaponOwned[wi] = true;
                    weaponStates[wi].magazine = WEAPONS[wi].magazineSize;
                    weaponStates[wi].reserve = WEAPONS[wi].startReserve;
                    killsSinceWeaponEligible = 0;

                    spawnFloatingText(std::string("NEW WEAPON  ") + def.name,
                                      noteAt, def.glow, 3.f);
                    switchWeapon(w);   // Equip it straight away.
                    return true;
                }

                // Already owned (e.g. debug key): it works as ammo.
                const int room = WEAPONS[wi].maxReserve - weaponStates[wi].reserve;
                if (room <= 0)
                    return deny("AMMO FULL");

                const int gained = std::min(room, WEAPONS[wi].startReserve);
                weaponStates[wi].reserve += gained;
                spawnFloatingText("+" + std::to_string(gained) + " " + def.name,
                                  noteAt, sf::Color(255, 215, 120), 2.f);
                return true;
            }

            case PickupCategory::PowerUp:
            {
                const int kind = powerUpKindOf(pickup.type);

                // Re-collecting refreshes the timer instead of stacking.
                powerDurations[kind] = static_cast<float>(def.amount);
                powerTimers[kind] = static_cast<float>(def.amount);

                spawnFloatingText(def.name, noteAt, def.glow, 3.f);
                return true;
            }

            default:
            {
                score += def.amount;
                ++runCoins;
                spawnFloatingText(
                    "+" + std::to_string(def.amount),
                    noteAt, sf::Color(255, 215, 80), 3.f
                );
                return true;
            }
        }
    };

    auto resetPickups = [&]()
    {
        pickups.clear();
        floatingTexts.clear();
        powerTimers.fill(0.f);
        powerDurations.fill(0.f);
        speedTrailTimer = 0.f;
        supplyTimer = PickupConfig::SUPPLY_INTERVAL_MIN +
            unit01(randomEngine) *
            (PickupConfig::SUPPLY_INTERVAL_MAX - PickupConfig::SUPPLY_INTERVAL_MIN);
    };

    resetPickups();

    // --------------------------------------------------
    // 9f. ENVIRONMENT GAMEPLAY: KILLS, DESTRUCTION, EXPLOSIONS (Phase 4)
    // --------------------------------------------------
    const sf::Texture* fxExplosionLarge = envTexture("destruction_fx/explosion_large.png");
    const sf::Texture* fxExplosionSmall = envTexture("destruction_fx/explosion_small.png");
    const sf::Texture* fxSmoke          = envTexture("destruction_fx/smoke_cloud.png");
    const sf::Texture* fxSplinters      = envTexture("destruction_fx/crate_splinters.png");
    const sf::Texture* fxDust           = envTexture("destruction_fx/dust_puff.png");
    const sf::Texture* fxImpact         = envTexture("destruction_fx/bullet_impact_concrete.png");
    const sf::Texture* decalBrokenCrate = envTexture("obstacles/crate_broken.png");
    const sf::Texture* decalBurntBarrel = envTexture("obstacles/barrel_exploded.png");

    auto addEffect = [&](const sf::Texture* texture, sf::Vector2f position, float life,
                         float startSize, float endSize, float startAlpha,
                         float rise = 0.f, float rotation = 0.f)
    {
        if (!texture || effectSprites.size() > 90)
            return;

        EffectSprite fx;
        fx.texture = texture;
        fx.position = position;
        fx.life = life;
        fx.startSize = startSize;
        fx.endSize = endSize;
        fx.startAlpha = startAlpha;
        fx.rise = rise;
        fx.rotation = rotation;
        effectSprites.push_back(fx);
    };

    // A zombie dies: death animation, score and a chance of a drop.
    // (Used by bullets AND explosions, so both are counted exactly once.)
    auto killZombie = [&](Zombie& zombie)
    {
        // Start the death animation instead of removing the zombie. Score is
        // awarded here, exactly once, because isDying blocks any further hits.
        zombie.isDying = true;
        zombie.deathFrame = 0;
        zombie.deathAnimationTimer = 0.f;
        zombie.corpseTimer = 0.f;
        zombie.deathFinished = false;

        // Cancel any attack in progress.
        zombie.isAttacking = false;
        zombie.attackFrame = 0;
        zombie.attackAnimationTimer = 0.f;
        zombie.attackDamageApplied = false;
        zombie.spawnShieldTimer = 0.f;

        // Larger burst when the zombie is killed.
        spawnBloodBurst(zombie.position + sf::Vector2f(0.f, -35.f), 24, true);

        audio.play(SoundId::ZombieDeath);
        pushNotice("ZOMBIE KILLED  +10", sf::Color(235, 150, 130));

        score += 10;
        ++waveZombiesKilled;
        ++totalZombiesKilled;

        rollZombieDrop(zombie.position);

        std::cout << "Zombie eliminated! Score: " << score << "\n";
    };

    // The player takes damage (zombie claw or explosion).
    auto onPlayerHurt = [&](float amount)
    {
        audio.play(SoundId::PlayerHurt);
        damageFlash = std::min(1.f, 0.55f + amount * 0.04f);
        cameraShake = std::max(cameraShake, 2.5f + amount * 0.25f);
    };

    // The player's health reached zero.
    auto onPlayerDeath = [&]()
    {
        gameOver = true;
        gameOverTimer = 0.f;
        gameOverSelection = 0;
        damageFlash = 1.f;
        cameraShake = std::max(cameraShake, 8.f);
        audio.stopSound(SoundId::Reload);
        audio.stopSound(SoundId::PlayerHurt);
        audio.play(SoundId::PlayerDeath);
    };

    // Generic debris burst (wood chips, concrete dust, sparks).
    auto spawnDebris = [&](sf::Vector2f position, sf::Color color, int count, float speed)
    {
        for (int i = 0; i < count; ++i)
        {
            const float angle = unit01(randomEngine) * 2.f * PI;
            const float velocity = speed * (0.4f + 0.8f * unit01(randomEngine));

            Particle chip;
            chip.position = position;
            chip.velocity = {std::cos(angle) * velocity, std::sin(angle) * velocity - 20.f};
            chip.life = 0.35f + 0.35f * unit01(randomEngine);
            chip.maxLife = chip.life;
            chip.size = 2.f + 2.f * unit01(randomEngine);
            chip.color = color;
            particles.push_back(chip);
        }
    };

    auto destroyCrate = [&](Obstacle& crate)
    {
        crate.alive = false;
        navDirty = true;

        const sf::Vector2f center = crate.spriteCenter;
        const float size = std::max(crate.spriteSize.x, crate.spriteSize.y);

        addEffect(fxSplinters, center, 0.45f, size * 0.6f, size * 1.5f, 255.f, -20.f,
                  unit01(randomEngine) * 360.f);
        addEffect(fxDust, center + sf::Vector2f(0.f, 6.f), 0.7f, size * 0.7f, size * 1.7f, 170.f, -12.f);
        spawnDebris(center, sf::Color(150, 100, 55), 14, 150.f);
        spawnDebris(center, sf::Color(100, 66, 36), 8, 110.f);

        if (decalBrokenCrate)
        {
            DynamicDecal decal;
            decal.texture = decalBrokenCrate;
            decal.position = center + sf::Vector2f(0.f, 4.f);
            decal.size = sf::Vector2f(decalBrokenCrate->getSize());
            decal.rotation = unit01(randomEngine) * 360.f;
            dynamicDecals.push_back(decal);
        }

        // Broken crates sometimes contain supplies.
        if (unit01(randomEngine) < WorldConfig::CRATE_DROP_CHANCE)
        {
            const float roll = unit01(randomEngine);
            const PickupCategory category =
                (roll < 0.40f) ? PickupCategory::Ammo :
                (roll < 0.75f) ? PickupCategory::Health :
                (roll < 0.92f) ? PickupCategory::PowerUp :
                                 PickupCategory::Coin;

            spawnPickup(
                rollTypeInCategory(category),
                sf::Vector2f(crate.moveRect.position.x + crate.moveRect.size.x * 0.5f,
                             crate.moveRect.position.y + crate.moveRect.size.y * 0.5f)
            );
        }
    };

    // Boom: damages zombies, the player, nearby crates and sets off other barrels.
    auto explodeBarrel = [&](Obstacle& barrel)
    {
        if (!barrel.alive)
            return;

        barrel.alive = false;
        barrel.fuse = -1.f;
        navDirty = true;

        const sf::Vector2f ground(
            barrel.moveRect.position.x + barrel.moveRect.size.x * 0.5f,
            barrel.moveRect.position.y + barrel.moveRect.size.y * 0.5f
        );
        const sf::Vector2f blastCenter = ground + sf::Vector2f(0.f, -16.f);

        // Visuals: fireball, secondary blasts, smoke, sparks and a scorch mark.
        addEffect(fxExplosionLarge, blastCenter, 0.55f, 60.f, 230.f, 255.f);
        for (int i = 0; i < 3; ++i)
            addEffect(fxExplosionSmall,
                      blastCenter + sf::Vector2f(signedUnit(randomEngine) * 50.f,
                                                 signedUnit(randomEngine) * 40.f),
                      0.4f + 0.1f * static_cast<float>(i), 30.f, 100.f, 230.f);
        addEffect(fxSmoke, blastCenter + sf::Vector2f(0.f, -14.f), 1.6f, 60.f, 170.f, 180.f, -28.f);
        addEffect(fxSmoke, blastCenter + sf::Vector2f(20.f, -4.f), 1.3f, 40.f, 130.f, 150.f, -22.f);

        spawnDebris(blastCenter, sf::Color(255, 190, 60), 26, 260.f);
        spawnDebris(blastCenter, sf::Color(255, 100, 30), 18, 200.f);
        spawnDebris(blastCenter, sf::Color(60, 55, 50), 14, 160.f);

        DynamicDecal scorch;
        scorch.position = ground + sf::Vector2f(0.f, 4.f);
        scorch.scorchRadius = 58.f;
        scorch.alpha = 150.f;
        dynamicDecals.push_back(scorch);

        if (decalBurntBarrel)
        {
            DynamicDecal wreck;
            wreck.texture = decalBurntBarrel;
            wreck.position = barrel.spriteCenter;
            wreck.size = sf::Vector2f(decalBurntBarrel->getSize());
            dynamicDecals.push_back(wreck);
        }

        cameraShake = std::max(cameraShake, WorldConfig::EXPLOSION_SHAKE);
        audio.play(SoundId::Explosion);

        // Walls protect: a blast does not pass through indestructible walls.
        auto shielded = [&](sf::Vector2f target) -> bool
        {
            for (const Obstacle& o : obstacles)
            {
                if (!o.alive || o.kind != ObstacleKind::Wall)
                    continue;

                float t = 0.f;
                if (segmentIntersectsRect(ground, target, o.moveRect, t))
                    return true;
            }
            return false;
        };

        // Zombies.
        for (Zombie& zombie : zombies)
        {
            if (zombie.isDying || zombie.health <= 0.f)
                continue;

            const float distance = vectorLength(zombie.position - ground);
            if (distance > WorldConfig::EXPLOSION_RADIUS || shielded(zombie.position))
                continue;

            const float falloff = 1.f - distance / WorldConfig::EXPLOSION_RADIUS;
            zombie.health -= WorldConfig::EXPLOSION_ZOMBIE_MIN_DAMAGE +
                (WorldConfig::EXPLOSION_ZOMBIE_MAX_DAMAGE -
                 WorldConfig::EXPLOSION_ZOMBIE_MIN_DAMAGE) * falloff;
            zombie.hitFlashTimer = 0.25f;
            spawnBloodBurst(zombie.position + sf::Vector2f(0.f, -39.f), 8, false);

            if (zombie.health <= 0.f)
                killZombie(zombie);
        }

        // The player is not immune to explosions (the shield helps).
        if (!gameOver)
        {
            const float distance = vectorLength(playerPosition - ground);

            if (distance < WorldConfig::EXPLOSION_RADIUS && !shielded(playerPosition))
            {
                const float falloff = 1.f - distance / WorldConfig::EXPLOSION_RADIUS;
                playerHealth = std::max(
                    0.f,
                    playerHealth -
                        WorldConfig::EXPLOSION_PLAYER_MAX_DAMAGE * falloff * shieldMultiplier()
                );

                onPlayerHurt(WorldConfig::EXPLOSION_PLAYER_MAX_DAMAGE * falloff);

                if (playerHealth <= 0.f)
                {
                    onPlayerDeath();
                    std::cout << "\nGAME OVER! Blown up. Final score: " << score
                              << " (reached wave " << currentWave << ")"
                              << "\nPress R to restart or M for menu.\n";
                }
            }
        }

        // Crates are damaged, other barrels start a short fuse (chain reaction).
        for (Obstacle& other : obstacles)
        {
            if (!other.alive || &other == &barrel || other.hp < 0.f)
                continue;

            const sf::Vector2f otherCenter(
                other.moveRect.position.x + other.moveRect.size.x * 0.5f,
                other.moveRect.position.y + other.moveRect.size.y * 0.5f
            );

            if (vectorLength(otherCenter - ground) > WorldConfig::EXPLOSION_RADIUS ||
                shielded(otherCenter))
                continue;

            if (other.explosive)
            {
                if (other.fuse < 0.f)
                    other.fuse = WorldConfig::EXPLOSION_CHAIN_DELAY +
                                 0.1f * unit01(randomEngine);
            }
            else
            {
                other.hp -= WorldConfig::EXPLOSION_PROP_DAMAGE;
                other.hitFlash = 0.15f;

                if (other.hp <= 0.f)
                    destroyCrate(other);
            }
        }
    };

    // A bullet hits a prop or wall at `impact` (screen position of the bullet).
    auto onBulletImpact = [&](Obstacle& hit, const Bullet& bullet, sf::Vector2f impact)
    {
        audio.play(SoundId::BulletImpact, 0.8f);

        switch (hit.kind)
        {
            case ObstacleKind::CrateSmall:
            case ObstacleKind::CrateLarge:
                spawnDebris(impact, sf::Color(150, 100, 55), 5, 110.f);
                break;

            case ObstacleKind::BarrelExplosive:
                spawnDebris(impact, sf::Color(255, 170, 60), 6, 120.f);
                break;

            case ObstacleKind::Fence:
                spawnDebris(impact, sf::Color(160, 110, 60), 4, 90.f);
                break;

            case ObstacleKind::Vehicle:
            case ObstacleKind::BarrelSolid:
            case ObstacleKind::UtilityBox:
                spawnDebris(impact, sf::Color(255, 215, 120), 5, 130.f);
                break;

            default:   // Walls, barricades, sandbags.
                addEffect(fxImpact, impact, 0.22f, 14.f, 30.f, 230.f, 0.f,
                          unit01(randomEngine) * 360.f);
                spawnDebris(impact, sf::Color(175, 175, 165), 5, 100.f);
                break;
        }

        hit.hitFlash = 0.1f;

        if (hit.hp < 0.f)
            return;   // Indestructible.

        if (hit.explosive)
        {
            if (hit.fuse < 0.f)
                hit.fuse = 0.05f;   // Bang.
            return;
        }

        hit.hp -= bullet.damage;

        if (hit.hp <= 0.f)
            destroyCrate(hit);
    };

    // Moves one bullet's ground-plane segment against the world. Returns true
    // if it hit something (the bullet is then placed at the impact point).
    auto bulletVsWorld = [&](Bullet& bullet) -> bool
    {
        const sf::Vector2f offset(0.f, WorldConfig::BULLET_PLANE_OFFSET);
        const sf::Vector2f from = bullet.previousGround;
        const sf::Vector2f to = bullet.position + offset;

        float bestT = 2.f;
        Obstacle* hit = nullptr;

        for (Obstacle& o : obstacles)
        {
            if (!o.alive || !o.blocksBullets)
                continue;

            float t = 0.f;
            if (segmentIntersectsRect(from, to, o.bulletRect, t) && t < bestT)
            {
                bestT = t;
                hit = &o;
            }
        }

        bullet.previousGround = to;

        if (!hit)
            return false;

        const sf::Vector2f impactGround = from + (to - from) * bestT;
        bullet.position = impactGround - offset;
        bullet.previousGround = impactGround;
        onBulletImpact(*hit, bullet, bullet.position);
        return true;
    };

    // --------------------------------------------------
    // 10a. SAVE SYSTEM  (Phase 7)
    // --------------------------------------------------
    // WHAT IS SAVED (saves/player_data.dat, saves/settings.dat)
    //   * Statistics: games played, play time, kills, coins, best score,
    //     best wave and best survival time (updated when a run ends).
    //   * Weapons discovered (slots 0..3 of weaponUnlocked).
    //   * A resumable run ("checkpoint"), written automatically at the START
    //     OF EVERY WAVE from wave 2 on: wave, score, kills, coins, survival
    //     time, health, position and the weapons owned (slots 8..11).
    //   * Settings: music / SFX volume, difficulty, fullscreen, window size.
    //
    // CONTINUE restarts at the saved wave with those values; the map and the
    // zombies are fresh (a checkpoint, not a frozen snapshot of the world).
    constexpr std::size_t SAVED_RUN_WEAPON_OFFSET = 8;
    static_assert(zsa::MAX_SAVED_WEAPONS >=
                  SAVED_RUN_WEAPON_OFFSET + static_cast<std::size_t>(WEAPON_COUNT),
                  "Increase zsa::MAX_SAVED_WEAPONS");

    bool runActive = false;               // A run is in progress (not yet recorded).
    bool lastRunNewBestScore = false;     // Shown on the game-over screen.
    bool lastRunNewBestWave = false;
    float saveNoticeTimer = 0.f;          // "PROGRESS SAVED" HUD indicator.
    bool windowModeChangeRequested = false;
    bool pendingFullscreen = false;

    // ---- UI icons (optional PNGs, 64 x 64) ----
    std::map<std::string, sf::Texture> uiIcons;

    auto uiIcon = [&](const std::string& file) -> const sf::Texture*
    {
        const auto found = uiIcons.find(file);
        if (found != uiIcons.end())
            return &found->second;

        const std::array<std::string, 5> roots = {
            "assets/ui/", "assets/icons/", "assets/", "assets/ui/icons/", ""
        };

        for (const std::string& root : roots)
        {
            sf::Texture texture;
            if (texture.loadFromFile(root + file))
            {
                texture.setSmooth(false);
                return &uiIcons.emplace(file, std::move(texture)).first->second;
            }
        }

        std::cerr << "Note: UI icon " << file << " not found (drawn without it).\n";
        return nullptr;
    };

    const sf::Texture* iconContinue = uiIcon("continue.png");
    const sf::Texture* iconProfile = uiIcon("profile.png");
    const sf::Texture* iconSave = uiIcon("save.png");
    const sf::Texture* iconStats = uiIcon("statistics.png");
    const sf::Texture* iconTrophy = uiIcon("trophy.png");

    auto drawIcon = [&](const sf::Texture* texture, sf::Vector2f center, float size,
                        sf::Color tint = sf::Color::White)
    {
        if (!texture)
            return;

        const sf::Vector2u textureSize = texture->getSize();
        const float scale = size / static_cast<float>(textureSize.x);

        sf::Sprite sprite(*texture);
        sprite.setOrigin({static_cast<float>(textureSize.x) * 0.5f,
                          static_cast<float>(textureSize.y) * 0.5f});
        sprite.setScale({scale, scale});
        sprite.setPosition(center);
        sprite.setColor(tint);
        window.draw(sprite);
    };

    // ---- Settings (settings.dat + audio.ini) ----
    auto saveAllSettings = [&]()
    {
        audio.saveSettings();   // Master volume and mute (audio.ini).

        const sf::Vector2u size = isFullscreen ? windowedSize : window.getSize();
        if (!isFullscreen)
            windowedSize = size;

        gameSettings.musicVolume = static_cast<float>(audio.settings().music);
        gameSettings.sfxVolume = static_cast<float>(audio.settings().sfx);
        gameSettings.difficulty = static_cast<std::uint8_t>(std::clamp(difficultyIndex, 0, 2));
        gameSettings.fullscreen = isFullscreen;
        gameSettings.windowWidth = std::clamp(size.x, 640u, 7680u);
        gameSettings.windowHeight = std::clamp(size.y, 480u, 4320u);

        std::string error;
        if (!saveManager.saveSettings(gameSettings, &error))
            std::cerr << "Could not save settings: " << error << "\n";
    };

    // Switching between windowed and fullscreen re-creates the window.
    auto applyWindowMode = [&](bool fullscreen)
    {
        if (!isFullscreen)
            windowedSize = window.getSize();   // Remember the windowed size.

        isFullscreen = fullscreen;
        createWindow(fullscreen);
        applyLetterbox();

        cursorHidden = !cursorHidden;   // Forces the cursor state to be re-applied.
        draggingSlider = -1;
        saveAllSettings();
    };

    // ---- Progress (player_data.dat) ----
    auto saveProgress = [&]()
    {
        std::string error;
        if (!saveManager.savePlayerData(playerData, &error))
            std::cerr << "Could not save progress: " << error << "\n";
    };

    auto markWeaponsDiscovered = [&]()
    {
        for (int i = 0; i < WEAPON_COUNT; ++i)
            if (weaponOwned[static_cast<std::size_t>(i)])
                playerData.weaponUnlocked[static_cast<std::size_t>(i)] = 1;
    };

    // Writes the "start of this wave" checkpoint.
    auto writeCheckpoint = [&](int wave)
    {
        markWeaponsDiscovered();

        for (int i = 0; i < WEAPON_COUNT; ++i)
            playerData.weaponUnlocked[SAVED_RUN_WEAPON_OFFSET + static_cast<std::size_t>(i)] =
                weaponOwned[static_cast<std::size_t>(i)] ? 1 : 0;

        zsa::SaveManager::setSavedRun(
            playerData,
            static_cast<std::uint32_t>(std::max(1, wave)),
            static_cast<std::uint32_t>(std::max(0, score)),
            static_cast<std::uint32_t>(std::max(0, totalZombiesKilled)),
            static_cast<std::uint32_t>(std::max(0, runCoins)),
            static_cast<std::uint64_t>(runSurvivalSeconds),
            playerHealth, playerPosition.x, playerPosition.y
        );

        saveProgress();
        saveNoticeTimer = 2.4f;
    };

    auto clearSavedRunWeapons = [&]()
    {
        for (int i = 0; i < WEAPON_COUNT; ++i)
            playerData.weaponUnlocked[SAVED_RUN_WEAPON_OFFSET + static_cast<std::size_t>(i)] = 0;
    };

    // The run is over (game over, restart, or abandoned without a checkpoint):
    // add it to the statistics and remove the checkpoint.
    auto finishRun = [&]()
    {
        if (!runActive)
            return;

        runActive = false;
        markWeaponsDiscovered();

        const std::uint32_t finalScore = static_cast<std::uint32_t>(std::max(0, score));
        const std::uint32_t finalWave = static_cast<std::uint32_t>(std::max(1, currentWave));

        lastRunNewBestScore = finalScore > 0u && finalScore > playerData.bestScore;
        lastRunNewBestWave = finalWave > playerData.bestWave && playerData.totalGamesPlayed > 0u;

        zsa::SaveManager::recordCompletedRun(
            playerData, finalScore, finalWave,
            static_cast<std::uint32_t>(std::max(0, totalZombiesKilled)),
            static_cast<std::uint32_t>(std::max(0, runCoins)),
            static_cast<std::uint64_t>(runSurvivalSeconds)
        );
        clearSavedRunWeapons();
        saveProgress();

        // Records per difficulty.
        const std::size_t level = static_cast<std::size_t>(std::clamp(difficultyIndex, 0, 2));
        difficultyBests.score[level] = std::max(difficultyBests.score[level], finalScore);
        difficultyBests.wave[level] = std::max(difficultyBests.wave[level], finalWave);
        saveDifficultyBests(difficultyBests, saveManager.getSaveDirectory());
        saveNoticeTimer = 2.4f;

        std::cout << "Run recorded. Best score: " << playerData.bestScore
                  << ", best wave: " << playerData.bestWave << "\n";
    };

    // Leaving to the main menu: a run with a checkpoint stays resumable,
    // a run without one (still on wave 1) is counted straight away.
    auto abandonToMenu = [&]()
    {
        if (!runActive)
            return;

        if (playerData.hasSavedRun)
            runActive = false;   // CONTINUE will resume from the checkpoint.
        else if (score > 0 || totalZombiesKilled > 0 || runSurvivalSeconds > 10.0)
            finishRun();
        else
            runActive = false;
    };

    // --------------------------------------------------
    // 10b. WAVE / ROUND CONTROL
    // --------------------------------------------------
    auto beginWave = [&](int wave)
    {
        const DifficultySettings& difficulty = DIFFICULTIES[difficultyIndex];

        currentWave = wave;
        waveZombieTotal = waveZombieCount(wave, difficulty);
        waveZombiesSpawned = 0;
        waveZombiesKilled = 0;

        intermission = false;
        intermissionTimer = 0.f;
        waveBannerTimer = WaveConfig::WAVE_BANNER_DURATION;

        audio.play(SoundId::WaveStart);
        pushNotice(isIntenseWave(wave) ? "INTENSE WAVE " + std::to_string(wave)
                                       : "WAVE " + std::to_string(wave),
                   sf::Color(255, 205, 95));

        // The first zombie shows up shortly after the wave begins.
        spawnTimer = std::max(
            0.f,
            waveSpawnInterval(wave, difficulty) - WaveConfig::FIRST_SPAWN_DELAY
        );

        std::cout << "Wave " << wave << " started: "
                  << waveZombieTotal << " zombies ("
                  << difficulty.name << ")\n";
    };

    auto startRun = [&](int startWave)
    {
        playerHealth = 100.f;
        playerDamageCooldown = 0.f;
        playerPosition = {WorldConfig::PLAYER_SPAWN_X, WorldConfig::PLAYER_SPAWN_Y};
        cameraCenter = playerPosition + sf::Vector2f(0.f, -WorldConfig::CAMERA_HEAD_OFFSET);
        resetDynamicWorld();
        bullets.clear();
        zombies.clear();
        particles.clear();
        score = 0;
        recoilAmount = 0.f;
        resetWeapons();
        resetPickups();
        muzzleFlashTimer = 0.f;
        currentAimAngle = 0.f;
        currentFrame = 0;
        animationTimer = 0.f;
        facingLeft = false;
        gameOver = false;
        lastSpawnSide = -1;

        // Phase 5: reset presentation state for the new round.
        paused = false;
        subPage = SubPage::Root;
        gameOverTimer = 0.f;
        damageFlash = 0.f;
        displayHealth = 100.f;
        hitMarkerTimer = 0.f;
        crosshairKick = 0.f;
        totalZombiesKilled = 0;
        runCoins = 0;
        runSurvivalSeconds = 0.0;
        lastRunNewBestScore = false;
        lastRunNewBestWave = false;
        damageNumbers.clear();
        notices.clear();
        screenFade = 1.f;
        audio.stopAllSfx();
        audio.setMusicDuck(1.f);

        runActive = true;
        beginWave(startWave);
    };

    // A brand-new run (menu, restart, game-over restart).
    auto startNewGame = [&]()
    {
        if (runActive)
        {
            finishRun();   // Restarting ends the current run; it still counts.
        }
        else if (playerData.hasSavedRun)
        {
            // Starting fresh replaces the checkpoint: count that run first.
            zsa::SaveManager::recordCompletedRun(
                playerData, playerData.savedScore, playerData.savedWave,
                playerData.savedRunKills, playerData.savedRunCoins,
                playerData.savedSurvivalSeconds
            );
            clearSavedRunWeapons();
            saveProgress();
        }

        startRun(1);
    };

    auto confirmDifficulty = [&](int index)
    {
        difficultyIndex = std::clamp(index, 0, 2);
        menuSelection = difficultyIndex;
        inMenu = false;
        saveAllSettings();   // The difficulty is a saved preference.

        std::cout << "Difficulty: " << DIFFICULTIES[difficultyIndex].name << "\n";
        startNewGame();
    };

    // CONTINUE: restart at the saved wave with the saved values.
    auto continueSavedRun = [&]() -> bool
    {
        if (!playerData.hasSavedRun)
            return false;

        const zsa::PlayerData saved = playerData;   // startRun must not touch the checkpoint.

        difficultyIndex = std::clamp<int>(gameSettings.difficulty, 0, 2);
        inMenu = false;
        startRun(static_cast<int>(saved.savedWave));

        score = static_cast<int>(saved.savedScore);
        totalZombiesKilled = static_cast<int>(saved.savedRunKills);
        runCoins = static_cast<int>(saved.savedRunCoins);
        runSurvivalSeconds = static_cast<double>(saved.savedSurvivalSeconds);
        playerHealth = std::clamp(saved.savedPlayerHealth, 1.f, 100.f);
        displayHealth = playerHealth;

        // Same place as the checkpoint, if it is still walkable.
        const sf::Vector2f savedPosition(saved.savedPlayerX, saved.savedPlayerY);
        if (isPositionFree(savedPosition, WorldConfig::PLAYER_RADIUS + 2.f))
        {
            playerPosition = savedPosition;
            cameraCenter = playerPosition + sf::Vector2f(0.f, -WorldConfig::CAMERA_HEAD_OFFSET);
        }

        // Weapons that were owned at the checkpoint (full magazines and
        // starting reserves; the pistol is always available).
        for (int i = 0; i < WEAPON_COUNT; ++i)
        {
            const std::size_t index = static_cast<std::size_t>(i);
            const bool owned = saved.weaponUnlocked[SAVED_RUN_WEAPON_OFFSET + index] != 0 ||
                               i == WeaponConfig::STARTING_WEAPON;
            weaponOwned[index] = owned;

            if (owned)
            {
                weaponStates[index].magazine = WEAPONS[i].magazineSize;
                weaponStates[index].reserve = WEAPONS[i].startReserve;
            }
        }
        currentWeapon = WeaponConfig::STARTING_WEAPON;
        killsSinceWeaponEligible = 0;

        pushNotice("RUN RESTORED", sf::Color(120, 205, 95));
        std::cout << "Continued saved run at wave " << saved.savedWave << "\n";
        return true;
    };

    // --------------------------------------------------
    // 10c. MENU / PAUSE / SETTINGS ACTIONS  (Phase 5)
    // --------------------------------------------------
    int controlsSelection = 0;

    auto uiClick = [&]() { audio.play(SoundId::MenuClick); };

    auto openMainMenu = [&]()
    {
        gameOver = false;
        paused = false;
        inMenu = true;
        subPage = SubPage::Root;
        abandonToMenu();
        menuSelection = playerData.hasSavedRun ? 6 : difficultyIndex;
        screenFade = 1.f;
        cancelReload();
        audio.stopAllSfx();
        audio.setMusicDuck(1.f);
    };

    auto pauseGame = [&]()
    {
        paused = true;
        subPage = SubPage::Root;
        pauseSelection = 0;
        audio.pauseAllSfx();
        audio.setMusicDuck(0.35f);
    };

    auto resumeGame = [&]()
    {
        paused = false;
        subPage = SubPage::Root;
        requireMouseRelease = true;   // The click that resumed must not fire a shot.
        audio.resumeAllSfx();
        audio.setMusicDuck(1.f);
    };

    auto leaveSettings = [&]()
    {
        saveAllSettings();
        draggingSlider = -1;
        subPage = SubPage::Root;
    };

    auto adjustSetting = [&](int row, int delta)
    {
        const AudioSettings& s = audio.settings();

        if (row == 0)      audio.setMaster(s.master + delta);
        else if (row == 1) audio.setMusicVolume(s.music + delta);
        else if (row == 2) audio.setSfxVolume(s.sfx + delta);

        saveAllSettings();
    };

    auto setSliderFromX = [&](int row, float x)
    {
        const UiRect bar = uiSliderBar(row);
        const int value = static_cast<int>(std::round((x - bar.x) / bar.w * 100.f));

        if (row == 0)      audio.setMaster(value);
        else if (row == 1) audio.setMusicVolume(value);
        else if (row == 2) audio.setSfxVolume(value);
    };

    auto activateMain = [&](int item)
    {
        uiClick();

        if (item < 3)
            confirmDifficulty(item);
        else if (item == 3)
        {
            subPage = SubPage::Settings;
            settingsSelection = 0;
        }
        else if (item == 4)
        {
            subPage = SubPage::Controls;
            controlsSelection = 0;
        }
        else if (item == 6)
        {
            if (!continueSavedRun())
                audio.play(SoundId::EmptyClick);   // Nothing to continue.
        }
        else if (item == 7)
        {
            subPage = SubPage::Profile;
            controlsSelection = 0;
        }
        else if (item == 8)
        {
            subPage = SubPage::Credits;
            screenFade = 0.7f;   // Soft fade into the credits artwork.
        }
        else
            window.close();
    };

    auto activatePause = [&](int item)
    {
        uiClick();

        switch (item)
        {
            case 0: resumeGame(); break;
            case 1: startNewGame(); break;        // Restart (also leaves the pause).
            case 2:
                subPage = SubPage::Settings;
                settingsSelection = 0;
                break;
            case 3: openMainMenu(); break;
            default: window.close(); break;
        }
    };

    auto activateGameOver = [&](int item)
    {
        uiClick();

        if (item == 0)
            startNewGame();
        else if (item == 1)
            openMainMenu();
        else
            window.close();
    };

    auto activateSettings = [&](int row)
    {
        if (row == 3)
        {
            audio.toggleMute();
            saveAllSettings();
            uiClick();
        }
        else if (row == 4)
        {
            uiClick();
            windowModeChangeRequested = true;       // Done after the event loop.
            pendingFullscreen = !isFullscreen;
        }
        else if (row == 5)
        {
            uiClick();
            leaveSettings();
        }
    };

    // Which UI screen is active decides the item count, layout and selection.
    auto uiItemCount = [&]() -> int
    {
        if (subPage == SubPage::Settings) return SETTINGS_ITEMS;
        if (subPage == SubPage::Controls || subPage == SubPage::Profile || subPage == SubPage::Credits) return 1;
        if (inMenu) return MAIN_MENU_ITEMS;
        if (paused) return PAUSE_ITEMS;
        return GAME_OVER_ITEMS;
    };

    auto uiItemRect = [&](int i) -> UiRect
    {
        if (subPage == SubPage::Settings) return uiSettingsRect(i);
        if (subPage == SubPage::Controls || subPage == SubPage::Profile || subPage == SubPage::Credits) return uiControlsBackRect();
        if (inMenu) return uiMainRect(i);
        if (paused) return uiPauseRect(i);
        return uiGameOverRect(i);
    };

    auto uiSelection = [&]() -> int&
    {
        if (subPage == SubPage::Settings) return settingsSelection;
        if (subPage == SubPage::Controls || subPage == SubPage::Profile || subPage == SubPage::Credits) return controlsSelection;
        if (inMenu) return menuSelection;
        if (paused) return pauseSelection;
        return gameOverSelection;
    };

    // Main menu order from top to bottom: continue, 3 difficulties, bottom row.
    auto mainMenuStep = [&](int current, int direction) -> int
    {
        static const int ORDER[MAIN_MENU_ITEMS] = {6, 0, 1, 2, 3, 4, 7, 8, 5};

        int position = 0;
        for (int i = 0; i < MAIN_MENU_ITEMS; ++i)
            if (ORDER[i] == current)
                position = i;

        for (int tries = 0; tries < MAIN_MENU_ITEMS; ++tries)
        {
            position = (position + direction + MAIN_MENU_ITEMS) % MAIN_MENU_ITEMS;
            if (ORDER[position] != 6 || playerData.hasSavedRun)
                return ORDER[position];
        }
        return current;
    };

    auto uiSelect = [&](int index)
    {
        int& selection = uiSelection();
        if (selection != index)
        {
            selection = index;
            audio.play(SoundId::MenuHover);
        }
    };

    auto uiActivate = [&](int index)
    {
        if (subPage == SubPage::Settings)      activateSettings(index);
        else if (subPage == SubPage::Controls || subPage == SubPage::Profile || subPage == SubPage::Credits) { uiClick(); subPage = SubPage::Root; }
        else if (inMenu)                       activateMain(index);
        else if (paused)                       activatePause(index);
        else                                   activateGameOver(index);
    };

    // Shared by the main menu and the pause menu.
    auto drawSettingsPage = [&]()
    {
        const AudioSettings& s = audio.settings();

        drawCenteredText(window, "SETTINGS", 400.f, 84.f, 5.f, sf::Color(255, 205, 95));

        drawUiSlider(window, 0, "MASTER VOLUME", s.master, s.muted, buttonHover[0]);
        drawUiSlider(window, 1, "MUSIC VOLUME", s.music, s.muted, buttonHover[1]);
        drawUiSlider(window, 2, "SOUND EFFECTS", s.sfx, s.muted, buttonHover[2]);

        drawUiButton(window, uiSettingsRect(3),
                     s.muted ? "SOUND  MUTED" : "SOUND  ON", 2,
                     s.muted ? sf::Color(245, 75, 65) : sf::Color(120, 205, 95),
                     buttonHover[3], buttonPress[3]);
        drawUiButton(window, uiSettingsRect(4),
                     isFullscreen ? "FULLSCREEN  ON" : "FULLSCREEN  OFF", 2,
                     isFullscreen ? sf::Color(120, 205, 95) : sf::Color(150, 185, 225),
                     buttonHover[4], buttonPress[4]);
        drawUiButton(window, uiSettingsRect(5), "BACK", 3, sf::Color(150, 185, 225),
                     buttonHover[5], buttonPress[5]);

        drawCenteredText(window, "LEFT RIGHT CHANGE  F11 FULLSCREEN  ESC BACK", 400.f, 552.f, 2.f,
                         sf::Color(190, 210, 190));
    };

    // Fade from black after every state change (menu <-> game <-> game over).
    auto drawFade = [&]()
    {
        if (screenFade <= 0.f)
            return;

        sf::RectangleShape fade({800.f, 600.f});
        fade.setFillColor(sf::Color(0, 0, 0, static_cast<std::uint8_t>(
            std::clamp(screenFade, 0.f, 1.f) * 255.f)));
        window.draw(fade);
    };

    // Loading finished: show the full bar briefly so the screen is readable.
    if (loadingArtLoaded)
    {
        sf::Clock loadingHold;
        while (window.isOpen() && loadingHold.getElapsedTime().asSeconds() < 0.6f)
        {
            showLoading(1.f);
            sf::sleep(sf::milliseconds(16));
        }
    }

    std::vector<sf::Vector2f> nextZombiePositions;   // Reused every frame (no per-frame allocation).
    sf::Clock clock;
    float clockTotalSeconds = 0.f;   // Used for blinking HUD text.

    // ==================================================
    // MAIN GAME LOOP
    // ==================================================
    while (window.isOpen())
    {
        // Real frame time: always runs (menus, pause screen, fades).
        const float rawDt = std::min(clock.restart().asSeconds(), 0.05f);
        uiTime += rawDt;
        audio.update(rawDt);
        screenFade = std::max(0.f, screenFade - rawDt / 0.45f);
        if (gameOver)
            gameOverTimer += rawDt;

        bool fireRequested = false;
        bool reloadRequested = false;
        bool debugPickupRequested = false;
        int weaponSwitchRequested = -1;
        sf::Vector2f shotTarget(0.f, 0.f);

        // --------------------------------------------------
        // 11. EVENTS  (menus / pause / game over  OR  gameplay input)
        // --------------------------------------------------
        while (const std::optional<sf::Event> event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }

            // Phase 7: window handling - keep the picture 4:3, F11 = fullscreen.
            if (event->is<sf::Event::Resized>())
                applyLetterbox();

            if (const auto* windowKey = event->getIf<sf::Event::KeyPressed>())
            {
                if (windowKey->code == sf::Keyboard::Key::F11)
                {
                    windowModeChangeRequested = true;
                    pendingFullscreen = !isFullscreen;
                }
            }

            // Alt-tab pauses a running game.
            if (event->is<sf::Event::FocusLost>() && !inMenu && !gameOver && !paused)
                pauseGame();

            // Credits: any key (except F11) or a mouse click returns to the menu.
            if (inMenu && subPage == SubPage::Credits)
            {
                const auto* creditsKey = event->getIf<sf::Event::KeyPressed>();
                const bool anyKey = creditsKey && creditsKey->code != sf::Keyboard::Key::F11;

                if (anyKey || event->is<sf::Event::MouseButtonPressed>())
                {
                    uiClick();
                    subPage = SubPage::Root;
                    screenFade = 0.7f;
                    continue;
                }
            }

            const bool uiMode = inMenu || paused || gameOver;

            if (uiMode)
            {
                if (const auto* keyEvent = event->getIf<sf::Event::KeyPressed>())
                {
                    const sf::Keyboard::Key key = keyEvent->code;
                    int& selection = uiSelection();
                    const int count = uiItemCount();

                    const bool mainRoot = inMenu && subPage == SubPage::Root;

                    if (key == sf::Keyboard::Key::Up)
                        uiSelect(mainRoot ? mainMenuStep(selection, -1) : (selection + count - 1) % count);
                    else if (key == sf::Keyboard::Key::Down)
                        uiSelect(mainRoot ? mainMenuStep(selection, 1) : (selection + 1) % count);
                    else if (key == sf::Keyboard::Key::Enter || key == sf::Keyboard::Key::Space)
                        uiActivate(selection);
                    else if (key == sf::Keyboard::Key::Escape)
                    {
                        if (subPage == SubPage::Settings)
                        {
                            uiClick();
                            leaveSettings();
                        }
                        else if (subPage == SubPage::Controls || subPage == SubPage::Profile || subPage == SubPage::Credits)
                        {
                            uiClick();
                            subPage = SubPage::Root;
                        }
                        else if (paused)
                        {
                            uiClick();
                            resumeGame();
                        }
                    }
                    else if ((key == sf::Keyboard::Key::Left || key == sf::Keyboard::Key::Right) &&
                             subPage == SubPage::Settings && selection < 3)
                    {
                        adjustSetting(selection, key == sf::Keyboard::Key::Left ? -5 : 5);
                        audio.play(SoundId::MenuHover);
                    }
                    else if (inMenu && subPage == SubPage::Root)
                    {
                        // Quick start: 1 / 2 / 3 pick a difficulty.
                        if (key == sf::Keyboard::Key::Num1 || key == sf::Keyboard::Key::Numpad1)
                            activateMain(0);
                        else if (key == sf::Keyboard::Key::Num2 || key == sf::Keyboard::Key::Numpad2)
                            activateMain(1);
                        else if (key == sf::Keyboard::Key::Num3 || key == sf::Keyboard::Key::Numpad3)
                            activateMain(2);
                        else if (key == sf::Keyboard::Key::C)
                            activateMain(6);     // Continue the saved run.
                        else if (key == sf::Keyboard::Key::P)
                            activateMain(7);     // Profile / statistics.
                    }
                    else if (gameOver)
                    {
                        if (key == sf::Keyboard::Key::R)
                            activateGameOver(0);
                        else if (key == sf::Keyboard::Key::M)
                            activateGameOver(1);
                    }
                }

                if (const auto* moveEvent = event->getIf<sf::Event::MouseMoved>())
                {
                    const sf::Vector2f point =
                        window.mapPixelToCoords(moveEvent->position, screenView);

                    if (draggingSlider >= 0)
                    {
                        setSliderFromX(draggingSlider, point.x);
                    }
                    else
                    {
                        const int count = uiItemCount();
                        for (int i = 0; i < count; ++i)
                        {
                            if (uiItemRect(i).contains(point))
                            {
                                uiSelect(i);
                                break;
                            }
                        }
                    }
                }

                if (const auto* mouseEvent = event->getIf<sf::Event::MouseButtonPressed>())
                {
                    if (mouseEvent->button == sf::Mouse::Button::Left)
                    {
                        const sf::Vector2f point =
                            window.mapPixelToCoords(mouseEvent->position, screenView);
                        const int count = uiItemCount();

                        for (int i = 0; i < count; ++i)
                        {
                            if (!uiItemRect(i).contains(point))
                                continue;

                            uiSelection() = i;

                            if (subPage == SubPage::Settings && i < 3)
                            {
                                draggingSlider = i;           // Slider: click or drag.
                                setSliderFromX(i, point.x);
                            }
                            else
                            {
                                buttonPress[static_cast<std::size_t>(i)] = 1.f;
                                uiActivate(i);
                            }
                            break;
                        }
                    }
                }

                if (const auto* releaseEvent = event->getIf<sf::Event::MouseButtonReleased>())
                {
                    if (releaseEvent->button == sf::Mouse::Button::Left && draggingSlider >= 0)
                    {
                        draggingSlider = -1;
                        saveAllSettings();
                        audio.play(SoundId::MenuClick);
                    }
                }
            }
            else
            {
                if (const auto* keyEvent =
                        event->getIf<sf::Event::KeyPressed>())
                {
                    // Weapon controls: 1-4 select, R reloads, F1 = alignment debug,
                    // ESC pauses.
                    switch (keyEvent->code)
                    {
                        case sf::Keyboard::Key::Escape:
                            pauseGame();
                            uiClick();   // After pausing, so the click is not frozen.
                            break;
                        case sf::Keyboard::Key::Num1:
                        case sf::Keyboard::Key::Numpad1:
                            weaponSwitchRequested = static_cast<int>(WeaponId::Pistol);
                            break;
                        case sf::Keyboard::Key::Num2:
                        case sf::Keyboard::Key::Numpad2:
                            weaponSwitchRequested = static_cast<int>(WeaponId::Shotgun);
                            break;
                        case sf::Keyboard::Key::Num3:
                        case sf::Keyboard::Key::Numpad3:
                            weaponSwitchRequested = static_cast<int>(WeaponId::AssaultRifle);
                            break;
                        case sf::Keyboard::Key::Num4:
                        case sf::Keyboard::Key::Numpad4:
                            weaponSwitchRequested = static_cast<int>(WeaponId::SniperRifle);
                            break;
                        case sf::Keyboard::Key::R:
                            reloadRequested = true;
                            break;
                        case sf::Keyboard::Key::F1:
                            showWeaponDebug = !showWeaponDebug;
                            break;
                        case sf::Keyboard::Key::F2:
                            if (PickupConfig::ENABLE_DEBUG_KEY)
                                debugPickupRequested = true;
                            break;
                        default:
                            break;
                    }
                }

                // Mouse wheel cycles through the weapons.
                if (const auto* wheelEvent =
                        event->getIf<sf::Event::MouseWheelScrolled>())
                {
                    if (wheelEvent->delta != 0.f)
                    {
                        const int from = (weaponSwitchRequested >= 0)
                            ? weaponSwitchRequested
                            : currentWeapon;
                        const int step = (wheelEvent->delta > 0.f) ? -1 : 1;

                        // Skip weapons that have not been found yet.
                        int next = from;
                        for (int tries = 0; tries < WEAPON_COUNT; ++tries)
                        {
                            next = (next + step + WEAPON_COUNT) % WEAPON_COUNT;
                            if (weaponOwned[static_cast<std::size_t>(next)])
                                break;
                        }
                        weaponSwitchRequested = next;
                    }
                }

                if (const auto* mouseEvent =
                        event->getIf<sf::Event::MouseButtonPressed>())
                {
                    if (mouseEvent->button == sf::Mouse::Button::Left)
                    {
                        const sf::Vector2f clickPosition =
                            window.mapPixelToCoords(mouseEvent->position);

                        fireRequested = true;
                        requireMouseRelease = false;   // A fresh click is always allowed.
                        shotTarget = clickPosition;
                    }
                }
            }
        }

        // Phase 7: fullscreen <-> windowed (requested by F11 or the settings screen).
        if (windowModeChangeRequested)
        {
            windowModeChangeRequested = false;
            applyWindowMode(pendingFullscreen);
            continue;   // Start the next frame on the new window.
        }

        // The system cursor shows in menus; a custom crosshair replaces it in play.
        {
            const bool wantHidden = !inMenu && !paused && !gameOver;
            if (wantHidden != cursorHidden)
            {
                window.setMouseCursorVisible(!wantHidden);
                window.setMouseCursorGrabbed(wantHidden);   // Keep the aim inside the window.
                cursorHidden = wantHidden;
            }
        }

        // Music follows the game state (cross-fades automatically).
        {
            MusicId wanted = MusicId::Gameplay;

            if (inMenu)
                wanted = MusicId::Menu;
            else if (gameOver)
                wanted = MusicId::GameOver;
            else if (isIntenseWave(intermission ? currentWave + 1 : currentWave))
                wanted = MusicId::Intense;

            audio.playMusic(wanted);

            const bool critical = !inMenu && !gameOver && playerHealth > 0.f && playerHealth <= 30.f;
            audio.setMusicDuck(paused ? 0.35f : (critical ? 0.6f : 1.f));
        }

        // Smooth hover / click animation of whichever buttons are on screen.
        {
            const int selected = (inMenu || paused || gameOver) ? uiSelection() : -1;
            const float follow = 1.f - std::exp(-16.f * rawDt);

            for (std::size_t i = 0; i < buttonHover.size(); ++i)
            {
                const float target = (static_cast<int>(i) == selected) ? 1.f : 0.f;
                buttonHover[i] += (target - buttonHover[i]) * follow;
                buttonPress[i] = std::max(0.f, buttonPress[i] - rawDt * 4.f);
            }
        }

        // --------------------------------------------------
        // 11b. MAIN MENU  (difficulty / settings / controls / exit)
        // --------------------------------------------------
        if (inMenu)
        {
            window.setView(screenView);
            window.clear(sf::Color(10, 12, 10));
            drawMenuBackground(window, uiTime);

            if (subPage == SubPage::Settings)
            {
                drawSettingsPage();
            }
            else if (subPage == SubPage::Controls)
            {
                drawCenteredText(window, "CONTROLS", 400.f, 70.f, 5.f, sf::Color(255, 205, 95));

                static const char* const LINES[][2] = {
                    {"W A S D",         "MOVE"},
                    {"MOUSE",           "AIM"},
                    {"LEFT CLICK",      "FIRE"},
                    {"R",               "RELOAD"},
                    {"1 2 3 4",         "SWITCH WEAPON"},
                    {"MOUSE WHEEL",     "CYCLE WEAPONS"},
                    {"ESC",             "PAUSE"},
                    {"WALK OVER ITEMS", "COLLECT"}
                };

                for (int i = 0; i < 8; ++i)
                {
                    const float y = 148.f + static_cast<float>(i) * 40.f;
                    drawShadowText(window, LINES[i][0], 170.f, y, 2.f, sf::Color(255, 235, 150));
                    drawShadowText(window, LINES[i][1], 420.f, y, 2.f, sf::Color(220, 232, 220));
                }

                drawUiButton(window, uiControlsBackRect(), "BACK", 3,
                             sf::Color(120, 205, 95), buttonHover[0], buttonPress[0]);
            }
            else if (subPage == SubPage::Credits)
            {
                if (creditsArtLoaded)
                {
                    // The artwork already contains the credits text and the
                    // "press any key" hint, so only the picture is drawn.
                    drawCover(creditsArt);
                }
                else
                {
                    drawCenteredText(window, "CREDITS", 400.f, 120.f, 5.f, sf::Color(255, 205, 95));
                    drawCenteredText(window, "ZOMBIE SURVIVAL ARENA", 400.f, 230.f, 3.f, sf::Color(220, 58, 45));
                    drawCenteredText(window, "THANK YOU FOR PLAYING", 400.f, 300.f, 2.f, sf::Color(245, 245, 230));
                    drawCenteredText(window, "PRESS ANY KEY TO RETURN", 400.f, 470.f, 2.f, sf::Color(190, 210, 190));
                }
            }
            else if (subPage == SubPage::Profile)
            {
                const sf::Color gold(255, 205, 95);
                const sf::Color cyan(90, 220, 255);
                const sf::Color light(235, 240, 230);
                const sf::Color dim(150, 172, 152);

                drawCenteredText(window, "PROFILE", 400.f, 32.f, 5.f, gold);
                drawIcon(iconProfile, {262.f, 50.f}, 44.f);

                auto panel = [&](float x, float y, float w, float h, sf::Color accent)
                {
                    sf::RectangleShape box({w, h});
                    box.setPosition({x, y});
                    box.setFillColor(sf::Color(14, 20, 16, 205));
                    box.setOutlineThickness(2.f);
                    box.setOutlineColor(sf::Color(accent.r, accent.g, accent.b, 160));
                    window.draw(box);
                };

                auto statRow = [&](float x, float y, float w, const std::string& label,
                                   const std::string& value, sf::Color valueColor)
                {
                    drawShadowText(window, label, x, y, 2.f, dim);
                    drawShadowText(window, value, x + w - pixelTextWidth(value, 2.f), y, 2.f, valueColor);
                };

                // Statistics.
                panel(50.f, 104.f, 340.f, 196.f, cyan);
                drawIcon(iconStats, {78.f, 130.f}, 28.f);
                drawShadowText(window, "STATISTICS", 100.f, 123.f, 2.f, cyan);
                statRow(68.f, 160.f, 304.f, "GAMES PLAYED", std::to_string(playerData.totalGamesPlayed), light);
                statRow(68.f, 192.f, 304.f, "PLAY TIME", formatDuration(playerData.totalPlayTimeSeconds), light);
                statRow(68.f, 224.f, 304.f, "ZOMBIES KILLED", std::to_string(playerData.totalKills), sf::Color(235, 150, 130));
                statRow(68.f, 256.f, 304.f, "COINS COLLECTED", std::to_string(playerData.totalCoinsCollected), sf::Color(255, 215, 80));

                // Personal bests.
                panel(410.f, 104.f, 340.f, 196.f, gold);
                drawIcon(iconTrophy, {438.f, 130.f}, 28.f);
                drawShadowText(window, "PERSONAL BEST", 460.f, 123.f, 2.f, gold);
                statRow(428.f, 160.f, 304.f, "BEST SCORE", std::to_string(playerData.bestScore), gold);
                statRow(428.f, 192.f, 304.f, "BEST WAVE", std::to_string(playerData.bestWave), gold);
                statRow(428.f, 224.f, 304.f, "BEST SURVIVAL", formatDuration(playerData.bestSurvivalTimeSeconds), gold);
                // Best score on each difficulty.
                for (int d = 0; d < 3; ++d)
                {
                    const float columnX = 428.f + static_cast<float>(d) * 104.f;
                    drawShadowText(window, DIFFICULTIES[d].name, columnX, 252.f, 2.f, DIFFICULTIES[d].accent);
                    drawShadowText(window, std::to_string(difficultyBests.score[static_cast<std::size_t>(d)]),
                                   columnX, 272.f, 2.f, light);
                }

                // Weapons found.
                int found = 0;
                for (int i = 0; i < WEAPON_COUNT; ++i)
                    if (playerData.weaponUnlocked[static_cast<std::size_t>(i)] != 0)
                        ++found;

                panel(50.f, 314.f, 700.f, 128.f, sf::Color(120, 205, 95));
                drawShadowText(window, "WEAPONS FOUND  " + std::to_string(found) + "/" + std::to_string(WEAPON_COUNT),
                               68.f, 326.f, 2.f, sf::Color(120, 205, 95));

                for (int i = 0; i < WEAPON_COUNT; ++i)
                {
                    const float x = 68.f + static_cast<float>(i) * 168.f;
                    const bool known = playerData.weaponUnlocked[static_cast<std::size_t>(i)] != 0;

                    sf::RectangleShape slot({160.f, 82.f});
                    slot.setPosition({x, 352.f});
                    slot.setFillColor(sf::Color(8, 12, 9, 200));
                    slot.setOutlineThickness(1.f);
                    slot.setOutlineColor(known ? sf::Color(120, 205, 95, 180) : sf::Color(70, 80, 72, 160));
                    window.draw(slot);

                    if (known && weaponTextureLoaded[static_cast<std::size_t>(i)])
                    {
                        const sf::Vector2u size = weaponTextures[static_cast<std::size_t>(i)].getSize();
                        const float scale = std::min(120.f / static_cast<float>(size.x),
                                                     40.f / static_cast<float>(size.y));
                        sf::Sprite sprite(weaponTextures[static_cast<std::size_t>(i)]);
                        sprite.setOrigin({static_cast<float>(size.x) * 0.5f, static_cast<float>(size.y) * 0.5f});
                        sprite.setScale({scale, scale});
                        sprite.setPosition({x + 80.f, 382.f});
                        window.draw(sprite);
                    }
                    else if (!known)
                    {
                        drawCenteredText(window, "?", x + 80.f, 369.f, 4.f, sf::Color(90, 100, 92));
                    }

                    drawCenteredText(window, known ? std::string(WEAPONS[i].name) : std::string("LOCKED"),
                                     x + 80.f, 414.f, 2.f,
                                     known ? sf::Color(235, 245, 225) : sf::Color(100, 112, 104));
                }

                // Saved run.
                panel(50.f, 454.f, 700.f, 52.f, sf::Color(120, 205, 95));
                drawIcon(iconContinue, {80.f, 480.f}, 30.f,
                         playerData.hasSavedRun ? sf::Color::White : sf::Color(255, 255, 255, 80));
                if (playerData.hasSavedRun)
                {
                    drawShadowText(window,
                        "SAVED RUN  " + std::string(DIFFICULTIES[std::clamp<int>(gameSettings.difficulty, 0, 2)].name) +
                        "  WAVE " + std::to_string(playerData.savedWave) +
                        "  SCORE " + std::to_string(playerData.savedScore) +
                        "  KILLS " + std::to_string(playerData.savedRunKills),
                        108.f, 472.f, 2.f, sf::Color(120, 205, 95));
                }
                else
                {
                    drawShadowText(window, "NO SAVED RUN  PROGRESS SAVES AT EVERY WAVE START",
                                   108.f, 472.f, 2.f, dim);
                }

                drawUiButton(window, uiControlsBackRect(), "BACK", 3,
                             sf::Color(120, 205, 95), buttonHover[0], buttonPress[0]);
            }
            else
            {
                // PHASE7_MAIN_MENU
                const float flicker =
                    0.82f + 0.18f * std::sin(uiTime * 9.f) * std::sin(uiTime * 3.1f + 1.f);
                const auto flickerAlpha = static_cast<std::uint8_t>(255.f * flicker);
                const sf::Color gold(255, 205, 95);

                drawCenteredText(window, "ZOMBIE SURVIVAL", 400.f, 30.f, 6.f,
                                 sf::Color(120, 205, 95, flickerAlpha));
                drawCenteredText(window, "ARENA", 400.f, 78.f, 8.f,
                                 sf::Color(220, 58, 45, flickerAlpha));

                // ---- CONTINUE (needs a saved run) ----
                {
                    const bool canContinue = playerData.hasSavedRun;
                    const UiRect r = uiMainRect(6);
                    const sf::Color accent = canContinue ? sf::Color(120, 205, 95)
                                                         : sf::Color(90, 100, 92);

                    drawUiButton(window, r, "", 3, accent, buttonHover[6], buttonPress[6]);
                    drawIcon(iconContinue, {r.x + 30.f, r.y + r.h / 2.f}, 32.f,
                             canContinue ? sf::Color::White : sf::Color(255, 255, 255, 80));
                    drawShadowText(window, "CONTINUE", r.x + 58.f, r.y + 14.f, 3.f,
                                   canContinue ? sf::Color(235, 245, 225) : sf::Color(120, 130, 122));

                    if (canContinue)
                    {
                        const std::string waveText = "WAVE " + std::to_string(playerData.savedWave);
                        const std::string scoreText = "SCORE " + std::to_string(playerData.savedScore);
                        drawPixelText(window, waveText,
                                      r.x + r.w - 12.f - pixelTextWidth(waveText, 2.f),
                                      r.y + 8.f, 2.f, gold);
                        drawPixelText(window, scoreText,
                                      r.x + r.w - 12.f - pixelTextWidth(scoreText, 2.f),
                                      r.y + 28.f, 2.f, sf::Color(190, 210, 190));
                    }
                    else
                    {
                        const std::string none = "NO SAVE";
                        drawPixelText(window, none,
                                      r.x + r.w - 12.f - pixelTextWidth(none, 2.f),
                                      r.y + 18.f, 2.f, sf::Color(120, 130, 122));
                    }
                }

                drawCenteredText(window, "NEW GAME  SELECT DIFFICULTY", 400.f, 214.f, 2.f,
                                 sf::Color(245, 245, 230));

                for (int i = 0; i < 3; ++i)
                {
                    const DifficultySettings& option = DIFFICULTIES[i];
                    const UiRect r = uiMainRect(i);
                    const std::size_t slot = static_cast<std::size_t>(i);

                    drawUiButton(window, r, "", 3, option.accent,
                                 buttonHover[slot], buttonPress[slot]);

                    drawShadowText(window, std::to_string(i + 1) + "  " + option.name,
                                   r.x + 20.f, r.y + 14.f, 3.f, option.accent);

                    const std::string bestText = "BEST " + std::to_string(highScores[slot]);
                    drawPixelText(window, bestText,
                                  r.x + r.w - 14.f - pixelTextWidth(bestText, 2.f),
                                  r.y + 18.f, 2.f, sf::Color(190, 210, 190));
                }

                // ---- Bottom row: settings, controls, profile, exit ----
                {
                    struct Extra { int item; const char* label; sf::Color accent; };
                    const Extra extras[5] = {
                        {3, "SETTINGS", sf::Color(150, 185, 225)},
                        {4, "CONTROLS", sf::Color(150, 185, 225)},
                        {7, "PROFILE",  sf::Color(90, 220, 255)},
                        {8, "CREDITS",  sf::Color(255, 205, 95)},
                        {5, "EXIT",     sf::Color(245, 75, 65)}
                    };

                    for (const Extra& extra : extras)
                    {
                        const std::size_t slot = static_cast<std::size_t>(extra.item);
                        const UiRect r = uiMainRect(extra.item);

                        drawUiButton(window, r, extra.item == 7 ? "" : extra.label, 2,
                                     extra.accent, buttonHover[slot], buttonPress[slot]);

                        if (extra.item == 7)
                        {
                            drawIcon(iconProfile, {r.x + 24.f, r.y + r.h / 2.f}, 26.f);
                            drawShadowText(window, "PROFILE", r.x + 44.f, r.y + 13.f, 2.f,
                                           sf::Color(235, 245, 235));
                        }
                    }
                }

                std::string description;
                sf::Color descriptionColor(190, 210, 190);

                if (menuSelection < 3)
                {
                    description = DIFFICULTIES[menuSelection].description;
                    descriptionColor = DIFFICULTIES[menuSelection].accent;
                }
                else if (menuSelection == 3)
                    description = "VOLUME  MUTE  FULLSCREEN";
                else if (menuSelection == 4)
                    description = "HOW TO PLAY";
                else if (menuSelection == 6)
                    description = playerData.hasSavedRun
                        ? "RESUME AT WAVE " + std::to_string(playerData.savedWave)
                        : "SAVES AT THE START OF EVERY WAVE";
                else if (menuSelection == 7)
                    description = "STATISTICS AND PERSONAL BESTS";
                else if (menuSelection == 8)
                    description = "ABOUT THE GAME";
                else
                    description = "CLOSE THE GAME";

                drawCenteredText(window, description, 400.f, 414.f, 2.f, descriptionColor);

                // ---- Personal best ----
                {
                    const std::string best = "PERSONAL BEST  SCORE " + std::to_string(playerData.bestScore) +
                        "  WAVE " + std::to_string(playerData.bestWave) +
                        "  TIME " + formatDuration(playerData.bestSurvivalTimeSeconds);
                    const float width = pixelTextWidth(best, 2.f) + 34.f;
                    const float left = 400.f - width / 2.f;

                    drawIcon(iconTrophy, {left + 13.f, 505.f}, 26.f);
                    drawShadowText(window, best, left + 34.f, 498.f, 2.f, gold);
                }

                drawCenteredText(window, "UP DOWN ENTER  1 2 3 START  C CONTINUE  P PROFILE  F11 FULLSCREEN",
                                 400.f, 548.f, 1.f, sf::Color(170, 190, 170));
            }

            drawFade();
            window.display();
            continue;
        }

        const DifficultySettings& difficulty = DIFFICULTIES[difficultyIndex];

        // Everything gameplay-related runs on deltaTime, which is 0 while paused:
        // zombies, bullets, timers, animations and power-ups all freeze.
        const float deltaTime = paused ? 0.f : rawDt;

        // Phase 7: run timer, finished runs, newly discovered weapons.
        saveNoticeTimer = std::max(0.f, saveNoticeTimer - rawDt);
        if (!gameOver)
            runSurvivalSeconds += static_cast<double>(deltaTime);

        if (gameOver && runActive)
            finishRun();   // Statistics + personal bests are saved at game over.

        {
            bool newWeapon = false;
            for (int i = 0; i < WEAPON_COUNT; ++i)
            {
                const std::size_t slot = static_cast<std::size_t>(i);
                if (weaponOwned[slot] && playerData.weaponUnlocked[slot] == 0)
                {
                    playerData.weaponUnlocked[slot] = 1;
                    newWeapon = true;
                }
            }
            if (newWeapon)
                saveProgress();
        }
        clockTotalSeconds += deltaTime;

        // --------------------------------------------------
        // 12. UPDATE BULLETS
        // --------------------------------------------------
        for (Bullet& bullet : bullets)
        {
            bullet.position += bullet.velocity * deltaTime;
            bullet.life -= deltaTime;

            // Walls, crates, barrels, cars and barricades stop bullets.
            if (!bullet.hitSolid && bulletVsWorld(bullet))
                bullet.hitSolid = true;
        }

        // --------------------------------------------------
        // 13. UPDATE EFFECTS
        // --------------------------------------------------
        muzzleFlashTimer = std::max(
            0.f,
            muzzleFlashTimer - deltaTime
        );

        recoilAmount = std::max(
            0.f,
            recoilAmount - RECOIL_RECOVERY_SPEED * deltaTime
        );

        // Update blood particles: move, slow down, and fade away.
        for (Particle& particle : particles)
        {
            particle.position += particle.velocity * deltaTime;
            particle.velocity *= std::max(0.f, 1.f - 3.8f * deltaTime);
            particle.life -= deltaTime;
        }

        particles.erase(
            std::remove_if(
                particles.begin(),
                particles.end(),
                [](const Particle& particle)
                {
                    return particle.life <= 0.f;
                }
            ),
            particles.end()
        );

        // Safety cap: never let particles pile up (e.g. many explosions at once).
        constexpr std::size_t MAX_PARTICLES = 1500;
        if (particles.size() > MAX_PARTICLES)
            particles.erase(particles.begin(),
                            particles.begin() + static_cast<std::ptrdiff_t>(particles.size() - MAX_PARTICLES));

        playerDamageCooldown = std::max(
            0.f,
            playerDamageCooldown - deltaTime
        );

        // --------------------------------------------------
        // 13b. WEAPON TIMERS, SWITCHING AND RELOADING
        // --------------------------------------------------
        fireCooldown      = std::max(0.f, fireCooldown - deltaTime);
        fireBufferTimer   = std::max(0.f, fireBufferTimer - deltaTime);
        switchTimer       = std::max(0.f, switchTimer - deltaTime);
        emptyClickTimer   = std::max(0.f, emptyClickTimer - deltaTime);
        noReserveTimer    = std::max(0.f, noReserveTimer - deltaTime);
        controlsHintTimer = std::max(0.f, controlsHintTimer - deltaTime);

        if (!sf::Mouse::isButtonPressed(sf::Mouse::Button::Left))
            requireMouseRelease = false;

        if (gameOver)
        {
            cancelReload();
        }
        else
        {
            if (weaponSwitchRequested >= 0)
            {
                if (!weaponOwned[static_cast<std::size_t>(weaponSwitchRequested)])
                    pushNotice(std::string(WEAPONS[weaponSwitchRequested].name) +
                               " LOCKED  KILL ZOMBIES TO FIND IT", sf::Color(255, 175, 60));
                else
                    switchWeapon(weaponSwitchRequested);
            }

            if (reloadRequested)
                startReload();

            if (reloading && switchTimer <= 0.f)
            {
                const WeaponDef& def = WEAPONS[currentWeapon];
                WeaponState& state = weaponStates[currentWeapon];

                reloadTimer += deltaTime;

                if (def.shellReload)
                {
                    // Shell by shell: every finished shell is kept at once.
                    while (reloading && reloadTimer >= def.reloadTime)
                    {
                        reloadTimer -= def.reloadTime;

                        if (state.reserve > 0 && state.magazine < def.magazineSize)
                        {
                            ++state.magazine;
                            --state.reserve;
                            ++reloadDone;
                            audio.play(SoundId::ShellLoad);
                        }

                        if (state.reserve <= 0 ||
                            state.magazine >= def.magazineSize)
                            cancelReload();   // Finished.
                    }
                }
                else if (reloadTimer >= def.reloadTime)
                {
                    // Only the missing bullets are loaded.
                    const int moved = std::min(
                        def.magazineSize - state.magazine,
                        state.reserve
                    );

                    state.magazine += moved;
                    state.reserve -= moved;
                    cancelReload();
                }
            }
        }

        // --------------------------------------------------
        // 13c. POWER-UP TIMERS, FLOATING TEXT AND SUPPLY DROPS
        // --------------------------------------------------
        if (!gameOver)
        {
            for (float& timer : powerTimers)
                timer = std::max(0.f, timer - deltaTime);

            // Occasional supply drop somewhere in the arena.
            if (!intermission)
            {
                supplyTimer -= deltaTime;

                if (supplyTimer <= 0.f)
                {
                    supplyTimer = PickupConfig::SUPPLY_INTERVAL_MIN +
                        unit01(randomEngine) *
                        (PickupConfig::SUPPLY_INTERVAL_MAX -
                         PickupConfig::SUPPLY_INTERVAL_MIN);

                    const float roll = unit01(randomEngine) * 100.f;
                    const PickupCategory category =
                        (roll < 45.f) ? PickupCategory::Health :
                        (roll < 80.f) ? PickupCategory::Ammo :
                        (roll < 95.f) ? PickupCategory::PowerUp :
                                        PickupCategory::Coin;

                    // Either the supply corner or somewhere reachable near the player.
                    for (int attempt = 0; attempt < 14; ++attempt)
                    {
                        sf::Vector2f spot;

                        if (unit01(randomEngine) < 0.35f)
                        {
                            spot = {
                                supplyZone.position.x + unit01(randomEngine) * supplyZone.size.x,
                                supplyZone.position.y + unit01(randomEngine) * supplyZone.size.y
                            };
                        }
                        else
                        {
                            const float angle = unit01(randomEngine) * 2.f * PI;
                            const float distance = 260.f + unit01(randomEngine) * 460.f;
                            spot = playerPosition +
                                sf::Vector2f(std::cos(angle), std::sin(angle)) * distance;
                        }

                        if (vectorLength(spot - playerPosition) < 110.f)
                            continue;

                        if (spawnPickup(rollTypeInCategory(category), spot))
                            break;
                    }
                }
            }

            if (PickupConfig::ENABLE_DEBUG_KEY && debugPickupRequested)
            {
                spawnPickup(
                    static_cast<PickupType>(debugPickupIndex % PICKUP_TYPE_COUNT),
                    playerPosition + sf::Vector2f(70.f, 0.f)
                );
                ++debugPickupIndex;
            }
        }

        for (FloatingText& note : floatingTexts)
        {
            note.life -= deltaTime;
            note.position.y -= 26.f * deltaTime;
        }

        floatingTexts.erase(
            std::remove_if(
                floatingTexts.begin(), floatingTexts.end(),
                [](const FloatingText& note) { return note.life <= 0.f; }
            ),
            floatingTexts.end()
        );

        // --------------------------------------------------
        // 13c2. PHASE 5 FEEDBACK TIMERS  (deltaTime = 0 while paused)
        // --------------------------------------------------
        damageFlash = std::max(0.f, damageFlash - deltaTime * 2.2f);
        hitMarkerTimer = std::max(0.f, hitMarkerTimer - deltaTime);
        crosshairKick = std::max(0.f, crosshairKick - deltaTime * 5.f);

        if (displayHealth > playerHealth)
            displayHealth = std::max(playerHealth, displayHealth - 45.f * deltaTime);
        else
            displayHealth = playerHealth;

        for (FloatingText& number : damageNumbers)
        {
            number.life -= deltaTime;
            number.position.y -= 46.f * deltaTime;
        }
        damageNumbers.erase(
            std::remove_if(damageNumbers.begin(), damageNumbers.end(),
                [](const FloatingText& n) { return n.life <= 0.f; }),
            damageNumbers.end());

        for (Notice& notice : notices)
            notice.life -= deltaTime;
        notices.erase(
            std::remove_if(notices.begin(), notices.end(),
                [](const Notice& n) { return n.life <= 0.f; }),
            notices.end());

        // Random zombie groans while zombies are alive.
        if (!gameOver)
            audio.updateZombieAmbient(deltaTime, countLivingZombies());

        // --------------------------------------------------
        // 13d. WORLD: EXPLOSION FUSES, EFFECTS AND CAMERA SHAKE
        // --------------------------------------------------
        for (EffectSprite& fx : effectSprites)
            fx.age += deltaTime;

        effectSprites.erase(
            std::remove_if(
                effectSprites.begin(), effectSprites.end(),
                [](const EffectSprite& fx) { return fx.age >= fx.life; }
            ),
            effectSprites.end()
        );

        for (Obstacle& prop : obstacles)
        {
            prop.hitFlash = std::max(0.f, prop.hitFlash - deltaTime);

            // Barrels that were hit (or caught in a blast) explode after a short fuse.
            if (prop.alive && prop.fuse >= 0.f && !gameOver)
            {
                prop.fuse -= deltaTime;

                if (prop.fuse <= 0.f)
                    explodeBarrel(prop);
            }
        }

        cameraShake = std::max(0.f, cameraShake - 24.f * deltaTime);

        // --------------------------------------------------
        // 14. PLAYER MOVEMENT
        // --------------------------------------------------
        sf::Vector2f movement(0.f, 0.f);

        if (!gameOver && !paused)
        {
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W))
                movement.y -= 1.f;

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S))
                movement.y += 1.f;

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A))
                movement.x -= 1.f;

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D))
                movement.x += 1.f;
        }

        const float movementLength = vectorLength(movement);

        if (movementLength > 0.001f)
            movement /= movementLength;

        // Walls, buildings, props and the map edge block the player; any
        // movement into them slides along the surface instead of sticking.
        playerPosition = moveActor(
            playerPosition,
            movement * PLAYER_SPEED * speedMultiplier() * deltaTime,
            WorldConfig::PLAYER_RADIUS
        );

        // --------------------------------------------------
        // 14b. PICKUPS: AGING, COLLECTION AND SPEED TRAIL
        // --------------------------------------------------
        if (!gameOver)
        {
            for (Pickup& pickup : pickups)
            {
                pickup.age += deltaTime;
                pickup.noticeCooldown = std::max(0.f, pickup.noticeCooldown - deltaTime);

                if (pickup.collecting)
                {
                    pickup.collectTimer += deltaTime;
                    continue;
                }

                const float distance = vectorLength(pickup.position - playerPosition);

                if (distance <= PickupConfig::COLLECT_RADIUS &&
                    pickup.age < pickup.lifetime &&
                    lineClearMove(playerPosition, pickup.position, 3.f))
                {
                    if (applyPickup(pickup))
                    {
                        pickup.collecting = true;   // Cannot be collected twice.
                        pickup.collectTimer = 0.f;

                        const PickupDef& pickedDef = PICKUPS[static_cast<int>(pickup.type)];
                        if (pickedDef.category == PickupCategory::Weapon)
                        {
                            audio.play(SoundId::PowerUp);
                            pushNotice(std::string("NEW WEAPON  ") + pickedDef.name, pickedDef.glow);
                        }
                        else if (pickedDef.category == PickupCategory::PowerUp)
                        {
                            audio.play(SoundId::PowerUp);
                            pushNotice(std::string("POWER-UP  ") + pickedDef.name, pickedDef.glow);
                        }
                        else
                        {
                            audio.play(SoundId::Pickup);
                        }

                        spawnPickupBurst(
                            pickup.position + sf::Vector2f(0.f, -18.f),
                            PICKUPS[static_cast<int>(pickup.type)].glow,
                            pickedDef.category == PickupCategory::Weapon ? 32 : 16, 1.2f
                        );
                    }
                }
            }

            pickups.erase(
                std::remove_if(
                    pickups.begin(), pickups.end(),
                    [](const Pickup& pickup)
                    {
                        return pickup.age >= pickup.lifetime ||
                               (pickup.collecting &&
                                pickup.collectTimer >= PickupConfig::COLLECT_ANIM_TIME);
                    }
                ),
                pickups.end()
            );

            // Speed boost leaves a glowing trail.
            speedTrailTimer -= deltaTime;
            if (speedMultiplier() > 1.f && movementLength > 0.001f &&
                speedTrailTimer <= 0.f)
            {
                speedTrailTimer = 0.04f;

                Particle trail;
                trail.position = playerPosition + sf::Vector2f(
                    signedUnit(randomEngine) * 6.f, -6.f
                );
                trail.velocity = {0.f, 0.f};
                trail.life = 0.35f;
                trail.maxLife = 0.35f;
                trail.size = 4.f;
                trail.color = sf::Color(120, 255, 190);
                particles.push_back(trail);
            }
        }

        // --------------------------------------------------
        // 14c. CAMERA AND ZOMBIE NAVIGATION
        // --------------------------------------------------
        {
            // Smooth follow, clamped so the view never leaves the map.
            const sf::Vector2f cameraTarget =
                playerPosition + sf::Vector2f(0.f, -WorldConfig::CAMERA_HEAD_OFFSET);
            const float follow = 1.f - std::exp(-WorldConfig::CAMERA_SMOOTHING * deltaTime);

            cameraCenter += (cameraTarget - cameraCenter) * follow;
            cameraCenter.x = std::clamp(cameraCenter.x, viewSize.x * 0.5f, WORLD_W - viewSize.x * 0.5f);
            cameraCenter.y = std::clamp(cameraCenter.y, viewSize.y * 0.5f, WORLD_H - viewSize.y * 0.5f);

            // Whole-pixel camera keeps the pixel art crisp.
            const sf::Vector2f snapped(std::round(cameraCenter.x), std::round(cameraCenter.y));
            aimView.setCenter(snapped);

            sf::Vector2f shake(0.f, 0.f);
            if (paused)
                shake = lastCameraShake;   // Frozen: no jitter while paused.
            else if (cameraShake > 0.01f)
                shake = {
                    std::round(signedUnit(randomEngine) * cameraShake),
                    std::round(signedUnit(randomEngine) * cameraShake)
                };
            lastCameraShake = shake;
            worldView.setCenter(snapped + shake);
        }

        updateNavigation(deltaTime);

        // --------------------------------------------------
        // 15. MOUSE AND FACING
        // --------------------------------------------------
        const sf::Vector2i mousePixel =
            sf::Mouse::getPosition(window);

        // The mouse is converted with the CAMERA view, so aiming stays correct
        // wherever the camera is.
        const sf::Vector2f mouseWorld =
            window.mapPixelToCoords(mousePixel, aimView);

        if (!paused && mouseWorld.x < playerPosition.x - 2.f)
            facingLeft = true;
        else if (!paused && mouseWorld.x > playerPosition.x + 2.f)
            facingLeft = false;

        const int facingRow = facingLeft ? 1 : 0;

        // --------------------------------------------------
        // 16. PLAYER WALKING ANIMATION
        // --------------------------------------------------
        if (movementLength > 0.001f && !gameOver)
        {
            animationTimer += deltaTime;

            while (animationTimer >= FRAME_TIME)
            {
                animationTimer -= FRAME_TIME;
                currentFrame = (currentFrame + 1) % COLUMNS;
            }
        }
        else
        {
            currentFrame = 0;
            animationTimer = 0.f;
        }

        const sf::IntRect frameRect(
            {currentFrame * FRAME_W, facingRow * FRAME_H},
            {FRAME_W, FRAME_H}
        );

        body.setTextureRect(frameRect);
        armsGun.setTextureRect(frameRect);

        // --------------------------------------------------
        // 17. AIMING AND CHARACTER LAYERS
        // --------------------------------------------------
        body.setPosition(playerPosition);

        const sf::Vector2f shoulderPosition =
            playerPosition + shoulderOffset;

        const sf::Vector2f aimDirection =
            mouseWorld - shoulderPosition;

        float targetAimAngle = std::atan2(
            aimDirection.y,
            std::abs(aimDirection.x)
        ) * RAD_TO_DEG;

        targetAimAngle = std::clamp(
            targetAimAngle,
            -MAX_AIM_ANGLE,
            MAX_AIM_ANGLE
        );

        const float smoothingFactor =
            1.f - std::exp(-AIM_SMOOTHING * deltaTime);

        currentAimAngle +=
            (targetAimAngle - currentAimAngle) * smoothingFactor;

        // Reload / weapon-switch animation: the gun dips and tilts.
        float weaponAnimTilt = 0.f;
        float weaponAnimDrop = 0.f;

        {
            const WeaponDef& def = WEAPONS[currentWeapon];

            if (switchTimer > 0.f)
            {
                // Weapon is being raised into position.
                const float t = std::clamp(
                    switchTimer / std::max(0.01f, def.swapTime), 0.f, 1.f
                );
                weaponAnimTilt = 55.f * t * t;
                weaponAnimDrop = 12.f * t * t;
            }
            else if (reloading)
            {
                if (def.shellReload)
                {
                    // Gentle bob for every shell pushed in.
                    const float phase = reloadTimer / def.reloadTime;
                    const float bob = 0.5f - 0.5f * std::cos(phase * 2.f * PI);
                    weaponAnimTilt = 22.f + 6.f * bob;
                    weaponAnimDrop = 4.f + 2.f * bob;
                }
                else
                {
                    // Lower, swap the magazine, bring the gun back up.
                    const float t = std::clamp(
                        reloadTimer / def.reloadTime, 0.f, 1.f
                    );
                    float pose = std::min({1.f, t / 0.18f, (1.f - t) / 0.18f});
                    pose = std::clamp(pose, 0.f, 1.f);
                    pose = pose * pose * (3.f - 2.f * pose);

                    const float jerk = (t > 0.45f && t < 0.60f)
                        ? std::sin((t - 0.45f) / 0.15f * PI)
                        : 0.f;

                    weaponAnimTilt = 40.f * pose + 5.f * jerk;
                    weaponAnimDrop = 8.f * pose + 2.f * jerk;
                }
            }
        }

        const sf::Vector2f weaponAnimOffset(0.f, weaponAnimDrop);

        const float animatedAngle = currentAimAngle + weaponAnimTilt;
        const float rotation =
            facingLeft ? -animatedAngle : animatedAngle;

        armsGun.setRotation(sf::degrees(rotation));

        const sf::Vector2f aimUnit = normalized(aimDirection);

        armsGun.setPosition(
            shoulderPosition - aimUnit * recoilAmount + weaponAnimOffset
        );

        // The point the GUN is really aiming at: straight along the barrel. The crosshair is drawn here and shots
        // and the laser go here, so crosshair, gun and bullets always agree.
        const sf::Vector2f aimMuzzle =
            armsGun.getTransform().transformPoint(weaponMuzzleLocal());
        const float aimRadians = currentAimAngle / RAD_TO_DEG;
        const sf::Vector2f gunDirection(
            (facingLeft ? -1.f : 1.f) * std::cos(aimRadians),
            std::sin(aimRadians)
        );
        // Fixed distance in front of the muzzle: the crosshair is rigidly
        // attached to the gun and only swings with its barrel.
        constexpr float AIM_RETICLE_DISTANCE = 75.f;
        const sf::Vector2f aimPoint = aimMuzzle + gunDirection * AIM_RETICLE_DISTANCE;

        // --------------------------------------------------
        // 18. WAVES: SPAWNING, PROGRESSION AND INTERMISSION
        // --------------------------------------------------
        if (!gameOver)
        {
            if (intermission)
            {
                // Break between waves: no spawning, just a countdown.
                intermissionTimer -= deltaTime;

                if (intermissionTimer <= 0.f)
                {
                    beginWave(currentWave + 1);
                    writeCheckpoint(currentWave);   // Phase 7: autosave once, at every wave start.
                }
            }
            else
            {
                spawnTimer += deltaTime;

                const float spawnInterval =
                    waveSpawnInterval(currentWave, difficulty);

                if (waveZombiesSpawned < waveZombieTotal &&
                    spawnTimer >= spawnInterval &&
                    countLivingZombies() < static_cast<std::size_t>(std::round(
                        static_cast<float>(waveMaxAlive(currentWave)) * difficulty.maxAliveMultiplier)))
                {
                    spawnTimer = 0.f;

                    Zombie zombie;
                    zombie.id = nextZombieId++;

                    const SpawnPoint spawn = chooseSpawnPoint();
                    zombie.position = spawn.position;
                    lastSpawnSide = spawn.side;

                    // Zombies gain one extra hit point every three waves,
                    // up to 8 HP, so later waves become tougher gradually.
                    zombie.maxHealth = std::min(
                        8.f,
                        3.f + static_cast<float>((currentWave - 1) / 3)
                    );
                    zombie.health = zombie.maxHealth;
                    zombie.speed =
                        (speedDistribution(randomEngine) + waveSpeedBonus(currentWave)) *
                        difficulty.zombieSpeedMultiplier;
                    zombie.spawnShieldTimer = 3.f;

                    zombies.push_back(zombie);
                    ++waveZombiesSpawned;

                    std::cout << "Zombie spawned! Wave " << currentWave
                              << " (" << waveZombiesSpawned << "/"
                              << waveZombieTotal << ")\n";
                }

                // Wave cleared only after every zombie has been removed.
                // This includes the complete death animation and corpse linger.
                if (waveZombiesSpawned >= waveZombieTotal &&
                    zombies.empty())
                {
                    intermission = true;
                    intermissionTimer = WaveConfig::INTERMISSION_DURATION;

                    playerHealth = std::min(
                        100.f,
                        playerHealth + difficulty.intermissionHeal
                    );

                    restockAmmo();

                    std::cout << "Wave " << currentWave << " cleared!\n";
                }
            }
        }

        // --------------------------------------------------
        // 19. ZOMBIE AI: CHASE AND ATTACK THE PLAYER
        // --------------------------------------------------
        if (!gameOver)
        {
            nextZombiePositions.assign(zombies.size(), sf::Vector2f(0.f, 0.f));   // Reuses its memory.

            for (std::size_t i = 0; i < zombies.size(); ++i)
            {
                Zombie& zombie = zombies[i];

                // Dying zombies only play their death animation.
                // They do not move, chase, attack, or turn around.
                if (zombie.isDying)
                {
                    nextZombiePositions[i] = zombie.position;

                    if (zombie.deathFrame < ZOMBIE_DEATH_FRAME_COUNT - 1)
                    {
                        zombie.deathAnimationTimer += deltaTime;

                        while (zombie.deathAnimationTimer >= ZOMBIE_DEATH_FRAME_TIME &&
                               zombie.deathFrame < ZOMBIE_DEATH_FRAME_COUNT - 1)
                        {
                            zombie.deathAnimationTimer -= ZOMBIE_DEATH_FRAME_TIME;
                            ++zombie.deathFrame;

                            // The corpse hits the ground: leave a blood stain.
                            if (zombie.deathFrame == ZOMBIE_DEATH_FRAME_COUNT - 1)
                                spawnBloodStain(zombie.position);
                        }
                    }
                    else
                    {
                        // Final frame: hold briefly, then mark for removal.
                        zombie.corpseTimer += deltaTime;

                        if (zombie.corpseTimer >= ZOMBIE_CORPSE_LINGER)
                            zombie.deathFinished = true;
                    }

                    continue;
                }

                const sf::Vector2f toPlayer = playerPosition - zombie.position;
                const float distanceToPlayer = vectorLength(toPlayer);

                zombie.facingLeft = (toPlayer.x < 0.f);
                zombie.hitFlashTimer = std::max(0.f, zombie.hitFlashTimer - deltaTime);
                zombie.spawnShieldTimer = std::max(0.f, zombie.spawnShieldTimer - deltaTime);
                zombie.attackCooldownTimer = std::max(0.f, zombie.attackCooldownTimer - deltaTime);

                // Stay in place until the current attack animation finishes.
                if (zombie.isAttacking)
                {
                    nextZombiePositions[i] = zombie.position;
                    zombie.attackAnimationTimer += deltaTime;

                    while (zombie.attackAnimationTimer >= ZOMBIE_ATTACK_FRAME_TIME)
                    {
                        zombie.attackAnimationTimer -= ZOMBIE_ATTACK_FRAME_TIME;
                        ++zombie.attackFrame;

                        // Apply one hit during the claw-swing frame.
                        if (zombie.attackFrame == ZOMBIE_ATTACK_DAMAGE_FRAME &&
                            !zombie.attackDamageApplied)
                        {
                            zombie.attackDamageApplied = true;

                            if (distanceToPlayer <= ZOMBIE_ATTACK_RANGE &&
                                playerDamageCooldown <= 0.f &&
                                lineClearMove(zombie.position, playerPosition, 2.f))
                            {
                                playerHealth = std::max(
                                    0.f,
                                    playerHealth -
                                        difficulty.zombieDamage * shieldMultiplier()
                                );
                                playerDamageCooldown = 0.65f;
                                onPlayerHurt(difficulty.zombieDamage * shieldMultiplier());

                                std::cout << "Zombie attack! Player health: "
                                          << playerHealth << "\n";

                                if (playerHealth <= 0.f)
                                {
                                    onPlayerDeath();
                                    std::cout << "\nGAME OVER! Final score: "
                                              << score
                                              << " (reached wave " << currentWave << ")"
                                              << "\nPress R to restart or M for menu.\n";
                                }
                            }
                        }

                        if (zombie.attackFrame >= ZOMBIE_ATTACK_FRAME_COUNT)
                        {
                            zombie.isAttacking = false;
                            zombie.attackFrame = 0;
                            zombie.attackAnimationTimer = 0.f;
                            zombie.attackCooldownTimer = ZOMBIE_ATTACK_COOLDOWN;
                            zombie.attackDamageApplied = false;
                            break;
                        }
                    }
                    continue;
                }

                // Stop at attack range. Start a new attack when its cooldown is ready.
                // (A wall between the zombie and the player blocks the attack.)
                if (distanceToPlayer <= ZOMBIE_ATTACK_RANGE &&
                    lineClearMove(zombie.position, playerPosition, 2.f))
                {
                    nextZombiePositions[i] = zombie.position;
                    zombie.animationFrame = 0;
                    zombie.animationTimer = 0.f;

                    if (zombie.attackCooldownTimer <= 0.f)
                    {
                        zombie.isAttacking = true;
                        zombie.attackFrame = 0;
                        zombie.attackAnimationTimer = 0.f;
                        zombie.attackDamageApplied = false;
                        audio.play(SoundId::ZombieAttack);
                    }
                    continue;
                }

                // Chase the player and avoid stacking with other zombies.
                sf::Vector2f separation(0.f, 0.f);
                constexpr float SEPARATION_RADIUS = 58.f;

                for (std::size_t j = 0; j < zombies.size(); ++j)
                {
                    if (i == j)
                        continue;

                    // Corpses don't push living zombies around.
                    if (zombies[j].isDying)
                        continue;

                    const sf::Vector2f away = zombie.position - zombies[j].position;
                    const float distance = vectorLength(away);

                    if (distance > 0.001f && distance < SEPARATION_RADIUS)
                    {
                        const float strength =
                            (SEPARATION_RADIUS - distance) / SEPARATION_RADIUS;
                        separation += (away / distance) * strength;
                    }
                }

                // Walk straight when the way is clear, otherwise follow the
                // navigation field around walls, buildings and props.
                const sf::Vector2f chaseDirection = zombieChaseDirection(zombie.position);
                const sf::Vector2f moveDirection =
                    normalized(chaseDirection + separation * 2.6f);

                // Face the way the zombie is actually walking.
                if (moveDirection.x < -0.12f)
                    zombie.facingLeft = true;
                else if (moveDirection.x > 0.12f)
                    zombie.facingLeft = false;

                nextZombiePositions[i] = moveActor(
                    zombie.position,
                    moveDirection * zombie.speed * deltaTime,
                    WorldConfig::ZOMBIE_RADIUS
                );

                zombie.animationTimer += deltaTime;
                while (zombie.animationTimer >= ZOMBIE_ANIMATION_TIME)
                {
                    zombie.animationTimer -= ZOMBIE_ANIMATION_TIME;
                    zombie.animationFrame =
                        (zombie.animationFrame + 1) % ZOMBIE_FRAME_COUNT;
                }
            }

            for (std::size_t i = 0; i < zombies.size(); ++i)
                zombies[i].position = nextZombiePositions[i];
        }

        // --------------------------------------------------
        // 20. BULLET-ZOMBIE COLLISIONS
        // --------------------------------------------------
        if (!gameOver)
        {
            for (Bullet& bullet : bullets)
            {
                if (bullet.life <= 0.f)
                    continue;

                for (Zombie& zombie : zombies)
                {
                    // Dying zombies can no longer be damaged,
                    // so bullets pass straight through them.
                    if (zombie.isDying ||
                        zombie.health <= 0.f ||
                        zombie.spawnShieldTimer > 0.f)
                        continue;

                    // A piercing bullet only damages each zombie once.
                    if (std::find(bullet.hitZombieIds.begin(),
                                  bullet.hitZombieIds.end(),
                                  zombie.id) != bullet.hitZombieIds.end())
                        continue;

                    // The sprite origin is at the feet. Aim the hitbox
                    // upward so bullets collide with the visible torso.
                    const sf::Vector2f zombieBodyCenter =
                        zombie.position + sf::Vector2f(0.f, -39.f);

                    const sf::Vector2f difference =
                        bullet.position - zombieBodyCenter;

                    const float distanceSquared =
                        difference.x * difference.x +
                        difference.y * difference.y;

                    // Covers the visible torso while remaining reasonably precise.
                    constexpr float HIT_RADIUS = 23.f;

                    if (distanceSquared <= HIT_RADIUS * HIT_RADIUS)
                    {
                        zombie.health -= bullet.damage;
                        zombie.hitFlashTimer = 0.18f;
                        bullet.hitZombieIds.push_back(zombie.id);

                        // Hit feedback: sound, hit marker and a damage number.
                        audio.play(SoundId::BulletImpact, 0.7f);
                        hitMarkerTimer = 0.2f;
                        hitMarkerKill = (zombie.health <= 0.f);
                        spawnDamageNumber(
                            zombie.position + sf::Vector2f(0.f, -64.f),
                            bullet.damage, hitMarkerKill);

                        if (bullet.pierceLeft > 0)
                            --bullet.pierceLeft;   // Keeps flying (sniper).
                        else
                            bullet.life = 0.f;

                        // Small impact burst on every bullet hit.
                        spawnBloodBurst(
                            zombie.position + sf::Vector2f(0.f, -39.f),
                            static_cast<int>(5.f + bullet.damage * 2.f),
                            false
                        );

                        if (zombie.health <= 0.f)
                            killZombie(zombie);

                        break;
                    }
                }
            }
        }

        // Remove zombies whose death animation has finished.
        zombies.erase(
            std::remove_if(
                zombies.begin(),
                zombies.end(),
                [](const Zombie& zombie)
                {
                    return zombie.deathFinished;
                }
            ),
            zombies.end()
        );

        // Remove expired or used bullets.
        bullets.erase(
            std::remove_if(
                bullets.begin(),
                bullets.end(),
                [](const Bullet& bullet)
                {
                    return bullet.life <= 0.f ||
                           bullet.hitSolid ||
                           bullet.position.x < -20.f ||
                           bullet.position.x > WORLD_W + 20.f ||
                           bullet.position.y < -80.f ||
                           bullet.position.y > WORLD_H + 20.f;
                }
            ),
            bullets.end()
        );

        // Player damage is applied at the correct moment in the attack animation.

        // --------------------------------------------------
        // 22. FIRING  (per-weapon behaviour, ammunition, reload rules)
        // --------------------------------------------------
        if (!gameOver && !paused)
        {
            const WeaponDef& weapon = WEAPONS[currentWeapon];
            WeaponState& ammo = weaponStates[currentWeapon];

            const bool fireHeld =
                !requireMouseRelease &&
                sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);

            if (fireRequested)
                fireBufferTimer = WeaponConfig::FIRE_INPUT_BUFFER;

            const bool autoHeld =
                fireHeld && weapon.mode == FireMode::FullAuto;
            const bool triggerPulled = fireBufferTimer > 0.f || autoHeld;

            // A magazine reload blocks shooting; shotgun shell loading does not.
            const bool reloadBlocksFire = reloading && !weapon.shellReload;
            const bool canShoot =
                switchTimer <= 0.f &&
                fireCooldown <= 0.f &&
                !reloadBlocksFire;

            if (triggerPulled && canShoot)
            {
                if (ammo.magazine <= 0)
                {
                    // Empty magazine: no shot. The player decides when to reload.
                    fireBufferTimer = 0.f;

                    if (!reloading && emptyClickTimer <= 0.f)
                    {
                        emptyClickTimer = 0.9f;
                        audio.play(SoundId::EmptyClick);
                    }

                    if (WeaponConfig::AUTO_RELOAD_ON_EMPTY)
                        startReload();
                }
                else
                {
                    // Firing interrupts shotgun shell loading (shells stay loaded).
                    if (reloading)
                        cancelReload();

                    const sf::Vector2f muzzleLocal = weaponMuzzleLocal();
                    const sf::Vector2f muzzleWorld =
                        armsGun.getTransform().transformPoint(muzzleLocal);

                    const sf::Vector2f baseDirection =
                        normalized(aimPoint - muzzleWorld);

                    if (vectorLength(baseDirection) > 0.001f)
                    {
                        --ammo.magazine;

                        // Shot sound, crosshair kick and a little camera shake
                        // (stronger for the shotgun and the sniper).
                        audio.playWeaponShot(currentWeapon);
                        crosshairKick = std::min(1.f, crosshairKick + 0.35f);
                        cameraShake = std::max(cameraShake, weapon.recoil * 0.22f);

                        fireCooldown = weapon.fireInterval / rapidFireMultiplier();
                        fireBufferTimer = 0.f;

                        // Automatic fire loses accuracy while the gun is kicking.
                        const float maxKick =
                            std::max(MAX_RECOIL, weapon.recoil);
                        const float bloom =
                            (weapon.mode == FireMode::FullAuto)
                                ? 1.f + 0.9f * (recoilAmount / maxKick)
                                : 1.f;
                        const float spread = weapon.spreadDegrees * bloom;

                        for (int p = 0; p < weapon.pellets; ++p)
                        {
                            float angleOffset;

                            if (weapon.pellets > 1)
                            {
                                // Even fan of pellets plus a little randomness.
                                const float slot =
                                    (static_cast<float>(p) + 0.5f) /
                                    static_cast<float>(weapon.pellets) * 2.f - 1.f;
                                angleOffset =
                                    slot * spread * 0.75f +
                                    signedUnit(randomEngine) * spread * 0.35f;
                            }
                            else
                            {
                                angleOffset = signedUnit(randomEngine) * spread;
                            }

                            const sf::Vector2f direction =
                                rotateVector(baseDirection, angleOffset);

                            const float speed = weapon.bulletSpeed *
                                (weapon.pellets > 1
                                    ? 1.f + 0.08f * signedUnit(randomEngine)
                                    : 1.f);

                            Bullet newBullet;
                            newBullet.position = muzzleWorld + direction * 5.f;
                            newBullet.previousGround = playerPosition;   // Wall test starts at the feet.
                            newBullet.velocity = direction * speed;
                            newBullet.life = weapon.bulletLife;
                            newBullet.damage = weapon.damage * damageMultiplier();
                            newBullet.pierceLeft = weapon.pierce;
                            newBullet.radius = weapon.bulletRadius;
                            newBullet.tailLength = weapon.bulletTail;
                            newBullet.color = weapon.bulletColor;
                            newBullet.outline = weapon.bulletOutline;

                            if (damageMultiplier() > 1.f)
                            {
                                // Boosted bullets glow red-orange and hit harder.
                                newBullet.color = sf::Color(255, 110, 80);
                                newBullet.outline = sf::Color(255, 210, 90);
                                newBullet.radius = weapon.bulletRadius + 1.f;
                            }

                            bullets.push_back(newBullet);
                        }

                        // Weapon effects.
                        muzzleFlashPosition = muzzleWorld;
                        muzzleFlashDirection = baseDirection;
                        muzzleFlashTimer = FLASH_DURATION;
                        muzzleFlashScale = weapon.flashScale;

                        recoilAmount = std::min(
                            recoilAmount + weapon.recoil,
                            maxKick
                        );

                        spawnCasing(
                            muzzleWorld - baseDirection * 18.f,
                            baseDirection,
                            currentWeapon == static_cast<int>(WeaponId::Shotgun)
                        );

                        armsGun.setPosition(
                            shoulderPosition - aimUnit * recoilAmount +
                            weaponAnimOffset
                        );
                    }
                }
            }
            else if (triggerPulled && reloadBlocksFire)
            {
                fireBufferTimer = 0.f;   // Cannot shoot while reloading.
            }
        }

        // --------------------------------------------------
        // 23. DRAW EVERYTHING
        // --------------------------------------------------
        highScores[difficultyIndex] = std::max(highScores[difficultyIndex], score);
        const int highScore = highScores[difficultyIndex];
        waveBannerTimer = std::max(0.f, waveBannerTimer - deltaTime);

        window.clear(sf::Color(18, 20, 18));

        // --------------------------------------------------
        // WORLD LAYER (drawn through the camera view)
        // --------------------------------------------------
        window.setView(worldView);

        const sf::Vector2f viewCenter = worldView.getCenter();
        auto onScreen = [&](const sf::Vector2f& p, float margin)
        {
            return std::abs(p.x - viewCenter.x) < viewSize.x * 0.5f + margin &&
                   std::abs(p.y - viewCenter.y) < viewSize.y * 0.5f + margin;
        };

        {
            sf::RenderStates states;

            for (const auto& entry : groundBatches)
            {
                states.texture = entry.second.texture;
                window.draw(entry.second.vertices, states);
            }

            for (const auto& entry : decalBatches)
            {
                states.texture = entry.second.texture;
                window.draw(entry.second.vertices, states);
            }

            // Scorch marks, broken crates and burnt barrels.
            for (const DynamicDecal& decal : dynamicDecals)
            {
                if (!onScreen(decal.position, 80.f))
                    continue;

                if (decal.scorchRadius > 0.f)
                {
                    sf::CircleShape scorch(decal.scorchRadius);
                    scorch.setOrigin({decal.scorchRadius, decal.scorchRadius});
                    scorch.setScale({1.f, 0.62f});
                    scorch.setPosition(decal.position);
                    scorch.setFillColor(sf::Color(decal.tint.r, decal.tint.g, decal.tint.b, static_cast<std::uint8_t>(decal.alpha)));
                    window.draw(scorch);
                }
                else if (decal.texture)
                {
                    sf::Sprite sprite(*decal.texture);
                    sprite.setOrigin(decal.size * 0.5f);
                    sprite.setPosition(decal.position);
                    sprite.setRotation(sf::degrees(decal.rotation));
                    window.draw(sprite);
                }
            }

            states.texture = nullptr;
            window.draw(shadowBatch.vertices, states);

            for (const auto& entry : wallBatches)
            {
                states.texture = entry.second.texture;
                window.draw(entry.second.vertices, states);
            }

            // Ground-level decoration (bushes, bins, debris, cones).
            for (const Decoration& decoration : decorations)
            {
                if (decoration.overhead || !onScreen(decoration.position, 60.f))
                    continue;

                sf::Sprite sprite(*decoration.texture);
                sprite.setOrigin(decoration.size * 0.5f);
                sprite.setPosition(decoration.position);
                sprite.setRotation(sf::degrees(decoration.rotation));
                window.draw(sprite);
            }
        }

        // Draw dying zombies first so corpses lie underneath
        // living zombies and the player.
        for (const Zombie& zombie : zombies)
        {
            if (!zombie.isDying)
                continue;

            const float deathHorizontalScale =
                zombie.facingLeft
                    ? -ZOMBIE_DEATH_SPRITE_SCALE
                    : ZOMBIE_DEATH_SPRITE_SCALE;

            const DeathFrame& deathFrame = deathFrames[zombie.deathFrame];

            zombieDeathSprite.setTextureRect(deathFrame.rect);
            zombieDeathSprite.setOrigin(deathFrame.origin);
            zombieDeathSprite.setScale({
                deathHorizontalScale, ZOMBIE_DEATH_SPRITE_SCALE
            });
            zombieDeathSprite.setPosition(
                zombie.position + sf::Vector2f(0.f, ZOMBIE_DEATH_GROUND_OFFSET_Y)
            );

            // Fade out during the final-frame linger.
            float corpseAlpha = 1.f;
            if (zombie.deathFrame >= ZOMBIE_DEATH_FRAME_COUNT - 1 &&
                ZOMBIE_CORPSE_LINGER > 0.f)
            {
                corpseAlpha = std::clamp(
                    1.f - zombie.corpseTimer / ZOMBIE_CORPSE_LINGER,
                    0.f,
                    1.f
                );
            }

            zombieDeathSprite.setColor(sf::Color(
                255, 255, 255,
                static_cast<std::uint8_t>(corpseAlpha * 255.f)
            ));
            window.draw(zombieDeathSprite);
        }

        // Pickups lie on the ground below the living zombies and the player.
        for (const Pickup& pickup : pickups)
        {
            const int index = static_cast<int>(pickup.type);
            const PickupDef& def = PICKUPS[index];

            float alpha = 1.f;
            float scale = 1.f;
            float lift = 0.f;

            const float timeLeft = pickup.lifetime - pickup.age;

            if (pickup.collecting)
            {
                // Collection animation: grow, rise and fade.
                const float k = std::clamp(
                    pickup.collectTimer / PickupConfig::COLLECT_ANIM_TIME, 0.f, 1.f
                );
                scale = 1.f + 0.9f * k;
                alpha = 1.f - k;
                lift = 26.f * k;
            }
            else
            {
                // Blink during the last seconds, then fade out completely.
                if (timeLeft < 4.f)
                    alpha = (std::fmod(pickup.age, 0.32f) < 0.16f) ? 0.4f : 1.f;
                if (timeLeft < 0.8f)
                    alpha *= std::clamp(timeLeft / 0.8f, 0.f, 1.f);
            }

            const float pulse = 0.5f + 0.5f * std::sin(pickup.age * 4.f + pickup.bobPhase);
            const float bob = std::sin(pickup.age * 3.2f + pickup.bobPhase) * 4.f;
            const sf::Vector2f center(
                pickup.position.x,
                pickup.position.y - 22.f - bob - lift
            );
            const float half = def.displaySize * 0.5f * scale;
            const auto a = [&](float value)
            {
                return static_cast<std::uint8_t>(std::clamp(value * alpha, 0.f, 255.f));
            };

            // Ground shadow (shrinks as the item floats up).
            sf::CircleShape shadow(half * 0.9f);
            shadow.setOrigin({half * 0.9f, half * 0.9f});
            shadow.setScale({1.f, 0.32f});
            shadow.setPosition(pickup.position + sf::Vector2f(0.f, 2.f));
            shadow.setFillColor(sf::Color(0, 0, 0, a(95.f - 25.f * pulse)));
            window.draw(shadow);

            // Soft glow made of two translucent discs plus a pulsing ring.
            const float glowRadius = half + 12.f + 3.f * pulse;
            sf::CircleShape glow(glowRadius);
            glow.setOrigin({glowRadius, glowRadius});
            glow.setPosition(center);
            glow.setFillColor(sf::Color(def.glow.r, def.glow.g, def.glow.b, a(34.f)));
            window.draw(glow);

            const float innerRadius = half + 5.f;
            glow.setRadius(innerRadius);
            glow.setOrigin({innerRadius, innerRadius});
            glow.setFillColor(sf::Color(def.glow.r, def.glow.g, def.glow.b, a(62.f)));
            glow.setOutlineThickness(1.5f);
            glow.setOutlineColor(sf::Color(def.glow.r, def.glow.g, def.glow.b, a(90.f + 110.f * pulse)));
            window.draw(glow);

            if (def.category == PickupCategory::Weapon &&
                weaponTextureLoaded[static_cast<std::size_t>(weaponIndexOf(pickup.type))])
            {
                // Weapon drop: the weapon's own sprite, hovering above the ground.
                const std::size_t w = static_cast<std::size_t>(weaponIndexOf(pickup.type));
                const sf::Vector2u size = weaponTextures[w].getSize();
                const float iconScale =
                    (def.displaySize * scale) / static_cast<float>(size.x);

                sf::Sprite icon(weaponTextures[w]);
                icon.setOrigin({
                    static_cast<float>(size.x) / 2.f,
                    static_cast<float>(size.y) / 2.f
                });
                icon.setScale({iconScale, iconScale});
                icon.setPosition(center);
                icon.setColor(sf::Color(255, 255, 255, a(255.f)));
                window.draw(icon);
            }
            else if (pickupTextureLoaded[index])
            {
                const sf::Vector2u size = pickupTextures[index].getSize();
                const float iconScale =
                    (def.displaySize * scale) / static_cast<float>(size.x);

                sf::Sprite icon(pickupTextures[index]);
                icon.setOrigin({
                    static_cast<float>(size.x) / 2.f,
                    static_cast<float>(size.y) / 2.f
                });
                icon.setScale({iconScale, iconScale});
                icon.setPosition(center);
                icon.setColor(sf::Color(255, 255, 255, a(255.f)));
                window.draw(icon);
            }
            else
            {
                // Placeholder if the PNG is missing.
                sf::CircleShape orb(half * 0.7f);
                orb.setOrigin({half * 0.7f, half * 0.7f});
                orb.setPosition(center);
                orb.setFillColor(sf::Color(def.glow.r, def.glow.g, def.glow.b, a(230.f)));
                orb.setOutlineThickness(2.f);
                orb.setOutlineColor(sf::Color(255, 255, 255, a(220.f)));
                window.draw(orb);
            }

            // Weapon loot gets a small name tag above it.
            if (def.category == PickupCategory::Weapon && !pickup.collecting)
                drawCenteredText(
                    window, def.name, center.x, center.y - half - 20.f, 1.5f,
                    sf::Color(def.glow.r, def.glow.g, def.glow.b, a(255.f))
                );
        }

        // Crates, barrels, cars, barricades and sandbags.
        for (const Obstacle& prop : obstacles)
        {
            if (!prop.alive || prop.kind == ObstacleKind::Wall ||
                prop.kind == ObstacleKind::Fence || !onScreen(prop.spriteCenter, 90.f))
                continue;

            if (!prop.texture)
            {
                // Missing PNG: draw the collision box so the prop is still visible.
                sf::RectangleShape box(prop.moveRect.size);
                box.setPosition(prop.moveRect.position);
                box.setFillColor(sf::Color(150, 110, 70));
                box.setOutlineThickness(2.f);
                box.setOutlineColor(sf::Color(40, 30, 20));
                window.draw(box);
                continue;
            }

            // Ground shadow.
            const float shadowWidth = prop.moveRect.size.x * 0.62f + 6.f;
            sf::CircleShape shadow(shadowWidth);
            shadow.setOrigin({shadowWidth, shadowWidth});
            shadow.setScale({1.f, 0.32f});
            shadow.setPosition({
                prop.moveRect.position.x + prop.moveRect.size.x * 0.5f,
                prop.moveRect.position.y + prop.moveRect.size.y
            });
            shadow.setFillColor(sf::Color(0, 0, 0, 80));
            window.draw(shadow);

            const bool damaged = prop.damagedTexture && prop.maxHp > 0.f &&
                                 prop.hp <= prop.maxHp * 0.5f;
            const sf::Texture& texture = damaged ? *prop.damagedTexture : *prop.texture;
            const sf::Vector2u textureSize = texture.getSize();

            sf::Sprite sprite(texture);
            sprite.setOrigin({
                static_cast<float>(textureSize.x) * 0.5f,
                static_cast<float>(textureSize.y) * 0.5f
            });
            sprite.setPosition(prop.spriteCenter);
            sprite.setRotation(sf::degrees(prop.rotation));

            if (prop.fuse >= 0.f)
            {
                // About to explode: flicker bright red.
                const bool flash = std::fmod(clockTotalSeconds, 0.12f) < 0.06f;
                sprite.setColor(flash ? sf::Color(255, 120, 90) : sf::Color(255, 255, 255));
            }
            else if (prop.hitFlash > 0.f)
                sprite.setColor(sf::Color(255, 205, 190));

            window.draw(sprite);
        }

        // Draw animated pixel-art zombies so the player stays in front.
        for (const Zombie& zombie : zombies)
        {
            if (zombie.isDying)
                continue;

            if (zombie.spawnShieldTimer > 0.f)
            {
                const float pulse =
                    0.5f + 0.5f * std::sin(zombie.spawnShieldTimer * 12.f);
                const auto shieldAlpha = static_cast<std::uint8_t>(
                    90.f + pulse * 100.f
                );

                zombieShield.setPosition(
                    zombie.position + sf::Vector2f(0.f, -40.f)
                );
                zombieShield.setFillColor(
                    sf::Color(35, 165, 255, 24)
                );
                zombieShield.setOutlineColor(
                    sf::Color(75, 205, 255, shieldAlpha)
                );
                window.draw(zombieShield);
            }

            drawGroundShadow(zombie.position, 1.15f, 95);

            // Red-white flash when hit; a faint red glow while the claw swings.
            const float hitK = std::clamp(zombie.hitFlashTimer / 0.18f, 0.f, 1.f);
            sf::Color zombieTint = lerpColor(sf::Color::White, sf::Color(255, 95, 85), hitK);
            if (zombie.isAttacking && zombie.attackFrame >= ZOMBIE_ATTACK_DAMAGE_FRAME - 1)
                zombieTint = lerpColor(zombieTint, sf::Color(255, 150, 140), 0.55f);

            const float zombieHorizontalScale =
                zombie.facingLeft ? -ZOMBIE_SPRITE_SCALE : ZOMBIE_SPRITE_SCALE;

            if (zombie.isAttacking)
            {
                zombieAttackSprite.setTextureRect(sf::IntRect(
                    {zombie.attackFrame * ZOMBIE_FRAME_W, 0},
                    {ZOMBIE_FRAME_W, ZOMBIE_FRAME_H}
                ));
                zombieAttackSprite.setScale({
                    zombieHorizontalScale, ZOMBIE_SPRITE_SCALE
                });
                zombieAttackSprite.setPosition(zombie.position);
                zombieAttackSprite.setColor(zombieTint);
                window.draw(zombieAttackSprite);
            }
            else
            {
                zombieSprite.setTextureRect(sf::IntRect(
                    {zombie.animationFrame * ZOMBIE_FRAME_W, 0},
                    {ZOMBIE_FRAME_W, ZOMBIE_FRAME_H}
                ));
                zombieSprite.setScale({
                    zombieHorizontalScale, ZOMBIE_SPRITE_SCALE
                });
                zombieSprite.setPosition(zombie.position);
                zombieSprite.setColor(zombieTint);
                window.draw(zombieSprite);
            }

            // Keep the health bar above the animated zombie.
            zombieHealthBackground.setPosition(
                zombie.position + sf::Vector2f(-14.f, -42.f)
            );
            if (zombie.spawnShieldTimer > 0.f)
            {
                zombieHealthBackground.setOutlineThickness(2.f);
                zombieHealthBackground.setOutlineColor(sf::Color(75, 205, 255));
            }
            else if (zombie.hitFlashTimer > 0.f)
            {
                zombieHealthBackground.setOutlineThickness(2.f);
                zombieHealthBackground.setOutlineColor(sf::Color(255, 65, 55));
            }
            else
            {
                zombieHealthBackground.setOutlineThickness(0.f);
            }
            window.draw(zombieHealthBackground);

            zombieHealthForeground.setSize({
                28.f * std::clamp(zombie.health / zombie.maxHealth, 0.f, 1.f),
                4.f
            });

            zombieHealthForeground.setPosition(
                zombie.position + sf::Vector2f(-14.f, -42.f)
            );
            window.draw(zombieHealthForeground);
        }

        // Blood particles are drawn above zombies and below the player.
        for (const Particle& particle : particles)
        {
            const float fade = std::clamp(
                particle.life / particle.maxLife,
                0.f,
                1.f
            );

            particlePixel.setSize({particle.size, particle.size});
            particlePixel.setPosition(particle.position);
            particlePixel.setFillColor(sf::Color(
                particle.color.r,
                particle.color.g,
                particle.color.b,
                static_cast<std::uint8_t>(fade * 245.f)
            ));
            window.draw(particlePixel);
        }

        // Player body and weapon layer.
        drawGroundShadow(playerPosition, 1.25f, 110);

        // Hurt feedback: red tint right after a hit, then a blink while the
        // short damage grace period (invulnerability) lasts.
        sf::Color playerTint = lerpColor(
            sf::Color::White, sf::Color(255, 100, 90), std::clamp(damageFlash * 1.4f, 0.f, 1.f));
        if (playerDamageCooldown > 0.f && !gameOver && std::fmod(clockTotalSeconds, 0.12f) < 0.06f)
            playerTint.a = 150;
        body.setColor(playerTint);
        armsGun.setColor(playerTint);
        window.draw(body);

        if (weaponUsesOverlay())
        {
            // The rifle painted into the hands sheet is hidden (clipped away)
            // and the selected weapon sprite is drawn in its place, using the
            // same transform as the arms so aiming and recoil still apply.
            const float frameX = static_cast<float>(currentFrame * FRAME_W);
            const float frameY = static_cast<float>(facingRow * FRAME_H);
            const float mirrorShift =
                muzzleLeft.x - (static_cast<float>(FRAME_W) - muzzleRight.x);

            const float clipLeft  = facingLeft
                ? static_cast<float>(FRAME_W) - WeaponConfig::SHEET_CUT_RIGHT + mirrorShift
                : 0.f;
            const float clipRight = facingLeft
                ? static_cast<float>(FRAME_W)
                : WeaponConfig::SHEET_CUT_RIGHT;
            const float frameH = static_cast<float>(FRAME_H);

            sf::VertexArray armsQuad(sf::PrimitiveType::TriangleStrip, 4);
            armsQuad[0].position = {clipLeft,  0.f};
            armsQuad[1].position = {clipRight, 0.f};
            armsQuad[2].position = {clipLeft,  frameH};
            armsQuad[3].position = {clipRight, frameH};
            armsQuad[0].texCoords = {frameX + clipLeft,  frameY};
            armsQuad[1].texCoords = {frameX + clipRight, frameY};
            armsQuad[2].texCoords = {frameX + clipLeft,  frameY + frameH};
            armsQuad[3].texCoords = {frameX + clipRight, frameY + frameH};
            for (std::size_t v = 0; v < 4; ++v)
                armsQuad[v].color = playerTint;

            sf::RenderStates armsStates;
            armsStates.transform = armsGun.getTransform();
            armsStates.texture = &armsGunTexture;
            window.draw(armsQuad, armsStates);

            const WeaponDef& held = WEAPONS[currentWeapon];
            const sf::Texture& heldTexture = weaponTextures[currentWeapon];
            const float heldW = static_cast<float>(heldTexture.getSize().x);
            const float heldH = static_cast<float>(heldTexture.getSize().y);

            sf::Sprite heldSprite(heldTexture);
            heldSprite.setOrigin({heldW, heldH * held.muzzleYRatio});
            heldSprite.setScale({facingLeft ? -1.f : 1.f, 1.f});
            heldSprite.setPosition(weaponMuzzleLocal());
            heldSprite.setColor(playerTint);

            sf::RenderStates heldStates;
            heldStates.transform = armsGun.getTransform();
            window.draw(heldSprite, heldStates);

            if (showWeaponDebug)
            {
                const sf::FloatRect bounds = heldSprite.getGlobalBounds();
                sf::RectangleShape box(bounds.size);
                box.setPosition(bounds.position);
                box.setFillColor(sf::Color::Transparent);
                box.setOutlineThickness(-3.f);
                box.setOutlineColor(sf::Color(255, 235, 60));
                window.draw(box, heldStates);
            }
        }
        else
        {
            window.draw(armsGun);
        }

        // Sniper laser sight.
        if (currentWeapon == static_cast<int>(WeaponId::SniperRifle) &&
            !reloading && switchTimer <= 0.f && !gameOver)
        {
            const sf::Vector2f laserStart =
                armsGun.getTransform().transformPoint(weaponMuzzleLocal());
            const sf::Vector2f laserDirection =
                normalized(aimPoint - laserStart);

            if (vectorLength(laserDirection) > 0.001f)
            {
                sf::RectangleShape laser({420.f, 1.5f});
                laser.setOrigin({0.f, 0.75f});
                laser.setPosition(laserStart);
                laser.setRotation(sf::degrees(std::atan2(
                    laserDirection.y, laserDirection.x
                ) * RAD_TO_DEG));
                laser.setFillColor(sf::Color(255, 60, 50, 70));
                window.draw(laser);
            }
        }

        if (showWeaponDebug)
        {
            const sf::Vector2f debugMuzzle =
                armsGun.getTransform().transformPoint(weaponMuzzleLocal());
            sf::CircleShape marker(3.f);
            marker.setOrigin({3.f, 3.f});
            marker.setPosition(debugMuzzle);
            marker.setFillColor(sf::Color(255, 60, 60));
            window.draw(marker);

            drawShadowText(window, "WEAPON DEBUG F1",
                           worldView.getCenter().x - 380.f,
                           worldView.getCenter().y - 204.f, 2.f,
                           sf::Color(255, 235, 60));
        }

        // Muzzle flash.
        if (muzzleFlashTimer > 0.f)
        {
            const float fade =
                muzzleFlashTimer / FLASH_DURATION;

            const auto alpha = static_cast<std::uint8_t>(
                std::clamp(fade * 255.f, 0.f, 255.f)
            );

            const float flashAngle = std::atan2(
                muzzleFlashDirection.y,
                muzzleFlashDirection.x
            ) * RAD_TO_DEG;

            flashRay.setScale({muzzleFlashScale, muzzleFlashScale});
            flashOuter.setScale({muzzleFlashScale, muzzleFlashScale});
            flashInner.setScale({muzzleFlashScale, muzzleFlashScale});

            flashRay.setPosition(muzzleFlashPosition);
            flashRay.setRotation(sf::degrees(flashAngle));
            flashRay.setFillColor(
                sf::Color(255, 135, 35, alpha)
            );

            flashOuter.setPosition(muzzleFlashPosition);
            flashOuter.setFillColor(
                sf::Color(255, 155, 45, alpha)
            );

            flashInner.setPosition(
                muzzleFlashPosition +
                muzzleFlashDirection * 5.f
            );
            flashInner.setFillColor(
                sf::Color(255, 245, 175, alpha)
            );

            window.draw(flashRay);
            window.draw(flashOuter);
            window.draw(flashInner);
        }

        // Bullets.
        for (const Bullet& bullet : bullets)
        {
            // Every bullet leaves a short tracer; the sniper's is long and bright.
            {
                const bool longTracer = bullet.tailLength > 0.f;
                const float tail = longTracer ? bullet.tailLength : 12.f;
                const float travelAngle = std::atan2(
                    bullet.velocity.y, bullet.velocity.x
                ) * RAD_TO_DEG;

                sf::RectangleShape tracer({tail, longTracer ? 2.f : 1.5f});
                tracer.setOrigin({tail, longTracer ? 1.f : 0.75f});
                tracer.setPosition(bullet.position);
                tracer.setRotation(sf::degrees(travelAngle));
                tracer.setFillColor(sf::Color(
                    bullet.color.r, bullet.color.g, bullet.color.b, longTracer ? 150 : 95
                ));
                window.draw(tracer);
            }

            bulletShape.setRadius(bullet.radius);
            bulletShape.setOrigin({bullet.radius, bullet.radius});
            bulletShape.setFillColor(bullet.color);
            bulletShape.setOutlineColor(bullet.outline);
            bulletShape.setPosition(bullet.position);
            window.draw(bulletShape);
        }

        // Lamps and signs hang above the characters.
        for (const Decoration& decoration : decorations)
        {
            if (!decoration.overhead || !onScreen(decoration.position, 60.f))
                continue;

            sf::Sprite sprite(*decoration.texture);
            sprite.setOrigin(decoration.size * 0.5f);
            sprite.setPosition(decoration.position);
            sprite.setRotation(sf::degrees(decoration.rotation));
            window.draw(sprite);
        }

        // Explosions, smoke, dust and impacts.
        for (const EffectSprite& fx : effectSprites)
        {
            if (!fx.texture || !onScreen(fx.position, 160.f))
                continue;

            const float progress = std::clamp(fx.age / fx.life, 0.f, 1.f);
            const float size = fx.startSize + (fx.endSize - fx.startSize) * progress;
            const sf::Vector2u textureSize = fx.texture->getSize();
            const float scale = size / static_cast<float>(textureSize.x);

            sf::Sprite sprite(*fx.texture);
            sprite.setOrigin({
                static_cast<float>(textureSize.x) * 0.5f,
                static_cast<float>(textureSize.y) * 0.5f
            });
            sprite.setScale({scale, scale});
            sprite.setPosition(fx.position + sf::Vector2f(0.f, fx.rise * fx.age));
            sprite.setRotation(sf::degrees(fx.rotation));
            sprite.setColor(sf::Color(
                255, 255, 255,
                static_cast<std::uint8_t>(std::clamp(fx.startAlpha * (1.f - progress), 0.f, 255.f))
            ));
            window.draw(sprite);
        }

        // Shield bubble around the player.
        if (powerTimers[static_cast<int>(PowerUpKind::Shield)] > 0.f && !gameOver)
        {
            const float remaining = powerTimers[static_cast<int>(PowerUpKind::Shield)];
            const float wobble = 0.5f + 0.5f * std::sin(clockTotalSeconds * 6.f);
            const bool expiring = remaining < 2.f;
            const float flicker =
                (expiring && std::fmod(clockTotalSeconds, 0.3f) < 0.15f) ? 0.35f : 1.f;

            const float radiusX = 30.f + 2.f * wobble;
            sf::CircleShape bubble(radiusX);
            bubble.setOrigin({radiusX, radiusX});
            bubble.setScale({1.f, 1.2f});
            bubble.setPosition(playerPosition + sf::Vector2f(0.f, -34.f));
            bubble.setFillColor(sf::Color(
                90, 200, 255, static_cast<std::uint8_t>((30.f + 20.f * wobble) * flicker)
            ));
            bubble.setOutlineThickness(2.f);
            bubble.setOutlineColor(sf::Color(
                150, 225, 255, static_cast<std::uint8_t>((140.f + 80.f * wobble) * flicker)
            ));
            window.draw(bubble);
        }

        // Floating pickup / score notifications.
        for (const FloatingText& note : floatingTexts)
        {
            const float fade = std::clamp(note.life / 0.45f, 0.f, 1.f);
            const sf::Color color(
                note.color.r, note.color.g, note.color.b,
                static_cast<std::uint8_t>(fade * 255.f)
            );

            drawShadowText(
                window, note.text,
                std::clamp(
                    note.position.x - pixelTextWidth(note.text, note.scale) / 2.f,
                    viewCenter.x - viewSize.x * 0.5f + 6.f,
                    viewCenter.x + viewSize.x * 0.5f - 6.f - pixelTextWidth(note.text, note.scale)
                ),
                note.position.y, note.scale, color
            );
        }

        // Damage dealt to zombies: floats up and fades out.
        for (const FloatingText& number : damageNumbers)
        {
            const float fade = std::clamp(number.life / number.maxLife * 1.6f, 0.f, 1.f);
            drawShadowText(
                window, number.text,
                number.position.x - pixelTextWidth(number.text, number.scale) / 2.f,
                number.position.y, number.scale,
                sf::Color(number.color.r, number.color.g, number.color.b,
                          static_cast<std::uint8_t>(fade * 255.f))
            );
        }

        // --------------------------------------------------
        // SCREEN LAYER (HUD): back to the fixed default view
        // --------------------------------------------------
        window.setView(screenView);

        // Soft dark vignette around the screen edges.
        {
            static sf::VertexArray vignette(sf::PrimitiveType::Triangles);   // Built once, drawn every frame.
            const sf::Color edge(0, 0, 0, 105);
            const sf::Color clear(0, 0, 0, 0);
            const float depth = 90.f;

            auto quad = [&](sf::Vector2f p0, sf::Vector2f p1, sf::Vector2f p2, sf::Vector2f p3,
                            sf::Color c0, sf::Color c1, sf::Color c2, sf::Color c3)
            {
                const sf::Vertex a{p0, c0, {}};
                const sf::Vertex b{p1, c1, {}};
                const sf::Vertex c{p2, c2, {}};
                const sf::Vertex d{p3, c3, {}};
                vignette.append(a); vignette.append(b); vignette.append(c);
                vignette.append(a); vignette.append(c); vignette.append(d);
            };

            if (vignette.getVertexCount() == 0)
            {
            quad({0.f, 0.f}, {depth, 0.f}, {depth, 600.f}, {0.f, 600.f}, edge, clear, clear, edge);
            quad({800.f - depth, 0.f}, {800.f, 0.f}, {800.f, 600.f}, {800.f - depth, 600.f}, clear, edge, edge, clear);
            quad({0.f, 0.f}, {800.f, 0.f}, {800.f, depth}, {0.f, depth}, edge, edge, clear, clear);
            quad({0.f, 600.f - depth}, {800.f, 600.f - depth}, {800.f, 600.f}, {0.f, 600.f}, clear, clear, edge, edge);
            }
            window.draw(vignette);
        }

        // Damage flash and the pulsing low-health warning.
        {
            const float pulse = 0.5f + 0.5f * std::sin(uiTime * 7.f);

            if (damageFlash > 0.f)
            {
                sf::RectangleShape flash({800.f, 600.f});
                flash.setFillColor(sf::Color(190, 0, 0, static_cast<std::uint8_t>(damageFlash * 60.f)));
                window.draw(flash);
                drawEdgeGlow(window, sf::Color(230, 20, 20, static_cast<std::uint8_t>(damageFlash * 200.f)), 150.f);
            }

            if (playerHealth > 0.f && playerHealth <= 30.f && !gameOver)
                drawEdgeGlow(window, sf::Color(210, 0, 0, static_cast<std::uint8_t>(55.f + 85.f * pulse)), 120.f);
        }

        // Minimap (top right, under the wave information).
        {
            constexpr float MAP_X = 620.f;
            constexpr float MAP_Y = 96.f;
            const float k = 160.f / WORLD_W;

            sf::RectangleShape frame({160.f, 120.f});
            frame.setPosition({MAP_X, MAP_Y});
            frame.setFillColor(sf::Color(0, 0, 0, 150));
            frame.setOutlineThickness(2.f);
            frame.setOutlineColor(sf::Color(95, 110, 95, 220));
            window.draw(frame);

            sf::Sprite miniMap(minimapTexture);
            miniMap.setPosition({MAP_X, MAP_Y});
            miniMap.setScale({2.f, 2.f});
            miniMap.setColor(sf::Color(255, 255, 255, 215));
            window.draw(miniMap);

            auto dot = [&](sf::Vector2f world, float size, sf::Color color)
            {
                sf::RectangleShape mark({size, size});
                mark.setOrigin({size * 0.5f, size * 0.5f});
                mark.setPosition({MAP_X + world.x * k, MAP_Y + world.y * k});
                mark.setFillColor(color);
                window.draw(mark);
            };

            // Supply corner and the camera's field of view.
            sf::RectangleShape supply({supplyZone.size.x * k, supplyZone.size.y * k});
            supply.setPosition({MAP_X + supplyZone.position.x * k, MAP_Y + supplyZone.position.y * k});
            supply.setFillColor(sf::Color(255, 215, 90, 40));
            supply.setOutlineThickness(1.f);
            supply.setOutlineColor(sf::Color(255, 215, 90, 140));
            window.draw(supply);

            sf::RectangleShape viewBox({viewSize.x * k, viewSize.y * k});
            viewBox.setPosition({
                MAP_X + (worldView.getCenter().x - viewSize.x * 0.5f) * k,
                MAP_Y + (worldView.getCenter().y - viewSize.y * 0.5f) * k
            });
            viewBox.setFillColor(sf::Color::Transparent);
            viewBox.setOutlineThickness(1.f);
            viewBox.setOutlineColor(sf::Color(255, 255, 255, 120));
            window.draw(viewBox);

            for (const Pickup& pickup : pickups)
                dot(pickup.position, 3.f, PICKUPS[static_cast<int>(pickup.type)].glow);

            for (const Zombie& zombie : zombies)
                if (!zombie.isDying)
                    dot(zombie.position, 2.f, sf::Color(235, 70, 60));

            dot(playerPosition, 4.f,
                std::fmod(clockTotalSeconds, 0.6f) < 0.4f ? sf::Color::White : sf::Color(120, 220, 255));
        }

        // Player health bar: lagging damage trail, colour change, numbers
        // and a pulsing warning when health is critical.
        {
            const float fraction = std::clamp(playerHealth / 100.f, 0.f, 1.f);
            const bool critical = playerHealth > 0.f && playerHealth <= 30.f;
            const float pulse = 0.5f + 0.5f * std::sin(uiTime * 7.f);

            playerHealthBackground.setSize({160.f, 12.f});
            playerHealthBackground.setPosition({20.f, 20.f});
            window.draw(playerHealthBackground);

            sf::RectangleShape trail({160.f * std::clamp(displayHealth / 100.f, 0.f, 1.f), 12.f});
            trail.setPosition({20.f, 20.f});
            trail.setFillColor(sf::Color(235, 205, 190, 200));
            window.draw(trail);

            playerHealthForeground.setSize({160.f * fraction, 12.f});
            playerHealthForeground.setPosition({20.f, 20.f});
            playerHealthForeground.setFillColor(
                playerHealth > 60.f ? sf::Color(75, 205, 90) :
                playerHealth > 30.f ? sf::Color(240, 185, 60) :
                                      lerpColor(sf::Color(215, 45, 40), sf::Color(255, 110, 90), pulse));
            window.draw(playerHealthForeground);

            sf::RectangleShape frame({160.f, 12.f});
            frame.setPosition({20.f, 20.f});
            frame.setFillColor(sf::Color::Transparent);
            frame.setOutlineThickness(2.f);
            frame.setOutlineColor(critical
                ? sf::Color(255, 70, 60, static_cast<std::uint8_t>(130.f + 125.f * pulse))
                : sf::Color(20, 24, 20, 220));
            window.draw(frame);

            drawCenteredText(
                window,
                std::to_string(static_cast<int>(std::ceil(playerHealth))) + "/100",
                100.f, 20.8f, 1.5f, sf::Color(250, 250, 240));

            if (critical && !gameOver)
                drawShadowText(window, "LOW HEALTH", 192.f, 21.f, 1.5f,
                               sf::Color(255, 90, 75, static_cast<std::uint8_t>(130.f + 125.f * pulse)));
        }

        // Live HUD (drawn with a small shadow so it stays readable).
        {
            // Top-left: score and difficulty.
            drawShadowText(
                window, "SCORE " + std::to_string(score),
                20.f, 42.f, 3.f,
                sf::Color(245, 245, 230)
            );
            drawShadowText(
                window, difficulty.name,
                20.f, 70.f, 2.f,
                difficulty.accent
            );

            // Top-right: wave, zombies remaining, best score.
            const std::string waveText =
                "WAVE " + std::to_string(currentWave);
            drawShadowText(
                window, waveText,
                780.f - pixelTextWidth(waveText, 3.f), 20.f, 3.f,
                sf::Color(255, 205, 95)
            );

            const int zombiesRemaining =
                std::max(0, waveZombieTotal - waveZombiesKilled);
            const std::string remainingText =
                "ZOMBIES LEFT " + std::to_string(zombiesRemaining);
            drawShadowText(
                window, remainingText,
                780.f - pixelTextWidth(remainingText, 2.f), 48.f, 2.f,
                sf::Color(235, 150, 130)
            );

            const std::string bestText =
                "BEST " + std::to_string(highScore);
            drawShadowText(
                window, bestText,
                780.f - pixelTextWidth(bestText, 2.f), 68.f, 2.f,
                sf::Color(190, 210, 190)
            );

            // Wave-start banner that fades out.
            if (waveBannerTimer > 0.f && !gameOver)
            {
                const auto bannerAlpha = static_cast<std::uint8_t>(
                    std::clamp(waveBannerTimer / 0.6f, 0.f, 1.f) * 255.f
                );

                // Pop-in: the title starts large and settles.
                const float bannerElapsed = WaveConfig::WAVE_BANNER_DURATION - waveBannerTimer;
                const float pop = std::max(0.f, 1.f - bannerElapsed / 0.25f);
                drawCenteredText(
                    window, "WAVE " + std::to_string(currentWave),
                    400.f, 120.f - 10.f * pop, 6.f + 3.f * pop,
                    sf::Color(255, 205, 95, bannerAlpha)
                );

                if (isIntenseWave(currentWave))
                    drawCenteredText(
                        window, "INTENSE WAVE",
                        400.f, 198.f, 2.f,
                        sf::Color(245, 75, 65, bannerAlpha)
                    );
                drawCenteredText(
                    window, std::to_string(waveZombieTotal) + " ZOMBIES",
                    400.f, 175.f, 2.f,
                    sf::Color(235, 150, 130, bannerAlpha)
                );
            }

            // Intermission: wave cleared + countdown to the next wave.
            if (intermission && !gameOver)
            {
                const int secondsLeft = std::max(
                    1, static_cast<int>(std::ceil(intermissionTimer))
                );

                drawCenteredText(
                    window, "WAVE " + std::to_string(currentWave) + " CLEARED",
                    400.f, 150.f, 5.f,
                    sf::Color(120, 215, 110)
                );
                drawCenteredText(
                    window, "HEALTH RESTORED",
                    400.f, 194.f, 2.f,
                    sf::Color(190, 235, 170)
                );
                drawCenteredText(
                    window, "AMMO RESTOCKED",
                    400.f, 212.f, 2.f,
                    sf::Color(255, 215, 130)
                );
                drawCenteredText(
                    window, "NEXT WAVE IN " + std::to_string(secondsLeft),
                    400.f, 235.f, 3.f,
                    sf::Color(255, 205, 95)
                );
            }
        }

        // --------------------------------------------------
        // PICKUP HUD: ACTIVE POWER-UPS
        // --------------------------------------------------
        {
            float rowY = 98.f;

            for (int kind = 0; kind < POWERUP_KIND_COUNT; ++kind)
            {
                const float remaining = powerTimers[kind];
                if (remaining <= 0.f)
                    continue;

                const int iconIndex = powerUpIconOf(kind);
                const PickupDef& def = PICKUPS[iconIndex];
                const float duration = std::max(0.01f, powerDurations[kind]);

                // Blink while about to expire.
                const bool hidden =
                    remaining < 2.f && std::fmod(clockTotalSeconds, 0.3f) < 0.12f;
                const float fadeAlpha = hidden ? 90.f : 255.f;

                sf::RectangleShape row({176.f, 30.f});
                row.setPosition({16.f, rowY});
                row.setFillColor(sf::Color(14, 18, 14, 170));
                row.setOutlineThickness(2.f);
                row.setOutlineColor(sf::Color(
                    def.glow.r, def.glow.g, def.glow.b,
                    static_cast<std::uint8_t>(fadeAlpha)
                ));
                window.draw(row);

                if (pickupTextureLoaded[iconIndex])
                {
                    const sf::Vector2u size = pickupTextures[iconIndex].getSize();
                    const float iconScale = 24.f / static_cast<float>(size.x);

                    sf::Sprite icon(pickupTextures[iconIndex]);
                    icon.setOrigin({
                        static_cast<float>(size.x) / 2.f,
                        static_cast<float>(size.y) / 2.f
                    });
                    icon.setScale({iconScale, iconScale});
                    icon.setPosition({16.f + 16.f, rowY + 15.f});
                    icon.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(fadeAlpha)));
                    window.draw(icon);
                }

                static const char* const HUD_NAMES[POWERUP_KIND_COUNT] = {
                    "SHIELD", "SPEED", "DAMAGE", "RAPID"
                };
                const std::string label =
                    std::string(HUD_NAMES[kind]) + " " +
                    std::to_string(static_cast<int>(std::ceil(remaining))) + "S";
                drawShadowText(
                    window, label, 16.f + 34.f, rowY + 5.f, 2.f,
                    sf::Color(def.glow.r, def.glow.g, def.glow.b,
                              static_cast<std::uint8_t>(fadeAlpha))
                );

                sf::RectangleShape barBack({132.f, 5.f});
                barBack.setPosition({16.f + 34.f, rowY + 19.f});
                barBack.setFillColor(sf::Color(55, 55, 50, 200));
                window.draw(barBack);

                sf::RectangleShape barFill({
                    132.f * std::clamp(remaining / duration, 0.f, 1.f), 5.f
                });
                barFill.setPosition(barBack.getPosition());
                barFill.setFillColor(def.glow);
                window.draw(barFill);

                rowY += 36.f;
            }
        }

        // --------------------------------------------------
        // WEAPON HUD
        // --------------------------------------------------
        {
            const WeaponDef& weapon = WEAPONS[currentWeapon];
            const WeaponState& ammo = weaponStates[currentWeapon];

            const sf::Color gold(255, 205, 95);
            const sf::Color red(245, 75, 65);
            const sf::Color amber(255, 175, 60);
            const sf::Color light(245, 245, 230);

            // ---- Weapon slots 1-4 (bottom-left) ----
            constexpr float SLOT_W = 80.f;
            constexpr float SLOT_H = 52.f;
            constexpr float SLOT_GAP = 6.f;
            constexpr float SLOT_X0 = 16.f;
            constexpr float SLOT_Y = 530.f;

            for (int i = 0; i < WEAPON_COUNT; ++i)
            {
                const WeaponDef& def = WEAPONS[i];
                const WeaponState& state = weaponStates[i];
                const bool selected = (i == currentWeapon);
                const bool dry = (state.magazine + state.reserve) <= 0;

                const float x = SLOT_X0 + static_cast<float>(i) * (SLOT_W + SLOT_GAP);
                const float y = selected ? SLOT_Y - 4.f : SLOT_Y;

                // Weapons not found yet: dark slot with a LOCKED label.
                if (!weaponOwned[static_cast<std::size_t>(i)])
                {
                    sf::RectangleShape locked({SLOT_W, SLOT_H});
                    locked.setPosition({x, y});
                    locked.setFillColor(sf::Color(12, 14, 12, 150));
                    locked.setOutlineThickness(2.f);
                    locked.setOutlineColor(sf::Color(55, 62, 55, 160));
                    window.draw(locked);

                    drawShadowText(window, std::to_string(i + 1), x + 6.f, y + 5.f, 2.f,
                                   sf::Color(95, 105, 95));
                    drawCenteredText(window, "LOCKED", x + SLOT_W / 2.f, y + 30.f, 1.5f,
                                     sf::Color(120, 130, 120));
                    continue;
                }

                sf::RectangleShape slot({SLOT_W, SLOT_H});
                slot.setPosition({x, y});
                slot.setFillColor(selected
                    ? sf::Color(55, 80, 58, 215)
                    : sf::Color(18, 22, 18, 170));
                slot.setOutlineThickness(selected ? 3.f : 2.f);
                slot.setOutlineColor(selected
                    ? gold
                    : sf::Color(75, 92, 76, 200));
                window.draw(slot);

                drawShadowText(
                    window, std::to_string(i + 1),
                    x + 6.f, y + 5.f, 2.f,
                    selected ? gold : sf::Color(170, 185, 170)
                );

                const std::string magText = std::to_string(state.magazine);
                drawShadowText(
                    window, magText,
                    x + SLOT_W - 6.f - pixelTextWidth(magText, 2.f), y + 5.f, 2.f,
                    state.magazine == 0 ? red : light
                );

                const auto iconAlpha = static_cast<std::uint8_t>(
                    dry ? 60 : (selected ? 255 : 150)
                );

                if (weaponTextureLoaded[i])
                {
                    const sf::Vector2u size = weaponTextures[i].getSize();
                    const float iconScale = std::min(
                        64.f / static_cast<float>(size.x),
                        24.f / static_cast<float>(size.y)
                    );

                    sf::Sprite icon(weaponTextures[i]);
                    icon.setOrigin({
                        static_cast<float>(size.x) / 2.f,
                        static_cast<float>(size.y) / 2.f
                    });
                    icon.setScale({iconScale, iconScale});
                    icon.setPosition({x + SLOT_W / 2.f, y + 34.f});
                    icon.setColor(sf::Color(255, 255, 255, iconAlpha));
                    window.draw(icon);
                }
                else
                {
                    drawCenteredText(
                        window, def.shortName,
                        x + SLOT_W / 2.f, y + 28.f, 2.f,
                        sf::Color(light.r, light.g, light.b, iconAlpha)
                    );
                }
            }

            // ---- Ammo panel (bottom-right) ----
            constexpr float PANEL_X = 566.f;
            constexpr float PANEL_Y = 506.f;
            constexpr float PANEL_W = 218.f;
            constexpr float PANEL_H = 78.f;

            sf::RectangleShape panel({PANEL_W, PANEL_H});
            panel.setPosition({PANEL_X, PANEL_Y});
            panel.setFillColor(sf::Color(14, 18, 14, 185));
            panel.setOutlineThickness(2.f);
            panel.setOutlineColor(weapon.bulletOutline);
            window.draw(panel);

            drawShadowText(window, weapon.name,
                           PANEL_X + 12.f, PANEL_Y + 8.f, 2.f, gold);

            const bool lowMagazine =
                ammo.magazine <= std::max(1, weapon.magazineSize / 4);
            const sf::Color magazineColor =
                ammo.magazine == 0 ? red : (lowMagazine ? amber : light);

            const std::string magazineText = std::to_string(ammo.magazine);
            drawShadowText(window, magazineText,
                           PANEL_X + 12.f, PANEL_Y + 26.f, 5.f, magazineColor);

            const std::string reserveText =
                "/ " + std::to_string(ammo.reserve);
            drawShadowText(
                window, reserveText,
                PANEL_X + 12.f + pixelTextWidth(magazineText, 5.f) + 10.f,
                PANEL_Y + 40.f, 3.f,
                ammo.reserve == 0 ? red : sf::Color(190, 210, 190)
            );

            // Reload progress bar.
            sf::RectangleShape reloadBack({PANEL_W - 24.f, 6.f});
            reloadBack.setPosition({PANEL_X + 12.f, PANEL_Y + PANEL_H - 12.f});
            reloadBack.setFillColor(sf::Color(55, 55, 50, 200));
            window.draw(reloadBack);

            if (reloading)
            {
                const float progress = weapon.shellReload
                    ? (static_cast<float>(reloadDone) +
                       reloadTimer / weapon.reloadTime) /
                      static_cast<float>(std::max(1, reloadTarget))
                    : reloadTimer / weapon.reloadTime;

                sf::RectangleShape reloadFill({
                    (PANEL_W - 24.f) * std::clamp(progress, 0.f, 1.f), 6.f
                });
                reloadFill.setPosition(reloadBack.getPosition());
                reloadFill.setFillColor(sf::Color(255, 205, 95));
                window.draw(reloadFill);
            }

            // ---- Status message (centre-bottom) ----
            const float blink = 0.5f + 0.5f * std::sin(
                static_cast<float>(std::fmod(
                    static_cast<double>(clockTotalSeconds), 100.0)) * 8.f
            );
            const auto blinkAlpha = static_cast<std::uint8_t>(130.f + 125.f * blink);

            bool otherWeaponHasAmmo = false;
            for (int i = 0; i < WEAPON_COUNT; ++i)
                if (i != currentWeapon &&
                    weaponStates[i].magazine + weaponStates[i].reserve > 0)
                    otherWeaponHasAmmo = true;

            if (!gameOver && !intermission)
            {
                if (reloading)
                {
                    drawCenteredText(window, "RELOADING", 400.f, 500.f, 3.f,
                                     sf::Color(255, 205, 95, blinkAlpha));
                }
                else if (ammo.magazine == 0 && ammo.reserve > 0)
                {
                    drawCenteredText(window, "EMPTY  PRESS R", 400.f, 500.f, 3.f,
                                     sf::Color(245, 75, 65, blinkAlpha));
                }
                else if (ammo.magazine == 0 && ammo.reserve == 0)
                {
                    drawCenteredText(
                        window,
                        otherWeaponHasAmmo ? "NO AMMO  SWITCH WEAPON" : "NO AMMO",
                        400.f, 500.f, 3.f,
                        sf::Color(245, 75, 65, blinkAlpha)
                    );
                }
                else if (noReserveTimer > 0.f)
                {
                    drawCenteredText(window, "NO RESERVE AMMO", 400.f, 500.f, 2.f,
                                     sf::Color(255, 175, 60));
                }
            }

            // Key hints at the start of a game.
            if (controlsHintTimer > 0.f && !gameOver)
            {
                const auto hintAlpha = static_cast<std::uint8_t>(
                    std::clamp(controlsHintTimer / 1.5f, 0.f, 1.f) * 255.f
                );
                drawCenteredText(
                    window, "KEYS 1 2 3 4 SWITCH  R RELOAD",
                    400.f, 470.f, 2.f,
                    sf::Color(190, 210, 190, hintAlpha)
                );
            }
        }

        // Kill / power-up / wave feed (right side, under the minimap).
        for (std::size_t i = 0; i < notices.size(); ++i)
        {
            const Notice& notice = notices[notices.size() - 1 - i];
            const auto alpha = static_cast<std::uint8_t>(
                std::clamp(notice.life / 0.6f, 0.f, 1.f) * 255.f);

            drawShadowText(
                window, notice.text,
                780.f - pixelTextWidth(notice.text, 2.f),
                226.f + static_cast<float>(i) * 18.f, 2.f,
                sf::Color(notice.color.r, notice.color.g, notice.color.b, alpha));
        }

        // Crosshair, hit marker and reload ring (only while actually playing).
        if (!paused && !gameOver)
        {
            // Snapped to whole pixels so the
            // crosshair stays crisp and steady. Fixed size: it no longer
            // opens up when firing or reloading.
            // The crosshair sits on the gun's aim point (where the barrel
            // points), so it moves exactly with the gun.
            const sf::Vector2f mapped = window.mapPixelToCoords(
                window.mapCoordsToPixel(aimPoint, worldView), screenView);
            const sf::Vector2f c(
                std::clamp(std::round(mapped.x), 6.f, 794.f),
                std::clamp(std::round(mapped.y), 6.f, 594.f));
            const float gap = 3.f;
            const sf::Color ink(245, 245, 230, 235);

            auto tick = [&](sf::Vector2f offset, sf::Vector2f size)
            {
                sf::RectangleShape shadow(size);
                shadow.setOrigin(size * 0.5f);
                shadow.setPosition(c + offset + sf::Vector2f(1.f, 1.f));
                shadow.setFillColor(sf::Color(0, 0, 0, 160));
                window.draw(shadow);

                sf::RectangleShape mark(size);
                mark.setOrigin(size * 0.5f);
                mark.setPosition(c + offset);
                mark.setFillColor(ink);
                window.draw(mark);
            };

            tick({-(gap + 2.f), 0.f}, {4.f, 2.f});
            tick({ (gap + 2.f), 0.f}, {4.f, 2.f});
            tick({0.f, -(gap + 2.f)}, {2.f, 4.f});
            tick({0.f,  (gap + 2.f)}, {2.f, 4.f});
            tick({0.f, 0.f}, {2.f, 2.f});

            if (hitMarkerTimer > 0.f)
            {
                const float k = hitMarkerTimer / 0.2f;
                const sf::Color markerColor = hitMarkerKill
                    ? sf::Color(255, 70, 60, static_cast<std::uint8_t>(255.f * k))
                    : sf::Color(255, 255, 255, static_cast<std::uint8_t>(255.f * k));
                const float reach = gap + 7.f + (1.f - k) * 3.f;

                for (int sx = -1; sx <= 1; sx += 2)
                {
                    for (int sy = -1; sy <= 1; sy += 2)
                    {
                        sf::RectangleShape diagonal({5.f, 2.f});
                        diagonal.setOrigin({2.5f, 1.f});
                        diagonal.setPosition(c + sf::Vector2f(
                            static_cast<float>(sx), static_cast<float>(sy)) * (reach * 0.7071f));
                        diagonal.setRotation(sf::degrees(sx * sy > 0 ? 45.f : -45.f));
                        diagonal.setFillColor(markerColor);
                        window.draw(diagonal);
                    }
                }
            }

            if (reloading)
            {
                const WeaponDef& rw = WEAPONS[currentWeapon];
                const float progress = rw.shellReload
                    ? (static_cast<float>(reloadDone) + reloadTimer / rw.reloadTime) /
                      static_cast<float>(std::max(1, reloadTarget))
                    : reloadTimer / rw.reloadTime;

                sf::RectangleShape back({36.f, 4.f});
                back.setPosition(c + sf::Vector2f(-18.f, gap + 10.f));
                back.setFillColor(sf::Color(20, 20, 18, 190));
                back.setOutlineThickness(1.f);
                back.setOutlineColor(sf::Color(0, 0, 0, 200));
                window.draw(back);

                sf::RectangleShape fill({36.f * std::clamp(progress, 0.f, 1.f), 4.f});
                fill.setPosition(back.getPosition());
                fill.setFillColor(sf::Color(255, 205, 95));
                window.draw(fill);
            }
        }

        // Pause screen: the frozen game stays visible underneath.
        if (paused)
        {
            sf::RectangleShape overlay({800.f, 600.f});
            overlay.setFillColor(sf::Color(0, 0, 0, 170));
            window.draw(overlay);

            if (subPage == SubPage::Settings)
            {
                drawSettingsPage();
            }
            else
            {
                static const char* const PAUSE_LABELS[PAUSE_ITEMS] = {
                    "RESUME", "RESTART", "SETTINGS", "MAIN MENU", "EXIT"
                };

                drawCenteredText(window, "PAUSED", 400.f, 88.f, 6.f, sf::Color(255, 205, 95));

                for (int i = 0; i < PAUSE_ITEMS; ++i)
                {
                    const std::size_t slot = static_cast<std::size_t>(i);
                    drawUiButton(window, uiPauseRect(i), PAUSE_LABELS[i], 3,
                                 i == PAUSE_ITEMS - 1 ? sf::Color(245, 75, 65) : sf::Color(120, 205, 95),
                                 buttonHover[slot], buttonPress[slot]);
                }

                drawCenteredText(window, "ESC RESUME", 400.f, 504.f, 2.f, sf::Color(190, 210, 190));

                {
                    const std::string note = "PROGRESS SAVES AT EVERY WAVE START";
                    const float width = pixelTextWidth(note, 1.f) + 24.f;
                    drawIcon(iconSave, {400.f - width / 2.f + 8.f, 538.f}, 18.f, sf::Color(255, 255, 255, 190));
                    drawShadowText(window, note, 400.f - width / 2.f + 24.f, 534.f, 1.f,
                                   sf::Color(150, 235, 255));
                }
            }
        }

        // Phase 7: "PROGRESS SAVED" indicator (after every checkpoint).
        if (saveNoticeTimer > 0.f && !paused)
        {
            const float fade = std::clamp(saveNoticeTimer / 0.6f, 0.f, 1.f) *
                               std::clamp((2.4f - saveNoticeTimer) / 0.2f, 0.f, 1.f);
            const auto alpha = static_cast<std::uint8_t>(fade * 255.f);
            const std::string text = gameOver ? "STATS SAVED" : "PROGRESS SAVED";
            const float width = pixelTextWidth(text, 2.f) + 32.f;
            const float left = 400.f - width / 2.f;

            sf::RectangleShape chip({width + 12.f, 30.f});
            chip.setPosition({left - 6.f, 6.f});
            chip.setFillColor(sf::Color(10, 18, 14, static_cast<std::uint8_t>(fade * 190.f)));
            chip.setOutlineThickness(1.f);
            chip.setOutlineColor(sf::Color(90, 220, 255, static_cast<std::uint8_t>(fade * 170.f)));
            window.draw(chip);

            drawIcon(iconSave, {left + 12.f, 21.f}, 22.f, sf::Color(255, 255, 255, alpha));
            drawShadowText(window, text, left + 30.f, 14.f, 2.f, sf::Color(150, 235, 255, alpha));
        }

        // Game-over screen: fades in, shows the run statistics and 3 buttons.
        if (gameOver)
        {
            const float appear = std::clamp(gameOverTimer / 0.8f, 0.f, 1.f);
            const auto a = [&](float value)
            {
                return static_cast<std::uint8_t>(std::clamp(value * appear, 0.f, 255.f));
            };

            sf::RectangleShape overlay({800.f, 600.f});
            overlay.setFillColor(sf::Color(0, 0, 0, a(210.f)));
            window.draw(overlay);

            drawCenteredText(window, "GAME OVER", 400.f, 100.f + 12.f * (1.f - appear), 7.f,
                             sf::Color(245, 75, 65, a(255.f)));

            drawCenteredText(window, "SCORE " + std::to_string(score), 400.f, 176.f, 3.f,
                             sf::Color(245, 245, 230, a(255.f)));

            if ((lastRunNewBestScore || (score > 0 && score >= highScore)) &&
                std::fmod(gameOverTimer, 0.8f) < 0.55f)
            {
                const std::string banner = lastRunNewBestScore ? "NEW PERSONAL BEST" : "NEW BEST";
                const float bannerWidth = pixelTextWidth(banner, 2.f);

                drawIcon(iconTrophy, {400.f - bannerWidth / 2.f - 18.f, 213.f}, 24.f,
                         sf::Color(255, 255, 255, a(255.f)));
                drawCenteredText(window, banner, 400.f, 206.f, 2.f,
                                 sf::Color(255, 205, 95, a(255.f)));
            }

            const int wavesSurvived = std::max(0, currentWave - (intermission ? 0 : 1));

            drawCenteredText(window,
                "WAVE REACHED " + std::to_string(currentWave) + "  " + difficulty.name,
                400.f, 238.f, 2.f,
                sf::Color(difficulty.accent.r, difficulty.accent.g, difficulty.accent.b, a(255.f)));
            drawCenteredText(window, "WAVES SURVIVED " + std::to_string(wavesSurvived),
                400.f, 260.f, 2.f, sf::Color(220, 232, 220, a(255.f)));
            drawCenteredText(window, "ZOMBIES KILLED " + std::to_string(totalZombiesKilled),
                400.f, 282.f, 2.f, sf::Color(235, 150, 130, a(255.f)));
            drawCenteredText(window, "BEST " + std::to_string(highScore),
                400.f, 304.f, 2.f, sf::Color(255, 205, 95, a(255.f)));
            drawCenteredText(window,
                "TIME " + formatDuration(static_cast<std::uint64_t>(runSurvivalSeconds)) +
                "  COINS " + std::to_string(runCoins) +
                (lastRunNewBestWave ? "  BEST WAVE" : ""),
                400.f, 326.f, 2.f, sf::Color(150, 235, 255, a(255.f)));

            if (appear > 0.6f)
            {
                static const char* const OVER_LABELS[GAME_OVER_ITEMS] = {
                    "RESTART  R", "MAIN MENU  M", "EXIT"
                };

                for (int i = 0; i < GAME_OVER_ITEMS; ++i)
                {
                    const std::size_t slot = static_cast<std::size_t>(i);
                    drawUiButton(window, uiGameOverRect(i), OVER_LABELS[i], 2,
                                 i == 0 ? sf::Color(120, 205, 95) :
                                 i == 1 ? sf::Color(150, 185, 225) : sf::Color(245, 75, 65),
                                 buttonHover[slot], buttonPress[slot]);
                }
            }
        }

        drawFade();

        window.display();
    }

    abandonToMenu();   // Closing the window mid-run keeps the checkpoint / counts the run.
    saveAllSettings();

    return 0;
}