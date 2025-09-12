#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

using namespace geode::prelude;

// --- Global state variable for the clickbot ---
bool g_clicking = false;

// --- Click thread function ---
// This function runs in a separate thread and simulates key presses
// based on the global state of the clickbot.
DWORD WINAPI clickThread(LPVOID) {
    // Loop indefinitely to check the clickbot state
    while (true) {
        // Retrieve the current settings from the mod
        // These values are automatically loaded and saved by Geode's settings system.
        auto mod = Mod::get();
        int holdTimeMs = mod->getSettingValue<int>("hold-time");
        int releaseTimeMs = mod->getSettingValue<int>("release-time");

        // Only run if the clickbot is enabled
        if (g_clicking) {
            // Press the space key
            keybd_event(VK_SPACE, 0, 0, 0);
            Sleep(holdTimeMs);

            // Release the space key
            keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0);
            Sleep(releaseTimeMs);
        } else {
            // Wait for a short period to avoid high CPU usage
            Sleep(50);
        }
    }
    return 0;
}

// The main class for the mod
class $modify(PlayLayer) {
    // This hook is called every frame
    void update(float dt) {
        PlayLayer::update(dt);

        // Check for a single press of the '\' key to toggle the clickbot
        if (getSingleClick(KEY_OEM_5)) {
            g_clicking = !g_clicking;
            log::info("Clickbot {}", g_clicking ? "started" : "stopped");
        }
    }
};

// This class is used to start the click thread when the mod is enabled
class $modify(MyMod) {
    void onEnable() {
        log::info("Starting click thread...");
        #ifdef GEODE_IS_WINDOWS
        CreateThread(nullptr, 0, clickThread, nullptr, 0, nullptr);
        #endif
    }
};
