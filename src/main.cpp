#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/FLAlertLayer.hpp>

using namespace geode::prelude;

static int g_holdTimeMs = 100;
static int g_releaseTimeMs = 100;
static bool g_clicking = false;
static bool g_popupOpen = false;
static float g_timer = 0.f;
static bool g_pressed = false;

class TimeEditPopup : public FLAlertLayer, public TextInputDelegate {
protected:
    TextInput* m_holdInput = nullptr;
    TextInput* m_releaseInput = nullptr;

public:
    static TimeEditPopup* create() {
        auto ret = new TimeEditPopup();
        if (ret && ret->init(nullptr, "Clickbot Settings", "OK", "Cancel", 300.f, true, 300.f, 200.f)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void setup() override {
        auto winSize = CCDirector::sharedDirector()->getWinSize();

        m_holdInput = TextInput::create(200.f, "Hold ms", "bigFont.fnt");
        m_holdInput->setString(std::to_string(g_holdTimeMs));
        m_holdInput->setPosition({ winSize.width / 2, winSize.height / 2 + 30 });
        this->addChild(m_holdInput);

        m_releaseInput = TextInput::create(200.f, "Release ms", "bigFont.fnt");
        m_releaseInput->setString(std::to_string(g_releaseTimeMs));
        m_releaseInput->setPosition({ winSize.width / 2, winSize.height / 2 - 10 });
        this->addChild(m_releaseInput);
    }

    void FLAlert_Clicked(FLAlertLayer* layer, bool btn2) override {
        if (!btn2) {
            try {
                g_holdTimeMs = std::stoi(m_holdInput->getString());
                g_releaseTimeMs = std::stoi(m_releaseInput->getString());
                log::info("Clickbot times updated: hold={}ms, release={}ms", g_holdTimeMs, g_releaseTimeMs);
            } catch (...) {
                log::error("Invalid input");
            }
        }
        g_popupOpen = false;
        this->removeFromParentAndCleanup(true);
    }
};

class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        g_timer = 0.f;
        g_pressed = false;
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        if (!m_player1) return;

        if (g_clicking) {
            g_timer += dt * 1000.f; // ms

            if (!g_pressed && g_timer >= g_releaseTimeMs) {
                // press jump
                this->pushButton(0, true); // 0 = jump button id
                g_pressed = true;
                g_timer = 0.f;
            } else if (g_pressed && g_timer >= g_holdTimeMs) {
                // release jump
                this->releaseButton(0, true);
                g_pressed = false;
                g_timer = 0.f;
            }
        }
    }
};

class $modify(MyDispatcher, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool idk) {
        // Toggle clicking with \
        if (key == enumKeyCodes::KEY_OEM_5 && down) {
            g_clicking = !g_clicking;
            log::info("Clickbot {}", g_clicking ? "started" : "stopped");
        }

        // Shift + \ opens popup
        if (key == enumKeyCodes::KEY_OEM_5 && down && (GetKeyState(KEY_Shift) & 0x8000)) {
            if (!g_popupOpen) {
                g_popupOpen = true;
                TimeEditPopup::create()->show();
            }
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, idk);
    }
};
