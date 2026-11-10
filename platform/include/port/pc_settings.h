// PC (Windows) settings: graphics, audio, controls, language, saves. Kept in <saves>/settings-pc.ini and changed from
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
int port_pc_ssao(void);         // screen-space ambient occlusion: 0 off, 1 on
void port_pc_set_ssao(int on);
int port_pc_msaa(void);           // samples a pixel: 1 (off), 2, 4, 8, 16
int port_pc_msaa_max(void);       // the most this graphics card allows (1: none, e.g. Direct3D 9); 0 until the renderer is up
void port_pc_next_msaa(void);     // the next choice the card allows (off after the last)
int port_pc_fsr(void);            // AMD FidelityFX Super Resolution 1 (EASU + RCAS) for the picture's upscale to the window
void port_pc_set_fsr(int on);
int port_pc_fullscreen(void);     // 1: fullscreen (the desktop's size), 0: a window
void port_pc_set_fullscreen(int on);
int port_pc_fps_limit(void);       // 60, or 30 (the game still runs at full speed: every second picture is drawn)
void port_pc_set_fps_limit(int fps);
void port_pc_apply_start(void);   // the renderer's side of the settings, once at start-up

// Audio: percentages. Master 0..200 (the output, after the game's mix), music and sound effects 0..100 (the game's own mix:
// JAudio's stream and sequence managers are the music, its sound-effect manager the effects).
enum { PORT_PC_VOL_MASTER, PORT_PC_VOL_MUSIC, PORT_PC_VOL_SFX, PORT_PC_VOL_COUNT };
int port_pc_volume(int which);
void port_pc_set_volume(int which, int percent);  // (clamped to the range)
int port_pc_volume_max(int which);
float port_pc_gain_master(void);  // the same as factors, for the audio threads
float port_pc_gain_music(void);
float port_pc_gain_sfx(void);

// Controls: each action has one keyboard / mouse input and one game-controller input.
//   keyboard codes: an SDL scancode, or PORT_PC_MOUSE + the SDL mouse button (1 left, 2 middle, 3 right, 4, 5)
//   controller codes: an SDL_GameControllerButton, or PORT_PC_PAD_LT / PORT_PC_PAD_RT; -1: none
enum { PORT_PC_ACT_JUMP, PORT_PC_ACT_STARBITS, PORT_PC_ACT_SPIN, PORT_PC_ACT_CROUCH, PORT_PC_ACT_CAMERA, PORT_PC_ACT_PLUS,
       PORT_PC_ACT_MINUS, PORT_PC_ACT_ONE, PORT_PC_ACT_TWO, PORT_PC_ACT_COUNT };
enum { PORT_PC_MOUSE = 1000, PORT_PC_PAD_LT = 100, PORT_PC_PAD_RT = 101 };
const char* port_pc_action_name(int action);
int port_pc_bind_key(int action);
int port_pc_bind_pad(int action);
void port_pc_reset_binds(void);
// Rebinding: the menu starts a capture, the window host feeds it the next key / mouse button / controller button pressed.
void port_pc_capture_begin(int action);
int port_pc_capture_active(void);                      // the action being captured, -1: none
void port_pc_capture_feed(int key, int pad, int cancel);  // (host) key or pad >= 0: assign (swapped with an action that had it)
int port_pc_capture_poll(void);                       // (menu) 0: still waiting, 1: assigned, -1: cancelled
// Names of inputs for the menu (the window host's, it knows SDL's names).
void port_pc_input_name(int code, int pad, char* out, int size);
int port_pc_gyro(void);  // a controller's gyroscope moves the pointer
void port_pc_set_gyro(int on);

// Game: the language (the disc's language names, "" = the disc's own; applies when the game starts again), saves.
const char* port_pc_language(void);
void port_pc_set_language(const char* name);
int port_pc_export_saves(void);  // a .zip of the saves (file dialog): 1 done, 0 cancelled, -1 failed
int port_pc_import_saves(void);  // the same back (the current saves kept as a backup folder); 1: restart to load them
void port_pc_restart(void);
// Skins: folders under <galaxy.exe's folder>/skins, each with files/... (converted game files that replace the game's own, e.g.
// files/ObjectData/Mario.arc). "" is the game's own look. Applies when the game starts again.
const char* port_pc_skin(void);
void port_pc_next_skin(void);
const char* port_pc_skin_files(void);  // the chosen skin's files folder, "" for none      // starts the game again (same command line) and ends this one

#ifdef __cplusplus
}
#endif
