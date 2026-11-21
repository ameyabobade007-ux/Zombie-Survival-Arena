#pragma once
// ============================================================================
//  Effects  -  Phase 5, Step 9   (SFML 3.x)
//  Screen shake, damage flash, low-health vignette, hit marker, floating damage
//  numbers, muzzle flash, wave banner, kill/power-up feed, fade transitions.
//  Everything is drawn with SFML shapes -> no sprites needed.
// ============================================================================
#include <SFML/Graphics.hpp>
#include <deque>
#include <functional>
#include <random>
#include <string>
#include <vector>

class ScreenShake {
public:
    void trigger(float intensityPixels, float durationSeconds);
    void update(float dt);
    sf::Vector2f offset() const { return m_offset; }   // add to your world view centre
private:
    float m_intensity = 0.f, m_duration = 0.f, m_time = 0.f;
    sf::Vector2f m_offset{0.f, 0.f};
    std::mt19937 m_rng{1234};
};

class DamageFlash {
public:
    void trigger(float strength = 1.f) { m_alpha = std::max(m_alpha, 0.55f * strength); }
    void update(float dt, float healthRatio);
    void draw(sf::RenderTarget& rt, sf::Vector2f screen) const;   // screen space
private:
    float m_alpha = 0.f;
    float m_pulse = 0.f;
    float m_health = 1.f;
};

class HitMarker {
public:
    void trigger(bool kill) { m_time = 0.18f; m_kill = kill; }
    void update(float dt) { m_time = std::max(0.f, m_time - dt); }
    void draw(sf::RenderTarget& rt, sf::Vector2f at) const;       // screen space, at crosshair
private:
    float m_time = 0.f; bool m_kill = false;
};

class FloatingText {
public:
    explicit FloatingText(const sf::Font& font) : m_font(&font) {}
    void spawn(sf::Vector2f worldPos, const std::string& text, sf::Color color = sf::Color(255, 230, 80), unsigned size = 22);
    void update(float dt);
    void draw(sf::RenderTarget& rt) const;                        // WORLD space (call with world view active)
private:
    struct Item { sf::Vector2f pos, vel; std::string text; sf::Color color; unsigned size; float life, maxLife; };
    const sf::Font* m_font;
    std::vector<Item> m_items;
    std::mt19937 m_rng{99};
};

class MuzzleFlash {
public:
    void spawn(sf::Vector2f muzzlePos, float angleDegrees, float size = 1.f);
    void update(float dt);
    void draw(sf::RenderTarget& rt) const;                        // WORLD space
private:
    struct Flash { sf::Vector2f pos; float angle, size, life; };
    std::vector<Flash> m_flashes;
};

// Big centred wave banner ("WAVE 3") + small sub line
class WaveBanner {
public:
    explicit WaveBanner(const sf::Font& font) : m_font(&font) {}
    void show(const std::string& title, const std::string& subtitle = "", float seconds = 2.6f);
    void update(float dt) { m_time = std::max(0.f, m_time - dt); }
    void draw(sf::RenderTarget& rt, sf::Vector2f screen) const;
private:
    const sf::Font* m_font;
    std::string m_title, m_sub;
    float m_time = 0.f, m_total = 1.f;
};

// Kill notifications and power-up announcements (stacked toasts)
class NotificationFeed {
public:
    explicit NotificationFeed(const sf::Font& font) : m_font(&font) {}
    void push(const std::string& text, sf::Color color = sf::Color::White, float seconds = 2.2f);
    void update(float dt);
    void clear() { m_items.clear(); }
    void draw(sf::RenderTarget& rt, sf::Vector2f anchorRightTop) const;
private:
    struct Item { std::string text; sf::Color color; float life, total; };
    const sf::Font* m_font;
    std::deque<Item> m_items;
};

// Fade out -> run callback while screen is black -> fade in
class Transition {
public:
    void start(std::function<void()> atBlack, float halfSeconds = 0.25f);
    void update(float dt);
    bool busy() const { return m_phase != Phase::Idle; }
    void draw(sf::RenderTarget& rt, sf::Vector2f screen) const;
private:
    enum class Phase { Idle, Out, In } m_phase = Phase::Idle;
    float m_t = 0.f, m_half = 0.25f;
    std::function<void()> m_cb;
};
