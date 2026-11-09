#include "Game/Screen/StarPointerController.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <JSystem/JUtility/JUTVideo.hpp>

namespace {
    // static const _32 hMarginH =
    // static const _32 hMarginV =
    // static const _32 hForcePointerOutVel =
    static const s32 hIsOutScreenCounter = 35;
    // static const _32 hNearValue =
    // static const _32 hDrawDebugRadius =
};  // namespace

DpdInfo::DpdInfo() : mPos(0.0f, 0.0f), mInScreen(false), mDrawReady(false), mZDepth(0), mViewDistZ(0.0f) {
}

StarPointerController::StarPointerController()
    : mPadChannel(-1), mScreenPos(0.0f, 0.0f), mOutScreenTime(0), mPastPosition(nullptr), mPastPointNum(0), mNextPastPosition(nullptr),
      mNextPastPointNum(0), mScreenDist(0.0f), mScreenSpeed(0.0f), mScreenVel(0.0f, 0.0f), mWorldVel(0.0f, 0.0f, 0.0f), mWorldPos(0.0f, 0.0f, 0.0f) {
    // FIXME: TVec2 emitting default ctor in new[], needs to be a null ctor.
    mPastPosition = new TVec2f[16];
    mNextPastPosition = new TVec2f[16];
}

bool StarPointerController::isOutScreenLong() const {
    return mOutScreenTime > ::hIsOutScreenCounter;
}

void StarPointerController::initAndSetPort(s32 padChannel) {
    mPadChannel = padChannel;
}

void StarPointerController::movement(const f32* pProjectionParams, const f32* pViewportParams) {
    storeDataFromCallback(pProjectionParams, pViewportParams);
    storePastPointingData();
    updateDpdInfo();
}

void StarPointerController::storeDataFromCallback(const f32* pProjectionParams, const f32* pViewportParams) {
    TVec3f v1(mInfo.mPos.x, mInfo.mPos.y, static_cast< f32 >(mInfo.mZDepth));

    bool ready = mInfo.mDrawReady == true;
#if defined(TARGET_PC) && !defined(__vita__)
    // The map ray below needs no depth read back, so it runs every frame: waiting for the draw-sync callback left the
    // pointer's world position stale for many frames (shots kept going to where the cursor was some time before).
    ready = ready || MR::isExistSceneObj(SceneObj_CollisionDirector);
#endif
    if (ready) {
        if (mInfo.mDrawReady == true) {
            TDDraw::invProject(&mWorldPos, v1, MR::getStarPointerViewMtx(), pProjectionParams, pViewportParams, false);
        }
#if defined(TARGET_PC) && !defined(__vita__)
        // The port cannot read the EFB depth back (GXPeekZ reports the far
        // plane), so in scenes with collision, what the pointer is on comes
        // from casting its ray against the map instead: the controller's aim
        // ray in the VR diorama, else the camera ray through the cursor.
        if (MR::isExistSceneObj(SceneObj_CollisionDirector)) {
            f32 o[3], d[3];
            bool vrRay = mPadChannel == WPAD_CHAN0 && port_vr_pointer_ray(o, d);
            TVec3f origin, dir;
            if (vrRay) {
                origin.set(o[0], o[1], o[2]);
                dir.set(d[0], d[1], d[2]);
            } else {
                // The camera ray through the cursor, from the current camera (the projection and viewport saved with the
                // draw-sync token can be another pass's, which left the target stuck at one place while standing).
                origin = MR::getCamPos();
                TVec3f through;
                MR::calcWorldPositionFromScreen(&through, mInfo.mPos, -1.0f);
                dir = through - origin;
                MR::normalizeOrZero(&dir);
            }
            TVec3f hit;
            if (!dir.isZero() && MR::getFirstPolyOnLineToMap(&hit, nullptr, origin, dir * 30000.0f)) {
                mWorldPos = hit;
            } else if (!dir.isZero()) {
                mWorldPos = origin + dir * 30000.0f;
            }
            // The VR laser reaches as far as star bits fly.
            TVec3f aimEnd;
            f32 reach;
            if (vrRay && MR::calcVrPointerAimEnd(&aimEnd, &reach, mPadChannel)) {
                port_vr_pointer_reach(reach);
            }
        }
#endif
        mInfo.mViewDistZ = calcViewDistanceZ(mWorldPos, MR::getStarPointerViewMtx());
#if defined(TARGET_PC) && !defined(__vita__)
        if (mPadChannel == WPAD_CHAN0) {
            port_vr_pointer_depth(mInfo.mViewDistZ);
        }
#endif
    }

    mPastInfo = mInfo;
    if (mPastInfo.mInScreen) {
        mOutScreenTime = 0;
    } else {
        mOutScreenTime++;
    }

    mInfo.mDrawReady = false;
}

void StarPointerController::storePastPointingData() {
    mPastPointNum = mNextPastPointNum;
    for (s32 idx = 0; idx < mNextPastPointNum; idx++) {
        mPastPosition[idx] = mNextPastPosition[idx];
    }

    s32 pastCount = MR::min(MR::getCorePadEnablePastCount(mPadChannel), 16);
    mNextPastPointNum = pastCount;

    for (s32 idx = 0; idx < pastCount; idx++) {
        calcPastPointingPosOnScreen(&mNextPastPosition[idx], idx);
    }
}

void StarPointerController::calcPastPointingPosOnScreen(TVec2f* pPos, s32 idx) const {
    MR::getCorePadPastPointingPos(pPos, idx, mPadChannel);
    pPos->x += 1.0f;
    pPos->y += 1.0f;
    pPos->x *= MR::getScreenWidth() * 0.5f;
    pPos->y *= static_cast< s32 >(JUTVideo::getManager()->getEfbHeight()) * 0.5f;
}

void StarPointerController::updateDpdInfo() {
    if (!MR::isCorePadPointInScreen(mPadChannel)) {
        TVec2f v1;
        v1.x = mInfo.mPos.x;
        v1.y = mInfo.mPos.y;
        mInfo.mInScreen = false;
        mScreenPos.set(v1);
        updateAdditionalInfo();
    } else {
        mScreenPos.set(mInfo.mPos);
        mInfo.mInScreen = true;
        mInfo.mPos.set(mNextPastPosition[0]);
        mNotInScreen = StarPointerFunction::forceInsideScreenEdge(&mInfo.mPos);
        mScreenDist = MR::getCorePadDistanceToDisplay(mPadChannel);
        updateAdditionalInfo();
    }
}

void StarPointerController::updateAdditionalInfo() {
    mScreenVel.set(mInfo.mPos);
    mScreenVel.sub(mScreenPos);
    mScreenSpeed = mScreenVel.length();
    mWorldVel.set< f32 >(mScreenVel.x, -mScreenVel.y, 0.0f);
    TPos3f mtx;
    PSMTXCopy(MR::getStarPointerViewMtx(), mtx);
    PSMTXInverse(mtx, mtx);
    PSMTXMultVecSR(mtx, &mWorldVel, &mWorldVel);
    MR::normalizeOrZero(&mWorldVel);
}

f32 StarPointerController::calcViewDistanceZ(const TVec3f& rPos, MtxPtr pMtx) {
    return MR::abs(rPos.x * pMtx[2][0] + rPos.y * pMtx[2][1] + rPos.z * pMtx[2][2] + pMtx[2][3]);
}

// NOTE: this is a debug symbol, TVec2f::sub is included here just to emit TVec symbol
void StarPointerController::drawDebug3D() const {
    TVec2f v1 = mScreenVel;
    v1.sub(mScreenPos);
}
