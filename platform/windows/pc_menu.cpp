// The pause menu's Settings button and the settings screens (see port/pc_menu.h).
//
// Layout (PauseMenu.arc, units of the 608 x 456 layout, y up): the top bar ends at y 77, the bottom bar starts at y -123.
// The game's two buttons sit under the pane "BackButton" (y 12): Back at +12, Quit 85 below. With the buttons at 80% and
// Quit 75 below Back, three fit between the bars: Back 41.6, Quit -28, Settings -88.
//
// Settings opens a list of pages: Graphics, Audio, Controls (two pages), Game. Each page is drawn with one pool of buttons
// (the PauseMenu layout again, showing only its Quit button), placed again for each page: at 55% in two columns at x -150 and
// +150, rows 54, 2, -50, -102 (the Audio page's - and + buttons are smaller, beside each value).
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "nw4r/lyt/pane.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "port/pc_menu.h"
#include "port/pc_settings.h"
#include "port/port.h"

namespace {
constexpr f32 kQuitStep = 75.0f;  // Quit's distance under Back (85 in the layout)

void PlaceButtons(LayoutActor* actor, f32 backButtonY, f32 scale, f32 x = 0.0f, f32 scaleX = 0.0f) {
    if (nw4r::lyt::Pane* p = MR::getPane(actor, "BackButton")) {
        p->mTranslate.x = x;
        p->mTranslate.y = backButtonY;
        p->mScale.x = scaleX > 0.0f ? scaleX : scale;
        p->mScale.y = scale;
    }
    if (nw4r::lyt::Pane* p = MR::getPane(actor, "BackDown")) p->mTranslate.y = -kQuitStep;
}

// One button: the PauseMenu layout again, showing only its Quit button, centred at (x, center).
class PortMenuButton : public LayoutActor {
public:
    PortMenuButton(f32 center, f32 scale, f32 x = 0.0f, bool round = false)
        : LayoutActor("PortMenuButton", true), mCtrl(nullptr), mCenter(center), mScale(scale), mX(x), mRound(round) {}

    // Round: the BackButton layout (BackButton.arc, the game's round "<" button). Its pill is a left cap, a centre and a right
    // cap: the centre and the text are hidden and the caps (32 wide) meet, a circle with the arrow in the middle.
    void InitRound() {
        initLayoutManagerWithTextBoxBufferLength("BackButton", 8, 1);
        static const char* const kHidden[] = {"ShaCButton", "PicCFrame", "PicCButton", "PicCShine", "PicArrow"};
        for (const char* name : kHidden) MR::hidePaneRecursive(this, name);
        static const char* const kLeft[] = {"ShaLButton", "PicLFrame", "PicLButton", "PicLShine"};
        static const char* const kRight[] = {"ShaRButton", "PicRFrame", "PicRButton", "PicRShine"};
        for (const char* name : kLeft)
            if (nw4r::lyt::Pane* q = MR::getPane(this, name)) q->mTranslate.x = 0.0f;  // (anchored at its right edge: spans -32..0)
        for (const char* name : kRight)
            if (nw4r::lyt::Pane* q = MR::getPane(this, name)) q->mTranslate.x = 0.0f;  // (anchored at its left edge: spans 0..32)
        if (nw4r::lyt::Pane* q = MR::getPane(this, "BButtonText")) {  // (the text was written for a wide button: larger)
            q->mScale.x = 1.8f;
            q->mScale.y = 1.8f;
        }
        static const char* const kText[] = {"Txt", "Sha", "TxtJpJa", "ShaJpJa", "TxtUsEn", "ShaUsEn", "TxtEuEn", "ShaEuEn"};
        for (const char* name : kText)  // (the text sat right of the arrow)
            if (nw4r::lyt::Pane* q = MR::getPane(this, name)) q->mTranslate.x = name[0] == 'S' ? 1.8f : 0.0f;
        if (nw4r::lyt::Pane* q = MR::getPane(this, "BoxButton")) {
            q->mSize.width = 64.0f;
            q->mSize.height = 64.0f;
        }
        PlaceRound();
        MR::createAndAddPaneCtrl(this, "Back", 1);
        MR::connectToSceneLayoutOnPause(this);
        mCtrl = new ButtonPaneController(this, "Back", "BoxButton", 0, true);
        mCtrl->_22 = false;
        kill();
    }
    void PlaceRound() {
        // The button's centre is (-181, -176) in the layout (Back -276, -176 and its child +95); the animations move Back,
        // so the whole layout is placed: centre at (mX, mCenter), scaled.
        if (nw4r::lyt::Pane* q = MR::getPane(this, "RootPane")) {
            q->mTranslate.x = mX + 181.0f * mScale;
            q->mTranslate.y = mCenter + 176.0f * mScale;
            q->mScale.x = mScale;
            q->mScale.y = mScale;
        }
    }

    virtual void init(const JMapInfoIter&) {
        if (mRound) {
            InitRound();
            return;
        }
        initLayoutManagerWithTextBoxBufferLength("PauseMenu", 64, 1);  // (text box buffers of 64 characters, 1 animation layer)
        static const char* const kHidden[] = {"BG", "PicStagebase", "PicStagebase1", "Stars", "Counter", "LetterButton", "PositionNull", "NBackUp"};
        for (const char* name : kHidden) MR::hidePaneRecursive(this, name);
        PlaceButtons(this, mCenter + kQuitStep * mScale, mScale, mX);
        MR::connectToSceneLayoutOnPause(this);
        mCtrl = new ButtonPaneController(this, "NBackDown", "BoxButton2", 0, true);
        mCtrl->_22 = false;
        kill();
    }

    virtual void control() {
        mCtrl->update();
        if (mLeaving && mCtrl->isHidden()) {
            mLeaving = false;
            kill();
        }
    }

    void SetText(const char* text) {
        const char* textPane = mRound ? "BButtonText" : "NBackDown";
        wchar_t wide[64];
        int i = 0;
        for (; text[i] && i < 63; ++i) wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(text[i]));
        wide[i] = 0;
        MR::setTextBoxMessageRecursive(this, textPane, wide);
    }
    // scaleX > 0: narrower than tall-scale (the small - / + buttons; the frame is fixed picture pieces, so only scaling shapes it).
    void Place(f32 x, f32 center, f32 scale, f32 scaleX) {
        mX = x;
        mCenter = center;
        mScale = scale;
        if (mRound) {
            PlaceRound();
            return;
        }
        PlaceButtons(this, mCenter + kQuitStep * mScale, mScale, mX, scaleX);
    }
    void Show() {
        mLeaving = false;
        if (MR::isDead(this)) appear();
        mCtrl->appear();
    }
    void Hide() {
        if (MR::isDead(this) || mCtrl->isHidden()) return;
        mCtrl->disappear();
        mLeaving = true;
    }
    bool Gone() const { return MR::isDead(this) || mCtrl->isHidden(); }

    ButtonPaneController* mCtrl;
    f32 mCenter, mScale, mX;
    bool mRound;
    bool mLeaving = false;
};

// ---- pages
enum class Page { Main, Graphics, Audio, Controls1, Controls2, Game };
enum Id {
    kNone,
    // main
    kGoGraphics, kGoAudio, kGoControls, kGoGame, kClose,
    // graphics
    kRes, kFsr, kFxaa, kMsaa, kFps, kVsync, kFull, kFpsLimit,
    // audio (value, minus, plus for each)
    kVolMaster, kVolMasterDown, kVolMasterUp, kVolMusic, kVolMusicDown, kVolMusicUp, kVolSfx, kVolSfxDown, kVolSfxUp,
    // controls
    kBindFirst, kBindLast = kBindFirst + PORT_PC_ACT_COUNT - 1, kGyro, kResetBinds, kNextPage, kPrevPage,
    // game
    kLanguage, kSkin, kRestart, kExport, kImport,
    // every page but the first
    kToMain,
};
struct Item {
    int id;
    f32 x, y, scale;
    f32 scaleX = 0.0f;  // (0: the same as scale; the small - / + buttons are narrower)
};
constexpr f32 kL = -150.0f, kR = 150.0f, kS = 0.55f;
const Item kMainItems[] = {{kGoGraphics, kL, 54, kS}, {kGoAudio, kL, 2, kS}, {kGoControls, kR, 54, kS}, {kGoGame, kR, 2, kS},
                           {kClose, 0, -76, kS}};
// Graphics: three columns of three (x -195, 0, 195; rows 40, -20, -80) at 50 %.
constexpr f32 kG = 0.5f, kGx = 195.0f;
const Item kGraphicsItems[] = {{kRes, -kGx, 40, kG},  {kFsr, 0, 40, kG},       {kFxaa, kGx, 40, kG},
                               {kMsaa, -kGx, -20, kG}, {kFps, 0, -20, kG},      {kVsync, kGx, -20, kG},
                               {kFull, -kGx, -80, kG}, {kFpsLimit, 0, -80, kG}, {kToMain, kGx, -80, kG}};
// Audio: each value with a small - / + button against its left and right end (the value is 140 wide at 55 %).
constexpr f32 kRound = 0.6f, kRoundX = 1.0f, kSide = 100.0f;  // (kRoundX > 0: a round button)
const Item kAudioItems[] = {{kVolMaster, 0, 54, kS}, {kVolMasterDown, -kSide, 54, kRound, kRoundX}, {kVolMasterUp, kSide, 54, kRound, kRoundX},
                            {kVolMusic, 0, 2, kS},   {kVolMusicDown, -kSide, 2, kRound, kRoundX},   {kVolMusicUp, kSide, 2, kRound, kRoundX},
                            {kVolSfx, 0, -50, kS},   {kVolSfxDown, -kSide, -50, kRound, kRoundX},   {kVolSfxUp, kSide, -50, kRound, kRoundX},
                            {kToMain, 0, -102, kS}};
const Item kControls1Items[] = {{kBindFirst + PORT_PC_ACT_JUMP, kL, 54, kS},   {kBindFirst + PORT_PC_ACT_STARBITS, kL, 2, kS},
                                {kBindFirst + PORT_PC_ACT_SPIN, kL, -50, kS},  {kBindFirst + PORT_PC_ACT_CROUCH, kL, -102, kS},
                                {kBindFirst + PORT_PC_ACT_CAMERA, kR, 54, kS}, {kBindFirst + PORT_PC_ACT_PLUS, kR, 2, kS},
                                {kNextPage, kR, -50, kS},                      {kToMain, kR, -102, kS}};
const Item kControls2Items[] = {{kBindFirst + PORT_PC_ACT_MINUS, kL, 54, kS}, {kBindFirst + PORT_PC_ACT_ONE, kL, 2, kS},
                                {kBindFirst + PORT_PC_ACT_TWO, kL, -50, kS},  {kGyro, kL, -102, kS},
                                {kResetBinds, kR, 54, kS},                    {kPrevPage, kR, -50, kS},
                                {kToMain, kR, -102, kS}};
const Item kGameItems[] = {{kLanguage, kL, 54, kS}, {kSkin, kL, 2, kS},   {kRestart, kL, -50, kS},
                           {kExport, kR, 54, kS},   {kImport, kR, 2, kS}, {kToMain, kR, -50, kS}};

struct PageItems {
    const Item* items;
    int count;
};
template <int N>
constexpr PageItems Of(const Item (&a)[N]) { return {a, N}; }
PageItems ItemsOf(Page p) {
    switch (p) {
    case Page::Main: return Of(kMainItems);
    case Page::Graphics: return Of(kGraphicsItems);
    case Page::Audio: return Of(kAudioItems);
    case Page::Controls1: return Of(kControls1Items);
    case Page::Controls2: return Of(kControls2Items);
    case Page::Game: return Of(kGameItems);
    }
    return Of(kMainItems);
}

constexpr int kPool = 10;
PortMenuButton* sSettings = nullptr;
PortMenuButton* sPool[kPool] = {};
constexpr int kRoundPool = 6;
PortMenuButton* sRoundPool[kRoundPool] = {};
PortMenuButton* sSlot[kPool] = {};  // the page's item i -> its button (set by ShowPage)
Page sPage = Page::Main;
enum class Screen { Opening, Editing, Applying, Capturing, Switching, Closing };
Screen sScreen = Screen::Opening;
int sChosen = -1;  // the pool slot
Page sNextPage = Page::Main;
char sNote[kPool][48];  // a one-time message on a button (until the page is shown again)
char sLanguageAtStart[32];
char sSkinAtStart[64];
bool sStartKept = false;

// "us_spanish" -> "Us Spanish"
void LanguageLabel(const char* name, char* out, int size) {
    int i = 0;
    bool word = true;
    for (; name[i] && i < size - 1; ++i) {
        const char c = name[i] == '_' ? ' ' : name[i];
        out[i] = word ? (char)toupper((unsigned char)c) : c;
        word = c == ' ';
    }
    out[i] = 0;
}
const char* CurrentLanguage() {
    if (port_pc_language()[0]) return port_pc_language();
    const int running = port_language_running();
    return port_language_name(running >= 0 ? running : 0);
}

void TextOf(int id, char* text, int size) {
    char a[24], b[24];
    switch (id) {
    case kGoGraphics: snprintf(text, size, "Graphics"); return;
    case kGoAudio: snprintf(text, size, "Audio"); return;
    case kGoControls: snprintf(text, size, "Controls"); return;
    case kGoGame: snprintf(text, size, "Game"); return;
    case kClose: case kToMain: snprintf(text, size, "Back"); return;
    case kRes: snprintf(text, size, "Resolution: %s", port_pc_res_name(port_pc_res_index())); return;
    case kFsr: snprintf(text, size, port_pc_fsr() ? "FSR: On" : "FSR: Off"); return;
    case kFxaa: snprintf(text, size, port_pc_fxaa() ? "FXAA: On" : "FXAA: Off"); return;
    case kMsaa:
        if (port_pc_msaa_max() == 1) snprintf(text, size, "MSAA: N/A");
        else if (port_pc_msaa() <= 1) snprintf(text, size, "MSAA: Off");
        else snprintf(text, size, "MSAA: %dx", port_pc_msaa());
        return;
    case kFps: snprintf(text, size, port_pc_fps_counter() ? "FPS counter: On" : "FPS counter: Off"); return;
    case kVsync: snprintf(text, size, port_pc_vsync() ? "VSync: On" : "VSync: Off"); return;
    case kFull: snprintf(text, size, port_pc_fullscreen() ? "Display: Fullscreen" : "Display: Window"); return;
    case kVolMaster: snprintf(text, size, "Master: %d%%", port_pc_volume(PORT_PC_VOL_MASTER)); return;
    case kVolMusic: snprintf(text, size, "Music: %d%%", port_pc_volume(PORT_PC_VOL_MUSIC)); return;
    case kVolSfx: snprintf(text, size, "Effects: %d%%", port_pc_volume(PORT_PC_VOL_SFX)); return;
    case kVolMasterDown: case kVolMusicDown: case kVolSfxDown: snprintf(text, size, "-"); return;
    case kVolMasterUp: case kVolMusicUp: case kVolSfxUp: snprintf(text, size, "+"); return;
    case kFpsLimit: snprintf(text, size, "FPS: %d", port_pc_fps_limit()); return;
    case kGyro: snprintf(text, size, port_pc_gyro() ? "Gyroscope: On" : "Gyroscope: Off"); return;
    case kResetBinds: snprintf(text, size, "Reset controls"); return;
    case kNextPage: snprintf(text, size, "More >"); return;
    case kPrevPage: snprintf(text, size, "< Previous"); return;
    case kLanguage: {
        char label[32];
        LanguageLabel(CurrentLanguage(), label, sizeof label);
        snprintf(text, size, "Language: %s", label);
        return;
    }
    case kSkin: snprintf(text, size, "Skin: %s", port_pc_skin()[0] ? port_pc_skin() : "Mario"); return;
    case kRestart:
        snprintf(text, size, strcmp(CurrentLanguage(), sLanguageAtStart) || strcmp(port_pc_skin(), sSkinAtStart) ? "Restart to apply"
                                                                                                                 : "Restart game");
        return;
    case kExport: snprintf(text, size, "Export saves (.zip)"); return;
    case kImport: snprintf(text, size, "Import saves (.zip)"); return;
    default: break;
    }
    if (id >= kBindFirst && id <= kBindLast) {
        const int act = id - kBindFirst;
        if (port_pc_capture_active() == act) {
            snprintf(text, size, "Press a key/button");
            return;
        }
        port_pc_input_name(port_pc_bind_key(act), 0, a, sizeof a);
        port_pc_input_name(port_pc_bind_pad(act), 1, b, sizeof b);
        snprintf(text, size, "%s: %s | %s", port_pc_action_name(act), a, b);
        return;
    }
    text[0] = 0;
}

void Texts() {
    const PageItems page = ItemsOf(sPage);
    char text[64];
    for (int i = 0; i < page.count; ++i) {
        if (sNote[i][0]) snprintf(text, sizeof text, "%s", sNote[i]);
        else TextOf(page.items[i].id, text, sizeof text);
        sSlot[i]->SetText(text);
    }
}

void ShowPage(Page p) {
    sPage = p;
    const PageItems page = ItemsOf(p);
    for (int i = 0; i < kPool; ++i) sNote[i][0] = 0;
    int pill = 0, round = 0;
    for (int i = 0; i < page.count; ++i) {
        sSlot[i] = page.items[i].scaleX > 0.0f ? sRoundPool[round++] : sPool[pill++];
        sSlot[i]->Place(page.items[i].x, page.items[i].y, page.items[i].scale, page.items[i].scaleX);
    }
    Texts();
    for (int i = 0; i < page.count; ++i) sSlot[i]->Show();
}
void HideAll() {
    for (PortMenuButton* b : sPool)
        if (b) b->Hide();
    for (PortMenuButton* b : sRoundPool)
        if (b) b->Hide();
}
bool AllGone() {
    for (PortMenuButton* b : sPool)
        if (b && !b->Gone()) return false;
    for (PortMenuButton* b : sRoundPool)
        if (b && !b->Gone()) return false;
    return true;
}

void StepVolume(int which, int delta, bool wrap) {
    int v = port_pc_volume(which) + delta;
    const int top = port_pc_volume_max(which);
    if (wrap && v > top) v = 0;
    port_pc_set_volume(which, v);
}

void NextLanguage() {
    const int count = port_language_count();
    if (count <= 0) return;
    int at = 0;
    for (int i = 0; i < count; ++i)
        if (!strcmp(port_language_name(i), CurrentLanguage())) at = i;
    port_pc_set_language(port_language_name((at + 1) % count));
}

// What a press does. Returns the page to go to (or the current one to stay); sets `capture` / `close`.
Page Activate(int slot, bool& capture, bool& close) {
    const int id = ItemsOf(sPage).items[slot].id;
    capture = close = false;
    switch (id) {
    case kGoGraphics: return Page::Graphics;
    case kGoAudio: return Page::Audio;
    case kGoControls: return Page::Controls1;
    case kGoGame: return Page::Game;
    case kClose: close = true; return sPage;
    case kToMain: return Page::Main;
    case kNextPage: return Page::Controls2;
    case kPrevPage: return Page::Controls1;
    case kRes: port_pc_set_res_index((port_pc_res_index() + 1) % port_pc_res_count()); break;
    case kFsr: port_pc_set_fsr(!port_pc_fsr()); break;
    case kFxaa: port_pc_set_fxaa(!port_pc_fxaa()); break;
    case kMsaa:
        if (port_pc_msaa_max() > 1) port_pc_next_msaa();
        break;
    case kFps: port_pc_set_fps_counter(!port_pc_fps_counter()); break;
    case kVsync: port_pc_set_vsync(!port_pc_vsync()); break;
    case kFull: port_pc_set_fullscreen(!port_pc_fullscreen()); break;
    case kFpsLimit: port_pc_set_fps_limit(port_pc_fps_limit() == 60 ? 30 : 60); break;
    case kVolMaster: StepVolume(PORT_PC_VOL_MASTER, 10, true); break;
    case kVolMasterDown: StepVolume(PORT_PC_VOL_MASTER, -10, false); break;
    case kVolMasterUp: StepVolume(PORT_PC_VOL_MASTER, 10, false); break;
    case kVolMusic: StepVolume(PORT_PC_VOL_MUSIC, 10, true); break;
    case kVolMusicDown: StepVolume(PORT_PC_VOL_MUSIC, -10, false); break;
    case kVolMusicUp: StepVolume(PORT_PC_VOL_MUSIC, 10, false); break;
    case kVolSfx: StepVolume(PORT_PC_VOL_SFX, 10, true); break;
    case kVolSfxDown: StepVolume(PORT_PC_VOL_SFX, -10, false); break;
    case kVolSfxUp: StepVolume(PORT_PC_VOL_SFX, 10, false); break;
    case kGyro: port_pc_set_gyro(!port_pc_gyro()); break;
    case kResetBinds: port_pc_reset_binds(); break;
    case kLanguage: NextLanguage(); break;
    case kSkin: port_pc_next_skin(); break;
    case kRestart: port_pc_restart(); break;
    case kExport: {
        const int r = port_pc_export_saves();
        snprintf(sNote[slot], sizeof sNote[slot], "%s", r > 0 ? "Saves exported" : r < 0 ? "Export failed" : "");
        break;
    }
    case kImport: {
        const int r = port_pc_import_saves();
        if (r > 0) port_pc_restart();  // (the game holds the old saves in memory: it starts again to load the imported ones)
        snprintf(sNote[slot], sizeof sNote[slot], "%s", r < 0 ? "Import failed" : "");
        break;
    }
    default:
        if (id >= kBindFirst && id <= kBindLast) {
            port_pc_capture_begin(id - kBindFirst);
            capture = true;
        }
        break;
    }
    return sPage;
}
}  // namespace

void port_pc_menu_init(LayoutActor* pauseMenu) {
    PlaceButtons(pauseMenu, 32.0f, 0.8f);  // Back 41.6, Quit -28
    sSettings = new PortMenuButton(-88.0f, 0.8f);
    sSettings->initWithoutIter();
    for (int i = 0; i < kPool; ++i) {
        sPool[i] = new PortMenuButton(0.0f, kS);
        sPool[i]->initWithoutIter();
    }
    for (int i = 0; i < kRoundPool; ++i) {
        sRoundPool[i] = new PortMenuButton(0.0f, kRound, 0.0f, true);
        sRoundPool[i]->initWithoutIter();
    }
}

void port_pc_menu_appear() {
    if (!sSettings) return;
    if (!sStartKept) {  // (what the game started with: a change shows "Restart to apply")
        sStartKept = true;
        snprintf(sLanguageAtStart, sizeof sLanguageAtStart, "%s", CurrentLanguage());
        snprintf(sSkinAtStart, sizeof sSkinAtStart, "%s", port_pc_skin());
    }
    sSettings->SetText("Settings");
    sSettings->Show();
}

bool port_pc_menu_select() {
    if (!sSettings || MR::isDead(sSettings)) return false;
    if (sSettings->mCtrl->isPointingTrigger()) MR::startSystemSE("SE_SY_SELECT_PAUSE_ITEM", -1, -1);
    return sSettings->mCtrl->trySelect();
}

bool port_pc_menu_settings_step(bool first) {
    if (!sSettings) return true;
    if (first) {
        sScreen = Screen::Opening;
        sChosen = -1;
    }
    const PageItems page = ItemsOf(sPage);
    switch (sScreen) {
    case Screen::Opening:
        if (sSettings->mCtrl->isTimingForSelectedSe()) MR::startSystemSE("SE_SY_FILE_SEL_UPPER_DECIDE", -1, -1);
        if (sSettings->mCtrl->isDecidedWait()) {
            sSettings->Hide();
            ShowPage(Page::Main);
            sScreen = Screen::Editing;
        }
        return false;
    case Screen::Editing:
        for (int i = 0; i < page.count; ++i) {
            if (sSlot[i]->mCtrl->isPointingTrigger()) MR::startSystemSE("SE_SY_SELECT_PAUSE_ITEM", -1, -1);
        }
        for (int i = 0; i < page.count; ++i) {
            if (sSlot[i]->mCtrl->trySelect()) {
                sChosen = i;
                sScreen = Screen::Applying;
                return false;
            }
        }
        if (MR::testCorePadTriggerB(0) || MR::testCorePadTriggerPlus(0)) {  // (B or + goes back a page, from the first one out)
            MR::startSystemSE("SE_SY_PAUSE_OFF", -1, -1);
            HideAll();
            if (sPage == Page::Main) {
                sScreen = Screen::Closing;
            } else {
                sNextPage = sPage == Page::Controls2 ? Page::Controls1 : Page::Main;
                sScreen = Screen::Switching;
            }
        }
        return false;
    case Screen::Applying: {
        ButtonPaneController* c = sSlot[sChosen]->mCtrl;
        const int id = page.items[sChosen].id;
        const bool back = id == kClose || id == kToMain;
        if (c->isTimingForSelectedSe()) MR::startSystemSE(back ? "SE_SY_PAUSE_OFF" : "SE_SY_FILE_SEL_UPPER_DECIDE", -1, -1);
        if (!c->isDecidedWait()) return false;
        bool capture, close;
        const Page next = Activate(sChosen, capture, close);
        if (close) {
            HideAll();
            sScreen = Screen::Closing;
        } else if (next != sPage) {
            HideAll();
            sNextPage = next;
            sScreen = Screen::Switching;
        } else {
            Texts();
            if (capture) {
                sScreen = Screen::Capturing;
            } else {
                c->forceToWait();  // (the button is ready for the next press)
                sScreen = Screen::Editing;
            }
        }
        return false;
    }
    case Screen::Capturing:
        if (port_pc_capture_poll() == 0) return false;
        MR::startSystemSE("SE_SY_FILE_SEL_UPPER_DECIDE", -1, -1);
        Texts();
        sSlot[sChosen]->mCtrl->forceToWait();
        sScreen = Screen::Editing;
        return false;
    case Screen::Switching:
        if (!AllGone()) return false;
        ShowPage(sNextPage);
        sScreen = Screen::Editing;
        return false;
    case Screen::Closing:
        if (!AllGone()) return false;
        sPage = Page::Main;
        return true;
    }
    return true;
}

void port_pc_menu_force_wait() {
    if (sSettings && !MR::isDead(sSettings) && !sSettings->mCtrl->isHidden()) sSettings->mCtrl->forceToWait();
}

void port_pc_menu_hide() {
    if (port_pc_capture_active() >= 0) port_pc_capture_feed(-1, -1, 1);
    if (sSettings) sSettings->Hide();
    HideAll();
}

void port_pc_menu_kill() {
    if (port_pc_capture_active() >= 0) port_pc_capture_feed(-1, -1, 1);
    if (sSettings && !MR::isDead(sSettings)) sSettings->kill();
    for (PortMenuButton* b : sPool)
        if (b && !MR::isDead(b)) b->kill();
    for (PortMenuButton* b : sRoundPool)
        if (b && !MR::isDead(b)) b->kill();
}
