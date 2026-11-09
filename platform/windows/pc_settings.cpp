// PC graphics settings (see port/pc_settings.h).
#include "port/pc_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "port/port.h"

extern "C" void port_vgx_request_render_size(int w, int h);  // platform/src/gx/vgx_galaxy.cpp: render thread, next frame

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

std::atomic<int> sRes{kDefaultRes}, sFps{1}, sVsync{1};
char sPath[1024];

void Save() {
    if (!sPath[0]) return;
    FILE* f = fopen(sPath, "w");
    if (!f) { port_log("pc settings: cannot write %s", sPath); return; }
    fprintf(f, "# Super Mario Galaxy (PC) - graphics settings (also in the pause menu: Settings)\n");
    fprintf(f, "resolution=%s\nfps_counter=%d\nvsync=%d\n", kRes[sRes.load()].name, sFps.load(), sVsync.load());
    fclose(f);
}
}  // namespace

extern "C" void port_pc_settings_init(const char* saveRoot) {
    snprintf(sPath, sizeof sPath, "%s/settings-pc.ini", saveRoot);
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
            }
        }
        fclose(f);
    } else {
        Save();  // (a fresh file with the defaults, to edit by hand if wanted)
    }
    // (environment variables override the file for this session, unsaved)
    if (const char* e = getenv("PETARI_FPS")) sFps = atoi(e) != 0;
    if (const char* e = getenv("PETARI_VSYNC")) sVsync = atoi(e) != 0;
    port_log("pc settings: resolution %s (%dx%d), FPS counter %s, vsync %s", kRes[sRes.load()].name, kRes[sRes.load()].w,
             kRes[sRes.load()].h, sFps.load() ? "on" : "off", sVsync.load() ? "on" : "off");
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
