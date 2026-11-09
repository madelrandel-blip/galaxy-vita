// PC (Windows) graphics settings: render resolution, FPS counter, vsync. Kept in <saves>/settings-pc.ini and changed from
// the pause menu's Settings button (platform/windows/pc_menu.cpp). Any thread may read them.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void port_pc_settings_init(const char* saveRoot);  // loads the file (environment variables still win for a session)

int port_pc_res_count(void);
int port_pc_res_index(void);
const char* port_pc_res_name(int index);           // "1080p", ...
void port_pc_res_size(int index, int* w, int* h);  // render target size for that choice
void port_pc_set_res_index(int index);             // applies at the next frame and saves

int port_pc_fps_counter(void);
void port_pc_set_fps_counter(int on);
int port_pc_vsync(void);
void port_pc_set_vsync(int on);

#ifdef __cplusplus
}
#endif
