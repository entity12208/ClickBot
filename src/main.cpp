// src/main.cpp
// Clickbot - corrected & multi-platform input backend
// Drop into a Geode mod src/ directory and build with the Geode SDK (CMake + GEODE_SDK).
//
// Build notes:
//  - Windows: no extra link flags needed (user32 is often already linked; otherwise add user32).
//  - macOS: add `-framework ApplicationServices` (ApplicationServices.framework) to link flags.
//  - Linux: link with -lX11 -lXtst (X11 + XTest).
//
// Platform limitations:
//  - Android: injecting into other apps requires INJECT_EVENTS / system signing; not implemented here.
//  - iOS: injecting events requires private APIs; not implemented here.

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

#ifdef __linux__
    #include <X11/Xlib.h>
    #include <X11/keysym.h>
    #include <X11/extensions/XTest.h>
#endif

#ifdef __APPLE__
    #include <ApplicationServices/ApplicationServices.h>
#endif

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
#endif

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

// ---------------- platform helpers ----------------
#ifdef _WIN32
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
#endif // _WIN32

#ifdef __APPLE__
static CGKeyCode macKeyForChar(char c) {
    // Very small helper: maps ascii letters/digits to common mac keycodes.
    // For production you likely want a more complete map handling layouts.
    if (c >= 'a' && c <= 'z') {
        // This mapping is NOT guaranteed for all layouts; users should provide mac keycodes for reliability.
        // We'll map a..z to the typical US keycodes for letters by using a simple offset: 'a'->0x00 etc is not universal.
    }
    return (CGKeyCode)0;
}

static void mac_send_key_down(CGKeyCode key) {
    CGEventRef ev = CGEventCreateKeyboardEvent(NULL, key, true);
    if (ev) {
        CGEventPost(kCGHIDEventTap, ev);
        CFRelease(ev);
    }
}
static void mac_send_key_up(CGKeyCode key) {
    CGEventRef ev = CGEventCreateKeyboardEvent(NULL, key, false);
    if (ev) {
        CGEventPost(kCGHIDEventTap, ev);
        CFRelease(ev);
    }
}
static void mac_send_key_tap(CGKeyCode key) {
    mac_send_key_down(key);
    mac_send_key_up(key);
}
static void mac_send_mouse_click() {
    // post left mouse down/up at current position
    CGEventRef d = CGEventCreate(NULL);
    CGPoint p = CGEventGetLocation(d);
    CFRelease(d);
    CGEventRef md = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown, p, kCGMouseButtonLeft);
    CGEventRef mu = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp, p, kCGMouseButtonLeft);
    if (md) { CGEventPost(kCGHIDEventTap, md); CFRelease(md); }
    if (mu) { CGEventPost(kCGHIDEventTap, mu); CFRelease(mu); }
}
#endif // __APPLE__

#ifdef __linux__
static Display* gXDisplay = nullptr;
static void x_open_display() {
    if (!gXDisplay) gXDisplay = XOpenDisplay(NULL);
}
static void x_close_display() {
    if (gXDisplay) { XCloseDisplay(gXDisplay); gXDisplay = nullptr; }
}
static void x_send_key_tap(KeySym ks) {
    x_open_display();
    if (!gXDisplay) return;
    KeyCode code = XKeysymToKeycode(gXDisplay, ks);
    if (!code) return;
    XTestFakeKeyEvent(gXDisplay, code, True, 0);
    XTestFakeKeyEvent(gXDisplay, code, False, 0);
    XFlush(gXDisplay);
}
static void x_send_button_click(int button) {
    x_open_display();
    if (!gXDisplay) return;
    XTestFakeButtonEvent(gXDisplay, button, True, 0);
    XTestFakeButtonEvent(gXDisplay, button, False, 0);
    XFlush(gXDisplay);
}
#endif // __linux__

// Android / iOS: NOTE = injecting system-wide events is restricted.
// Android requires INJECT_EVENTS (system-signed) or root; iOS requires private APIs.
// We provide stubs that log and return; see notes in the about/README for details.
static void android_send_key_tap(int /*key*/) {
    // Not implemented - requires system privileges (INJECT_EVENTS).
    geode::log("Clickbot") << "android_send_key_tap: not implemented (requires INJECT_EVENTS)";
}
static void android_send_mouse_click() {
    geode::log("Clickbot") << "android_send_mouse_click: not implemented (requires privileged APIs)";
}
static void ios_send_key_tap(int /*key*/) {
    geode::log("Clickbot") << "ios_send_key_tap: not implemented (private APIs required)";
}
static void ios_send_mouse_click() {
    geode::log("Clickbot") << "ios_send_mouse_click: not implemented (private APIs required)";
}

// ---------------- worker ----------------
static void workerLoop() {
    // refresh "configuration" from constants (we are self-contained)
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
        // hold/release loop (ms)
        while (gRunning) {
#ifdef _WIN32
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
#elif defined(__APPLE__)
            // mac: we don't attempt char->CGKeyCode mapping here; user should set CFG_key to "click" for mouse,
            // or extend mapping for letter keys.
            if (gCurrentKey == "click" || gCurrentKey == "mouse" || gCurrentKey == "left") {
                mac_send_mouse_click();
            } else {
                // stub: user would need to supply a proper CGKeyCode mapping for reliability
            }
            std::this_thread::sleep_for(std::chrono::milliseconds((int)gHoldMs.load()));
#elif defined(__linux__)
            if (gCurrentKey == "click" || gCurrentKey == "mouse" || gCurrentKey == "left") {
                x_send_button_click(1);
            } else {
                // very basic: attempt to send lower-case ASCII via X11 keysym
                if (!gCurrentKey.empty() && gCurrentKey.size() == 1) {
                    x_send_key_tap((KeySym)XStringToKeysym(gCurrentKey.c_str()));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds((int)gHoldMs.load()));
#else
            // Android / iOS stubs - log and sleep
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif
            // release wait
            std::this_thread::sleep_for(std::chrono::milliseconds((int)gReleaseMs.load()));
        }
    } else {
        // CPS mode (interval) or hold (-1)
        if (gInterval < 0) {
            while (gRunning) {
#ifdef _WIN32
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
#elif defined(__APPLE__)
                // mac stub
                while (gRunning) std::this_thread::sleep_for(std::chrono::milliseconds(50));
#elif defined(__linux__)
                while (gRunning) std::this_thread::sleep_for(std::chrono::milliseconds(50));
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif
            }
        } else {
            using clock = std::chrono::steady_clock;
            auto next = clock::now();
            while (gRunning) {
                auto now = clock::now();
                if (now >= next) {
#ifdef _WIN32
                    if (gCurrentKey == "click" || gCurrentKey == "mouse" || gCurrentKey == "left") {
                        sendMouseClick();
                    } else {
                        WORD vk = keyStringToVK(gCurrentKey);
                        sendKeyTap(vk);
                    }
#elif defined(__APPLE__)
                    if (gCurrentKey == "click" || gCurrentKey == "mouse" || gCurrentKey == "left") {
                        mac_send_mouse_click();
                    } else {
                        // mac key tap not implemented for arbitrary chars in this sample
                    }
#elif defined(__linux__)
                    if (gCurrentKey == "click" || gCurrentKey == "mouse" || gCurrentKey == "left") {
                        x_send_button_click(1);
                    } else {
                        if (!gCurrentKey.empty() && gCurrentKey.size() == 1) {
                            x_send_key_tap((KeySym)XStringToKeysym(gCurrentKey.c_str()));
                        }
                    }
#else
                    // android/ios: no-op
#endif
                    // advance next by the configured interval (cast to clock::duration to avoid MSVC overload issues)
                    next += std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(gInterval.load()));
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
            }
        }
    }
}

// start/stop helpers
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

// Utility: build status text
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

// Hook MenuLayer and register keyboard listener via Director dispatcher
class $modify(MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        // Create a Cocos keyboard listener and add it via Director's event dispatcher
        auto listener = cocos2d::EventListenerKeyboard::create();
        listener->onKeyPressed = [this](cocos2d::EventKeyboard::KeyCode key, cocos2d::Event* event) {
            // backslash -> show popup
            if (key == cocos2d::EventKeyboard::KeyCode::KEY_BACKSLASH) {
                auto text = makeStatusText();
                FLAlertLayer::create("Clickbot", text, "OK")->show();
                return;
            }

            // Right shift toggle (debounced)
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

            // Slash + RightShift => stop
            if (key == cocos2d::EventKeyboard::KeyCode::KEY_SLASH) {
                if (gRightShiftDown && gRunning) stopClicker();
                return;
            }

            // adjust settings at runtime (CPS +/-)
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

        // Use Director's dispatcher to ensure the listener is attached
        auto disp = cocos2d::Director::getInstance()->getEventDispatcher();
        if (disp) {
            disp->addEventListenerWithSceneGraphPriority(listener, this);
        }

        return true;
    }
};

$on_unload {
    stopClicker();
#if defined(__linux__)
    x_close_display(); // cleanup X display
#endif
}
