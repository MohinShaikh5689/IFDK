// gow3 (iOS): the game window is the app's CAMetalLayer (Gow3GameView.swift), handed over with
// gow3_gpu_set_surface. It replaces shim/window.cpp: there is no SDL video on iOS, because UIKit
// belongs to the React Native shell. The swapchain is a VK_EXT_metal_surface on that layer.
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#import <QuartzCore/CAMetalLayer.h>
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
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    return is_open;
}

} // namespace Frontend
