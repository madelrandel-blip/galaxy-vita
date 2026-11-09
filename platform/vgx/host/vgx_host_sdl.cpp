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
#include <SDL2/SDL_syswm.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>  // (SetEnvironmentVariableA: ANGLE's platform for the Direct3D 9 attempt)

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "port/input.h"
#include "port/pc_settings.h"

namespace vgx::host {

namespace {
SDL_Window* g_window = nullptr;
EGLDisplay g_eglDisplay = EGL_NO_DISPLAY;
EGLSurface g_eglSurface = EGL_NO_SURFACE;
EGLContext g_eglContext = EGL_NO_CONTEXT;

// The window's drawable size in pixels (the EGL surface's).
void DrawableSize(int* w, int* h) {
    EGLint ew = 0, eh = 0;
    if (g_eglSurface != EGL_NO_SURFACE && eglQuerySurface(g_eglDisplay, g_eglSurface, EGL_WIDTH, &ew) &&
        eglQuerySurface(g_eglDisplay, g_eglSurface, EGL_HEIGHT, &eh) && ew > 0 && eh > 0) {
        *w = ew;
        *h = eh;
        return;
    }
    SDL_GetWindowSize(g_window, w, h);
}

// One attempt: ANGLE's display for `platform` (EGL_PLATFORM_ANGLE_TYPE_*), a window surface on `hwnd`, an OpenGL ES `major`
// context, made current. On failure everything it made is let go and `why` says what failed.
bool TryEgl(HWND hwnd, EGLint platform, int major, std::string& why) {
    using GetPlatformDisplay = EGLDisplay(EGLAPIENTRY*)(EGLenum, void*, const EGLint*);
    const auto getPlatformDisplay = reinterpret_cast<GetPlatformDisplay>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    char text[160];
    if (!getPlatformDisplay) { why = "no eglGetPlatformDisplayEXT"; return false; }
    const EGLint displayAttr[] = {0x3203 /* EGL_PLATFORM_ANGLE_TYPE_ANGLE */, platform, EGL_NONE};
    EGLDisplay d = getPlatformDisplay(0x3202 /* EGL_PLATFORM_ANGLE_ANGLE */, EGL_DEFAULT_DISPLAY, displayAttr);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, nullptr, nullptr)) {
        std::snprintf(text, sizeof text, "eglInitialize failed (0x%X)", (unsigned)eglGetError());
        why = text;
        return false;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint configAttr[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                 EGL_RENDERABLE_TYPE, major >= 3 ? 0x40 /* EGL_OPENGL_ES3_BIT */ : EGL_OPENGL_ES2_BIT, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!eglChooseConfig(d, configAttr, &config, 1, &count) || count < 1) {
        std::snprintf(text, sizeof text, "no OpenGL ES %d configuration (0x%X)", major, (unsigned)eglGetError());
        why = text;
        eglTerminate(d);
        return false;
    }
    EGLSurface surface = eglCreateWindowSurface(d, config, reinterpret_cast<EGLNativeWindowType>(hwnd), nullptr);
    if (surface == EGL_NO_SURFACE) {
        std::snprintf(text, sizeof text, "eglCreateWindowSurface failed (0x%X)", (unsigned)eglGetError());
        why = text;
        eglTerminate(d);
        return false;
    }
    const EGLint contextAttr[] = {EGL_CONTEXT_CLIENT_VERSION, major, EGL_NONE};
    EGLContext c = eglCreateContext(d, config, EGL_NO_CONTEXT, contextAttr);
    if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, surface, surface, c)) {
        std::snprintf(text, sizeof text, "OpenGL ES %d context failed (0x%X)", major, (unsigned)eglGetError());
        why = text;
        if (c != EGL_NO_CONTEXT) eglDestroyContext(d, c);
        eglDestroySurface(d, surface);
        eglTerminate(d);
        return false;
    }
    g_eglDisplay = d;
    g_eglSurface = surface;
    g_eglContext = c;
    return true;
}
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
            DrawableSize(&dw, &dh);
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

// ---- OpenGL ES 2.0 contexts (DirectX 9 graphics through ANGLE's Direct3D 9 renderer): no glBlitFramebuffer, so the picture
// goes to the window as a textured rectangle drawn from the EFB's colour texture.
bool g_es2 = false;
GLuint g_blitProg = 0;
GLint g_blitSrc = -1, g_blitTex = -1;

GLuint CompileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof log, nullptr, log);
        std::fprintf(stderr, "[vgx-sdl] present shader: %s\n", log);
    }
    return sh;
}

bool MakeBlitProgram() {
    static const char* kVs =
        "attribute vec2 aPos;\n"
        "uniform vec4 uSrc;\n"
        "varying vec2 vUv;\n"
        "void main() {\n"
        "    vec2 t = aPos * 0.5 + 0.5;\n"
        "    vUv = uSrc.xy + vec2(t.x, 1.0 - t.y) * uSrc.zw;\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";
    static const char* kFs =
        "precision mediump float;\n"
        "uniform sampler2D uTex;\n"
        "varying vec2 vUv;\n"
        "void main() { gl_FragColor = vec4(texture2D(uTex, vUv).rgb, 1.0); }\n";
    g_blitProg = glCreateProgram();
    glAttachShader(g_blitProg, CompileShader(GL_VERTEX_SHADER, kVs));
    glAttachShader(g_blitProg, CompileShader(GL_FRAGMENT_SHADER, kFs));
    glBindAttribLocation(g_blitProg, 0, "aPos");
    glLinkProgram(g_blitProg);
    GLint ok = 0;
    glGetProgramiv(g_blitProg, GL_LINK_STATUS, &ok);
    g_blitSrc = glGetUniformLocation(g_blitProg, "uSrc");
    g_blitTex = glGetUniformLocation(g_blitProg, "uTex");
    return ok != 0;
}

// Draws EFB columns x..x+w, rows y..y+h into the window's view (the default framebuffer is bound).
void PresentEs2(unsigned efb, int x, int y, int w, int h, int dw, int dh) {
    GLint tex = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, efb);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &tex);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    int tw = 0, th = 0;
    backend::RenderTargetSize(tw, th);
    if (!tex || tw <= 0 || th <= 0 || !g_blitProg) return;
    glViewport(g_viewX, dh - g_viewY - g_viewH, g_viewW, g_viewH);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(g_blitProg);
    // (the picture's top row: EFB row y when flipped - the default - else y + h)
    const float u0 = float(x) / tw, du = float(w) / tw;
    const float vTop = g_flipY ? float(y) / th : float(y + h) / th, dv = (g_flipY ? float(h) : -float(h)) / th;
    glUniform4f(g_blitSrc, u0, vTop, du, dv);
    glUniform1i(g_blitTex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
    GLint minF = 0, magF = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minF);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magF);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLint attribOn = 0;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attribOn);
    static const float kQuad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kQuad);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (!attribOn) glDisableVertexAttribArray(0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minF);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magF);
    glViewport(0, 0, dw, dh);
}

bool Present(unsigned efb, int x, int y, int w, int h) {
    if (efb == 0 || w <= 0 || h <= 0) { Pump(); return false; }
    int dw = 1, dh = 1;
    DrawableSize(&dw, &dh);
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
    if (g_es2) {
        PresentEs2(efb, x, y, w, h, dw, dh);
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, efb);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        // (window rows run bottom-up: the view's top edge is at dh - g_viewY)
        glBlitFramebuffer(x, sy0, x + w, sy1, g_viewX, dh - g_viewY, g_viewX + g_viewW, dh - g_viewY - g_viewH, GL_COLOR_BUFFER_BIT,
                          GL_LINEAR);
    }
    g_showFps = port_pc_fps_counter() != 0;  // (the pause menu's Settings, F3, settings-pc.ini)
    if (g_showFps) DrawFps(dw, dh);
    {
        static int vsyncNow = -1;
        const int vsync = port_pc_vsync() ? 1 : 0;
        if (vsync != vsyncNow) { eglSwapInterval(g_eglDisplay, vsync); vsyncNow = vsync; }
    }
    eglSwapBuffers(g_eglDisplay, g_eglSurface);
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
    int w = 1280, h = 720;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && (usable.w < w + 40 || usable.h < h + 80)) {
        // (small screens: 16:9 at 80% of the usable desktop, so the title bar and the picture fit)
        h = usable.h * 8 / 10;
        w = h * 16 / 9;
        if (w > usable.w * 9 / 10) { w = usable.w * 9 / 10; h = w * 9 / 16; }
    }
    if (const char* e = std::getenv("PETARI_WINDOW")) std::sscanf(e, "%dx%d", &w, &h);
    // The window is plain SDL; its OpenGL ES context is made here with EGL, so that ANGLE's renderer is asked for by name:
    // OpenGL ES 3.0 on Direct3D 11, else ES 2.0 on Direct3D 11, else ES 2.0 on Direct3D 9 (DirectX 9 graphics, or Windows 7
    // without the Platform Update's DXGI 1.2). ANGLE_DEFAULT_PLATFORM=d3d9 (set by main.cpp's probe, or by hand) starts there.
    g_window = SDL_CreateWindow("Super Mario Galaxy (PC)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_window) { VGX_ERR("SDL_CreateWindow failed: %s\n", SDL_GetError()); return false; }
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(g_window, &wm)) { VGX_ERR("SDL_GetWindowWMInfo failed: %s\n", SDL_GetError()); return false; }
    const HWND hwnd = wm.info.win.window;
    constexpr EGLint kD3D11 = 0x3208, kD3D9 = 0x3207;  // EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE / _D3D9_ANGLE
    struct Attempt { EGLint platform; int major; const char* what; };
    static const Attempt kAttempts[] = {{kD3D11, 3, "OpenGL ES 3.0 on Direct3D 11"}, {kD3D11, 2, "OpenGL ES 2.0 on Direct3D 11"},
                                        {kD3D9, 2, "OpenGL ES 2.0 on Direct3D 9"}};
    const char* forced = std::getenv("ANGLE_DEFAULT_PLATFORM");
    const bool onlyD3D9 = forced && !std::strcmp(forced, "d3d9");
    std::string failures;
    bool ok = false;
    for (const Attempt& a : kAttempts) {
        if (onlyD3D9 && a.platform != kD3D9) continue;
        std::string why;
        if (TryEgl(hwnd, a.platform, a.major, why)) {
            g_es2 = a.major < 3;
            std::fprintf(stderr, "[vgx-sdl] graphics: %s\n", a.what);
            ok = true;
            break;
        }
        failures += std::string(a.what) + ": " + why + "\n";
        std::fprintf(stderr, "[vgx-sdl] %s failed: %s\n", a.what, why.c_str());
    }
    if (!ok) {
        const std::string text =
            "The game could not start its graphics (OpenGL ES through ANGLE on Direct3D 11 or 9).\n\n" + failures +
            "\nWindows 7: install Service Pack 1 and the Platform Update (KB2670838), then restart.\n"
            "Also install the latest driver from your graphics card's maker (not the one from Windows Update).";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Super Mario Galaxy (PC)", text.c_str(), nullptr);
        std::fflush(nullptr);
        std::_Exit(3);
    }
    if (g_es2 && !MakeBlitProgram()) VGX_ERR("present program failed to link\n");
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
