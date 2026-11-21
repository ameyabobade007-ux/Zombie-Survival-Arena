# Integrating Phase 5 into your existing game

I haven't seen your `main.cpp`, so this package is self-contained and does not overwrite anything of yours.
`demo_main.cpp` shows a working example of every hook below. If you send me your main.cpp I can merge it for you.

## 1. Add the files
Copy `include/*.hpp`, `src/*.cpp`, `assets/audio/`, `assets/ui/` into your project (keep your existing assets in place).
Add the 5 `.cpp` files to your build. Link `sfml-audio` (you may not link it yet).
SFML 3 needs C++17.

## 2. Startup (once)
```cpp
#include "AudioManager.hpp"
#include "Effects.hpp"
#include "HUD.hpp"
#include "Screens.hpp"

AudioManager audio("assets/audio");
audio.load();            // missing files are listed on stderr, game still runs
audio.loadSettings();    // remembered volumes
```
Create screens/effects once (they need your `sf::Font` and a fixed logical size such as 1280x720):
```cpp
MainMenuScreen mainMenu(font, audio, {1280.f, 720.f});   // + SettingsScreen, PauseScreen, GameOverScreen, ControlsScreen
HUD hud(font);  ScreenShake shake;  DamageFlash flash;  HitMarker hitMarker;
FloatingText floating(font);  MuzzleFlash muzzle;  WaveBanner banner(font);  NotificationFeed feed(font);  Transition transition;
```

## 3. Every frame
```cpp
audio.update(dt);                       // REQUIRED (music fades + overlap timers)
transition.update(dt);
// only while state == Playing (this is what makes pause freeze everything):
shake.update(dt); flash.update(dt, hp/maxHp); hitMarker.update(dt);
floating.update(dt); muzzle.update(dt); banner.update(dt); feed.update(dt); hud.update(dt);
audio.updateZombieAmbient(dt, zombies.size());   // random growls
```

## 4. Sound hooks - put ONE line at each place in your code
| Where in your game | Add |
|---|---|
| Weapon fires (weapon index 0-3) | `audio.playWeaponShot(i);  muzzle.spawn(muzzlePos, angleDeg);  shake.trigger(3.f, 0.12f);` |
| Reload starts | `audio.play(SoundId::Reload);`   (weapon switch while reloading: `audio.stopSound(SoundId::Reload);`) |
| Bullet hits zombie / obstacle | `audio.play(SoundId::BulletImpact, 0.1f);  floating.spawn(zombiePos, std::to_string(dmg));  hitMarker.trigger(killed);` |
| Zombie attacks player | `audio.play(SoundId::ZombieAttack);` |
| Player takes damage | `audio.play(SoundId::PlayerHurt);  flash.trigger();  shake.trigger(8.f, 0.3f);` |
| Zombie dies | `audio.play(SoundId::ZombieDeath);  feed.push("Zombie killed +100");` |
| Item picked up | `audio.play(SoundId::Pickup);` |
| Power-up activates | `audio.play(SoundId::PowerUp);  feed.push("POWER-UP: Double Damage!");` |
| New wave | `audio.play(SoundId::WaveStart);  banner.show("WAVE " + std::to_string(w));` |
| Player dies | `audio.play(SoundId::PlayerDeath);  audio.playMusic(MusicId::GameOver);` |

## 5. Music per game state
```cpp
audio.playMusic(MusicId::Menu);      // entering main menu
audio.playMusic(MusicId::Gameplay);  // normal wave
audio.playMusic(MusicId::Intense);   // hard wave (e.g. wave % 3 == 0 or many zombies)
audio.playMusic(MusicId::GameOver);  // death
audio.pauseMusic(); / audio.resumeMusic();   // pause menu
```
`playMusic` cross-fades automatically; calling it again with the track already playing does nothing.

## 6. HUD, drawing order
```cpp
window.setView(worldView);   worldView.setCenter(base + shake.offset());
  ... your world ...  muzzle.draw(window);  floating.draw(window);
window.setView(uiView);                         // fixed 1280x720 view
  flash.draw(window, size);  hud.draw(window, hudData, size, mouse);  banner.draw(window, size);
  feed.draw(window, {size.x - 28.f, 64.f});  hitMarker.draw(window, mouse);
  // then pause/game-over screen, then transition.draw(window, size);
```
Fill `HudData` from your player/weapon (health, ammo, wave, score, power-up timers, `weaponIcon` = your existing weapon texture).
Hide the system cursor while playing: `window.setMouseCursorVisible(false)`.

## 7. Pause menu = freeze everything
When state is `Paused`, simply do not run your world update (zombies, bullets, timers, animations, power-ups).
Still call `audio.update(dt)` and `pauseScreen.update(...)`.

## 8. Using your own sounds
Replace files in `assets/audio/` keeping the same names (WAV for effects; music accepts .ogg/.wav/.flac/.mp3).
Good free sources: freesound.org, opengameart.org, kenney.nl (check licences).
Tweak per-sound overlap limits / voices / base volumes in the `kSfx` table at the top of `src/AudioManager.cpp`.
