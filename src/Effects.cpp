#include "Effects.hpp"
#include "UIWidgets.hpp"
#include <algorithm>
#include <cmath>

// ------------------------------------------------------------ ScreenShake --
void ScreenShake::trigger(float intensity, float duration)
{
    if (intensity >= m_intensity * (m_duration > 0 ? (1.f - m_time / m_duration) : 0.f)) {
        m_intensity = intensity; m_duration = duration; m_time = 0.f;
    }
}
void ScreenShake::update(float dt)
{
    if (m_time >= m_duration) { m_offset = {0.f, 0.f}; return; }
    m_time += dt;
    const float fall = std::max(0.f, 1.f - m_time / m_duration);
    std::uniform_real_distribution<float> d(-1.f, 1.f);
    m_offset = {d(m_rng) * m_intensity * fall, d(m_rng) * m_intensity * fall};
}

// ------------------------------------------------------------ DamageFlash --
void DamageFlash::update(float dt, float healthRatio)
{
    m_alpha = std::max(0.f, m_alpha - dt * 1.6f);
    m_health = healthRatio;
    m_pulse += dt * (healthRatio < 0.3f ? 5.f : 0.f);
}
void DamageFlash::draw(sf::RenderTarget& rt, sf::Vector2f screen) const
{
    if (m_alpha > 0.f) {
        sf::RectangleShape r(screen);
        r.setFillColor(sf::Color(200, 0, 0, static_cast<std::uint8_t>(m_alpha * 255.f)));
        rt.draw(r);
    }
    if (m_health < 0.3f && m_health > 0.f) {         // pulsing red vignette (low-health warning)
        const float s = 0.5f + 0.5f * std::sin(m_pulse);
        const float a = (0.12f + 0.25f * s) * (1.f - m_health / 0.3f * 0.5f);
        const sf::Color edge(190, 0, 0, static_cast<std::uint8_t>(a * 255.f));
        const sf::Color none(190, 0, 0, 0);
        const float t = 140.f;
        auto strip = [&](sf::FloatRect fr, bool vertical, bool flip) {
            ui::drawGradient(rt, fr, flip ? none : edge, flip ? edge : none);
            (void)vertical;
        };
        strip({{0.f, 0.f}, {screen.x, t}}, true, false);
        strip({{0.f, screen.y - t}, {screen.x, t}}, true, true);
        // side edges via thin rectangles (alpha falls off in steps)
        for (int i = 0; i < 6; ++i) {
            const float w = 90.f * (1.f - i / 6.f);
            const sf::Color c(190, 0, 0, static_cast<std::uint8_t>(a * 255.f * 0.35f));
            sf::RectangleShape L({w, screen.y}); L.setFillColor(c); rt.draw(L);
            sf::RectangleShape R({w, screen.y}); R.setPosition({screen.x - w, 0.f}); R.setFillColor(c); rt.draw(R);
        }
    }
}

// -------------------------------------------------------------- HitMarker --
void HitMarker::draw(sf::RenderTarget& rt, sf::Vector2f at) const
{
    if (m_time <= 0.f) return;
    const float k = m_time / 0.18f;
    const sf::Color c = m_kill ? sf::Color(255, 60, 60, static_cast<std::uint8_t>(255 * k))
                               : sf::Color(255, 255, 255, static_cast<std::uint8_t>(255 * k));
    const float len = m_kill ? 12.f : 9.f, gap = m_kill ? 8.f : 6.f + (1.f - k) * 4.f;
    for (int i = 0; i < 4; ++i) {
        sf::RectangleShape line({len, 2.5f});
        line.setOrigin({-gap, 1.25f});
        line.setPosition(at);
        line.setRotation(sf::degrees(45.f + 90.f * static_cast<float>(i)));
        line.setFillColor(c);
        rt.draw(line);
    }
}

// ----------------------------------------------------------- FloatingText --
void FloatingText::spawn(sf::Vector2f p, const std::string& text, sf::Color c, unsigned size)
{
    std::uniform_real_distribution<float> dx(-25.f, 25.f);
    m_items.push_back({{p.x + dx(m_rng), p.y - 10.f}, {dx(m_rng) * 0.6f, -70.f}, text, c, size, 0.9f, 0.9f});
    if (m_items.size() > 80) m_items.erase(m_items.begin());
}
void FloatingText::update(float dt)
{
    for (auto& i : m_items) { i.life -= dt; i.pos += i.vel * dt; i.vel.y += 60.f * dt; }
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [](const Item& i) { return i.life <= 0.f; }), m_items.end());
}
void FloatingText::draw(sf::RenderTarget& rt) const
{
    for (const auto& i : m_items) {
        const float a = std::clamp(i.life / i.maxLife * 1.5f, 0.f, 1.f);
        const float pop = 1.f + 0.5f * std::max(0.f, (i.life / i.maxLife - 0.8f) * 5.f);
        ui::drawText(rt, *m_font, i.text, static_cast<unsigned>(i.size * pop), i.pos,
                     ui::withAlpha(i.color, a), ui::Align::Center, 2.f, ui::withAlpha(sf::Color::Black, a));
    }
}

// ------------------------------------------------------------- MuzzleFlash --
void MuzzleFlash::spawn(sf::Vector2f p, float a, float s) { m_flashes.push_back({p, a, s, 0.06f}); }
void MuzzleFlash::update(float dt)
{
    for (auto& f : m_flashes) f.life -= dt;
    m_flashes.erase(std::remove_if(m_flashes.begin(), m_flashes.end(), [](const Flash& f) { return f.life <= 0.f; }), m_flashes.end());
}
void MuzzleFlash::draw(sf::RenderTarget& rt) const
{
    for (const auto& f : m_flashes) {
        const float k = f.life / 0.06f;
        sf::ConvexShape cone(3);
        cone.setPoint(0, {0.f, 0.f});
        cone.setPoint(1, {34.f * f.size, -10.f * f.size});
        cone.setPoint(2, {34.f * f.size, 10.f * f.size});
        cone.setPosition(f.pos);
        cone.setRotation(sf::degrees(f.angle));
        cone.setFillColor(sf::Color(255, 220, 120, static_cast<std::uint8_t>(230 * k)));
        rt.draw(cone);
        sf::CircleShape core(7.f * f.size);
        core.setOrigin({core.getRadius(), core.getRadius()});
        core.setPosition(f.pos);
        core.setFillColor(sf::Color(255, 255, 220, static_cast<std::uint8_t>(255 * k)));
        rt.draw(core);
    }
}

// -------------------------------------------------------------- WaveBanner --
void WaveBanner::show(const std::string& t, const std::string& s, float sec) { m_title = t; m_sub = s; m_time = m_total = sec; }
void WaveBanner::draw(sf::RenderTarget& rt, sf::Vector2f screen) const
{
    if (m_time <= 0.f) return;
    const float el = m_total - m_time;
    float a = 1.f;
    if (el < 0.35f) a = el / 0.35f; else if (m_time < 0.6f) a = m_time / 0.6f;
    const float slide = (1.f - std::min(1.f, el / 0.35f)) * 60.f;
    sf::RectangleShape band({screen.x, 120.f});
    band.setPosition({0.f, screen.y * 0.36f - 60.f});
    band.setFillColor(sf::Color(0, 0, 0, static_cast<std::uint8_t>(150 * a)));
    rt.draw(band);
    ui::drawText(rt, *m_font, m_title, 64, {screen.x * 0.5f + slide, screen.y * 0.36f - 10.f}, ui::withAlpha(sf::Color(230, 40, 30), a), ui::Align::Center, 3.f, ui::withAlpha(sf::Color::Black, a));
    if (!m_sub.empty())
        ui::drawText(rt, *m_font, m_sub, 24, {screen.x * 0.5f, screen.y * 0.36f + 36.f}, ui::withAlpha(sf::Color(235, 235, 235), a), ui::Align::Center, 1.5f);
}

// -------------------------------------------------------- NotificationFeed --
void NotificationFeed::push(const std::string& t, sf::Color c, float s)
{
    m_items.push_back({t, c, s, s});
    if (m_items.size() > 6) m_items.pop_front();
}
void NotificationFeed::update(float dt)
{
    for (auto& i : m_items) i.life -= dt;
    while (!m_items.empty() && m_items.front().life <= 0.f) m_items.pop_front();
}
void NotificationFeed::draw(sf::RenderTarget& rt, sf::Vector2f anchor) const
{
    float y = anchor.y;
    for (const auto& i : m_items) {
        const float a = std::clamp(i.life / 0.4f, 0.f, 1.f) * std::clamp((i.total - i.life) / 0.15f, 0.f, 1.f);
        ui::drawText(rt, *m_font, i.text, 22, {anchor.x, y}, ui::withAlpha(i.color, a), ui::Align::Right, 2.f, ui::withAlpha(sf::Color::Black, a));
        y += 30.f;
    }
}

// -------------------------------------------------------------- Transition --
void Transition::start(std::function<void()> cb, float half)
{
    if (busy()) return;
    m_cb = std::move(cb); m_half = half; m_t = 0.f; m_phase = Phase::Out;
}
void Transition::update(float dt)
{
    if (m_phase == Phase::Idle) return;
    m_t += dt;
    if (m_t >= m_half) {
        if (m_phase == Phase::Out) { if (m_cb) m_cb(); m_phase = Phase::In; m_t = 0.f; }
        else m_phase = Phase::Idle;
    }
}
void Transition::draw(sf::RenderTarget& rt, sf::Vector2f screen) const
{
    if (m_phase == Phase::Idle) return;
    const float k = std::clamp(m_t / m_half, 0.f, 1.f);
    const float a = (m_phase == Phase::Out) ? k : 1.f - k;
    sf::RectangleShape r(screen);
    r.setFillColor(sf::Color(0, 0, 0, static_cast<std::uint8_t>(a * 255.f)));
    rt.draw(r);
}
