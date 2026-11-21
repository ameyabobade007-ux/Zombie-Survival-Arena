#pragma once
// ============================================================================
//  AudioManager  -  Phase 5, Step 1-4   (SFML 3.x)
//  * Short effects : sf::SoundBuffer + pooled sf::Sound objects (no per-frame allocation)
//  * Music         : sf::Music with fade-out / fade-in transitions
//  * Volumes       : master, music, sfx + mute, saved to / loaded from a small text file
// ============================================================================
#include <SFML/Audio.hpp>
#include <array>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

enum class SoundId {
    PistolShot, ShotgunShot, RifleShot, SniperShot,
    BulletImpact, ZombieGrowl, ZombieAttack, ZombieDeath,
    PlayerHurt, PlayerDeath, Reload, Pickup, PowerUp,
    MenuClick, MenuHover, WaveStart,
    Count
};

enum class MusicId { None, Menu, Gameplay, Intense, GameOver, Count };

class AudioManager {
public:
    // basePath = folder that contains "music/" and "sfx/"  (e.g. "assets/audio")
    explicit AudioManager(std::string basePath = "assets/audio");

    // Loads every file. Returns false if at least one file is missing
    // (the game keeps running; missing sounds are simply silent).
    bool load();
    const std::vector<std::string>& missingFiles() const { return m_missing; }

    // ---- sound effects -----------------------------------------------------
    // pitchVariance 0.08 => pitch is randomised by +-8 % (sounds less repetitive)
    void play(SoundId id, float pitchVariance = 0.0f, float volumeScale = 1.0f);
    void stopSound(SoundId id);

    // Convenience helpers
    void playWeaponShot(int weaponIndex);      // 0 pistol, 1 shotgun, 2 rifle, 3 sniper
    void stopAllSounds();

    // Plays a random growl now and then while zombies are alive. Call every frame.
    void updateZombieAmbient(float dt, int zombiesAlive);

    // ---- music -------------------------------------------------------------
    void playMusic(MusicId id, bool loop = true);   // fades out the old track, fades in the new one
    void stopMusic(bool fade = true);
    void pauseMusic();
    void resumeMusic();
    MusicId currentMusic() const { return m_currentMusic; }

    // ---- volumes (0..1) ------------------------------------------------------
    void  setMasterVolume(float v);   float masterVolume() const { return m_master; }
    void  setMusicVolume(float v);    float musicVolume()  const { return m_music; }
    void  setSfxVolume(float v);      float sfxVolume()    const { return m_sfx; }
    void  setMuted(bool m);           bool  muted()        const { return m_muted; }
    void  toggleMute() { setMuted(!m_muted); }

    // ---- settings persistence (Phase 6 can move this into the save system) ---
    bool saveSettings(const std::string& file = "settings.cfg") const;
    bool loadSettings(const std::string& file = "settings.cfg");

    // Must be called once per frame (drives music fading).
    void update(float dt);

private:
    struct SfxEntry {
        sf::SoundBuffer buffer;
        std::vector<sf::Sound> voices;   // pool: several copies may play at once
        std::size_t next = 0;
        float minInterval = 0.0f;        // stops machine-gun style overlap spam
        float sinceLast  = 999.0f;
        float baseVolume = 1.0f;
        bool  loaded = false;
    };

    float effectiveSfxVolume() const;    // 0..100 for SFML
    float effectiveMusicVolume() const;  // 0..100 for SFML
    void  applyMusicVolume();
    bool  tryLoadMusic(MusicId id, const std::string& stem);
    sf::Music* music(MusicId id);

    std::string m_base;
    std::array<SfxEntry, static_cast<std::size_t>(SoundId::Count)> m_sfx_entries;
    std::array<std::unique_ptr<sf::Music>, static_cast<std::size_t>(MusicId::Count)> m_tracks;
    std::vector<std::string> m_missing;

    // music state machine
    MusicId m_currentMusic = MusicId::None;
    MusicId m_pendingMusic = MusicId::None;
    bool    m_pendingLoop = true;
    bool    m_hasPending = false;
    float   m_fade = 0.0f;               // 0..1 multiplier of the current track
    float   m_fadeSpeed = 1.0f / 0.8f;   // fade time = 0.8 s
    enum class FadeState { Idle, FadingOut, FadingIn } m_fadeState = FadeState::Idle;

    float m_master = 0.8f, m_music = 0.6f, m_sfx = 0.85f;
    bool  m_muted = false;
    float m_growlTimer = 3.0f;
    std::mt19937 m_rng{std::random_device{}()};
};
