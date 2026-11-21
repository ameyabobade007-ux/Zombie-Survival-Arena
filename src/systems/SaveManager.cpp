#include "systems/SaveManager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <exception>
#include <limits>
#include <type_traits>
#include <vector>

namespace
{
    constexpr std::uint32_t FILE_VERSION = 1;
    constexpr char PLAYER_MAGIC[4] = {'Z', 'S', 'P', 'D'};
    constexpr char SETTINGS_MAGIC[4] = {'Z', 'S', 'S', 'T'};

    template <typename T>
    void appendValue(std::vector<std::uint8_t>& bytes, const T& value)
    {
        static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
        const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }

    template <typename T>
    bool readValue(const std::vector<std::uint8_t>& bytes, std::size_t& offset, T& value)
    {
        static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
        if (offset + sizeof(T) > bytes.size()) return false;
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        offset += sizeof(T);
        return true;
    }

    std::uint32_t checksum(const std::vector<std::uint8_t>& bytes)
    {
        std::uint32_t hash = 2166136261u;
        for (std::uint8_t byte : bytes)
        {
            hash ^= byte;
            hash *= 16777619u;
        }
        return hash;
    }

    void setError(std::string* error, const std::string& message)
    {
        if (error) *error = message;
    }

    bool writeFile(const std::string& path,
                   const char magic[4],
                   const std::vector<std::uint8_t>& payload,
                   std::string* error)
    {
        try
        {
            const std::filesystem::path finalPath(path);
            if (!finalPath.parent_path().empty())
                std::filesystem::create_directories(finalPath.parent_path());

            const std::string tempPath = path + ".tmp";
            std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                setError(error, "Could not create save file: " + tempPath);
                return false;
            }

            const std::uint32_t version = FILE_VERSION;
            const std::uint32_t payloadSize = static_cast<std::uint32_t>(payload.size());
            const std::uint32_t hash = checksum(payload);

            out.write(magic, 4);
            out.write(reinterpret_cast<const char*>(&version), sizeof(version));
            out.write(reinterpret_cast<const char*>(&payloadSize), sizeof(payloadSize));
            out.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
            if (!payload.empty())
                out.write(reinterpret_cast<const char*>(payload.data()),
                          static_cast<std::streamsize>(payload.size()));
            out.close();

            if (!out)
            {
                std::filesystem::remove(tempPath);
                setError(error, "Writing the save file failed.");
                return false;
            }

            std::error_code ec;
            std::filesystem::rename(tempPath, finalPath, ec);
            if (ec)
            {
                // Windows rename does not replace an existing destination.
                std::filesystem::remove(finalPath, ec);
                ec.clear();
                std::filesystem::rename(tempPath, finalPath, ec);
            }
            if (ec)
            {
                std::filesystem::remove(tempPath);
                setError(error, "Could not finalize save file: " + ec.message());
                return false;
            }
            return true;
        }
        catch (const std::exception& e)
        {
            setError(error, e.what());
            return false;
        }
    }

    bool readFile(const std::string& path,
                  const char expectedMagic[4],
                  std::vector<std::uint8_t>& payload,
                  std::string* error)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            setError(error, "Save file does not exist yet: " + path);
            return false;
        }

        char magic[4]{};
        std::uint32_t version = 0, payloadSize = 0, storedHash = 0;
        in.read(magic, 4);
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        in.read(reinterpret_cast<char*>(&payloadSize), sizeof(payloadSize));
        in.read(reinterpret_cast<char*>(&storedHash), sizeof(storedHash));

        if (!in || !std::equal(magic, magic + 4, expectedMagic))
        {
            setError(error, "Save file header is invalid or corrupted.");
            return false;
        }
        if (version != FILE_VERSION)
        {
            setError(error, "Unsupported save-file version.");
            return false;
        }
        if (payloadSize > 1024 * 1024)
        {
            setError(error, "Save file is unexpectedly large; refusing to load it.");
            return false;
        }

        payload.resize(payloadSize);
        if (payloadSize)
            in.read(reinterpret_cast<char*>(payload.data()), payloadSize);

        if (!in || in.peek() != std::ifstream::traits_type::eof())
        {
            setError(error, "Save file is incomplete or has unexpected extra data.");
            return false;
        }
        if (checksum(payload) != storedHash)
        {
            setError(error, "Save-file checksum failed; data may be corrupted.");
            return false;
        }
        return true;
    }

    bool validPlayerData(const zsa::PlayerData& d)
    {
        return std::isfinite(d.savedPlayerHealth) &&
               std::isfinite(d.savedPlayerX) &&
               std::isfinite(d.savedPlayerY) &&
               d.savedPlayerHealth >= 0.0f && d.savedPlayerHealth <= 10000.0f &&
               d.savedWave <= 1000000u;
    }

    bool validSettings(const zsa::GameSettings& s)
    {
        return std::isfinite(s.musicVolume) && std::isfinite(s.sfxVolume) &&
               s.musicVolume >= 0.0f && s.musicVolume <= 100.0f &&
               s.sfxVolume >= 0.0f && s.sfxVolume <= 100.0f &&
               s.difficulty <= 2 &&
               s.windowWidth >= 640 && s.windowWidth <= 7680 &&
               s.windowHeight >= 480 && s.windowHeight <= 4320;
    }
}

namespace zsa
{
    SaveManager::SaveManager(const std::string& saveDirectory)
        : m_saveDirectory(saveDirectory) {}

    std::string SaveManager::playerDataPath() const
    {
        return (std::filesystem::path(m_saveDirectory) / "player_data.dat").string();
    }

    std::string SaveManager::settingsPath() const
    {
        return (std::filesystem::path(m_saveDirectory) / "settings.dat").string();
    }

    bool SaveManager::savePlayerData(const PlayerData& data, std::string* error) const
    {
        if (!validPlayerData(data))
        {
            setError(error, "Player data contains invalid values.");
            return false;
        }

        std::vector<std::uint8_t> payload;
        appendValue(payload, data.totalGamesPlayed);
        appendValue(payload, data.totalPlayTimeSeconds);
        appendValue(payload, data.totalKills);
        appendValue(payload, data.totalCoinsCollected);
        appendValue(payload, data.bestScore);
        appendValue(payload, data.bestWave);
        appendValue(payload, data.bestSurvivalTimeSeconds);
        for (auto unlocked : data.weaponUnlocked) appendValue(payload, unlocked);
        const std::uint8_t hasRun = data.hasSavedRun ? 1 : 0;
        appendValue(payload, hasRun);
        appendValue(payload, data.savedWave);
        appendValue(payload, data.savedScore);
        appendValue(payload, data.savedRunKills);
        appendValue(payload, data.savedRunCoins);
        appendValue(payload, data.savedSurvivalSeconds);
        appendValue(payload, data.savedPlayerHealth);
        appendValue(payload, data.savedPlayerX);
        appendValue(payload, data.savedPlayerY);

        return writeFile(playerDataPath(), PLAYER_MAGIC, payload, error);
    }

    bool SaveManager::loadPlayerData(PlayerData& data, std::string* error) const
    {
        data = PlayerData{};
        std::vector<std::uint8_t> payload;
        if (!readFile(playerDataPath(), PLAYER_MAGIC, payload, error)) return false;

        std::size_t o = 0;
        std::uint8_t hasRun = 0;
        bool ok =
            readValue(payload, o, data.totalGamesPlayed) &&
            readValue(payload, o, data.totalPlayTimeSeconds) &&
            readValue(payload, o, data.totalKills) &&
            readValue(payload, o, data.totalCoinsCollected) &&
            readValue(payload, o, data.bestScore) &&
            readValue(payload, o, data.bestWave) &&
            readValue(payload, o, data.bestSurvivalTimeSeconds);

        for (auto& unlocked : data.weaponUnlocked)
            ok = ok && readValue(payload, o, unlocked);

        ok = ok &&
             readValue(payload, o, hasRun) &&
             readValue(payload, o, data.savedWave) &&
             readValue(payload, o, data.savedScore) &&
             readValue(payload, o, data.savedRunKills) &&
             readValue(payload, o, data.savedRunCoins) &&
             readValue(payload, o, data.savedSurvivalSeconds) &&
             readValue(payload, o, data.savedPlayerHealth) &&
             readValue(payload, o, data.savedPlayerX) &&
             readValue(payload, o, data.savedPlayerY);

        if (!ok || o != payload.size() || hasRun > 1 || !validPlayerData(data))
        {
            data = PlayerData{};
            setError(error, "Player save payload is invalid; safe defaults were loaded.");
            return false;
        }

        data.hasSavedRun = (hasRun != 0);
        for (auto& unlocked : data.weaponUnlocked) unlocked = unlocked ? 1 : 0;
        return true;
    }

    bool SaveManager::saveSettings(const GameSettings& settings, std::string* error) const
    {
        if (!validSettings(settings))
        {
            setError(error, "Settings contain invalid values.");
            return false;
        }

        std::vector<std::uint8_t> payload;
        appendValue(payload, settings.musicVolume);
        appendValue(payload, settings.sfxVolume);
        appendValue(payload, settings.difficulty);
        const std::uint8_t fullscreen = settings.fullscreen ? 1 : 0;
        appendValue(payload, fullscreen);
        appendValue(payload, settings.windowWidth);
        appendValue(payload, settings.windowHeight);

        return writeFile(settingsPath(), SETTINGS_MAGIC, payload, error);
    }

    bool SaveManager::loadSettings(GameSettings& settings, std::string* error) const
    {
        settings = GameSettings{};
        std::vector<std::uint8_t> payload;
        if (!readFile(settingsPath(), SETTINGS_MAGIC, payload, error)) return false;

        std::size_t o = 0;
        std::uint8_t fullscreen = 0;
        const bool ok =
            readValue(payload, o, settings.musicVolume) &&
            readValue(payload, o, settings.sfxVolume) &&
            readValue(payload, o, settings.difficulty) &&
            readValue(payload, o, fullscreen) &&
            readValue(payload, o, settings.windowWidth) &&
            readValue(payload, o, settings.windowHeight);

        if (!ok || o != payload.size() || fullscreen > 1 || !validSettings(settings))
        {
            settings = GameSettings{};
            setError(error, "Settings save is invalid; default settings were loaded.");
            return false;
        }
        settings.fullscreen = (fullscreen != 0);
        return true;
    }

    void SaveManager::recordCompletedRun(PlayerData& d,
                                         std::uint32_t score,
                                         std::uint32_t wave,
                                         std::uint32_t kills,
                                         std::uint32_t coins,
                                         std::uint64_t survivalSeconds)
    {
        ++d.totalGamesPlayed;
        d.totalKills += kills;
        d.totalCoinsCollected += coins;
        d.totalPlayTimeSeconds += survivalSeconds;
        d.bestScore = std::max(d.bestScore, score);
        d.bestWave = std::max(d.bestWave, wave);
        d.bestSurvivalTimeSeconds = std::max(d.bestSurvivalTimeSeconds, survivalSeconds);
        clearSavedRun(d);
    }

    void SaveManager::setSavedRun(PlayerData& d,
                                  std::uint32_t wave,
                                  std::uint32_t score,
                                  std::uint32_t kills,
                                  std::uint32_t coins,
                                  std::uint64_t survivalSeconds,
                                  float health,
                                  float playerX,
                                  float playerY)
    {
        d.hasSavedRun = true;
        d.savedWave = std::max(1u, wave);
        d.savedScore = score;
        d.savedRunKills = kills;
        d.savedRunCoins = coins;
        d.savedSurvivalSeconds = survivalSeconds;
        d.savedPlayerHealth = std::max(0.0f, health);
        d.savedPlayerX = playerX;
        d.savedPlayerY = playerY;
    }

    void SaveManager::clearSavedRun(PlayerData& d)
    {
        d.hasSavedRun = false;
        d.savedWave = 1;
        d.savedScore = 0;
        d.savedRunKills = 0;
        d.savedRunCoins = 0;
        d.savedSurvivalSeconds = 0;
        d.savedPlayerHealth = 100.0f;
        d.savedPlayerX = 0.0f;
        d.savedPlayerY = 0.0f;
    }
}
