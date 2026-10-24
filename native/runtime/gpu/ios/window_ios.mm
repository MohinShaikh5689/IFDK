// gow3 (iOS): the game window is the app's CAMetalLayer (Gow3GameView.swift), handed over with
// gow3_gpu_set_surface. It replaces shim/window.cpp: there is no SDL video on iOS, because UIKit
// belongs to the React Native shell. The swapchain is a VK_EXT_metal_surface on that layer.
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <utility>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL.h>
#include "common/logging/log.h"
#include "gow3_overlay.h"
#include "gow3core.h"
#include "sdl_window.h"

namespace {
std::mutex g_surface_mutex;
std::condition_variable g_surface_cv;
CAMetalLayer* g_layer; // strong (ARC)
u32 g_width, g_height, g_refresh_hz = 60;
} // namespace

// runtime_pad.c: the on-screen controls' buttons (libScePad bits).
extern "C" uint32_t gow3_touch_buttons(void);

namespace {
// The on-screen buttons as gamepad events for the overlay (save list, settings menu, R3+L2), which
// reads events only. L2/R2 are triggers, so they go as axis motion.
void ReplayTouchButtons() {
    static constexpr std::pair<uint32_t, SDL_GamepadButton> buttons[] = {
        {0x4000, SDL_GAMEPAD_BUTTON_SOUTH},        {0x2000, SDL_GAMEPAD_BUTTON_EAST},
        {0x8000, SDL_GAMEPAD_BUTTON_WEST},         {0x1000, SDL_GAMEPAD_BUTTON_NORTH},
        {0x10, SDL_GAMEPAD_BUTTON_DPAD_UP},        {0x40, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
        {0x80, SDL_GAMEPAD_BUTTON_DPAD_LEFT},      {0x20, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
        {0x400, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {0x800, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
        {0x2, SDL_GAMEPAD_BUTTON_LEFT_STICK},      {0x4, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
        {0x8, SDL_GAMEPAD_BUTTON_START},           {0x80000000u, SDL_GAMEPAD_BUTTON_BACK},
    };
    static constexpr std::pair<uint32_t, SDL_GamepadAxis> triggers[] = {
        {0x100, SDL_GAMEPAD_AXIS_LEFT_TRIGGER}, {0x200, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}};
    static uint32_t last;
    const uint32_t now = gow3_touch_buttons();
    const uint32_t changed = now ^ last;
    last = now;
    if (!changed) {
        return;
    }
    const u64 timestamp = SDL_GetTicksNS();
    // The Menu button: the menu's own shortcut, L2 held while R3 goes down.
    if ((changed & 0x20000000u) && (now & 0x20000000u)) {
        SDL_Event event{};
        event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        event.gaxis.timestamp = timestamp;
        event.gaxis.axis = u8(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        event.gaxis.value = 32767;
        Gow3Overlay::HandleEvent(event);
        SDL_Event press{};
        press.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        press.gbutton.timestamp = timestamp;
        press.gbutton.button = u8(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        press.gbutton.down = true;
        Gow3Overlay::HandleEvent(press);
        press.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
        press.gbutton.down = false;
        Gow3Overlay::HandleEvent(press);
        event.gaxis.value = 0;
        Gow3Overlay::HandleEvent(event);
    }
    for (const auto& [bit, button] : buttons) {
        if (!(changed & bit)) continue;
        SDL_Event event{};
        event.type = (now & bit) ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
        event.gbutton.timestamp = timestamp;
        event.gbutton.button = u8(button);
        event.gbutton.down = (now & bit) != 0;
        Gow3Overlay::HandleEvent(event);
    }
    for (const auto& [bit, axis] : triggers) {
        if (!(changed & bit)) continue;
        SDL_Event event{};
        event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        event.gaxis.timestamp = timestamp;
        event.gaxis.axis = u8(axis);
        event.gaxis.value = (now & bit) ? 32767 : 0;
        Gow3Overlay::HandleEvent(event);
    }
}
} // namespace

extern "C" void gow3_gpu_set_surface(void* metal_layer, uint32_t width, uint32_t height,
                                     uint32_t refresh_hz) {
    std::scoped_lock lock{g_surface_mutex};
    g_layer = (__bridge CAMetalLayer*)metal_layer;
    g_width = width;
    g_height = height;
    if (refresh_hz) g_refresh_hz = refresh_hz;
    g_surface_cv.notify_all();
}

namespace Frontend {

u32 DisplayRefreshHz() {
    std::scoped_lock lock{g_surface_mutex};
    return g_refresh_hz;
}

WindowSDL::WindowSDL(s32 width_, s32 height_, const char* title) : width{width_}, height{height_} {
    base_title = title;
    std::unique_lock lock{g_surface_mutex};
    if (!g_layer) {
        std::printf("Window: waiting for the app's Metal layer (gow3_gpu_set_surface)\n");
        std::fflush(stdout);
        g_surface_cv.wait(lock, [] { return g_layer != nil; });
    }
    window_info.type = WindowSystemType::Metal;
    window_info.render_surface = (__bridge void*)g_layer;
    window_info.render_surface_scale = float(g_layer.contentsScale);
    width = s32(g_width);
    height = s32(g_height);
    LOG_INFO(Frontend, "Window {}x{} on the app's CAMetalLayer, {} Hz", g_width, g_height, g_refresh_hz);
}

WindowSDL::~WindowSDL() = default;

void WindowSDL::ApplyDisplayMode(int mode) {
    display_mode = mode; // always fullscreen on the phone
}

// There is no keyboard path yet: the game's text dialog (save name) is confirmed at once with its
// initial text, which is what pressing Enter in the PC window does.
void WindowSDL::BeginTextInput(const std::string& initial, const std::string& prompt) {
    std::scoped_lock lock{text_mutex};
    text = initial;
    text_prompt = prompt;
    text_state = 1;
    text_requested = false;
    text_active = false;
}

int WindowSDL::PollTextInput(std::string& out) {
    std::scoped_lock lock{text_mutex};
    out = text;
    return text_state;
}

void WindowSDL::UpdateTextTitle() {
    Gow3Overlay::SetTextEntry(text_active, text_prompt, text);
}

bool WindowSDL::PollEvents() {
    {
        // The layer's drawable size follows the view (rotation, split screen): the swapchain
        // reads the new size from GetWidth/GetHeight when it is recreated.
        std::scoped_lock lock{g_surface_mutex};
        width = s32(g_width);
        height = s32(g_height);
    }
    // Gamepads (runtime_pad.c) are SDL's, updated by its event pump as on the PC window thread;
    // the in-game menu (R3+L2) takes their events first. SDL video is never initialized here.
    if (SDL_WasInit(SDL_INIT_GAMEPAD)) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            Gow3Overlay::HandleEvent(event);
        }
    }
    ReplayTouchButtons();
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    return is_open;
}

} // namespace Frontend
