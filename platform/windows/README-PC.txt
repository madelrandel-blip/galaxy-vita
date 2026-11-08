Super Mario Galaxy - PC (Windows x64), unofficial build of galaxy-vita / Petari
==============================================================================

No game data is included. You need your own Wii disc (USA, RMGE01).

The game data must be the disc *converted* for the port (not just extracted):

  Easiest: copy the "game" folder from a PC where it already works, next to galaxy.exe.

  From scratch (needs Python 3 and the galaxy-vita source, tools/cook/cook.py):
    a. Dolphin's DolphinTool:  DolphinTool extract -i "Super Mario Galaxy.rvz" -o raw -g
    b. python tools/cook/cook.py raw/DATA game --with-movies
    c. copy raw/DATA/sys/main.dol into game/sys/
  You end up with game\sys\ and gameiles\ next to galaxy.exe.

Run galaxy.exe. Saves, the shader cache and game.log go to the "saves" folder next to it.
Other folders: galaxy.exe <game-folder> [save-folder]
Needs Windows 10/11 x64 and a GPU with Direct3D 11.

Controls
  Keyboard + mouse                      Controller (Xbox / PlayStation)
  WASD          move                    left stick    move
  Space         A (jump)                A / Cross     A (jump)
  Mouse left    B (shoot Star Bits)     RB / R1, RT   B (shoot Star Bits)
  Mouse         pointer                 right stick   pointer
  E, mouse right spin (shake)           X / Square    spin (shake)
  Shift         Z (crouch)              LT / L2       Z (crouch)
  Q             C (camera)              LB / L1       C (camera)
  Arrows        D-pad                   D-pad         D-pad
  1 / 2         1 / 2                   Y / B         1 / 2
  Enter / Bksp  + / -                   Start / Back  + / -
  Escape        Home                    F11 or Alt+Enter: fullscreen

F3: FPS counter on/off. Default rendering is 1516x1080 (1080 lines).

Options (environment variables)
  GXV_RENDER_SIZE=640x456   render resolution (640x456 = the Wii's own; 2160x1536 ~ 1440p)
  PETARI_WINDOW=1920x1080   window size        PETARI_FULLSCREEN=1  start fullscreen
  PETARI_ASPECT=4:3         picture aspect (default 16:9; "fill" stretches)
  PETARI_VSYNC=0            no vsync           PETARI_FLIPY=0       if the picture is upside down
