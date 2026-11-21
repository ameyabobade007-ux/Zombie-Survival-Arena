#include "HUD.hpp"
#include "UIWidgets.hpp"
#include <cmath>

using ui::Align;

static void panel(sf::RenderTarget& rt, sf::Vector2f pos, sf::Vector2f size)
{
    sf::RectangleShape p(size);
    p.setPosition(pos);
    p.setFillColor(sf::Color(10, 10, 12, 170));
    p.setOutlineThickness(1.5f);
    p.setOutlineColor(sf::Color(255, 255, 255, 40));
    rt.draw(p);
}

void HUD::draw(sf::RenderTarget& rt, const HudData& d, sf::Vector2f screen, sf::Vector2f mouse) const
{
    drawTop(rt, d, screen);
    drawPowerUps(rt, d);
    drawHealth(rt, d, screen);
    drawWeapon(rt, d, screen);
    drawCrosshair(rt, d, mouse);
}

void HUD::drawHealth(sf::RenderTarget& rt, const HudData& d, sf::Vector2f screen) const
{
    const sf::Vector2f pos{24.f, screen.y - 78.f}, size{330.f, 54.f};
    panel(rt, pos, size);
    const float ratio = std::clamp(d.health / std::max(1.f, d.maxHealth), 0.f, 1.f);
    const bool low = ratio < 0.3f;
    const float pulse = low ? 0.5f + 0.5f * std::sin(m_time * 9.f) : 0.f;

    sf::RectangleShape back({size.x - 20.f, 18.f});
    back.setPosition({pos.x + 10.f, pos.y + 28.f});
    back.setFillColor(sf::Color(50, 20, 20));
    rt.draw(back);

    sf::Color good(60, 200, 70), mid(230, 190, 40), bad(230, 40, 35);
    sf::Color c = ratio > 0.5f ? ui::lerpColor(mid, good, (ratio - 0.5f) * 2.f) : ui::lerpColor(bad, mid, ratio * 2.f);
    if (low) c = ui::lerpColor(bad, sf::Color(255, 140, 120), pulse);
    sf::RectangleShape fill({(size.x - 20.f) * ratio, 18.f});
    fill.setPosition(back.getPosition());
    fill.setFillColor(c);
    rt.draw(fill);

    // little cross icon
    sf::RectangleShape c1({4.f, 14.f}), c2({14.f, 4.f});
    c1.setOrigin({2.f, 7.f}); c2.setOrigin({7.f, 2.f});
    c1.setPosition({pos.x + 20.f, pos.y + 14.f}); c2.setPosition(c1.getPosition());
    c1.setFillColor(c); c2.setFillColor(c);
    rt.draw(c1); rt.draw(c2);

    ui::drawText(rt, *m_font, "HEALTH", 16, {pos.x + 36.f, pos.y + 14.f}, sf::Color(200, 200, 205));
    ui::drawText(rt, *m_font, std::to_string(static_cast<int>(std::ceil(d.health))) + " / " + std::to_string(static_cast<int>(d.maxHealth)),
                 18, {pos.x + size.x - 12.f, pos.y + 14.f}, sf::Color::White, Align::Right);
    if (low)
        ui::drawText(rt, *m_font, "LOW HEALTH!", 22, {pos.x + size.x * 0.5f, pos.y - 16.f},
                     ui::withAlpha(sf::Color(255, 70, 60), 0.4f + 0.6f * pulse), Align::Center, 2.f);
}

void HUD::drawWeapon(sf::RenderTarget& rt, const HudData& d, sf::Vector2f screen) const
{
    const sf::Vector2f size{330.f, 96.f}, pos{screen.x - size.x - 24.f, screen.y - size.y - 24.f};
    panel(rt, pos, size);

    // icon box
    sf::RectangleShape box({112.f, 64.f});
    box.setPosition({pos.x + 12.f, pos.y + 16.f});
    box.setFillColor(sf::Color(30, 30, 34, 220));
    rt.draw(box);
    if (d.weaponIcon) {
        sf::Sprite spr(*d.weaponIcon);
        const sf::Vector2f ts(d.weaponIcon->getSize());
        const float sc = std::min(100.f / ts.x, 54.f / ts.y);
        spr.setScale({sc, sc});
        spr.setOrigin(ts * 0.5f);
        spr.setPosition({box.getPosition().x + 56.f, box.getPosition().y + 32.f});
        rt.draw(spr);
    } else {   // placeholder silhouette
        sf::RectangleShape body({70.f, 14.f}); body.setOrigin({35.f, 7.f});
        body.setPosition({box.getPosition().x + 56.f, box.getPosition().y + 30.f}); body.setFillColor(sf::Color(170, 170, 175));
        sf::RectangleShape grip({14.f, 24.f}); grip.setPosition({box.getPosition().x + 36.f, box.getPosition().y + 34.f}); grip.setFillColor(sf::Color(120, 120, 125));
        rt.draw(body); rt.draw(grip);
    }

    ui::drawText(rt, *m_font, d.weaponName, 22, {pos.x + 140.f, pos.y + 26.f}, sf::Color::White, Align::Left, 1.f);
    const bool lowAmmo = d.magSize > 0 && d.magAmmo <= std::max(1, d.magSize / 4);
    const sf::Color ammoColor = d.magAmmo == 0 ? sf::Color(255, 70, 60) : (lowAmmo ? sf::Color(255, 190, 60) : sf::Color::White);
    ui::drawText(rt, *m_font, std::to_string(d.magAmmo), 40, {pos.x + 140.f, pos.y + 64.f}, ammoColor, Align::Left, 2.f);
    ui::drawText(rt, *m_font, "/ " + std::to_string(d.reserveAmmo), 24, {pos.x + 215.f, pos.y + 70.f}, sf::Color(170, 170, 178), Align::Left);

    if (d.reloading) {
        sf::RectangleShape bg({size.x - 24.f, 8.f}); bg.setPosition({pos.x + 12.f, pos.y - 14.f}); bg.setFillColor(sf::Color(40, 40, 44, 220)); rt.draw(bg);
        sf::RectangleShape fg({(size.x - 24.f) * std::clamp(d.reloadProgress, 0.f, 1.f), 8.f}); fg.setPosition(bg.getPosition()); fg.setFillColor(sf::Color(255, 200, 60)); rt.draw(fg);
        ui::drawText(rt, *m_font, "RELOADING...", 18, {pos.x + size.x * 0.5f, pos.y - 30.f}, sf::Color(255, 200, 60), Align::Center, 1.5f);
    } else if (d.magAmmo == 0 && d.reserveAmmo > 0) {
        ui::drawText(rt, *m_font, "PRESS R TO RELOAD", 18, {pos.x + size.x * 0.5f, pos.y - 20.f},
                     ui::withAlpha(sf::Color(255, 200, 60), 0.5f + 0.5f * std::sin(m_time * 8.f)), Align::Center, 1.5f);
    } else if (d.magAmmo == 0 && d.reserveAmmo == 0) {
        ui::drawText(rt, *m_font, "OUT OF AMMO", 18, {pos.x + size.x * 0.5f, pos.y - 20.f}, sf::Color(255, 70, 60), Align::Center, 1.5f);
    }
}

void HUD::drawTop(sf::RenderTarget& rt, const HudData& d, sf::Vector2f screen) const
{
    panel(rt, {24.f, 20.f}, {230.f, 66.f});
    ui::drawText(rt, *m_font, "WAVE " + std::to_string(d.wave), 26, {38.f, 40.f}, sf::Color(235, 60, 45), Align::Left, 1.5f);
    ui::drawText(rt, *m_font, "Zombies: " + std::to_string(d.zombiesRemaining), 18, {38.f, 68.f}, sf::Color(210, 210, 215));

    panel(rt, {screen.x * 0.5f - 130.f, 20.f}, {260.f, 56.f});
    ui::drawText(rt, *m_font, "SCORE", 14, {screen.x * 0.5f, 33.f}, sf::Color(170, 170, 178), Align::Center);
    ui::drawText(rt, *m_font, std::to_string(d.score), 28, {screen.x * 0.5f, 58.f}, sf::Color::White, Align::Center, 1.5f);

    ui::drawText(rt, *m_font, "BEST " + std::to_string(std::max(d.highScore, d.score)), 18,
                 {screen.x - 28.f, 32.f}, sf::Color(255, 210, 80), Align::Right, 1.5f);
}

void HUD::drawPowerUps(sf::RenderTarget& rt, const HudData& d) const
{
    float y = 104.f;
    for (const auto& p : d.powerUps) {
        if (p.remaining <= 0.f) continue;
        panel(rt, {24.f, y}, {230.f, 34.f});
        const bool blink = p.remaining < 2.f && std::fmod(m_time, 0.4f) < 0.2f;
        ui::drawText(rt, *m_font, p.name, 16, {34.f, y + 11.f}, blink ? sf::Color(255, 120, 100) : sf::Color::White);
        ui::drawText(rt, *m_font, std::to_string(static_cast<int>(std::ceil(p.remaining))) + "s", 16, {244.f, y + 11.f}, sf::Color(200, 200, 205), Align::Right);
        sf::RectangleShape bar({206.f * std::clamp(p.remaining / std::max(0.1f, p.duration), 0.f, 1.f), 5.f});
        bar.setPosition({34.f, y + 23.f}); bar.setFillColor(p.color);
        rt.draw(bar);
        y += 40.f;
    }
}

void HUD::drawCrosshair(sf::RenderTarget& rt, const HudData& d, sf::Vector2f m) const
{
    const float gap = 7.f + d.crosshairSpread * 16.f, len = 10.f;
    const sf::Color c = d.reloading ? sf::Color(255, 200, 60) : sf::Color(255, 255, 255, 230);
    for (int i = 0; i < 4; ++i) {
        sf::RectangleShape l({len, 2.f});
        l.setOrigin({-gap, 1.f});
        l.setPosition(m);
        l.setRotation(sf::degrees(90.f * static_cast<float>(i)));
        l.setFillColor(c);
        l.setOutlineThickness(1.f); l.setOutlineColor(sf::Color(0, 0, 0, 160));
        rt.draw(l);
    }
    sf::CircleShape dot(1.8f); dot.setOrigin({1.8f, 1.8f}); dot.setPosition(m); dot.setFillColor(sf::Color(255, 60, 50));
    rt.draw(dot);
}
