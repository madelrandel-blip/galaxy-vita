// The pause menu's Settings button and its screen (Windows build): platform/windows/pc_menu.cpp.
// Each button is the pause menu's own "Quit" button - a second copy of the PauseMenu layout with everything else hidden and
// its own ButtonPaneController - so it looks, animates, sounds and answers the pointer exactly as the game's buttons do.
#pragma once

class LayoutActor;

void port_pc_menu_init(LayoutActor* pauseMenu);  // PauseMenu::init: makes the buttons, makes room for a third button
void port_pc_menu_appear();                      // PauseMenu::appear: the Settings button slides in with the others
bool port_pc_menu_select();                      // PauseMenu::exeSelecting: true when Settings was chosen
bool port_pc_menu_settings_step(bool first);     // the settings screen, once a turn; true when it has closed again
void port_pc_menu_force_wait();                  // PauseMenu::forceToWaitAllButton
void port_pc_menu_hide();                        // PauseMenu::exeDisappear
void port_pc_menu_kill();                        // PauseMenu::kill
