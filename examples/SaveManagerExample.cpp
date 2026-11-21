#include "systems/SaveManager.h"
#include <iostream>

int main()
{
    zsa::SaveManager saveManager("saves");

    zsa::PlayerData playerData;
    zsa::GameSettings settings;
    std::string error;

    // Load existing data. If this is the first launch, defaults remain active.
    if (!saveManager.loadPlayerData(playerData, &error))
        std::cout << "Player data: " << error << '\n';

    if (!saveManager.loadSettings(settings, &error))
        std::cout << "Settings: " << error << '\n';

    // Example: unlock the pistol and save the player's progress.
    playerData.weaponUnlocked[0] = 1;
    if (!saveManager.savePlayerData(playerData, &error))
        std::cerr << "Save failed: " << error << '\n';

    // Example: settings changed in your Settings screen.
    settings.musicVolume = 60.0f;
    settings.sfxVolume = 75.0f;
    settings.difficulty = 1; // Normal
    settings.fullscreen = false;
    if (!saveManager.saveSettings(settings, &error))
        std::cerr << "Settings save failed: " << error << '\n';

    // Example at Game Over:
    zsa::SaveManager::recordCompletedRun(playerData, 12500, 6, 84, 35, 420);
    saveManager.savePlayerData(playerData, &error);

    return 0;
}
