#include "AudioManager.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
struct SfxInfo { const char* file; int voices; float minInterval; float volume; };
// order MUST match enum SoundId
constexpr SfxInfo kSfx[] = {
    {"pistol_shot.wav",   4, 0.05f, 0.9f},
    {"shotgun_shot.wav",  3, 0.10f, 1.0f},
    {"rifle_shot.wav",    6, 0.03f, 0.8f},
    {"sniper_shot.wav",   2, 0.10f, 1.0f},
    {"bullet_impact.wav", 8, 0.04f, 0.6f},
    {"zombie_growl.wav",  4, 0.30f, 0.7f},
    {"zombie_attack.wav", 4, 0.20f, 0.8f},
    {"zombie_death.wav",  6, 0.08f, 0.8f},
    {"player_hurt.wav",   2, 0.15f, 1.0f},
    {"player_death.wav",  1, 0.00f, 1.0f},
    {"reload.wav",        1, 0.00f, 0.9f},
    {"pickup.wav",        3, 0.05f, 0.9f},
    {"powerup.wav",       2, 0.10f, 1.0f},
    {"menu_click.wav",    3, 0.03f, 0.9f},
    {"menu_hover.wav",    2, 0.03f, 0.5f},
    {"wave_start.wav",    1, 0.00f, 1.0f},
};
static_assert(sizeof(kSfx) / sizeof(kSfx[0]) == static_cast<std::size_t>(SoundId::Count), "SfxInfo table mismatch");
}

AudioManager::AudioManager(std::string basePath) : m_base(std::move(basePath)) {}

bool AudioManager::load()
{
    namespace fs = std::filesystem;
    m_missing.clear();

    for (std::size_t i = 0; i < static_cast<std::size_t>(SoundId::Count); ++i) {
        SfxEntry& e = m_sfx_entries[i];
        const std::string path = m_base + "/sfx/" + kSfx[i].file;
        e.voices.clear(); e.loaded = false;
        if (!fs::exists(path)) { m_missing.push_back(path); continue; }
        if (!e.buffer.loadFromFile(path)) { m_missing.push_back(path); continue; }
        e.minInterval = kSfx[i].minInterval;
        e.baseVolume  = kSfx[i].volume;
        e.voices.reserve(static_cast<std::size_t>(kSfx[i].voices));
        for (int v = 0; v < kSfx[i].voices; ++v) e.voices.emplace_back(e.buffer);   // SFML 3: Sound needs a buffer
        e.loaded = true;
    }

    tryLoadMusic(MusicId::Menu,     "menu_theme");
    tryLoadMusic(MusicId::Gameplay, "gameplay_theme");
    tryLoadMusic(MusicId::Intense,  "intense_wave");
    tryLoadMusic(MusicId::GameOver, "game_over");

    for (const auto& m : m_missing) std::cerr << "[Audio] missing/invalid: " << m << '\n';
    return m_missing.empty();
}

bool AudioManager::tryLoadMusic(MusicId id, const std::string& stem)
{
    // accepts .ogg, .wav, .flac or .mp3 so you can drop in whatever you have
    for (const char* ext : {".ogg", ".wav", ".flac", ".mp3"}) {
        const std::string path = m_base + "/music/" + stem + ext;
        if (!std::filesystem::exists(path)) continue;
        auto mus = std::make_unique<sf::Music>();
        if (mus->openFromFile(path)) {
            mus->setLooping(true);
            m_tracks[static_cast<std::size_t>(id)] = std::move(mus);
            return true;
        }
    }
    m_missing.push_back(m_base + "/music/" + stem + ".ogg");
    return false;
}

sf::Music* AudioManager::music(MusicId id)
{
    if (id == MusicId::None) return nullptr;
    return m_tracks[static_cast<std::size_t>(id)].get();
}

float AudioManager::effectiveSfxVolume() const   { return m_muted ? 0.f : m_master * m_sfx   * 100.f; }
float AudioManager::effectiveMusicVolume() const { return m_muted ? 0.f : m_master * m_music * 100.f * m_fade; }

// ------------------------------------------------------------------ sfx ----
void AudioManager::play(SoundId id, float pitchVariance, float volumeScale)
{
    SfxEntry& e = m_sfx_entries[static_cast<std::size_t>(id)];
    if (!e.loaded || e.voices.empty()) return;
    if (e.sinceLast < e.minInterval) return;           // prevents ugly overlap
    e.sinceLast = 0.f;

    sf::Sound& s = e.voices[e.next];
    e.next = (e.next + 1) % e.voices.size();           // round-robin through the pool

    float pitch = 1.f;
    if (pitchVariance > 0.f) {
        std::uniform_real_distribution<float> d(1.f - pitchVariance, 1.f + pitchVariance);
        pitch = d(m_rng);
    }
    s.setPitch(pitch);
    s.setVolume(std::clamp(effectiveSfxVolume() * e.baseVolume * volumeScale, 0.f, 100.f));
    s.play();
}

void AudioManager::stopSound(SoundId id)
{
    for (auto& s : m_sfx_entries[static_cast<std::size_t>(id)].voices) s.stop();
}

void AudioManager::stopAllSounds()
{
    for (auto& e : m_sfx_entries) for (auto& s : e.voices) s.stop();
}

void AudioManager::playWeaponShot(int weaponIndex)
{
    switch (weaponIndex) {
        case 0:  play(SoundId::PistolShot,  0.04f); break;
        case 1:  play(SoundId::ShotgunShot, 0.03f); break;
        case 2:  play(SoundId::RifleShot,   0.05f); break;
        default: play(SoundId::SniperShot,  0.02f); break;
    }
}

void AudioManager::updateZombieAmbient(float dt, int zombiesAlive)
{
    if (zombiesAlive <= 0) { m_growlTimer = 2.0f; return; }
    m_growlTimer -= dt;
    if (m_growlTimer <= 0.f) {
        std::uniform_real_distribution<float> d(2.0f, 6.0f);
        // more zombies => more frequent growls, but never spammy
        m_growlTimer = d(m_rng) / (1.0f + std::min(zombiesAlive, 20) * 0.08f);
        play(SoundId::ZombieGrowl, 0.15f, 0.6f);
    }
}

// ---------------------------------------------------------------- music ----
void AudioManager::playMusic(MusicId id, bool loop)
{
    if (id == m_currentMusic && m_fadeState != FadeState::FadingOut) return;  // already playing
    if (id == MusicId::None) { stopMusic(true); return; }

    m_pendingMusic = id; m_pendingLoop = loop; m_hasPending = true;
    if (m_currentMusic == MusicId::None || !music(m_currentMusic)) {            // nothing playing -> start now
        m_fade = 0.f; m_currentMusic = MusicId::None; m_fadeState = FadeState::FadingOut;
    } else {
        m_fadeState = FadeState::FadingOut;
    }
}

void AudioManager::stopMusic(bool fade)
{
    m_hasPending = false; m_pendingMusic = MusicId::None;
    if (!fade) {
        if (auto* m = music(m_currentMusic)) m->stop();
        m_currentMusic = MusicId::None; m_fadeState = FadeState::Idle; m_fade = 0.f;
    } else if (m_currentMusic != MusicId::None) {
        m_fadeState = FadeState::FadingOut;
    }
}

void AudioManager::pauseMusic()  { if (auto* m = music(m_currentMusic)) m->pause(); }
void AudioManager::resumeMusic() { if (auto* m = music(m_currentMusic)) if (m->getStatus() != sf::SoundSource::Status::Playing) m->play(); }

void AudioManager::applyMusicVolume()
{
    if (auto* m = music(m_currentMusic)) m->setVolume(effectiveMusicVolume());
}

void AudioManager::update(float dt)
{
    for (auto& e : m_sfx_entries) e.sinceLast += dt;

    switch (m_fadeState) {
    case FadeState::Idle: break;
    case FadeState::FadingOut:
        m_fade -= m_fadeSpeed * dt;
        if (m_fade <= 0.f || m_currentMusic == MusicId::None) {
            m_fade = 0.f;
            if (auto* m = music(m_currentMusic)) m->stop();
            m_currentMusic = MusicId::None;
            if (m_hasPending) {
                m_hasPending = false;
                if (auto* n = music(m_pendingMusic)) {
                    m_currentMusic = m_pendingMusic;
                    n->setLooping(m_pendingLoop);
                    n->setVolume(0.f);
                    n->play();
                    m_fadeState = FadeState::FadingIn;
                } else m_fadeState = FadeState::Idle;
            } else m_fadeState = FadeState::Idle;
        }
        break;
    case FadeState::FadingIn:
        m_fade += m_fadeSpeed * dt;
        if (m_fade >= 1.f) { m_fade = 1.f; m_fadeState = FadeState::Idle; }
        break;
    }
    applyMusicVolume();
}

// -------------------------------------------------------------- volumes ----
void AudioManager::setMasterVolume(float v) { m_master = std::clamp(v, 0.f, 1.f); applyMusicVolume(); }
void AudioManager::setMusicVolume(float v)  { m_music  = std::clamp(v, 0.f, 1.f); applyMusicVolume(); }
void AudioManager::setSfxVolume(float v)    { m_sfx    = std::clamp(v, 0.f, 1.f); }
void AudioManager::setMuted(bool m)         { m_muted = m; applyMusicVolume(); if (m) stopAllSounds(); }

// ---------------------------------------------------------- persistence ----
bool AudioManager::saveSettings(const std::string& file) const
{
    std::ofstream f(file);
    if (!f) return false;
    f << "master=" << m_master << "\nmusic=" << m_music << "\nsfx=" << m_sfx << "\nmuted=" << (m_muted ? 1 : 0) << '\n';
    return true;
}

bool AudioManager::loadSettings(const std::string& file)
{
    std::ifstream f(file);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        float val = 0.f;
        try { val = std::stof(line.substr(eq + 1)); } catch (...) { continue; }
        if (key == "master") setMasterVolume(val);
        else if (key == "music") setMusicVolume(val);
        else if (key == "sfx") setSfxVolume(val);
        else if (key == "muted") m_muted = val != 0.f;
    }
    applyMusicVolume();
    return true;
}
