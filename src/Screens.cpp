#include "Screens.hpp"
#include <cmath>
#include <random>

using ui::Align;

// ------------------------------------------------------------ ButtonColumn --
void ButtonColumn::add(const std::string& label, MenuAction a, sf::Vector2f c, sf::Vector2f s)
{ m_buttons.emplace_back(*m_font, label, c, s); m_actions.push_back(a); }

void ButtonColumn::update(float dt, sf::Vector2f mouse)
{ for (auto& b : m_buttons) if (b.update(dt, mouse)) m_audio->play(SoundId::MenuHover); }

MenuAction ButtonColumn::click(const sf::Event& e, sf::Vector2f mouse)
{
    if (const auto* p = e.getIf<sf::Event::MouseButtonPressed>()) {
        if (p->button != sf::Mouse::Button::Left) return MenuAction::None;
        for (std::size_t i = 0; i < m_buttons.size(); ++i)
            if (m_buttons[i].contains(mouse)) { m_buttons[i].click(); m_audio->play(SoundId::MenuClick); return m_actions[i]; }
    }
    return MenuAction::None;
}
void ButtonColumn::draw(sf::RenderTarget& rt) const { for (const auto& b : m_buttons) b.draw(rt); }

static void dim(sf::RenderTarget& rt, sf::Vector2f s, std::uint8_t a)
{ sf::RectangleShape r(s); r.setFillColor(sf::Color(0, 0, 0, a)); rt.draw(r); }

// ---------------------------------------------------------------- MainMenu --
MainMenuScreen::MainMenuScreen(const sf::Font& font, AudioManager& audio, sf::Vector2f screen)
    : m_font(&font), m_screen(screen), m_buttons(font, audio)
{
    const float cx = screen.x * 0.5f; float y = screen.y * 0.45f;
    m_buttons.add("PLAY",     MenuAction::Play,     {cx, y});          y += 68.f;
    m_buttons.add("SETTINGS", MenuAction::Settings, {cx, y});          y += 68.f;
    m_buttons.add("CONTROLS", MenuAction::Controls, {cx, y});          y += 68.f;
    m_buttons.add("EXIT",     MenuAction::Exit,     {cx, y});

    std::mt19937 rng(5);
    std::uniform_real_distribution<float> rx(0.f, screen.x), ry(0.f, screen.y), rv(8.f, 30.f), rr(1.f, 3.5f), rp(0.f, 6.28f), rs(8.f, 22.f), rsc(0.8f, 1.5f);
    for (int i = 0; i < 70; ++i) m_embers.push_back({{rx(rng), ry(rng)}, {rv(rng) * 0.3f, -rv(rng)}, rr(rng), rp(rng)});
    for (int i = 0; i < 6; ++i)  m_zombies.push_back({rx(rng), rs(rng), rsc(rng), rp(rng)});
}

MenuAction MainMenuScreen::handleEvent(const sf::Event& e, sf::Vector2f mouse) { return m_buttons.click(e, mouse); }

void MainMenuScreen::update(float dt, sf::Vector2f mouse)
{
    m_time += dt;
    m_buttons.update(dt, mouse);
    for (auto& e : m_embers) {
        e.pos += e.vel * dt; e.pos.x += std::sin(m_time + e.phase) * 12.f * dt;
        if (e.pos.y < -10.f) { e.pos.y = m_screen.y + 10.f; }
        if (e.pos.x > m_screen.x + 10.f) e.pos.x = -10.f;
    }
    for (auto& z : m_zombies) { z.x += z.speed * dt; if (z.x > m_screen.x + 80.f) z.x = -80.f; }
}

void MainMenuScreen::draw(sf::RenderTarget& rt)
{
    ui::drawGradient(rt, {{0.f, 0.f}, m_screen}, sf::Color(8, 8, 14), sf::Color(55, 12, 12));
    // blood moon
    sf::CircleShape moon(90.f); moon.setOrigin({90.f, 90.f}); moon.setPosition({m_screen.x * 0.78f, m_screen.y * 0.22f});
    moon.setFillColor(sf::Color(150, 20, 20, 140)); rt.draw(moon);
    sf::CircleShape halo(150.f); halo.setOrigin({150.f, 150.f}); halo.setPosition(moon.getPosition());
    halo.setFillColor(sf::Color(160, 20, 20, 35)); rt.draw(halo);

    // shambling silhouettes along the bottom
    for (const auto& z : m_zombies) {
        const float bob = std::sin(m_time * 3.f + z.phase) * 4.f, base = m_screen.y - 30.f;
        sf::RectangleShape body({26.f * z.scale, 60.f * z.scale}); body.setOrigin({13.f * z.scale, 60.f * z.scale});
        body.setPosition({z.x, base + bob}); body.setFillColor(sf::Color(4, 6, 6, 235)); rt.draw(body);
        sf::CircleShape head(11.f * z.scale); head.setOrigin({11.f * z.scale, 11.f * z.scale});
        head.setPosition({z.x, base - 66.f * z.scale + bob}); head.setFillColor(sf::Color(4, 6, 6, 235)); rt.draw(head);
        sf::RectangleShape arm({44.f * z.scale, 9.f * z.scale}); arm.setPosition({z.x, base - 52.f * z.scale + bob}); arm.setFillColor(sf::Color(4, 6, 6, 235)); rt.draw(arm);
    }
    for (const auto& e : m_embers) {
        const float a = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(m_time * 2.f + e.phase));
        sf::CircleShape c(e.r); c.setPosition(e.pos); c.setFillColor(ui::withAlpha(sf::Color(255, 120, 40), a)); rt.draw(c);
    }
    dim(rt, m_screen, 40);

    const float pulse = 1.f + 0.02f * std::sin(m_time * 2.f);
    const float cx = m_screen.x * 0.5f;
    const int flick = (std::fmod(m_time, 4.f) > 3.85f) ? 1 : 0;       // occasional glitch flicker
    ui::drawText(rt, *m_font, "ZOMBIE", static_cast<unsigned>(86 * pulse), {cx + flick * 5.f, m_screen.y * 0.16f}, sf::Color(220, 30, 25), Align::Center, 5.f);
    ui::drawText(rt, *m_font, "SURVIVAL ARENA", static_cast<unsigned>(52 * pulse), {cx, m_screen.y * 0.28f}, sf::Color(235, 235, 235), Align::Center, 4.f);
    m_buttons.draw(rt);
    ui::drawText(rt, *m_font, "v1.0  -  Phase 5", 14, {16.f, m_screen.y - 16.f}, sf::Color(150, 150, 150));
}

// ---------------------------------------------------------------- Settings --
SettingsScreen::SettingsScreen(const sf::Font& font, AudioManager& audio, sf::Vector2f screen, bool overlay)
    : m_font(&font), m_audio(&audio), m_screen(screen), m_overlay(overlay),
      m_master("Master Volume", {screen.x * 0.5f - 250.f, screen.y * 0.34f}, 500.f, audio.masterVolume()),
      m_music ("Music Volume",  {screen.x * 0.5f - 250.f, screen.y * 0.48f}, 500.f, audio.musicVolume()),
      m_sfx   ("Sound Effects", {screen.x * 0.5f - 250.f, screen.y * 0.62f}, 500.f, audio.sfxVolume()),
      m_mute(font, "MUTE: OFF", {screen.x * 0.5f, screen.y * 0.74f}, {320.f, 50.f}),
      m_back(font, "BACK", {screen.x * 0.5f, screen.y * 0.85f}, {320.f, 56.f})
{ refreshFromAudio(); }

void SettingsScreen::refreshFromAudio()
{
    m_master.setValue(m_audio->masterVolume()); m_music.setValue(m_audio->musicVolume()); m_sfx.setValue(m_audio->sfxVolume());
    m_mute.setLabel(m_audio->muted() ? "MUTE: ON" : "MUTE: OFF");
}

MenuAction SettingsScreen::handleEvent(const sf::Event& e, sf::Vector2f mouse)
{
    using R = ui::Slider::Result;
    if (m_master.handleEvent(e, mouse) != R::None) m_audio->setMasterVolume(m_master.value());
    if (m_music.handleEvent(e, mouse)  != R::None) m_audio->setMusicVolume(m_music.value());
    if (m_sfx.handleEvent(e, mouse)    == R::Released) { m_audio->setSfxVolume(m_sfx.value()); m_audio->play(SoundId::PistolShot); }
    else if (e.is<sf::Event::MouseMoved>()) m_audio->setSfxVolume(m_sfx.value());

    if (const auto* p = e.getIf<sf::Event::MouseButtonPressed>()) {
        if (p->button == sf::Mouse::Button::Left) {
            if (m_mute.contains(mouse)) { m_mute.click(); m_audio->toggleMute(); m_audio->play(SoundId::MenuClick); refreshFromAudio(); }
            else if (m_back.contains(mouse)) { m_back.click(); m_audio->play(SoundId::MenuClick); m_audio->saveSettings(); return MenuAction::Back; }
        }
    }
    if (const auto* k = e.getIf<sf::Event::KeyPressed>()) {
        if (k->code == sf::Keyboard::Key::M) { m_audio->toggleMute(); refreshFromAudio(); }
    }
    return MenuAction::None;
}

void SettingsScreen::update(float dt, sf::Vector2f mouse)
{
    if (m_mute.update(dt, mouse)) m_audio->play(SoundId::MenuHover);
    if (m_back.update(dt, mouse)) m_audio->play(SoundId::MenuHover);
}

void SettingsScreen::draw(sf::RenderTarget& rt)
{
    if (m_overlay) dim(rt, m_screen, 200);
    else { ui::drawGradient(rt, {{0.f, 0.f}, m_screen}, sf::Color(8, 8, 14), sf::Color(40, 10, 10)); }
    ui::drawText(rt, *m_font, "AUDIO SETTINGS", 52, {m_screen.x * 0.5f, m_screen.y * 0.14f}, sf::Color(230, 40, 30), Align::Center, 3.f);
    const bool muted = m_audio->muted();
    m_master.draw(rt, *m_font, muted); m_music.draw(rt, *m_font, muted); m_sfx.draw(rt, *m_font, muted);
    m_mute.draw(rt); m_back.draw(rt);
    ui::drawText(rt, *m_font, "Press M to mute / unmute", 16, {m_screen.x * 0.5f, m_screen.y * 0.93f}, sf::Color(160, 160, 165), Align::Center);
}

// ---------------------------------------------------------------- Controls --
ControlsScreen::ControlsScreen(const sf::Font& font, AudioManager& audio, sf::Vector2f screen)
    : m_font(&font), m_screen(screen), m_buttons(font, audio)
{ m_buttons.add("BACK", MenuAction::Back, {screen.x * 0.5f, screen.y * 0.88f}); }

MenuAction ControlsScreen::handleEvent(const sf::Event& e, sf::Vector2f mouse) { return m_buttons.click(e, mouse); }
void ControlsScreen::update(float dt, sf::Vector2f mouse) { m_buttons.update(dt, mouse); }
void ControlsScreen::draw(sf::RenderTarget& rt)
{
    ui::drawGradient(rt, {{0.f, 0.f}, m_screen}, sf::Color(8, 8, 14), sf::Color(40, 10, 10));
    ui::drawText(rt, *m_font, "CONTROLS", 52, {m_screen.x * 0.5f, m_screen.y * 0.13f}, sf::Color(230, 40, 30), Align::Center, 3.f);
    const char* rows[][2] = {{"W A S D", "Move"}, {"Mouse", "Aim"}, {"Left Click", "Shoot"}, {"R", "Reload"},
                             {"1 / 2 / 3 / 4", "Pistol / Shotgun / Rifle / Sniper"}, {"Esc", "Pause"}, {"M", "Mute / Unmute"}};
    float y = m_screen.y * 0.27f;
    for (const auto& r : rows) {
        ui::drawText(rt, *m_font, r[0], 26, {m_screen.x * 0.5f - 40.f, y}, sf::Color(255, 190, 70), Align::Right);
        ui::drawText(rt, *m_font, r[1], 26, {m_screen.x * 0.5f + 20.f, y}, sf::Color(225, 225, 230), Align::Left);
        y += 46.f;
    }
    m_buttons.draw(rt);
}

// ------------------------------------------------------------------- Pause --
PauseScreen::PauseScreen(const sf::Font& font, AudioManager& audio, sf::Vector2f screen)
    : m_font(&font), m_screen(screen), m_buttons(font, audio)
{
    float y = screen.y * 0.36f; const float cx = screen.x * 0.5f;
    m_buttons.add("RESUME",      MenuAction::Resume,   {cx, y}); y += 64.f;
    m_buttons.add("RESTART",     MenuAction::Restart,  {cx, y}); y += 64.f;
    m_buttons.add("SETTINGS",    MenuAction::Settings, {cx, y}); y += 64.f;
    m_buttons.add("MAIN MENU",   MenuAction::MainMenu, {cx, y}); y += 64.f;
    m_buttons.add("EXIT GAME",   MenuAction::Exit,     {cx, y});
}
MenuAction PauseScreen::handleEvent(const sf::Event& e, sf::Vector2f mouse) { return m_buttons.click(e, mouse); }
void PauseScreen::update(float dt, sf::Vector2f mouse) { m_buttons.update(dt, mouse); }
void PauseScreen::draw(sf::RenderTarget& rt)
{
    dim(rt, m_screen, 175);
    ui::drawText(rt, *m_font, "PAUSED", 64, {m_screen.x * 0.5f, m_screen.y * 0.2f}, sf::Color(235, 235, 235), Align::Center, 4.f);
    m_buttons.draw(rt);
}

// --------------------------------------------------------------- Game over --
GameOverScreen::GameOverScreen(const sf::Font& font, AudioManager& audio, sf::Vector2f screen)
    : m_font(&font), m_screen(screen), m_buttons(font, audio)
{
    m_buttons.add("RESTART",   MenuAction::Restart,  {screen.x * 0.5f - 180.f, screen.y * 0.86f}, {300.f, 56.f});
    m_buttons.add("MAIN MENU", MenuAction::MainMenu, {screen.x * 0.5f + 180.f, screen.y * 0.86f}, {300.f, 56.f});
}
MenuAction GameOverScreen::handleEvent(const sf::Event& e, sf::Vector2f mouse) { return m_time > 0.6f ? m_buttons.click(e, mouse) : MenuAction::None; }
void GameOverScreen::update(float dt, sf::Vector2f mouse) { m_time += dt; m_buttons.update(dt, mouse); }
void GameOverScreen::draw(sf::RenderTarget& rt)
{
    const float a = std::min(1.f, m_time / 0.8f);
    dim(rt, m_screen, static_cast<std::uint8_t>(210 * a));
    const float cx = m_screen.x * 0.5f;
    ui::drawText(rt, *m_font, "GAME OVER", 88, {cx, m_screen.y * 0.18f - (1.f - a) * 40.f}, ui::withAlpha(sf::Color(220, 25, 20), a), Align::Center, 5.f);

    sf::RectangleShape box({520.f, 280.f}); box.setOrigin({260.f, 0.f}); box.setPosition({cx, m_screen.y * 0.32f});
    box.setFillColor(ui::withAlpha(sf::Color(12, 12, 16), 0.8f * a)); box.setOutlineThickness(2.f); box.setOutlineColor(ui::withAlpha(sf::Color(200, 40, 30), a));
    rt.draw(box);

    auto row = [&](const std::string& k, const std::string& v, float y, sf::Color vc) {
        ui::drawText(rt, *m_font, k, 26, {cx - 220.f, y}, ui::withAlpha(sf::Color(190, 190, 195), a), Align::Left);
        ui::drawText(rt, *m_font, v, 30, {cx + 220.f, y}, ui::withAlpha(vc, a), Align::Right, 1.f);
    };
    float y = m_screen.y * 0.32f + 45.f;
    row("Final Score",     std::to_string(m_stats.score),         y, sf::Color::White);                y += 56.f;
    row("Highest Score",   std::to_string(m_stats.highScore),     y, sf::Color(255, 210, 80));         y += 56.f;
    row("Waves Survived",  std::to_string(m_stats.wavesSurvived), y, sf::Color::White);                y += 56.f;
    row("Zombies Killed",  std::to_string(m_stats.zombiesKilled), y, sf::Color::White);
    if (m_stats.newHighScore)
        ui::drawText(rt, *m_font, "NEW HIGH SCORE!", 30, {cx, m_screen.y * 0.32f + 300.f + 6.f * std::sin(m_time * 6.f)},
                     ui::withAlpha(sf::Color(255, 210, 80), a), Align::Center, 2.f);
    m_buttons.draw(rt);
}
