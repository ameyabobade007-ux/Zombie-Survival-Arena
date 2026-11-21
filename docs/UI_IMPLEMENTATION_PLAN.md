
# ZOMBIE SURVIVAL ARENA
# PHASE 5 - UI IMPLEMENTATION PLAN
# SFML 3.1 - C++ 2D GAME

==================================================
1. PROJECT OBJECTIVE
==================================================

Improve the user interface and gameplay presentation
of Zombie Survival Arena.

The UI system must work with the existing Phase 4
gameplay systems.

IMPORTANT:
- Do not replace existing player sprites.
- Do not replace zombie sprites.
- Do not remove existing weapons.
- Do not remove existing pickups.
- Do not change existing collision detection.
- Do not break zombie AI.
- Do not break player animations.
- Do not remove the existing map or environment.
- Reuse existing game data wherever possible.

==================================================
2. MAIN MENU
==================================================

Create a polished main menu.

Required elements:

1. Game title:
   ZOMBIE SURVIVAL ARENA

2. Play button
3. Settings button
4. Controls button
5. Exit button

Visual improvements:

- Dark zombie-apocalypse background.
- Clear readable game title.
- Attractive button design.
- Button hover animation.
- Button click animation.
- Smooth menu transitions.
- Menu background music.
- Menu click sound.
- Menu hover sound.

Button functionality:

PLAY:
Start the game.

SETTINGS:
Open audio and game settings.

CONTROLS:
Display game controls.

EXIT:
Close the application safely.

==================================================
3. IN-GAME HUD
==================================================

Create a clean and readable gameplay HUD.

Required elements:

PLAYER HEALTH:
- Health bar.
- Current health value.
- Maximum health value.
- Low-health warning.
- Red warning effect when health is critical.

WEAPON DISPLAY:
- Current weapon name.
- Weapon icon if available.
- Current magazine ammunition.
- Reserve ammunition.
- Reloading indicator.

GAME INFORMATION:
- Current wave number.
- Remaining zombie count.
- Total zombies killed.
- Current score.

POWER-UPS:
- Active power-up name.
- Remaining duration.
- Visual timer indicator.

CROSSHAIR:
- Clear aiming indicator.
- Hit confirmation.
- Visual feedback when shooting.

IMPORTANT:
All HUD information must use the actual values
from the existing player and gameplay systems.

Do not create duplicate health, ammo, score or
wave variables.

==================================================
4. PAUSE MENU
==================================================

Create a pause menu.

Required buttons:

1. Resume
2. Restart
3. Settings
4. Main Menu
5. Exit

PAUSE FUNCTIONALITY:

When the player presses ESC:

- Pause the game.
- Display the pause menu.
- Stop zombie movement.
- Stop player movement.
- Stop bullet movement.
- Stop gameplay timers.
- Stop power-up timers.
- Stop gameplay animation updates.

RESUME:
Continue the game from the paused state.

RESTART:
Restart the current game.

SETTINGS:
Open settings.

MAIN MENU:
Return to the main menu.

EXIT:
Close the application.

IMPORTANT:
Rendering can continue while paused, but gameplay
updates must remain frozen.

==================================================
5. AUDIO SETTINGS MENU
==================================================

Create an audio settings screen.

Required settings:

MASTER VOLUME:
Default: 80

MUSIC VOLUME:
Default: 60

SOUND EFFECTS VOLUME:
Default: 85

MUTE:
Default: OFF

Requirements:

- Master volume slider.
- Music volume slider.
- SFX volume slider.
- Mute toggle.
- Back button.

Connect the controls to AudioManager.

Use AudioConfig.hpp to load and save settings.

Configuration file:

config/audio.ini

Format:

master=80
music=60
sfx=85
muted=0

Settings must be saved when changed or when
the user leaves the settings screen.

==================================================
6. GAME OVER SCREEN
==================================================

Create a game-over screen.

Required information:

- GAME OVER title.
- Final score.
- Highest score.
- Waves survived.
- Total zombies killed.

Required buttons:

1. Restart
2. Main Menu
3. Exit

Visual improvements:

- Dark background overlay.
- Clear statistics.
- Smooth transition.
- Game-over music.
- Menu click sounds.

Use the existing score, wave and kill statistics
from the game.

==================================================
7. GAMEPLAY VISUAL EFFECTS
==================================================

Implement the following effects.

SCREEN SHAKE:
- Trigger when the player receives heavy damage.
- Trigger when powerful weapons fire.
- Keep the effect brief and controlled.

DAMAGE FLASH:
- Display a short red overlay when the player
  receives damage.

LOW HEALTH WARNING:
- Show a warning when health becomes critical.
- Use a subtle pulsing effect.

MUZZLE FLASH:
- Show a brief firing flash when weapons shoot.

HIT MARKER:
- Display confirmation when a bullet hits a zombie.

FLOATING DAMAGE NUMBERS:
- Display damage dealt to zombies.
- Move numbers upward.
- Fade them out smoothly.

RELOAD FEEDBACK:
- Display a reload indicator.
- Show appropriate animation feedback.

WAVE ANNOUNCEMENT:
- Display a notification when a new wave starts.

KILL NOTIFICATION:
- Display a short notification when a zombie dies.

POWER-UP NOTIFICATION:
- Display the activated power-up name.

SMOOTH TRANSITIONS:
- Fade between menus and game states.

==================================================
8. UI ANIMATION REQUIREMENTS
==================================================

All UI animations should be smooth.

Required animations:

- Button hover scaling.
- Button color transition.
- Button click feedback.
- Menu fade-in.
- Menu fade-out.
- HUD health changes.
- Low-health pulsing.
- Wave announcement animation.
- Floating damage number animation.
- Notification fade-out.

Avoid excessive animations that reduce gameplay
performance or make the interface difficult to read.

==================================================
9. GAME STATES
==================================================

Maintain clear game states.

Required states:

MAIN_MENU
PLAYING
PAUSED
SETTINGS
CONTROLS
GAME_OVER

Game state transitions must be handled safely.

Example:

MAIN_MENU -> PLAYING
PLAYING -> PAUSED
PAUSED -> PLAYING
PAUSED -> SETTINGS
SETTINGS -> PAUSED
PLAYING -> GAME_OVER
GAME_OVER -> PLAYING
GAME_OVER -> MAIN_MENU

Do not update gameplay systems when the game is
paused, in the main menu, or on the game-over screen.

==================================================
10. INPUT REQUIREMENTS
==================================================

Keyboard support:

ESC:
Pause or resume the game.

ENTER:
Activate the selected menu button.

UP ARROW:
Move menu selection upward.

DOWN ARROW:
Move menu selection downward.

LEFT / RIGHT:
Adjust selected settings slider.

Use the existing game input system wherever possible.

Do not break existing movement and shooting controls.

==================================================
11. PERFORMANCE REQUIREMENTS
==================================================

- Avoid unnecessary object creation every frame.
- Avoid expensive UI calculations.
- Reuse existing textures and fonts.
- Avoid loading assets repeatedly.
- Use delta time for animations.
- Keep gameplay performance smooth.
- Avoid memory leaks.
- Keep UI code modular.

==================================================
12. RECOMMENDED SOURCE ORGANISATION
==================================================

Existing project:

include/
    AudioManager.hpp
    AudioConfig.hpp

src/
    AudioManager.cpp

Recommended future UI files:

include/
    MainMenu.hpp
    HUD.hpp
    PauseMenu.hpp
    SettingsMenu.hpp
    GameOverScreen.hpp
    UIEffects.hpp

src/
    MainMenu.cpp
    HUD.cpp
    PauseMenu.cpp
    SettingsMenu.cpp
    GameOverScreen.cpp
    UIEffects.cpp

Do not create duplicate files if equivalent
systems already exist in the project.

==================================================
13. TESTING CHECKLIST
==================================================

MAIN MENU:
[ ] Play button works.
[ ] Settings button works.
[ ] Controls button works.
[ ] Exit button works.
[ ] Hover effects work.
[ ] Click effects work.

HUD:
[ ] Health displays correctly.
[ ] Ammo displays correctly.
[ ] Weapon name displays correctly.
[ ] Wave displays correctly.
[ ] Zombie count displays correctly.
[ ] Score displays correctly.
[ ] Power-up timers work.

PAUSE MENU:
[ ] ESC pauses the game.
[ ] Resume works.
[ ] Restart works.
[ ] Settings works.
[ ] Main Menu works.
[ ] Gameplay completely freezes.

AUDIO SETTINGS:
[ ] Master volume works.
[ ] Music volume works.
[ ] SFX volume works.
[ ] Mute works.
[ ] Settings save correctly.
[ ] Settings load correctly.

GAME OVER:
[ ] Game-over screen appears.
[ ] Final score displays.
[ ] Highest score displays.
[ ] Waves survived displays.
[ ] Zombies killed displays.
[ ] Restart works.
[ ] Main Menu works.

VISUAL EFFECTS:
[ ] Screen shake works.
[ ] Damage flash works.
[ ] Low-health warning works.
[ ] Muzzle flash works.
[ ] Hit marker works.
[ ] Floating damage numbers work.
[ ] Wave announcement works.
[ ] Kill notification works.

==================================================
14. PHASE 5 COMPLETION REQUIREMENTS
==================================================

Phase 5 is complete when:

1. Audio system is integrated.
2. All weapon sounds work.
3. Zombie and player sounds work.
4. Background music changes correctly.
5. Audio settings work.
6. Main menu is polished.
7. HUD is polished.
8. Pause menu works correctly.
9. Game-over screen works correctly.
10. Gameplay visual effects work.
11. All systems are tested.
12. Existing Phase 4 gameplay remains functional.

==================================================
15. IMPORTANT DEVELOPMENT RULES
==================================================

- Use C++.
- Use SFML 3.1.
- Preserve the existing project architecture.
- Preserve existing gameplay mechanics.
- Do not replace existing sprites unnecessarily.
- Do not remove existing assets.
- Do not rewrite main.cpp without reviewing it.
- Integrate features incrementally.
- Test each feature before moving forward.
- Keep code modular and reusable.

FINAL OBJECTIVE:

Deliver a polished 2D Zombie Survival Arena with
professional audio, attractive menus, readable HUD,
responsive controls, smooth transitions and
satisfying gameplay feedback.
