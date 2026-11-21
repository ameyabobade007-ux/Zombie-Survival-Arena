#pragma once
// ============================================================================
//  UIWidgets  -  Phase 5, Step 5/7   (SFML 3.x)
//  Reusable Button + Slider + text helpers. No image files required.
// ============================================================================
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cstdint>
#include <string>

namespace ui {

enum class Align { Left, Center, Right };

inline sf::Color withAlpha(sf::Color c, float a01) {
    c.a = static_cast<std::uint8_t>(std::clamp(a01, 0.f, 1.f) * 255.f);
    return c;
}
inline sf::Color lerpColor(sf::Color a, sf::Color b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto L = [t](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(x + (y - x) * t); };
    return {L(a.r, b.r), L(a.g, b.g), L(a.b, b.b), L(a.a, b.a)};
}

// Draws one line of text. `pos` is the left / centre / right anchor depending on `align`.
void drawText(sf::RenderTarget& rt, const sf::Font& font, const std::string& str, unsigned size,
              sf::Vector2f pos, sf::Color color, Align align = Align::Left,
              float outline = 0.f, sf::Color outlineColor = sf::Color(0, 0, 0, 220));

// Vertical gradient rectangle (top -> bottom)
void drawGradient(sf::RenderTarget& rt, sf::FloatRect rect, sf::Color top, sf::Color bottom);

// ---------------------------------------------------------------------------
class Button {
public:
    Button(const sf::Font& font, std::string label, sf::Vector2f center, sf::Vector2f size = {320.f, 56.f});

    // returns true on the frame the mouse STARTS hovering (use it to play the hover sound)
    bool update(float dt, sf::Vector2f mouse);
    bool contains(sf::Vector2f p) const;
    void click() { m_press = 1.f; }          // starts the click animation
    void setLabel(const std::string& s) { m_label = s; }
    void setPosition(sf::Vector2f c) { m_center = c; }
    void draw(sf::RenderTarget& rt) const;

private:
    const sf::Font* m_font;
    std::string m_label;
    sf::Vector2f m_center, m_size;
    float m_hover = 0.f;   // 0..1 smoothed
    float m_press = 0.f;   // 1..0 click flash
    bool  m_hovered = false;
};

// ---------------------------------------------------------------------------
class Slider {
public:
    enum class Result { None, Changed, Released };
    Slider(std::string label, sf::Vector2f pos, float width, float value01);

    Result handleEvent(const sf::Event& e, sf::Vector2f mouse);
    void   setValue(float v) { m_value = std::clamp(v, 0.f, 1.f); }
    float  value() const { return m_value; }
    void   draw(sf::RenderTarget& rt, const sf::Font& font, bool dimmed = false) const;

private:
    float valueFromMouse(float mx) const;
    std::string m_label;
    sf::Vector2f m_pos;      // left end of the track
    float m_width, m_value;
    bool  m_drag = false;
};

} // namespace ui
