#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace zsa
{
    // Keep this value in sync with the number of weapons in your game.
    constexpr std::size_t MAX_SAVED_WEAPONS = 16;

    struct PlayerData
    {
        std::uint64_t totalGamesPlayed = 0;
        std::uint64_t totalPlayTimeSeconds = 0;
        std::uint64_t totalKills = 0;
        std::uint64_t totalCoinsCollected = 0;

        std::uint32_t bestScore = 0;
        std::uint32_t bestWave = 0;
        std::uint64_t bestSurvivalTimeSeconds = 0;

        std::array<std::uint8_t, MAX_SAVED_WEAPONS> weaponUnlocked{};

        // A saved run is only a checkpoint of these values. Full world resume
        // also requires saving enemy positions, player position, drops, etc.
        bool hasSavedRun = false;
        std::uint32_t savedWave = 1;
        std::uint32_t savedScore = 0;
        std::uint32_t savedRunKills = 0;
        std::uint32_t savedRunCoins = 0;
        std::uint64_t savedSurvivalSeconds = 0;
        float savedPlayerHealth = 100.0f;
        float savedPlayerX = 0.0f;
        float savedPlayerY = 0.0f;
    };

    struct GameSettings
    {
        float musicVolume = 70.0f;       // Range: 0 to 100
        float sfxVolume = 80.0f;         // Range: 0 to 100
        std::uint8_t difficulty = 1;     // 0 = Easy, 1 = Normal, 2 = Hard
        bool fullscreen = false;
        std::uint32_t windowWidth = 1280;
        std::uint32_t windowHeight = 720;
    };

    class SaveManager
    {
    public:
        explicit SaveManager(const std::string& saveDirectory = "saves");

        // Missing or invalid files return false and leave safe default data.
        bool loadPlayerData(PlayerData& data, std::string* error = nullptr) const;
        bool savePlayerData(const PlayerData& data, std::string* error = nullptr) const;

        bool loadSettings(GameSettings& settings, std::string* error = nullptr) const;
        bool saveSettings(const GameSettings& settings, std::string* error = nullptr) const;

        // Call once when a run starts, then update stats at game over.
        static void recordCompletedRun(PlayerData& data,
                                       std::uint32_t score,
                                       std::uint32_t wave,
                                       std::uint32_t kills,
                                       std::uint32_t coins,
                                       std::uint64_t survivalSeconds);

        // Call this when the player chooses Continue / reaches a checkpoint.
        static void setSavedRun(PlayerData& data,
                                std::uint32_t wave,
                                std::uint32_t score,
                                std::uint32_t kills,
                                std::uint32_t coins,
                                std::uint64_t survivalSeconds,
                                float health,
                                float playerX,
                                float playerY);

        static void clearSavedRun(PlayerData& data);

        const std::string& getSaveDirectory() const { return m_saveDirectory; }

    private:
        std::string m_saveDirectory;
        std::string playerDataPath() const;
        std::string settingsPath() const;
    };
}
