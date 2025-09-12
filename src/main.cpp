#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <windows.h>

using namespace geode::prelude;

// --- Default values ---
int g_holdTimeMs = 100;
int g_releaseTimeMs = 100;

// --- Global state ---
bool g_clicking = false;

// Click thread
DWORD WINAPI clickThread(LPVOID) {
    while (true) {
        if (g_clicking) {
            // Press space
            keybd_event(VK_SPACE, 0, 0, 0);
            Sleep(g_holdTimeMs);

            // Release space
            keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0);
            Sleep(g_releaseTimeMs);
        } else {
            Sleep(50); // idle
        }
    }
    return 0;
}

// Popup class for editing times
class TimeEditPopup : public geode::Popup<> {
protected:
    InputNode* m_holdInput;
    InputNode* m_releaseInput;

    bool setup() override {
        setTitle("Clickbot Settings");

        m_holdInput = InputNode::create("Hold Time (ms)", "0123456789");
        m_releaseInput = InputNode::create("Release Time (ms)", "0123456789");

        m_holdInput->setString(std::to_string(g_holdTimeMs));
        m_releaseInput->setString(std::to_string(g_releaseTimeMs));

        m_mainLayer->addChildAtPosition(m_holdInput, Anchor::Center, ccp(0, 30));
        m_mainLayer->addChildAtPosition(m_releaseInput, Anchor::Center, ccp(0, -10));

        auto okBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("OK", 50, true, "bigFont.fnt", "GJ_button_01.png", 0, 0.7f),
            this, menu_selector(TimeEditPopup::onOK)
        );
        m_buttonMenu->addChild(okBtn);

        return true;
    }

    void onOK(CCObject*) {
        g_holdTimeMs = std::stoi(m_holdInput->getString());
        g_releaseTimeMs = std::stoi(m_releaseInput->getString());
        log::info("Updated times: hold={}ms release={}ms", g_holdTimeMs, g_releaseTimeMs);
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

// PlayLayer hook
class $modify(PlayLayer) {
    bool init(GJGameLevel* level) {
        if (!PlayLayer::init(level)) return false;

        // Start click thread
        CreateThread(nullptr, 0, clickThread, nullptr, 0, nullptr);
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        // Toggle clickbot with "\"
        if (GetAsyncKeyState(VK_OEM_5) & 1) {
            g_clicking = !g_clicking;
            log::info("Clickbot {}", g_clicking ? "started" : "stopped");
        }

        // Shift + "\" opens popup
        if ((GetAsyncKeyState(VK_OEM_5) & 1) && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
            TimeEditPopup::create()->show();
        }
    }
};
