#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/InputNode.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/MenuLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

using namespace geode::prelude;

// --- Global state variables for the clickbot ---
int g_holdTimeMs = 100;
int g_releaseTimeMs = 100;
bool g_clicking = false;

// --- Click thread function ---
// This function runs in a separate thread and simulates key presses
// based on the global state of the clickbot.
DWORD WINAPI clickThread(LPVOID) {
    // Loop indefinitely to check the clickbot state
    while (true) {
        // Only run if the clickbot is enabled
        if (g_clicking) {
            // Press the space key
            keybd_event(VK_SPACE, 0, 0, 0);
            Sleep(g_holdTimeMs);

            // Release the space key
            keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0);
            Sleep(g_releaseTimeMs);
        } else {
            // Wait for a short period to avoid high CPU usage
            Sleep(50);
        }
    }
    return 0;
}

// --- Popup class for editing times ---
// This class creates a simple popup for the user to edit the
// hold and release times of the clickbot.
class TimeEditPopup : public geode::Popup<> {
protected:
    InputNode* m_holdInput;
    InputNode* m_releaseInput;

    bool setup() override {
        setTitle("Clickbot Settings");

        // Create input fields for hold and release times
        m_holdInput = InputNode::create("Hold Time (ms)", "0123456789");
        m_releaseInput = InputNode::create("Release Time (ms)", "0123456789");

        // Set the default string values for the input nodes
        m_holdInput->setString(std::to_string(g_holdTimeMs));
        m_releaseInput->setString(std::to_string(g_releaseTimeMs));

        // Add input nodes to the popup's main layer
        m_mainLayer->addChildAtPosition(m_holdInput, Anchor::Center, ccp(0, 30));
        m_mainLayer->addChildAtPosition(m_releaseInput, Anchor::Center, ccp(0, -10));

        // Create the OK button
        auto okBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("OK", 50, true, "bigFont.fnt", "GJ_button_01.png", 0, 0.7f),
            this, menu_selector(TimeEditPopup::onOK)
        );
        m_buttonMenu->addChild(okBtn);

        return true;
    }

    // Callback for the OK button
    void onOK(CCObject*) {
        try {
            // Safely convert the input strings to integers
            g_holdTimeMs = std::stoi(m_holdInput->getString());
            g_releaseTimeMs = std::stoi(m_releaseInput->getString());
            
            // Log the updated values for debugging
            log::info("Updated times: hold={}ms release={}ms", g_holdTimeMs, g_releaseTimeMs);
        } catch (const std::exception& e) {
            // Handle invalid input
            log::error("Invalid input: {}", e.what());
            // Optionally show an error message to the user
            geode::createQuickPopup(
                "Error",
                "Invalid input detected. Please enter a number.",
                "OK",
                nullptr,
                nullptr,
                false
            );
        }
        this->onClose(nullptr);
    }

public:
    static TimeEditPopup* create() {
        auto ret = new TimeEditPopup();
        if (ret && ret->initAnchored(200.f, 150.f)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// --- Mod class ---
// This class handles the main logic of the mod, including the keybinds
// and starting the click thread.
class $modify(PlayLayer) {
    // This hook is called every frame
    void update(float dt) {
        PlayLayer::update(dt);

        // Get key states for keybinds
        bool backslashPressed = getKeyState(KEY_OEM_5);
        bool shiftPressed = getKeyState(KEY_SHIFT);

        // Check for keypress events
        if (getSingleClick(KEY_OEM_5)) { // Check for a single press of the '\' key
            if (shiftPressed) {
                // Shift + '\' opens the popup
                TimeEditPopup::create()->show();
            } else {
                // '\' by itself toggles the clickbot
                g_clicking = !g_clicking;
                log::info("Clickbot {}", g_clicking ? "started" : "stopped");
            }
        }
    }
};

// The main class for the mod, where the click thread is created
class $modify(MyMod) {
    // This function is called when the mod is loaded and enabled
    void onEnable() {
        log::info("Starting click thread...");
        #ifdef GEODE_IS_WINDOWS
        CreateThread(nullptr, 0, clickThread, nullptr, 0, nullptr);
        #endif
    }
};
