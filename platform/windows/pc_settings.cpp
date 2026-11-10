// PC graphics settings (see port/pc_settings.h).
#include "port/pc_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <algorithm>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <zlib.h>

#include "port/port.h"

extern "C" void port_vgx_request_render_size(int w, int h);  // platform/src/gx/vgx_galaxy.cpp: render thread, next frame
extern "C" void port_vgx_request_msaa(int samples);
extern "C" int port_vgx_msaa_max(void);

namespace {
struct Resolution {
    int w, h;
    const char* name;
};
// The Wii's EFB is 640x456; every choice keeps that shape (the picture is stretched to 16:9 on screen, as on a TV).
const Resolution kRes[] = {
    {640, 456, "Wii"}, {1280, 912, "2x"}, {1516, 1080, "1080p"}, {2022, 1440, "1440p"}, {3032, 2160, "4K"},
};
constexpr int kResCount = sizeof kRes / sizeof kRes[0];
constexpr int kDefaultRes = 2;  // 1080p

std::atomic<int> sRes{kDefaultRes}, sFps{1}, sVsync{1}, sFxaa{0}, sMsaa{1}, sFsr{0}, sFull{0};
char sPath[1024];
char sRoot[1024];

// Audio
const int kVolMax[PORT_PC_VOL_COUNT] = {200, 100, 100};
const char* const kVolKey[PORT_PC_VOL_COUNT] = {"volume_master", "volume_music", "volume_sfx"};
std::atomic<int> sVol[PORT_PC_VOL_COUNT] = {{100}, {100}, {100}};

// Controls (SDL scancodes, SDL_GameControllerButton values; see port/pc_settings.h)
struct Action {
    const char* id;
    const char* name;
    int key, pad;
};
const Action kActions[PORT_PC_ACT_COUNT] = {
    {"jump", "Jump", 44 /* Space */, 0 /* A */},
    {"star_bits", "Star Bits", PORT_PC_MOUSE + 1, PORT_PC_PAD_RT},
    {"spin", "Spin", PORT_PC_MOUSE + 3, 2 /* X */},
    {"crouch", "Crouch", 225 /* Left Shift */, PORT_PC_PAD_LT},
    {"camera", "Camera (C)", 20 /* Q */, 9 /* LB */},
    {"plus", "Pause (+)", 40 /* Enter */, 6 /* Start */},
    {"minus", "Minus (-)", 42 /* Backspace */, 4 /* Back */},
    {"one", "Button 1", 30 /* 1 */, 3 /* Y */},
    {"two", "Button 2", 31 /* 2 */, 1 /* B */},
};
std::atomic<int> sKey[PORT_PC_ACT_COUNT], sPad[PORT_PC_ACT_COUNT];
std::atomic<int> sGyro{0}, sFpsLimit{60};
std::atomic<int> sCapture{-1}, sCaptureResult{0};
char sLanguage[32];
char sSkin[64];
char sSkinStart[64];  // (the skin the game started with)

int ClampVol(int which, int v) {
    if (v < 0) v = 0;
    if (v > kVolMax[which]) v = kVolMax[which];
    return v;
}

void Save() {
    if (!sPath[0]) return;
    FILE* f = fopen(sPath, "w");
    if (!f) { port_log("pc settings: cannot write %s", sPath); return; }
    fprintf(f, "# Super Mario Galaxy (PC) - graphics settings (also in the pause menu: Settings)\n");
    fprintf(f, "resolution=%s\nfps_counter=%d\nvsync=%d\nfxaa=%d\nmsaa=%d\nfsr=%d\nfullscreen=%d\n", kRes[sRes.load()].name,
            sFps.load(), sVsync.load(), sFxaa.load(), sMsaa.load(), sFsr.load(), sFull.load());
    for (int i = 0; i < PORT_PC_VOL_COUNT; ++i) fprintf(f, "%s=%d\n", kVolKey[i], sVol[i].load());
    fprintf(f, "skin=%s\n", sSkin);
    fprintf(f, "fps_limit=%d\ngyro=%d\nlanguage=%s\n", sFpsLimit.load(), sGyro.load(), sLanguage);
    fprintf(f, "# controls: bind_<action>=<keyboard>,<controller> (SDL scancode or 1000 + mouse button; SDL controller button, 100 LT, 101 RT; -1 none)\n");
    for (int i = 0; i < PORT_PC_ACT_COUNT; ++i) fprintf(f, "bind_%s=%d,%d\n", kActions[i].id, sKey[i].load(), sPad[i].load());
    fclose(f);
}
}  // namespace

extern "C" void port_pc_settings_init(const char* saveRoot) {
    snprintf(sPath, sizeof sPath, "%s/settings-pc.ini", saveRoot);
    snprintf(sRoot, sizeof sRoot, "%s", saveRoot);
    for (int i = 0; i < PORT_PC_ACT_COUNT; ++i) {
        sKey[i] = kActions[i].key;
        sPad[i] = kActions[i].pad;
    }
    if (FILE* f = fopen(sPath, "r")) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char* eq = strchr(line, '=');
            if (!eq || line[0] == '#') continue;
            *eq = 0;
            char* v = eq + 1;
            v[strcspn(v, "\r\n")] = 0;
            if (!strcmp(line, "resolution")) {
                for (int i = 0; i < kResCount; ++i)
                    if (!strcmp(v, kRes[i].name)) sRes = i;
            } else if (!strcmp(line, "fps_counter")) {
                sFps = atoi(v) != 0;
            } else if (!strcmp(line, "vsync")) {
                sVsync = atoi(v) != 0;
            } else if (!strcmp(line, "fxaa")) {
                sFxaa = atoi(v) != 0;
            } else if (!strcmp(line, "msaa")) {
                const int m = atoi(v);
                sMsaa = m >= 16 ? 16 : m >= 8 ? 8 : m >= 4 ? 4 : m >= 2 ? 2 : 1;
            } else if (!strcmp(line, "fsr")) {
                sFsr = atoi(v) != 0;
            } else if (!strcmp(line, "fullscreen")) {
                sFull = atoi(v) != 0;
            } else if (!strcmp(line, "fps_limit")) {
                sFpsLimit = atoi(v) == 30 ? 30 : 60;
            } else if (!strcmp(line, "gyro")) {
                sGyro = atoi(v) != 0;
            } else if (!strcmp(line, "skin")) {
                snprintf(sSkin, sizeof sSkin, "%s", v);
            } else if (!strcmp(line, "language")) {
                snprintf(sLanguage, sizeof sLanguage, "%s", v);
            } else if (!strncmp(line, "volume_", 7)) {
                for (int i = 0; i < PORT_PC_VOL_COUNT; ++i)
                    if (!strcmp(line, kVolKey[i])) sVol[i] = ClampVol(i, atoi(v));
            } else if (!strncmp(line, "bind_", 5)) {
                for (int i = 0; i < PORT_PC_ACT_COUNT; ++i) {
                    int k = -1, p = -1;
                    if (!strcmp(line + 5, kActions[i].id) && sscanf(v, "%d,%d", &k, &p) == 2) {
                        sKey[i] = k;
                        sPad[i] = p;
                    }
                }
            }
        }
        fclose(f);
    } else {
        Save();  // (a fresh file with the defaults, to edit by hand if wanted)
    }
    snprintf(sSkinStart, sizeof sSkinStart, "%s", sSkin);
    // (environment variables override the file for this session, unsaved)
    if (const char* e = getenv("PETARI_FPS")) sFps = atoi(e) != 0;
    if (const char* e = getenv("PETARI_VSYNC")) sVsync = atoi(e) != 0;
    port_log("pc settings: resolution %s (%dx%d), FPS counter %s, vsync %s, FXAA %s, MSAA %dx, FSR %s", kRes[sRes.load()].name,
             kRes[sRes.load()].w, kRes[sRes.load()].h, sFps.load() ? "on" : "off", sVsync.load() ? "on" : "off",
             sFxaa.load() ? "on" : "off", sMsaa.load(), sFsr.load() ? "on" : "off");
}

extern "C" int port_pc_res_count(void) { return kResCount; }
extern "C" int port_pc_res_index(void) { return sRes.load(); }
extern "C" const char* port_pc_res_name(int i) { return i >= 0 && i < kResCount ? kRes[i].name : "?"; }
extern "C" void port_pc_res_size(int i, int* w, int* h) {
    if (i < 0 || i >= kResCount) i = kDefaultRes;
    *w = kRes[i].w;
    *h = kRes[i].h;
}
extern "C" void port_pc_set_res_index(int i) {
    if (i < 0 || i >= kResCount) return;
    sRes = i;
    port_vgx_request_render_size(kRes[i].w, kRes[i].h);
    port_log("pc settings: resolution %s (%dx%d)", kRes[i].name, kRes[i].w, kRes[i].h);
    Save();
}

extern "C" int port_pc_fps_counter(void) { return sFps.load(); }
extern "C" void port_pc_set_fps_counter(int on) {
    sFps = on != 0;
    Save();
}
extern "C" int port_pc_vsync(void) { return sVsync.load(); }
extern "C" void port_pc_set_vsync(int on) {
    sVsync = on != 0;
    Save();
}

extern "C" int port_pc_fxaa(void) { return sFxaa.load(); }
extern "C" void port_pc_set_fxaa(int on) {
    sFxaa = on != 0;
    Save();
}
extern "C" int port_pc_msaa(void) {
    const int top = port_vgx_msaa_max();
    const int m = sMsaa.load();
    return top > 0 && m > top ? top : m;
}
extern "C" int port_pc_msaa_max(void) { return port_vgx_msaa_max(); }
extern "C" void port_pc_next_msaa(void) {
    const int top = port_vgx_msaa_max();
    int m = port_pc_msaa() * 2;
    if (m > top || m > 16) m = 1;
    sMsaa = m;
    port_vgx_request_msaa(m);
    port_log("pc settings: MSAA %dx", m);
    Save();
}
extern "C" int port_pc_fsr(void) { return sFsr.load(); }
extern "C" void port_pc_set_fsr(int on) {
    sFsr = on != 0;
    Save();
}
extern "C" int port_pc_fullscreen(void) { return sFull.load(); }
extern "C" void port_pc_set_fullscreen(int on) {
    sFull = on != 0;
    Save();
}
extern "C" void port_pc_apply_start(void) {
    if (sMsaa.load() > 1) port_vgx_request_msaa(sMsaa.load());  // (clamped to the card's most by the renderer)
}

// ---- audio
extern "C" int port_pc_volume(int which) { return which >= 0 && which < PORT_PC_VOL_COUNT ? sVol[which].load() : 100; }
extern "C" int port_pc_volume_max(int which) { return which >= 0 && which < PORT_PC_VOL_COUNT ? kVolMax[which] : 100; }
extern "C" void port_pc_set_volume(int which, int percent) {
    if (which < 0 || which >= PORT_PC_VOL_COUNT) return;
    sVol[which] = ClampVol(which, percent);
    Save();
}
extern "C" float port_pc_gain_master(void) { return sVol[PORT_PC_VOL_MASTER].load(std::memory_order_relaxed) * 0.01f; }
extern "C" float port_pc_gain_music(void) { return sVol[PORT_PC_VOL_MUSIC].load(std::memory_order_relaxed) * 0.01f; }
extern "C" float port_pc_gain_sfx(void) { return sVol[PORT_PC_VOL_SFX].load(std::memory_order_relaxed) * 0.01f; }

// ---- controls
extern "C" const char* port_pc_action_name(int a) { return a >= 0 && a < PORT_PC_ACT_COUNT ? kActions[a].name : "?"; }
extern "C" int port_pc_bind_key(int a) { return a >= 0 && a < PORT_PC_ACT_COUNT ? sKey[a].load(std::memory_order_relaxed) : -1; }
extern "C" int port_pc_bind_pad(int a) { return a >= 0 && a < PORT_PC_ACT_COUNT ? sPad[a].load(std::memory_order_relaxed) : -1; }
extern "C" void port_pc_reset_binds(void) {
    for (int i = 0; i < PORT_PC_ACT_COUNT; ++i) {
        sKey[i] = kActions[i].key;
        sPad[i] = kActions[i].pad;
    }
    Save();
}
extern "C" void port_pc_capture_begin(int action) {
    sCaptureResult = 0;
    sCapture = action;
}
extern "C" int port_pc_capture_active(void) { return sCapture.load(); }
extern "C" void port_pc_capture_feed(int key, int pad, int cancel) {
    const int a = sCapture.load();
    if (a < 0 || a >= PORT_PC_ACT_COUNT) return;
    if (cancel) {
        sCapture = -1;
        sCaptureResult = -1;
        return;
    }
    // The input moves here; an action that had it gets this action's old one (a swap: nothing is left unbound by accident).
    std::atomic<int>* table = key >= 0 ? sKey : sPad;
    const int code = key >= 0 ? key : pad;
    if (code < 0) return;
    for (int i = 0; i < PORT_PC_ACT_COUNT; ++i)
        if (i != a && table[i].load() == code) table[i] = table[a].load();
    table[a] = code;
    sCapture = -1;
    sCaptureResult = 1;
    Save();
}
extern "C" int port_pc_capture_poll(void) {
    if (sCapture.load() >= 0) return 0;
    return sCaptureResult.load() < 0 ? -1 : 1;
}
extern "C" int port_pc_gyro(void) { return sGyro.load(std::memory_order_relaxed); }
extern "C" void port_pc_set_gyro(int on) {
    sGyro = on != 0;
    Save();
}

// ---- language
extern "C" const char* port_pc_language(void) { return sLanguage; }
extern "C" void port_pc_set_language(const char* name) {
    snprintf(sLanguage, sizeof sLanguage, "%s", name ? name : "");
    Save();
}

// ---- saves: a .zip of <saves>/title (the console's save data); stored entries are written, stored and deflated ones read.
namespace {
std::wstring Wide(const std::string& s) {
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], (int)w.size());
    if (!w.empty()) w.pop_back();
    return w;
}
std::string Narrow(const std::wstring& w) {
    std::string s(WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], (int)s.size(), nullptr, nullptr);
    if (!s.empty()) s.pop_back();
    return s;
}
bool ReadAll(const std::wstring& path, std::vector<unsigned char>& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    out.clear();
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return true;
}
// Files under dir (relative names with '/'), recursively.
void ListFiles(const std::wstring& root, const std::wstring& rel, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"/" + rel + L"/*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring sub = rel + L"/" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ListFiles(root, sub, out);
        else out.push_back(sub);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
void Put16(std::vector<unsigned char>& v, unsigned x) { v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF); }
void Put32(std::vector<unsigned char>& v, unsigned long x) { Put16(v, x & 0xFFFF); Put16(v, (x >> 16) & 0xFFFF); }
unsigned Get16(const unsigned char* p) { return p[0] | (p[1] << 8); }
unsigned long Get32(const unsigned char* p) { return Get16(p) | ((unsigned long)Get16(p + 2) << 16); }

bool FileDialog(bool save, std::wstring& path) {
    wchar_t buf[MAX_PATH] = L"SMG-saves.zip";
    if (!save) buf[0] = 0;
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = GetForegroundWindow();  // (the game's window: the dialog stays in front of it)
    ofn.lpstrFilter = L"Zip (*.zip)\0*.zip\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"zip";
    ofn.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))) return false;
    path = buf;
    return true;
}
}  // namespace

extern "C" int port_pc_export_saves(void) {
    std::wstring path;
    if (!FileDialog(true, path)) return 0;
    const std::wstring root = Wide(sRoot);
    std::vector<std::wstring> files;
    ListFiles(root, L"title", files);
    if (files.empty()) return -1;
    std::vector<unsigned char> zip, dir;
    unsigned long count = 0;
    for (const std::wstring& rel : files) {
        std::vector<unsigned char> data;
        if (!ReadAll(root + L"/" + rel, data)) return -1;
        const std::string name = Narrow(rel);
        const unsigned long crc = crc32(0, data.data(), (uInt)data.size());
        const unsigned long offset = (unsigned long)zip.size();
        Put32(zip, 0x04034b50); Put16(zip, 20); Put16(zip, 0x0800); Put16(zip, 0); Put32(zip, 0);  // (stored, UTF-8 name)
        Put32(zip, crc); Put32(zip, (unsigned long)data.size()); Put32(zip, (unsigned long)data.size());
        Put16(zip, (unsigned)name.size()); Put16(zip, 0);
        zip.insert(zip.end(), name.begin(), name.end());
        zip.insert(zip.end(), data.begin(), data.end());
        Put32(dir, 0x02014b50); Put16(dir, 20); Put16(dir, 20); Put16(dir, 0x0800); Put16(dir, 0); Put32(dir, 0);
        Put32(dir, crc); Put32(dir, (unsigned long)data.size()); Put32(dir, (unsigned long)data.size());
        Put16(dir, (unsigned)name.size()); Put16(dir, 0); Put16(dir, 0); Put16(dir, 0); Put16(dir, 0); Put32(dir, 0); Put32(dir, offset);
        dir.insert(dir.end(), name.begin(), name.end());
        ++count;
    }
    const unsigned long dirOffset = (unsigned long)zip.size();
    zip.insert(zip.end(), dir.begin(), dir.end());
    Put32(zip, 0x06054b50); Put16(zip, 0); Put16(zip, 0); Put16(zip, count); Put16(zip, count);
    Put32(zip, (unsigned long)dir.size()); Put32(zip, dirOffset); Put16(zip, 0);
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return -1;
    const bool ok = fwrite(zip.data(), 1, zip.size(), f) == zip.size();
    fclose(f);
    port_log("pc settings: saves exported (%lu files) to %s", count, Narrow(path).c_str());
    return ok ? 1 : -1;
}

extern "C" int port_pc_import_saves(void) {
    std::wstring path;
    if (!FileDialog(false, path)) return 0;
    std::vector<unsigned char> zip;
    if (!ReadAll(path, zip) || zip.size() < 22) return -1;
    // The end of central directory record, then each entry.
    size_t end = zip.size() - 22;
    while (end > 0 && Get32(&zip[end]) != 0x06054b50) --end;
    if (Get32(&zip[end]) != 0x06054b50) return -1;
    const unsigned entries = Get16(&zip[end + 10]);
    size_t at = Get32(&zip[end + 16]);
    struct Entry {
        std::string name;
        std::vector<unsigned char> data;
    };
    std::vector<Entry> out;
    for (unsigned e = 0; e < entries; ++e) {
        if (at + 46 > zip.size() || Get32(&zip[at]) != 0x02014b50) return -1;
        const unsigned method = Get16(&zip[at + 10]);
        const unsigned long crc = Get32(&zip[at + 16]), csize = Get32(&zip[at + 20]), usize = Get32(&zip[at + 24]);
        const unsigned nlen = Get16(&zip[at + 28]), xlen = Get16(&zip[at + 30]), clen = Get16(&zip[at + 32]);
        const unsigned long local = Get32(&zip[at + 42]);
        if (at + 46 + nlen > zip.size()) return -1;
        std::string name(reinterpret_cast<const char*>(&zip[at + 46]), nlen);
        at += 46 + nlen + xlen + clen;
        for (char& c : name)
            if (c == '\\') c = '/';
        // Only the save data folder, wherever it sits in the archive (a zip of the whole "saves" folder works too).
        const size_t t = name.find("title/");
        if (t == std::string::npos || name.back() == '/') continue;
        name = name.substr(t);
        if (name.find("..") != std::string::npos || name.find(':') != std::string::npos) return -1;
        if (local + 30 > zip.size() || Get32(&zip[local]) != 0x04034b50) return -1;
        const size_t data = local + 30 + Get16(&zip[local + 26]) + Get16(&zip[local + 28]);
        if (data + csize > zip.size()) return -1;
        Entry entry{name, std::vector<unsigned char>(usize)};
        if (method == 0) {
            if (csize != usize) return -1;
            if (usize) memcpy(entry.data.data(), &zip[data], usize);
        } else if (method == 8) {
            z_stream z{};
            if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return -1;
            z.next_in = &zip[data];
            z.avail_in = (uInt)csize;
            z.next_out = entry.data.data();
            z.avail_out = (uInt)usize;
            const int r = inflate(&z, Z_FINISH);
            inflateEnd(&z);
            if (r != Z_STREAM_END || z.total_out != usize) return -1;
        } else {
            return -1;
        }
        if (crc32(0, entry.data.data(), (uInt)entry.data.size()) != crc) return -1;
        out.push_back(std::move(entry));
    }
    if (out.empty()) return -1;
    // The current saves are kept (title-backup-<date>), then the archive's are written.
    const std::wstring root = Wide(sRoot);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t backup[64];
    swprintf(backup, 64, L"/title-backup-%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    if (GetFileAttributesW((root + L"/title").c_str()) != INVALID_FILE_ATTRIBUTES &&
        !MoveFileW((root + L"/title").c_str(), (root + backup).c_str()))
        return -1;
    for (const Entry& entry : out) {
        std::wstring file = root + L"/" + Wide(entry.name);
        for (size_t i = root.size() + 1; i < file.size(); ++i)
            if (file[i] == L'/') {
                file[i] = 0;
                CreateDirectoryW(file.c_str(), nullptr);
                file[i] = L'/';
            }
        FILE* f = _wfopen(file.c_str(), L"wb");
        if (!f) return -1;
        fwrite(entry.data.data(), 1, entry.data.size(), f);
        fclose(f);
    }
    port_log("pc settings: saves imported (%u files) from %s", (unsigned)out.size(), Narrow(path).c_str());
    return 1;
}

extern "C" void port_pc_restart(void) {
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = GetCommandLineW();
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (CreateProcessW(exe, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    fflush(nullptr);
    _exit(0);
}

extern "C" int port_pc_fps_limit(void) { return sFpsLimit.load(std::memory_order_relaxed); }
extern "C" void port_pc_set_fps_limit(int fps) {
    sFpsLimit = fps == 30 ? 30 : 60;
    Save();
}

// ---- skins
namespace {
std::string SkinsDir() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    return Narrow(dir) + "/skins";
}
// The skins there (folders with a files/ folder), sorted.
std::vector<std::string> SkinNames() {
    std::vector<std::string> names;
    WIN32_FIND_DATAW fd;
    const std::wstring dir = Wide(SkinsDir());
    HANDLE h = FindFirstFileW((dir + L"/*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return names;
    do {
        const std::wstring n = fd.cFileName;
        if (n == L"." || n == L".." || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        const DWORD files = GetFileAttributesW((dir + L"/" + n + L"/files").c_str());
        const DWORD ini = GetFileAttributesW((dir + L"/" + n + L"/skin.ini").c_str());
        if ((files != INVALID_FILE_ATTRIBUTES && (files & FILE_ATTRIBUTE_DIRECTORY)) || ini != INVALID_FILE_ATTRIBUTES)
            names.push_back(Narrow(n));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    return names;
}
std::string sSkinFiles;
}  // namespace

extern "C" const char* port_pc_skin(void) { return sSkin; }
extern "C" void port_pc_next_skin(void) {
    // "" (the game's own), then each folder in order, then "" again.
    const std::vector<std::string> names = SkinNames();
    size_t at = 0;  // (index into "" + names)
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == sSkin) at = i + 1;
    at = (at + 1) % (names.size() + 1);
    snprintf(sSkin, sizeof sSkin, "%s", at ? names[at - 1].c_str() : "");
    Save();
}
extern "C" const char* port_pc_skin_files(void) {
    if (!sSkinStart[0]) return "";
    if (sSkinFiles.empty()) sSkinFiles = SkinsDir() + "/" + sSkinStart + "/files";
    return sSkinFiles.c_str();
}
// skins/<name>/skin.ini: look=luigi uses Luigi's own models, animations, face, power-up suits and voice; look=mario Mario's.
// The skin the game started with (a change applies at the next start).
// skins/<name>/skin.ini anim_scale=<factor> (0.25..4; default 1): the player's animations move the joints this much farther
// apart, for a model made at another size than Mario (the animations carry Mario's bone lengths).
extern "C" float port_pc_skin_anim_scale(void) {
    static float scale = [] {
        float v = 1.0f;
        if (!sSkinStart[0]) return v;
        if (FILE* f = _wfopen(Wide(SkinsDir() + "/" + sSkinStart + "/skin.ini").c_str(), L"r")) {
            char line[128];
            while (fgets(line, sizeof line, f)) {
                float x;
                if (sscanf(line, "anim_scale=%f", &x) == 1 && x >= 0.25f && x <= 4.0f) v = x;
            }
            fclose(f);
        }
        if (v != 1.0f) port_log("skin: %s, animation scale %.2f", sSkinStart, v);
        return v;
    }();
    return scale;
}

extern "C" int port_pc_skin_look(void) {
    static int look = [] {
        if (!sSkinStart[0]) return -1;
        int v = -1;
        if (FILE* f = _wfopen(Wide(SkinsDir() + "/" + sSkinStart + "/skin.ini").c_str(), L"r")) {
            char line[128];
            while (fgets(line, sizeof line, f)) {
                line[strcspn(line, "\r\n")] = 0;
                if (!strcmp(line, "look=luigi")) v = 1;
                if (!strcmp(line, "look=mario")) v = 0;
            }
            fclose(f);
        }
        port_log("skin: %s, look %s", sSkinStart, v == 1 ? "luigi" : v == 0 ? "mario" : "of the save");
        return v;
    }();
    return look;
}
