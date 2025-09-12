// src/main.cpp
// Clickbot - Windows-only version
// Build with Geode SDK (CMake + GEODE_SDK).
//
// Notes:
//  - Windows: no extra link flags needed (user32 is often already linked; otherwise add user32).

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <chrono>
#include <thread>
#include <atomic>
#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// Cocos includes for keyboard listener types
#include <cocos2d.h>

using namespace geode::prelude;

// ----------------- runtime config (edit constants here) -----------------
static std::string CFG_key = "space";     // "click" -> mouse left button
static double      CFG_cps = 10.0;        // CPS in CPS mode (-1 = hold)
static bool        CFG_useHoldRelease = false;
static double      CFG_holdMs = 50.0;     // ms
static double      CFG_releaseMs = 50.0;  // ms
// -----------------------------------------------------------------------

static std::atomic<bool> gRunning(false);
static std::atomic<double> gInterval(0.001);
static std::string gCurrentKey = CFG_key;
static std::atomic<bool> gUseHoldRelease(CFG_useHoldRelease);
static std::atomic<double> gHoldMs(CFG_holdMs);
static std::atomic<double> gReleaseMs(CFG_releaseMs);
static std::atomic<bool> gToggleLock(false);
static std::thread gWorker;
static std::atomic<bool> gRightShiftDown(false);

// ---------------- Windows helpers ----------------
static WORD keyStringToVK(const std::string& s) {
    if (s == "space") return VK_SPACE;
    if (s == "enter") return VK_RETURN;
    if (s == "shift") return VK_SHIFT;
    if (s == "ctrl")  return VK_CONTROL;
    if (s == "alt")   return VK_MENU;
    if (s == "tab")   return VK_TAB;
    if (s == "backspace") return VK_BACK;
    if (s == "esc" || s == "escape") return VK_ESCAPE;
    if (s == "left")  return VK_LEFT;
    if (s == "right") return VK_RIGHT;
    if (s == "up")    return VK_UP;
    if (s == "down")  return VK_DOWN;
    if (s.size() == 1) {
        char c = s[0];
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        SHORT vk = VkKeyScanA(c);
        return (WORD)(vk & 0xFF);
    }
    return VK_SPACE;
}
static void sendKeyDown(WORD vk) {
    INPUT inp{};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wVk = vk;
    SendInput(1, &inp, sizeof(inp));
}
static void sendKeyUp(WORD vk) {
    INPUT inp{};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wVk = vk;
    inp.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &inp, sizeof(inp));
}
static void sendKeyTap(WORD vk) {
    sendKeyDown(vk);
    sendKeyUp(vk);
}
static void sendMouseDown() {
    INPUT inp{};
    inp.type = INPUT_MOUSE;
    inp.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &inp, sizeof(inp));
}
static void sendMouseUp() {
    INPUT inp{};
    inp.type = INPUT_MOUSE;
    inp.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &inp, sizeof(inp));
}
static void sendMouseClick() {
    sendMouseDown();
    sendMouseUp();
}

// ---------------- worker ----------------
static void workerLoop() {
    gCurrentKey = CFG_key;
    gUseHoldRelease = CFG_useHoldRelease;
    gHoldMs = CFG_holdMs;
    gReleaseMs = CFG_releaseMs;
    if (!gUseHoldRelease) {
        if (CFG_cps == -1.0) gInterval = -1.0;
        else if (CFG_cps > 0.0) gInterval = 1.0 / CFG_cps;
        else gInterval = 0.001;
    }

    if (gUseHoldRelease) {
        while (gRunning) {
            if (gCurrentKey == "click" || gCurrentKey == "mouse" || gCurrentKey == "left") {
                sendMouseDown();
                std::this_thread::sleep_for(std::chrono::milliseconds((int)gHoldMs.load()));
                sendMouseUp();
            } else {
                WORD vk = keyStringToVK(gCurrentKey);
                sendKeyDown(vk);
                std::this_thread::sleep_for(std::chrono::milliseconds((int)gHoldMs.load()));
                sendKeyUp(vk);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds((int)gReleaseMs.load()));
        }
    } else {
        if (gInterval < 0) {
            while (gRunning) {
                if (gCurrentKey == "click" || gCurrentKey == "mouse" || gCurrentKey == "left") {
                    sendMouseDown();
                    while (gRunning) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    sendMouseUp();
                } else {
                    WORD vk = keyStringToVK(gCurrentKey);
                    sendKeyDown(vk);
                    while (gRunning) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    sendKeyUp(vk);
                }
            }
        } else {
            using clock = std::chrono::steady_clock;
            auto next = clock::now();
            while (gRunning) {
                auto now = clock::now();
                if (now >= next) {
                    if (gCurrentKey == "click" || gCurrentKey == "mouse" || gCurrentKey == "left") {
                        sendMouseClick();
                    } else {
                        WORD vk = keyStringToVK(gCurrentKey);
                        sendKeyTap(vk);
                    }
                    next += std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(gInterval.load()));
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
            }
        }
    }
}

static void startClicker() {
    if (gRunning) return;
    gCurrentKey = CFG_key;
    gUseHoldRelease = CFG_useHoldRelease;
    gHoldMs = CFG_holdMs;
    gReleaseMs = CFG_releaseMs;
    if (!gUseHoldRelease) {
        if (CFG_cps == -1.0) gInterval = -1.0;
        else if (CFG_cps > 0.0) gInterval = 1.0 / CFG_cps;
        else gInterval = 0.001;
    }
    gRunning = true;
    gWorker = std::thread(workerLoop);
}
static void stopClicker() {
    if (!gRunning) return;
    gRunning = false;
    if (gWorker.joinable()) gWorker.join();
}

// ---------------- status ----------------
static std::string makeStatusText() {
    std::ostringstream ss;
    ss << "Clickbot status:\n";
    ss << (gRunning ? "Running\n" : "Idle\n");
    ss << "Key: " << gCurrentKey << "\n";
    if (gUseHoldRelease) {
        ss << "Mode: Hold/Release\n";
        ss << "Hold: " << (int)gHoldMs.load() << " ms\n";
        ss << "Release: " << (int)gReleaseMs.load() << " ms\n";
    } else {
        ss << "Mode: CPS\n";
        double iv = gInterval.load();
        if (iv < 0) ss << "CPS: Hold until stop\n";
        else {
            double cps = (iv > 0) ? (1.0 / iv) : 0.0;
            ss << "CPS: " << std::fixed << std::setprecision(2) << cps << "\n";
        }
    }
    ss << "\nControls (Menu):\n";
    ss << "\\ : Status popup\nRightShift : Toggle start/stop\n/ + RightShift : Stop\n= / - : + / - CPS\nH : Toggle Hold/Release\n[ / ] : Hold -/+ 10ms\n, / . : Release -/+ 10ms\n";
    return ss.str();
}

// ---------------- hook ----------------
class $modify(MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto listener = cocos2d::EventListenerKeyboard::create();
        listener->onKeyPressed = [this](cocos2d::EventKeyboard::KeyCode key, cocos2d::Event* event) {
            if (key == cocos2d::EventKeyboard::KeyCode::KEY_BACKSLASH) {
                auto text = makeStatusText();
                FLAlertLayer::create("Clickbot", text, "OK")->show();
                return;
            }
            if (key == cocos2d::EventKeyboard::KeyCode::KEY_RSHIFT) {
                gRightShiftDown = true;
                if (!gToggleLock.exchange(true)) {
                    std::thread([](){
                        std::this_thread::sleep_for(std::chrono::milliseconds(150));
                        if (!gRunning) startClicker();
                        else stopClicker();
                        gToggleLock = false;
                    }).detach();
                }
                return;
            }
            if (key == cocos2d::EventKeyboard::KeyCode::KEY_SLASH) {
                if (gRightShiftDown && gRunning) stopClicker();
                return;
            }
            if (key == cocos2d::EventKeyboard::KeyCode::KEY_EQUALS) {
                CFG_cps += 1.0;
            } else if (key == cocos2d::EventKeyboard::KeyCode::KEY_MINUS) {
                CFG_cps = std::max(0.0, CFG_cps - 1.0);
            } else if (key == cocos2d::EventKeyboard::KeyCode::KEY_H) {
                CFG_useHoldRelease = !CFG_useHoldRelease;
                gUseHoldRelease = CFG_useHoldRelease;
            } else if (key == cocos2d::EventKeyboard::KeyCode::KEY_LEFT_BRACKET) {
                CFG_holdMs = std::max(0.0, CFG_holdMs - 10.0);
                gHoldMs = CFG_holdMs;
            } else if (key == cocos2d::EventKeyboard::KeyCode::KEY_RIGHT_BRACKET) {
                CFG_holdMs += 10.0;
                gHoldMs = CFG_holdMs;
            } else if (key == cocos2d::EventKeyboard::KeyCode::KEY_COMMA) {
                CFG_releaseMs = std::max(0.0, CFG_releaseMs - 10.0);
                gReleaseMs = CFG_releaseMs;
            } else if (key == cocos2d::EventKeyboard::KeyCode::KEY_PERIOD) {
                CFG_releaseMs += 10.0;
                gReleaseMs = CFG_releaseMs;
            }
        };
        listener->onKeyReleased = [](cocos2d::EventKeyboard::KeyCode key, cocos2d::Event* event) {
            if (key == cocos2d::EventKeyboard::KeyCode::KEY_RSHIFT) {
                gRightShiftDown = false;
            }
        };
        auto disp = cocos2d::Director::getInstance()->getEventDispatcher();
        if (disp) disp->addEventListenerWithSceneGraphPriority(listener, this);
        return true;
    }
};

$on_unload {
    stopClicker();
}
