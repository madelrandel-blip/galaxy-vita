// Windows launcher: boots the game with the vgx renderer in an SDL window
// (platform/vgx/host/vgx_host_sdl.cpp owns the window, presentation and input).
//
//   galaxy.exe [game-folder] [save-folder]
//
// game-folder: the Wii disc extracted with Dolphin ("Extract Entire Disc": sys/ and files/).
// Defaults: "game" and "saves" next to galaxy.exe. The log goes to <save-folder>/game.log.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <unknwn.h>  // (IUnknown: the DXGI / Direct3D 11 probe below)

#include <direct.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

#include "port/input.h"
#include "port/pc_settings.h"
#include "port/port.h"

extern "C" void port_boot(const char*, const char*);

// ANGLE picks Direct3D 11 or 9 once, when EGL first starts, and keeps it (a later ANGLE_DEFAULT_PLATFORM is not read). Its
// Direct3D 11 renderer needs DXGI 1.2 (Windows 7: the Platform Update, KB2670838) and feature level 10_0 for OpenGL ES 3.0, so
// this decides before anything starts: without either, ANGLE's Direct3D 9 renderer (OpenGL ES 2.0). Returns why, or nullptr.
static const char* chooseAngleDirect3D9() {
    if (getenv("ANGLE_DEFAULT_PLATFORM")) return nullptr;  // (set by hand: kept)
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    using CreateFactory1 = HRESULT(WINAPI*)(REFIID, void**);
    const auto createFactory = dxgi ? reinterpret_cast<CreateFactory1>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
    static const GUID kFactory1 = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
    static const GUID kFactory2 = {0x50c83a1c, 0xe072, 0x4c48, {0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0}};
    IUnknown* factory = nullptr;
    if (!createFactory || FAILED(createFactory(kFactory1, reinterpret_cast<void**>(&factory))) || !factory) return "no DXGI 1.1";
    IUnknown* factory2 = nullptr;
    const bool dxgi12 = SUCCEEDED(factory->QueryInterface(kFactory2, reinterpret_cast<void**>(&factory2))) && factory2;
    if (factory2) factory2->Release();
    factory->Release();
    if (!dxgi12) return "no DXGI 1.2 (Windows 7 without the Platform Update, KB2670838)";
    HMODULE d3d11 = LoadLibraryA("d3d11.dll");
    using CreateDevice = HRESULT(WINAPI*)(void*, int, HMODULE, UINT, const int*, UINT, UINT, void**, int*, void**);
    const auto createDevice = d3d11 ? reinterpret_cast<CreateDevice>(GetProcAddress(d3d11, "D3D11CreateDevice")) : nullptr;
    int level = 0;  // (D3D_DRIVER_TYPE_HARDWARE 1, D3D11_SDK_VERSION 7; no device made, only its feature level asked)
    if (!createDevice || FAILED(createDevice(nullptr, 1, nullptr, 0, nullptr, 0, 7, nullptr, &level, nullptr))) return "no Direct3D 11 device";
    if (level < 0xa000) return "graphics below Direct3D feature level 10_0";
    return nullptr;
}

static std::string exeDir() {
    char path[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string s(path, n);
    const size_t slash = s.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : s.substr(0, slash);
}

static std::string slashes(std::string s) {
    for (char& c : s)
        if (c == '\\') c = '/';
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

static bool isDir(const std::string& p) {
    struct _stat64 st;
    return _stat64(p.c_str(), &st) == 0 && (st.st_mode & _S_IFDIR);
}

static void message(const char* text) {
    fprintf(stderr, "%s\n", text);
    MessageBoxA(nullptr, text, "Super Mario Galaxy (PC)", MB_OK | MB_ICONERROR);
}

static LONG WINAPI crashed(EXCEPTION_POINTERS* info) {
    char text[512];
    const EXCEPTION_RECORD* r = info->ExceptionRecord;
    const uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
    const uintptr_t at = (uintptr_t)r->ExceptionAddress;
    snprintf(text, sizeof(text), "The game crashed.\n\nexception %08lx at %p (galaxy.exe+%llx)\naccess %llx %p\n\nSee game.log in the save folder.",
             (unsigned long)r->ExceptionCode, r->ExceptionAddress, (unsigned long long)(at - base),
             r->NumberParameters > 0 ? (unsigned long long)r->ExceptionInformation[0] : 0ull,
             r->NumberParameters > 1 ? (void*)r->ExceptionInformation[1] : nullptr);
    port_log("%s", text);
    fflush(nullptr);
    message(text);
    return EXCEPTION_EXECUTE_HANDLER;
}

// First-chance handler for fatal exceptions, heap corruption included (which never reaches the
// unhandled filter): writes the code and the call stack as galaxy.exe offsets (galaxy.map) to
// stderr with no heap use.
// Integer division by zero gives 0 on the Wii's PowerPC (and on the Vita's ARM), and the game's code relies on it
// (JPAVolumeSphere divides by mDivNumber - 1, which can be 0). x86 traps: emulate the consoles - quotient 0,
// remainder = dividend - and step over the div / idiv instruction.
static int insnLengthDiv(const uint8_t* p, bool* opsize16, bool* rexW, bool* byteOp) {
    int i = 0;
    *opsize16 = *rexW = *byteOp = false;
    for (;; ++i) {
        const uint8_t b = p[i];
        if (b == 0x66) *opsize16 = true;
        else if (b == 0x67 || b == 0xF2 || b == 0xF3 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65) {}
        else break;
    }
    if ((p[i] & 0xF0) == 0x40) { *rexW = (p[i] & 0x08) != 0; ++i; }
    if (p[i] != 0xF7 && p[i] != 0xF6) return 0;
    *byteOp = p[i] == 0xF6;
    ++i;
    const uint8_t modrm = p[i++];
    const int mod = modrm >> 6, reg = (modrm >> 3) & 7, rm = modrm & 7;
    if (reg != 6 && reg != 7) return 0;  // (not div / idiv)
    if (mod != 3) {
        if (rm == 4) {
            const uint8_t sib = p[i++];
            if (mod == 0 && (sib & 7) == 5) i += 4;
        } else if (mod == 0 && rm == 5) {
            i += 4;
        }
        if (mod == 1) i += 1;
        else if (mod == 2) i += 4;
    }
    return i;
}

static LONG WINAPI divideByZero(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_INT_DIVIDE_BY_ZERO) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = info->ContextRecord;
    bool o16, w, b8;
    const int len = insnLengthDiv((const uint8_t*)c->Rip, &o16, &w, &b8);
    if (!len) return EXCEPTION_CONTINUE_SEARCH;
    if (b8) {
        c->Rax = (c->Rax & ~0xFFFFull) | ((c->Rax & 0xFF) << 8);  // AL = 0, AH = dividend's low byte
    } else if (w) {
        c->Rdx = c->Rax; c->Rax = 0;
    } else if (o16) {
        c->Rdx = (c->Rdx & ~0xFFFFull) | (c->Rax & 0xFFFF); c->Rax &= ~0xFFFFull;
    } else {
        c->Rdx = c->Rax & 0xFFFFFFFFull; c->Rax = 0;
    }
    static volatile LONG reported = 0;
    if (InterlockedIncrement(&reported) <= 5) {
        char line[160];
        const int n = snprintf(line, sizeof(line), "note: integer division by zero at exe+%llx answered 0, as on the Wii\n",
                               (unsigned long long)(c->Rip - (uintptr_t)GetModuleHandleA(nullptr)));
        DWORD written;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), line, (DWORD)n, &written, nullptr);
    }
    c->Rip += len;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static LONG WINAPI fatalFirstChance(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != 0xC0000374 && code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_PRIV_INSTRUCTION && code != 0xC0000409)
        return EXCEPTION_CONTINUE_SEARCH;
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
    char line[4096];
    int n = snprintf(line, sizeof(line), "FATAL exception %08lx at %p (thread %lu)\n  stack:", (unsigned long)code,
                     info->ExceptionRecord->ExceptionAddress, (unsigned long)GetCurrentThreadId());
    void* frames[48];
    const USHORT count = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
    for (USHORT i = 0; i < count && n < (int)sizeof(line) - 40; ++i) {
        const uintptr_t pc = (uintptr_t)frames[i];
        if (pc >= base && pc < base + 0x08000000) n += snprintf(line + n, sizeof(line) - n, " exe+%llx", (unsigned long long)(pc - base));
        else n += snprintf(line + n, sizeof(line) - n, " %p", (void*)pc);
    }
    n += snprintf(line + n, sizeof(line) - n, "\n");
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), line, (DWORD)n, &written, nullptr);
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char** argv) {
    _set_fmode(_O_BINARY);  // (every file the port opens is binary)
    timeBeginPeriod(1);     // 1 ms sleeps: the emulated Wii clock and the frame pacing rely on them
    AddVectoredExceptionHandler(1, fatalFirstChance);
    AddVectoredExceptionHandler(1, divideByZero);  // (added last with "first": runs before the fatal report)
    SetUnhandledExceptionFilter(crashed);

    const std::string here = exeDir();
    const std::string game = slashes(argc > 1 ? argv[1] : here + "/game");
    const std::string saves = slashes(argc > 2 ? argv[2] : here + "/saves");
    if (!isDir(game + "/files") || !isDir(game + "/sys")) {
        std::string text = "Game files not found in:\n" + game +
                           "\n\nExtract your own Super Mario Galaxy disc with Dolphin (right click > Properties > Filesystem > "
                           "Extract Entire Disc) into that folder, so that it holds sys\\ and files\\.\n\n"
                           "Or start: galaxy.exe <game-folder> [save-folder]";
        message(text.c_str());
        return 2;
    }
    _mkdir(saves.c_str());
    port_log_file((saves + "/game.log").c_str());

    // The vgx renderer is the one with a window; the legacy GLES 3.2 recorder only captures.
    if (!getenv("PETARI_BACKEND")) _putenv_s("PETARI_BACKEND", "vgx");
    // (desktop default 1 drops the drawing of frames that queue up behind a slow software GL; a real GPU keeps up)
    if (!getenv("GXV_SKIP_BEHIND")) _putenv_s("GXV_SKIP_BEHIND", "0");
    // The picture repairs the Vita build turns on (vita/vgx-game/entry.cpp): the Lumas' colours (TEV inputs read a colour register's
    // low 8 bits), Mario's eyes (the final alpha wraps as on the Wii), and the 1.2 / 1.3 picture and safety fixes. NAME=0 turns one off.
    // GXV_ANIM_LOD=0: no deferred far models (a PC has the CPU; deferred, the dome map's miniature galaxies stayed at its centre).
    for (const char* kv : {"GXV_TEVWRAP=2", "GXV_FIX_TEVIN=1", "GXV_CLIP_CAPTURE_FIX=1", "GXV_ASTRO_DOME_ZOOM_GUARD=1", "GXV_DSTALPHA_ZERO=1",
                           "GXV_EFB_CREATE_GUARD=1", "GXV_RT_ALLOC_GUARD=1", "GXV_EFB_REBIND_GUARD=1", "GXV_DLCACHE_BOUNDS_GUARD=1",
                           "GXV_GPU_TIMER_GUARD=1", "GXV_FIX_INDPREV=1", "GXV_FIX_DSTA=1", "GXV_FIX_LOGICOP=1", "GXV_FIX_TEXOFF=1",
                           "GXV_COPYVAR=1", "GXV_LTMERGE=1", "GXV_PALROWS=1", "GXV_FIX_LODBIAS=1", "GXV_FIX_LINES=1",
                           "GXV_ANIM_LOD=0"}) {
        const std::string pair(kv);
        const size_t eq = pair.find('=');
        const std::string name = pair.substr(0, eq);
        if (!getenv(name.c_str())) _putenv_s(name.c_str(), pair.c_str() + eq + 1);
    }
    // Graphics settings (<saves>/settings-pc.ini, the pause menu's Settings). Render resolution: 1080 lines by default
    // (the Wii's EFB is 640x456; the picture keeps that shape and is stretched to 16:9 on screen as on a TV).
    // GXV_RENDER_SIZE=WxH overrides it for a session.
    port_pc_settings_init(saves.c_str());
    // Settings > Game > Language (the game reads it when it starts; PETARI_LANGUAGE still wins for a session).
    if (!getenv("PETARI_LANGUAGE") && port_pc_language()[0]) _putenv_s("PETARI_LANGUAGE", port_pc_language());
    if (const char* why = chooseAngleDirect3D9()) {
        _putenv_s("ANGLE_DEFAULT_PLATFORM", "d3d9");  // (the C runtime's copy and Windows' own: ANGLE may read either)
        SetEnvironmentVariableA("ANGLE_DEFAULT_PLATFORM", "d3d9");
        port_log("graphics: ANGLE on Direct3D 9 (OpenGL ES 2.0): %s", why);
    }
    if (!getenv("GXV_RENDER_SIZE")) {
        int rw = 0, rh = 0;
        port_pc_res_size(port_pc_res_index(), &rw, &rh);
        char size[32];
        snprintf(size, sizeof size, "%dx%d", rw, rh);
        _putenv_s("GXV_RENDER_SIZE", size);
    }
    port_pc_apply_start();  // (MSAA: asked of the renderer now, applied with its first frame)
    if (!getenv("PETARI_SHADER_CACHE")) _putenv_s("PETARI_SHADER_CACHE", (saves + "/shaders.bin").c_str());

    PortPadState pad{};
    pad.connected = 1;
    pad.pointerValid = 1;
    pad.pointerDist = 1.0f;
    port_input_set(0, &pad);

    port_boot(game.c_str(), saves.c_str());
    // The game runs on its own threads; the window and input live on the render thread.
    for (;;) Sleep(1000);
}
