Super Mario Galaxy - PC (Windows x64), unofficial build of galaxy-vita / Petari
==============================================================================

No game data is included. You need your own Wii disc (USA, RMGE01).

1. In Dolphin: right click the game > Properties > Filesystem > right click the disc >
   "Extract Entire Disc". Extract it into a folder named "game" next to galaxy.exe,
   so that you have game\sys\ and game\files\.
2. Run galaxy.exe. Saves and game.log go to the "saves" folder next to it.
   Other folders: galaxy.exe <game-folder> [save-folder]

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

Options (environment variables)
  PETARI_WINDOW=1920x1080   window size        PETARI_FULLSCREEN=1  start fullscreen
  PETARI_ASPECT=4:3         picture aspect (default 16:9; "fill" stretches)
  PETARI_VSYNC=0            no vsync           PETARI_FLIPY=1       if the picture is upside down
