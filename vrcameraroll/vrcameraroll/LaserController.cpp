#include "LaserController.h"
#include <cstdio>
#include <cstring>

// ────────────────────────────────────────────
// ポインターライン用テクスチャ（4x1000 px 赤ライン）
// ────────────────────────────────────────────
static std::vector<uint8_t> MakePointerTexture() {
    constexpr int W = 4, H = 1000; // aspect 250:1 → 太さ0.002m × 250 = 0.5m
    std::vector<uint8_t> pixels(W * H * 4);
    for (int i = 0; i < W * H; ++i) {
        pixels[i*4+0] = 255; pixels[i*4+1] = 60;
        pixels[i*4+2] = 60;  pixels[i*4+3] = 220;
    }
    return pixels;
}

// ────────────────────────────────────────────
// ヒットドット用テクスチャ（64x64 px 白円）
// ────────────────────────────────────────────
static std::vector<uint8_t> MakeHitDotTexture() {
    constexpr int W = 64, H = 64;
    std::vector<uint8_t> pixels(W * H * 4, 0);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float dx = x - 31.5f, dy = y - 31.5f;
            if (dx*dx + dy*dy <= 28.0f*28.0f) {
                int i = (y*W + x)*4;
                pixels[i+0] = 255; pixels[i+1] = 255;
                pixels[i+2] = 255; pixels[i+3] = 200;
            }
        }
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
    vr::VROverlay()->SetOverlayRaw(m_ptr_line, ptex.data(), 4, 1000, 4);
    vr::VROverlay()->SetOverlayWidthInMeters(m_ptr_line, 0.002f); // 太さ2mm, 長さ = 0.002 × 250 = 0.5m
    vr::VROverlay()->ShowOverlay(m_ptr_line);

    // ヒットドット overlay（最初は非表示）
    vr::VROverlay()->CreateOverlay("camera_roll.hit_dot", "Hit Dot", &m_hit_dot);
    auto dtex = MakeHitDotTexture();
    vr::VROverlay()->SetOverlayRaw(m_hit_dot, dtex.data(), 64, 64, 4);
    vr::VROverlay()->SetOverlayWidthInMeters(m_hit_dot, 0.02f);

    // デフォルト回転行列（キャリブレーション済み値）
    m_rot.m[0][0]=+0.028238f; m_rot.m[0][1]=+0.999467f; m_rot.m[0][2]=-0.016173f;
    m_rot.m[1][0]=-0.774502f; m_rot.m[1][1]=+0.032105f; m_rot.m[1][2]=+0.631752f;
    m_rot.m[2][0]=+0.631936f; m_rot.m[2][1]=-0.005313f; m_rot.m[2][2]=+0.774998f;
}

LaserController::~LaserController() {
    if (m_ptr_line != vr::k_ulOverlayHandleInvalid)
        vr::VROverlay()->DestroyOverlay(m_ptr_line);
    if (m_hit_dot != vr::k_ulOverlayHandleInvalid)
        vr::VROverlay()->DestroyOverlay(m_hit_dot);
}

void LaserController::UpdatePointerTransform(vr::TrackedDeviceIndex_t right_hand) {
    if (right_hand == vr::k_unTrackedDeviceIndexInvalid) return;
    vr::HmdMatrix34_t t = {};
    // ポインターラインは縦長 (4x256px) なので長軸 = local Y。
    // 判定レイ方向 (BuildIntersectionParams) は m_rot の列2 (Z軸) を使う。
    // 見た目と判定を一致させるため、overlay の local Y を列2、local Z を -列1 にする。
    // (列0は変えず、列1↔列2を交換して符号補正することで行列式=+1を保つ)
    // overlay 中心 = レイ原点 + 0.25m × レーザー方向 (-m_rot 列2)
    constexpr float half_len = 0.25f;
    t.m[0][0]= m_rot.m[0][0]; t.m[0][1]= m_rot.m[0][2]; t.m[0][2]=-m_rot.m[0][1]; t.m[0][3]=m_tx + half_len*(-m_rot.m[0][2]);
    t.m[1][0]= m_rot.m[1][0]; t.m[1][1]= m_rot.m[1][2]; t.m[1][2]=-m_rot.m[1][1]; t.m[1][3]=m_ty + half_len*(-m_rot.m[1][2]);
    t.m[2][0]= m_rot.m[2][0]; t.m[2][1]= m_rot.m[2][2]; t.m[2][2]=-m_rot.m[2][1]; t.m[2][3]=m_tz + half_len*(-m_rot.m[2][2]);
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
    // overlay の long axis (local Y) = 列2 の方向、レイは先端側（+Y 方向）に向けるため符号反転
    params.vDirection = {
        -(m[0][0]*m_rot.m[0][2] + m[0][1]*m_rot.m[1][2] + m[0][2]*m_rot.m[2][2]),
        -(m[1][0]*m_rot.m[0][2] + m[1][1]*m_rot.m[1][2] + m[1][2]*m_rot.m[2][2]),
        -(m[2][0]*m_rot.m[0][2] + m[2][1]*m_rot.m[1][2] + m[2][2]*m_rot.m[2][2])
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

void LaserController::UpdateHitDot(const vr::VROverlayIntersectionResults_t* res,
                                    vr::TrackedDeviceIndex_t right_hand,
                                    const vr::TrackedDevicePose_t* pose) {
    if (!res || !pose) {
        vr::VROverlay()->HideOverlay(m_hit_dot);
        return;
    }

    // vPoint (ワールド座標) → コントローラーローカル座標: R^T * (P - t)
    const auto& M = pose->mDeviceToAbsoluteTracking.m;
    float dx = res->vPoint.v[0] - M[0][3];
    float dy = res->vPoint.v[1] - M[1][3];
    float dz = res->vPoint.v[2] - M[2][3];
    float lx = M[0][0]*dx + M[1][0]*dy + M[2][0]*dz;
    float ly = M[0][1]*dx + M[1][1]*dy + M[2][1]*dz;
    float lz = M[0][2]*dx + M[1][2]*dy + M[2][2]*dz;

    // 向きはレーザーラインと同じ m_rot 基底を使用
    vr::HmdMatrix34_t t = {};
    for (int r = 0; r < 3; ++r) {
        t.m[r][0] = m_rot.m[r][0];
        t.m[r][1] = m_rot.m[r][1];
        t.m[r][2] = m_rot.m[r][2];
    }
    t.m[0][3] = lx;
    t.m[1][3] = ly;
    t.m[2][3] = lz;

    vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(
        m_hit_dot, right_hand, &t);
    vr::VROverlay()->ShowOverlay(m_hit_dot);
}

void LaserController::PrintParams() const {
    printf("ptr_rot:\n");
    for (int r = 0; r < 3; ++r)
        printf("  [%.6f, %.6f, %.6f]\n",
            m_rot.m[r][0], m_rot.m[r][1], m_rot.m[r][2]);
    printf("ptr_t: [%.6f, %.6f, %.6f]\n", m_tx, m_ty, m_tz);
}
