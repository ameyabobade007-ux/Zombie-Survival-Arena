#pragma once
// ============================================================================
//  Screens  -  Phase 5, Steps 5, 7, 8   (SFML 3.x)
//  MainMenuScreen, SettingsScreen, ControlsScreen, PauseScreen, GameOverScreen
//  All screens return a MenuAction; your game state machine decides what to do.
// ============================================================================
#include "AudioManager.hpp"
#include "UIWidgets.hpp"
#include <memory>
#include <vector>

enum class MenuAction { None, Play, Settings, Controls, Exit, Back, Resume, Restart, MainMenu };

class Screen {
public:
    virtual ~Screen() = default;
    virtual MenuAction handleEvent(const sf::Event& e, sf::Vector2f mouse) = 0;
    virtual void update(float dt, sf::Vector2f mouse) = 0;
    virtual void draw(sf::RenderTarget& rt) = 0;
};

// A vertical list of buttons with hover/click sounds
class ButtonColumn {
public:
    ButtonColumn(const sf::Font& font, AudioManager& audio) : m_font(&font), m_audio(&audio) {}
    void add(const std::string& label, MenuAction action, sf::Vector2f center, sf::Vector2f size = {320.f, 56.f});
    void update(float dt, sf::Vector2f mouse);
    MenuAction click(const sf::Event& e, sf::Vector2f mouse);
    void draw(sf::RenderTarget& rt) const;
private:
    const sf::Font* m_font; AudioManager* m_audio;
    std::vector<ui::Button> m_buttons; std::vector<MenuAction> m_actions;
};

class MainMenuScreen : public Screen {
public:
    MainMenuScreen(const sf::Font& font, AudioManager& audio, sf::Vector2f screen);
    MenuAction handleEvent(const sf::Event& e, sf::Vector2f mouse) override;
    void update(float dt, sf::Vector2f mouse) override;
    void draw(sf::RenderTarget& rt) override;
private:
    struct Ember { sf::Vector2f pos, vel; float r, phase; };
    struct Shambler { float x, speed, scale, phase; };
    const sf::Font* m_font; sf::Vector2f m_screen;
    ButtonColumn m_buttons;
    std::vector<Ember> m_embers; std::vector<Shambler> m_zombies;
    float m_time = 0.f;
};

class SettingsScreen : public Screen {
public:
    SettingsScreen(const sf::Font& font, AudioManager& audio, sf::Vector2f screen, bool overlay = false);
    MenuAction handleEvent(const sf::Event& e, sf::Vector2f mouse) override;
    void update(float dt, sf::Vector2f mouse) override;
    void draw(sf::RenderTarget& rt) override;
    void refreshFromAudio();      // call when the screen is opened
    void setOverlay(bool o) { m_overlay = o; }
private:
    const sf::Font* m_font; AudioManager* m_audio; sf::Vector2f m_screen; bool m_overlay;
    ui::Slider m_master, m_music, m_sfx;
    ui::Button m_mute, m_back;
};

class ControlsScreen : public Screen {
public:
    ControlsScreen(const sf::Font& font, AudioManager& audio, sf::Vector2f screen);
    MenuAction handleEvent(const sf::Event& e, sf::Vector2f mouse) override;
    void update(float dt, sf::Vector2f mouse) override;
    void draw(sf::RenderTarget& rt) override;
private:
    const sf::Font* m_font; sf::Vector2f m_screen; ButtonColumn m_buttons;
};

class PauseScreen : public Screen {
public:
    PauseScreen(const sf::Font& font, AudioManager& audio, sf::Vector2f screen);
    MenuAction handleEvent(const sf::Event& e, sf::Vector2f mouse) override;
    void update(float dt, sf::Vector2f mouse) override;
    void draw(sf::RenderTarget& rt) override;
private:
    const sf::Font* m_font; sf::Vector2f m_screen; ButtonColumn m_buttons;
};

struct GameOverStats { long score = 0, highScore = 0; int wavesSurvived = 0, zombiesKilled = 0; bool newHighScore = false; };

class GameOverScreen : public Screen {
public:
    GameOverScreen(const sf::Font& font, AudioManager& audio, sf::Vector2f screen);
    void setStats(const GameOverStats& s) { m_stats = s; m_time = 0.f; }
    MenuAction handleEvent(const sf::Event& e, sf::Vector2f mouse) override;
    void update(float dt, sf::Vector2f mouse) override;
    void draw(sf::RenderTarget& rt) override;
private:
    const sf::Font* m_font; sf::Vector2f m_screen; ButtonColumn m_buttons; GameOverStats m_stats; float m_time = 0.f;
};
