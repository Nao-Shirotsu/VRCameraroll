#include "LaserController.h"
#include <cstdio>
#include <cstring>

// ────────────────────────────────────────────
// ポインターライン用テクスチャ（4x256 px 赤ライン）
// ────────────────────────────────────────────
static std::vector<uint8_t> MakePointerTexture() {
    constexpr int W = 4, H = 256;
    std::vector<uint8_t> pixels(W * H * 4);
    for (int i = 0; i < W * H; ++i) {
        pixels[i*4+0] = 255; pixels[i*4+1] = 60;
        pixels[i*4+2] = 60;  pixels[i*4+3] = 220;
    }
    return pixels;
}

LaserController::LaserController() {
    // global overlay input blocking を無効化（AFK 対策）
    vr::EVRSettingsError se = vr::VRSettingsError_None;
    vr::VRSettings()->SetBool("steamvr", "globalInputOverlaysEnabled", false, &se);

    // ポインターライン overlay
    vr::VROverlay()->CreateOverlay("camera_roll.ptr_line", "Pointer Line", &m_ptr_line);
    auto ptex = MakePointerTexture();
    vr::VROverlay()->SetOverlayRaw(m_ptr_line, ptex.data(), 4, 256, 4);
    vr::VROverlay()->SetOverlayWidthInMeters(m_ptr_line, 0.004f);
    vr::VROverlay()->ShowOverlay(m_ptr_line);

    // デフォルト回転行列（キャリブレーション済み値）
    m_rot.m[0][0]=-0.996330f; m_rot.m[0][1]=-0.010861f; m_rot.m[0][2]=-0.084879f;
    m_rot.m[1][0]=-0.074455f; m_rot.m[1][1]=+0.598924f; m_rot.m[1][2]=+0.797335f;
    m_rot.m[2][0]=+0.042176f; m_rot.m[2][1]=+0.800731f; m_rot.m[2][2]=-0.597535f;
}

LaserController::~LaserController() {
    if (m_ptr_line != vr::k_ulOverlayHandleInvalid)
        vr::VROverlay()->DestroyOverlay(m_ptr_line);
}

void LaserController::UpdatePointerTransform(vr::TrackedDeviceIndex_t right_hand) {
    if (right_hand == vr::k_unTrackedDeviceIndexInvalid) return;
    vr::HmdMatrix34_t t = {};
    t.m[0][0]=m_rot.m[0][0]; t.m[0][1]=m_rot.m[0][1]; t.m[0][2]=m_rot.m[0][2]; t.m[0][3]=m_tx;
    t.m[1][0]=m_rot.m[1][0]; t.m[1][1]=m_rot.m[1][1]; t.m[1][2]=m_rot.m[1][2]; t.m[1][3]=m_ty;
    t.m[2][0]=m_rot.m[2][0]; t.m[2][1]=m_rot.m[2][1]; t.m[2][2]=m_rot.m[2][2]; t.m[2][3]=m_tz;
    vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(m_ptr_line, right_hand, &t);
}

vr::VROverlayIntersectionParams_t LaserController::BuildIntersectionParams(
    const vr::TrackedDevicePose_t& pose) const
{
    auto& m = pose.mDeviceToAbsoluteTracking.m;
    vr::VROverlayIntersectionParams_t params;
    params.eOrigin = vr::TrackingUniverseStanding;

    // レーザー原点 = コントローラー空間の ptr_t をワールド空間に変換
    params.vSource = {
        m[0][0]*m_tx + m[0][1]*m_ty + m[0][2]*m_tz + m[0][3],
        m[1][0]*m_tx + m[1][1]*m_ty + m[1][2]*m_tz + m[1][3],
        m[2][0]*m_tx + m[2][1]*m_ty + m[2][2]*m_tz + m[2][3]
    };

    // 方向 = ptr_rot の第3列（ローカル Z 軸）をワールド空間に変換
    params.vDirection = {
        m[0][0]*m_rot.m[0][2] + m[0][1]*m_rot.m[1][2] + m[0][2]*m_rot.m[2][2],
        m[1][0]*m_rot.m[0][2] + m[1][1]*m_rot.m[1][2] + m[1][2]*m_rot.m[2][2],
        m[2][0]*m_rot.m[0][2] + m[2][1]*m_rot.m[1][2] + m[2][2]*m_rot.m[2][2]
    };

    return params;
}

TriggerableButton* LaserController::HitTest(
    const std::vector<TriggerableButton*>& buttons,
    const vr::VROverlayIntersectionParams_t& params,
    vr::VROverlayIntersectionResults_t* out) const
{
    for (auto* btn : buttons) {
        if (btn && btn->Intersected(params, out))
            return btn;
    }
    return nullptr;
}

bool LaserController::IsTriggerPressed(vr::IVRSystem* vr_system,
                                        vr::TrackedDeviceIndex_t right_hand) const
{
    if (right_hand == vr::k_unTrackedDeviceIndexInvalid) return false;
    vr::VRControllerState_t ctrl = {};
    return vr_system->GetControllerState(right_hand, &ctrl, sizeof(ctrl))
        && (ctrl.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) != 0;
}

void LaserController::PrintParams() const {
    printf("ptr_rot:\n");
    for (int r = 0; r < 3; ++r)
        printf("  [%.6f, %.6f, %.6f]\n",
            m_rot.m[r][0], m_rot.m[r][1], m_rot.m[r][2]);
    printf("ptr_t: [%.6f, %.6f, %.6f]\n", m_tx, m_ty, m_tz);
}
