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

#include <direct.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

#include "port/input.h"
#include "port/port.h"

extern "C" void port_boot(const char*, const char*);

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

int main(int argc, char** argv) {
    _set_fmode(_O_BINARY);  // (every file the port opens is binary)
    timeBeginPeriod(1);     // 1 ms sleeps: the emulated Wii clock and the frame pacing rely on them
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
