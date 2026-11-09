// Windowed host for the vgx GL back end (Windows build): an SDL2 window with a
// GLES 3 context (ANGLE on Direct3D 11), the display copy blitted into it, and
// keyboard / mouse / game controller turned into a Wii Remote + Nunchuk.
//
// Everything here runs on the render thread: SDL's window, its GL context and
// its event queue all belong to the thread that created the window.
//
// Controls (keyboard + mouse)            Controls (game controller)
//   WASD          Nunchuk stick             left stick     Nunchuk stick
//   Space         A (jump)                  A / Cross      A (jump)
//   Mouse left    B (shoot Star Bits)       RB / R1        B (shoot Star Bits)
//   Mouse move    pointer                   right stick    pointer
//   E, mouse right shake (spin)             X / Square     shake (spin)
//   Shift         Z (crouch)                LT / L2        Z (crouch)
//   Q             C (camera)                LB / L1        C (camera)
//   Arrows        D-pad                     D-pad          D-pad
//   1 / 2         1 / 2                     Y / Triangle   1,  B / Circle  2
//   Enter         +   Backspace  -          Start +, Back -
//   Escape        Home   F11  fullscreen
#define SDL_MAIN_HANDLED
#include "vgx_cmd.h"

#include <SDL2/SDL.h>
#include <GLES3/gl3.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "port/input.h"
#include "port/pc_settings.h"

namespace vgx::host {

namespace {
SDL_Window* g_window = nullptr;
SDL_GLContext g_context = nullptr;
SDL_GameController* g_pad = nullptr;
bool g_flipY = true;  // (ANGLE / D3D11: the EFB comes out bottom-up relative to the Vita host)
float g_aspect = 16.0f / 9.0f;  // 0: stretch to the window
bool g_showFps = true;          // FPS counter in the top-left corner (F3)
// presented picture inside the window (drawable pixels), for the mouse pointer
int g_viewX = 0, g_viewY = 0, g_viewW = 1, g_viewH = 1;

// Wii buttons (revolution/wpad.h)
constexpr uint32_t kLeft = 0x0001, kRight = 0x0002, kDown = 0x0004, kUp = 0x0008, kPlus = 0x0010, kTwo = 0x0100,
                   kOne = 0x0200, kB = 0x0400, kA = 0x0800, kMinus = 0x1000, kZ = 0x2000, kC = 0x4000, kHome = 0x8000;

float g_cursorX = 0.0f, g_cursorY = 0.0f;  // -1..1, pointer
bool g_mouseUsed = false;
int g_shakeFrames = 0;
bool g_shakeHeld = false;

float Axis(Sint16 v) {
    float f = v / 32767.0f;
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    const float dead = 0.18f;
    if (std::fabs(f) < dead) return 0.0f;
    return (f - std::copysign(dead, f)) / (1.0f - dead);
}

void OpenFirstPad() {
    if (g_pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i) && (g_pad = SDL_GameControllerOpen(i))) {
            std::fprintf(stderr, "[vgx-sdl] controller: %s\n", SDL_GameControllerName(g_pad));
            return;
        }
    }
}

void ToggleFullscreen() {
    const Uint32 flags = SDL_GetWindowFlags(g_window);
    SDL_SetWindowFullscreen(g_window, (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void UpdatePad() {
    PortPadState pad{};
    pad.connected = 1;
    pad.pointerDist = 1.0f;
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    const Uint32 mouse = SDL_GetMouseState(nullptr, nullptr);
    uint32_t b = 0;
    if (k[SDL_SCANCODE_SPACE]) b |= kA;
    if (mouse & SDL_BUTTON(SDL_BUTTON_LEFT)) b |= kB;
    if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) b |= kZ;
    if (k[SDL_SCANCODE_Q]) b |= kC;
    if (k[SDL_SCANCODE_LEFT]) b |= kLeft;
    if (k[SDL_SCANCODE_RIGHT]) b |= kRight;
    if (k[SDL_SCANCODE_UP]) b |= kUp;
    if (k[SDL_SCANCODE_DOWN]) b |= kDown;
    if (k[SDL_SCANCODE_1]) b |= kOne;
    if (k[SDL_SCANCODE_2]) b |= kTwo;
    if (k[SDL_SCANCODE_RETURN]) b |= kPlus;
    if (k[SDL_SCANCODE_BACKSPACE]) b |= kMinus;
    if (k[SDL_SCANCODE_ESCAPE]) b |= kHome;
    float sx = (k[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    float sy = (k[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    bool shake = k[SDL_SCANCODE_E] || (mouse & SDL_BUTTON(SDL_BUTTON_RIGHT));

    if (g_pad) {
        auto btn = [](SDL_GameControllerButton x) { return SDL_GameControllerGetButton(g_pad, x) != 0; };
        if (btn(SDL_CONTROLLER_BUTTON_A)) b |= kA;
        if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= kB;
        if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) b |= kC;
        if (btn(SDL_CONTROLLER_BUTTON_Y)) b |= kOne;
        if (btn(SDL_CONTROLLER_BUTTON_B)) b |= kTwo;
        if (btn(SDL_CONTROLLER_BUTTON_START)) b |= kPlus;
        if (btn(SDL_CONTROLLER_BUTTON_BACK)) b |= kMinus;
        if (btn(SDL_CONTROLLER_BUTTON_GUIDE)) b |= kHome;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) b |= kLeft;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= kRight;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)) b |= kUp;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) b |= kDown;
        if (SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000) b |= kZ;
        if (SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000) b |= kB;
        if (btn(SDL_CONTROLLER_BUTTON_X)) shake = true;
        const float lx = Axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX));
        const float ly = -Axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY));
        if (lx != 0.0f || ly != 0.0f) { sx = lx; sy = ly; }
        const float rx = Axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTX));
        const float ry = Axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTY));
        if (rx != 0.0f || ry != 0.0f) {
            g_mouseUsed = false;
            g_cursorX = std::fmax(-1.0f, std::fmin(1.0f, g_cursorX + rx * 0.04f));
            g_cursorY = std::fmax(-1.0f, std::fmin(1.0f, g_cursorY + ry * 0.04f));
        }
    }
    const float len = std::sqrt(sx * sx + sy * sy);
    if (len > 1.0f) { sx /= len; sy /= len; }
    pad.stickX = sx;
    pad.stickY = sy;
    pad.buttons = b;

    // A shake is a short burst of large accelerations, started on the press.
    if (shake && !g_shakeHeld) g_shakeFrames = 10;
    g_shakeHeld = shake;
    if (g_shakeFrames > 0) {
        const float s = (g_shakeFrames & 1) ? 2.8f : -2.8f;
        pad.accX = s;
        pad.accY = s * 0.6f;
        --g_shakeFrames;
    }

    pad.pointerValid = 1;
    pad.pointerX = g_cursorX;
    pad.pointerY = g_cursorY;
    port_input_set(0, &pad);
}

void Pump() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            std::fflush(nullptr);
            std::_Exit(0);  // (the port has no coordinated shutdown: as the Linux harness, leave without destructors)
        case SDL_CONTROLLERDEVICEADDED: OpenFirstPad(); break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (g_pad && !SDL_GameControllerGetAttached(g_pad)) { SDL_GameControllerClose(g_pad); g_pad = nullptr; OpenFirstPad(); }
            break;
        case SDL_KEYDOWN:
            if (e.key.keysym.scancode == SDL_SCANCODE_F3) port_pc_set_fps_counter(!port_pc_fps_counter());
            if (e.key.keysym.scancode == SDL_SCANCODE_F11 ||
                (e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)))
                ToggleFullscreen();
            break;
        case SDL_MOUSEMOTION: {
            int ww = 1, wh = 1, dw = 1, dh = 1;
            SDL_GetWindowSize(g_window, &ww, &wh);
            SDL_GL_GetDrawableSize(g_window, &dw, &dh);
            const float px = e.motion.x * (float)dw / ww, py = e.motion.y * (float)dh / wh;
            g_cursorX = std::fmax(-1.0f, std::fmin(1.0f, (px - g_viewX) / g_viewW * 2.0f - 1.0f));
            g_cursorY = std::fmax(-1.0f, std::fmin(1.0f, (py - g_viewY) / g_viewH * 2.0f - 1.0f));
            g_mouseUsed = true;
            break;
        }
        default: break;
        }
    }
    UpdatePad();
}

// FPS counter (top-left corner, F3 toggles, PETARI_FPS=0 starts it hidden): pictures shown per second,
// drawn as 3x5-cell digits with scissored clears, so it needs no shader, texture or font.
int g_fpsShown = 0;
unsigned g_fpsFrames = 0;
Uint64 g_fpsSince = 0;

void FillRect(int x, int y, int w, int h, int dh, float r, float g, float b) {
    glScissor(x, dh - y - h, w, h);  // (y from the window's top)
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void DrawFps(int dw, int dh) {
    ++g_fpsFrames;
    const Uint64 now = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    if (!g_fpsSince) g_fpsSince = now;
    if (now - g_fpsSince >= freq / 2) {
        g_fpsShown = (int)std::lround(g_fpsFrames * (double)freq / (double)(now - g_fpsSince));
        g_fpsFrames = 0;
        g_fpsSince = now;
    }
    static const char* const kGlyphs[10] = {"111101101101111", "010110010010111", "111001111100111", "111001111001111",
                                            "101101111001001", "111100111001111", "111100111101111", "111001001001001",
                                            "111101111101111", "111101111001111"};
    char text[8];
    const int len = std::snprintf(text, sizeof(text), "%d", g_fpsShown < 0 ? 0 : (g_fpsShown > 999 ? 999 : g_fpsShown));
    const int cell = dh >= 1000 ? 5 : (dh >= 600 ? 4 : 3);
    const int pad = cell * 2, x0 = 8, y0 = 8;
    const int boxW = pad * 2 + len * cell * 4 - cell, boxH = pad * 2 + cell * 5;
    // colour: green at 55+, yellow at 30+, red below
    const float r = g_fpsShown >= 55 ? 0.3f : 1.0f, g = g_fpsShown >= 30 ? 1.0f : 0.3f, b = 0.3f;
    glEnable(GL_SCISSOR_TEST);
    FillRect(x0, y0, boxW, boxH, dh, 0.0f, 0.0f, 0.0f);
    for (int i = 0; i < len; ++i) {
        const char* glyph = kGlyphs[text[i] - '0'];
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 3; ++col)
                if (glyph[row * 3 + col] == '1')
                    FillRect(x0 + pad + i * cell * 4 + col * cell, y0 + pad + row * cell, cell, cell, dh, r, g, b);
    }
    glDisable(GL_SCISSOR_TEST);
    (void)dw;
}

bool Present(unsigned efb, int x, int y, int w, int h) {
    if (efb == 0 || w <= 0 || h <= 0) { Pump(); return false; }
    int dw = 1, dh = 1;
    SDL_GL_GetDrawableSize(g_window, &dw, &dh);
    int vw = dw, vh = dh;
    if (g_aspect > 0.0f) {
        if (dw > dh * g_aspect) vw = (int)std::lround(dh * g_aspect);
        else vh = (int)std::lround(dw / g_aspect);
    }
    g_viewX = (dw - vw) / 2; g_viewY = (dh - vh) / 2; g_viewW = vw > 0 ? vw : 1; g_viewH = vh > 0 ? vh : 1;

    int sy0 = y + h, sy1 = y;  // (source rows bottom-up, as the Vita host: the picture comes out upright)
    if (g_flipY) { const int t = sy0; sy0 = sy1; sy1 = t; }
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, dw, dh);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, efb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    // (window rows run bottom-up: the view's top edge is at dh - g_viewY)
    glBlitFramebuffer(x, sy0, x + w, sy1, g_viewX, dh - g_viewY, g_viewX + g_viewW, dh - g_viewY - g_viewH, GL_COLOR_BUFFER_BIT,
                      GL_LINEAR);
    g_showFps = port_pc_fps_counter() != 0;  // (the pause menu's Settings, F3, settings-pc.ini)
    if (g_showFps) DrawFps(dw, dh);
    {
        static int vsyncNow = -1;
        const int vsync = port_pc_vsync() ? 1 : 0;
        if (vsync != vsyncNow) { SDL_GL_SetSwapInterval(vsync); vsyncNow = vsync; }
    }
    SDL_GL_SwapWindow(g_window);
    glBindFramebuffer(GL_FRAMEBUFFER, efb);
    Pump();
    return true;
}
}  // namespace

bool CreateContext() {
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");          // GLES through EGL (ANGLE's libEGL / libGLESv2)
    SDL_SetHint(SDL_HINT_VIDEO_WIN_D3DCOMPILER, "none");  // (ANGLE brings its own shader compiler)
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        VGX_ERR("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    if (const char* e = std::getenv("PETARI_FLIPY")) g_flipY = std::atoi(e) != 0;
    if (const char* e = std::getenv("PETARI_ASPECT")) {
        int a = 0, b = 0;
        if (std::sscanf(e, "%d:%d", &a, &b) == 2 && a > 0 && b > 0) g_aspect = (float)a / b;
        else g_aspect = 0.0f;  // anything else: stretch
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);  // (the back end draws into its own framebuffer objects)
    int w = 1280, h = 720;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && (usable.w < w + 40 || usable.h < h + 80)) {
        // (small screens: 16:9 at 80% of the usable desktop, so the title bar and the picture fit)
        h = usable.h * 8 / 10;
        w = h * 16 / 9;
        if (w > usable.w * 9 / 10) { w = usable.w * 9 / 10; h = w * 9 / 16; }
    }
    if (const char* e = std::getenv("PETARI_WINDOW")) std::sscanf(e, "%dx%d", &w, &h);
    g_window = SDL_CreateWindow("Super Mario Galaxy (PC)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_window) { VGX_ERR("SDL_CreateWindow failed: %s\n", SDL_GetError()); return false; }
    g_context = SDL_GL_CreateContext(g_window);
    if (!g_context) { VGX_ERR("SDL_GL_CreateContext (GLES 3.0) failed: %s\n", SDL_GetError()); return false; }
    SDL_GL_MakeCurrent(g_window, g_context);
    std::fprintf(stderr, "[vgx-sdl] GL %s / %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
    if (std::getenv("PETARI_FULLSCREEN")) ToggleFullscreen();
    OpenFirstPad();
    backend::SetPresentHook(Present);
    Pump();
    return true;
}

void Idle() {
    if (g_window) Pump();
}

}  // namespace vgx::host
