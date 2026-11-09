// The pause menu's Settings button and the settings screen (see port/pc_menu.h).
//
// Layout (PauseMenu.arc, units of the 608 x 456 layout, y up): the top bar ends at y 77, the bottom bar starts at y -123.
// The game's two buttons sit under the pane "BackButton" (y 12): Back at +12, Quit 85 below. With the buttons at 80% and
// Quit 75 below Back, three fit between the bars: Back 41.6, Quit -28, Settings -88. The settings screen's eight buttons
// (55%) are in two columns at x -150 and +150, rows 54, 2, -50, -102, while Back and Quit are away.
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "nw4r/lyt/pane.h"

#include "port/pc_menu.h"
#include "port/pc_settings.h"
#include "port/port.h"

namespace {
constexpr f32 kQuitStep = 75.0f;  // Quit's distance under Back (85 in the layout)

void PlaceButtons(LayoutActor* actor, f32 backButtonY, f32 scale, f32 x = 0.0f) {
    if (nw4r::lyt::Pane* p = MR::getPane(actor, "BackButton")) {
        p->mTranslate.x = x;
        p->mTranslate.y = backButtonY;
        p->mScale.x = scale;
        p->mScale.y = scale;
    }
    if (nw4r::lyt::Pane* p = MR::getPane(actor, "BackDown")) p->mTranslate.y = -kQuitStep;
}

// One button: the PauseMenu layout again, showing only its Quit button, centred at `center`.
class PortMenuButton : public LayoutActor {
public:
    PortMenuButton(f32 center, f32 scale, f32 x = 0.0f)
        : LayoutActor("PortMenuButton", true), mCtrl(nullptr), mCenter(center), mScale(scale), mX(x) {}

    virtual void init(const JMapInfoIter&) {
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
        wchar_t wide[64];
        int i = 0;
        for (; text[i] && i < 63; ++i) wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(text[i]));
        wide[i] = 0;
        MR::setTextBoxMessageRecursive(this, "NBackDown", wide);
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
    bool mLeaving = false;
};

enum { kRes, kFsr, kFxaa, kMsaa, kFps, kVsync, kFull, kBack, kOptions };
PortMenuButton* sSettings = nullptr;
PortMenuButton* sOpt[kOptions] = {};
enum class Screen { Opening, Editing, Applying, Closing };
Screen sScreen = Screen::Opening;
int sChosen = -1;

void OptionTexts() {
    char text[64];
    snprintf(text, sizeof text, "Resolution: %s", port_pc_res_name(port_pc_res_index()));
    sOpt[kRes]->SetText(text);
    sOpt[kFsr]->SetText(port_pc_fsr() ? "FSR: On" : "FSR: Off");
    sOpt[kFxaa]->SetText(port_pc_fxaa() ? "FXAA: On" : "FXAA: Off");
    if (port_pc_msaa_max() == 1) snprintf(text, sizeof text, "MSAA: N/A");
    else if (port_pc_msaa() <= 1) snprintf(text, sizeof text, "MSAA: Off");
    else snprintf(text, sizeof text, "MSAA: %dx", port_pc_msaa());
    sOpt[kMsaa]->SetText(text);
    sOpt[kFps]->SetText(port_pc_fps_counter() ? "FPS counter: On" : "FPS counter: Off");
    sOpt[kVsync]->SetText(port_pc_vsync() ? "VSync: On" : "VSync: Off");
    sOpt[kFull]->SetText(port_pc_fullscreen() ? "Display: Fullscreen" : "Display: Window");
    sOpt[kBack]->SetText("Back");
}

void Apply(int option) {
    switch (option) {
    case kRes: port_pc_set_res_index((port_pc_res_index() + 1) % port_pc_res_count()); break;
    case kFsr: port_pc_set_fsr(!port_pc_fsr()); break;
    case kFxaa: port_pc_set_fxaa(!port_pc_fxaa()); break;
    case kMsaa:
        if (port_pc_msaa_max() > 1) port_pc_next_msaa();
        break;
    case kFps: port_pc_set_fps_counter(!port_pc_fps_counter()); break;
    case kVsync: port_pc_set_vsync(!port_pc_vsync()); break;
    case kFull: port_pc_set_fullscreen(!port_pc_fullscreen()); break;
    default: break;
    }
    OptionTexts();
}
}  // namespace

void port_pc_menu_init(LayoutActor* pauseMenu) {
    PlaceButtons(pauseMenu, 32.0f, 0.8f);  // Back 41.6, Quit -28
    sSettings = new PortMenuButton(-88.0f, 0.8f);
    sSettings->initWithoutIter();
    // (left: Resolution, FSR, FXAA, MSAA; right: FPS counter, VSync, Display, Back)
    static const f32 kCenters[kOptions] = {54.0f, 2.0f, -50.0f, -102.0f, 54.0f, 2.0f, -50.0f, -102.0f};
    static const f32 kX[kOptions] = {-150.0f, -150.0f, -150.0f, -150.0f, 150.0f, 150.0f, 150.0f, 150.0f};
    for (int i = 0; i < kOptions; ++i) {
        sOpt[i] = new PortMenuButton(kCenters[i], 0.55f, kX[i]);
        sOpt[i]->initWithoutIter();
    }
}

void port_pc_menu_appear() {
    if (!sSettings) return;
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
    switch (sScreen) {
    case Screen::Opening:
        if (sSettings->mCtrl->isTimingForSelectedSe()) MR::startSystemSE("SE_SY_FILE_SEL_UPPER_DECIDE", -1, -1);
        if (sSettings->mCtrl->isDecidedWait()) {
            sSettings->Hide();
            OptionTexts();
            for (PortMenuButton* b : sOpt) b->Show();
            sScreen = Screen::Editing;
        }
        return false;
    case Screen::Editing:
        for (int i = 0; i < kOptions; ++i) {
            if (sOpt[i]->mCtrl->isPointingTrigger()) MR::startSystemSE("SE_SY_SELECT_PAUSE_ITEM", -1, -1);
        }
        for (int i = 0; i < kOptions; ++i) {
            if (sOpt[i]->mCtrl->trySelect()) {
                sChosen = i;
                sScreen = Screen::Applying;
                return false;
            }
        }
        if (MR::testCorePadTriggerB(0) || MR::testCorePadTriggerPlus(0)) {  // (B or + leaves, as Back)
            MR::startSystemSE("SE_SY_PAUSE_OFF", -1, -1);
            for (PortMenuButton* b : sOpt) b->Hide();
            sScreen = Screen::Closing;
        }
        return false;
    case Screen::Applying: {
        ButtonPaneController* c = sOpt[sChosen]->mCtrl;
        if (c->isTimingForSelectedSe()) MR::startSystemSE(sChosen == kBack ? "SE_SY_PAUSE_OFF" : "SE_SY_FILE_SEL_UPPER_DECIDE", -1, -1);
        if (!c->isDecidedWait()) return false;
        if (sChosen == kBack) {
            for (PortMenuButton* b : sOpt) b->Hide();
            sScreen = Screen::Closing;
            return false;
        }
        Apply(sChosen);
        c->forceToWait();  // (the button is ready for the next press)
        sScreen = Screen::Editing;
        return false;
    }
    case Screen::Closing:
        for (PortMenuButton* b : sOpt)
            if (!b->Gone()) return false;
        return true;
    }
    return true;
}

void port_pc_menu_force_wait() {
    if (sSettings && !MR::isDead(sSettings) && !sSettings->mCtrl->isHidden()) sSettings->mCtrl->forceToWait();
}

void port_pc_menu_hide() {
    if (sSettings) sSettings->Hide();
    for (PortMenuButton* b : sOpt)
        if (b) b->Hide();
}

void port_pc_menu_kill() {
    if (sSettings && !MR::isDead(sSettings)) sSettings->kill();
    for (PortMenuButton* b : sOpt)
        if (b && !MR::isDead(b)) b->kill();
}
