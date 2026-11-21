#pragma once
// ============================================================================
//  HUD  -  Phase 5, Step 6   (SFML 3.x)
//  Fill a HudData struct from your game each frame and call hud.draw(...).
// ============================================================================
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

struct PowerUpTimer {
    std::string name;
    float remaining = 0.f;
    float duration  = 1.f;
    sf::Color color = sf::Color(80, 200, 255);
};

struct HudData {
    float health = 100.f, maxHealth = 100.f;
    std::string weaponName = "Pistol";
    const sf::Texture* weaponIcon = nullptr;   // optional: reuse your existing weapon sprite texture
    int  magAmmo = 0, magSize = 0, reserveAmmo = 0;
    bool  reloading = false;
    float reloadProgress = 0.f;                // 0..1
    int  wave = 1, zombiesRemaining = 0;
    long score = 0, highScore = 0;
    std::vector<PowerUpTimer> powerUps;
    float crosshairSpread = 0.f;               // 0..1  (grows while shooting/moving)
};

class HUD {
public:
    explicit HUD(const sf::Font& font) : m_font(&font) {}
    void update(float dt) { m_time += dt; }
    // screen-space (call with the default/UI view active)
    void draw(sf::RenderTarget& rt, const HudData& d, sf::Vector2f screen, sf::Vector2f mouse) const;
    void drawCrosshair(sf::RenderTarget& rt, const HudData& d, sf::Vector2f mouse) const;
private:
    void drawHealth(sf::RenderTarget&, const HudData&, sf::Vector2f screen) const;
    void drawWeapon(sf::RenderTarget&, const HudData&, sf::Vector2f screen) const;
    void drawTop(sf::RenderTarget&, const HudData&, sf::Vector2f screen) const;
    void drawPowerUps(sf::RenderTarget&, const HudData&) const;
    const sf::Font* m_font;
    float m_time = 0.f;
};
