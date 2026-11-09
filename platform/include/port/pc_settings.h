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

// Picture smoothing and upscaling (the window host applies FXAA and FSR at the present; MSAA is the renderer's).
int port_pc_fxaa(void);
void port_pc_set_fxaa(int on);
int port_pc_msaa(void);           // samples a pixel: 1 (off), 2, 4, 8, 16
int port_pc_msaa_max(void);       // the most this graphics card allows (1: none, e.g. Direct3D 9); 0 until the renderer is up
void port_pc_next_msaa(void);     // the next choice the card allows (off after the last)
int port_pc_fsr(void);            // AMD FidelityFX Super Resolution 1 (EASU + RCAS) for the picture's upscale to the window
void port_pc_set_fsr(int on);
void port_pc_apply_start(void);   // the renderer's side of the settings, once at start-up

#ifdef __cplusplus
}
#endif
