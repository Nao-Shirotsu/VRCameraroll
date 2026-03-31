#pragma execution_character_set("utf-8")
#include <openvr.h>
#include <Windows.h>

#include "CameraRollUI.h"
#include "LaserController.h"
#ifdef UI_ADJUST
#include "DebugUI.h"
#endif

static const char* kImageFolder = "C:\\Users\\albus\\Pictures\\VRChat\\2026-03";

int main() {
    vr::EVRInitError err = vr::VRInitError_None;
    vr::IVRSystem* vr_system = vr::VR_Init(&err, vr::VRApplication_Overlay);
    if (err != vr::VRInitError_None) return 1;

    // LaserController を先に作る:
    // コンストラクタ内で globalInputOverlaysEnabled=false を設定するため、
    // CameraRollUI の overlay 作成より前に呼ぶ必要がある。
    LaserController laser;

    CameraRollUI camera_roll;
    camera_roll.LoadImages(kImageFolder);

#ifdef UI_ADJUST
    DebugUI debug_ui(camera_roll, laser);
#endif

    bool prev_trigger = false;
    bool prev_any_hit = false;

    while (true) {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) break;

        // ── 左コントローラー取得 ──
        vr::TrackedDeviceIndex_t left_hand =
            vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);

        camera_roll.UpdateTransforms(left_hand);

#ifdef UI_ADJUST
        debug_ui.UpdateTransforms(left_hand,
            camera_roll.RotX(), camera_roll.RotY(), camera_roll.RotZ(),
            camera_roll.OffX(), camera_roll.OffY(), camera_roll.OffZ());
#endif

        // ── 右コントローラー pose 取得 ──
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
        vr_system->GetDeviceToAbsoluteTrackingPose(
            vr::TrackingUniverseStanding, 0.0f, poses, vr::k_unMaxTrackedDeviceCount);

        vr::TrackedDeviceIndex_t right_hand =
            vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);
        const bool right_valid = (right_hand != vr::k_unTrackedDeviceIndexInvalid)
                               && poses[right_hand].bPoseIsValid;

        // ── レーザーライン更新 ──
        if (right_valid)
            laser.UpdatePointerTransform(right_hand);

        // ── 全ボタン収集 ──
        auto buttons = camera_roll.Buttons();
#ifdef UI_ADJUST
        auto db = debug_ui.Buttons();
        buttons.insert(buttons.end(), db.begin(), db.end());
#endif

        // ── 交差判定 ──
        TriggerableButton* hit = nullptr;
        vr::VROverlayIntersectionResults_t hit_res = {};
        if (right_valid) {
            auto params = laser.BuildIntersectionParams(poses[right_hand]);
            hit = laser.HitTest(buttons, params, &hit_res);
        }

        laser.UpdateHitDot(
            hit ? &hit_res : nullptr,
            right_hand,
            right_valid ? &poses[right_hand] : nullptr);

        const bool any_hit = (hit != nullptr);
        if (any_hit != prev_any_hit) {
            if (any_hit) {
                // どのボタンが当たったかインデックスを特定
                int hit_idx = -1;
                for (size_t i = 0; i < buttons.size(); ++i) {
                    if (buttons[i] == hit) { hit_idx = (int)i; break; }
                }
                printf("HIT btn[%d] handle=%llu  dist=%.3fm  uv=(%.3f,%.3f)\n",
                    hit_idx, (unsigned long long)hit->Handle(),
                    hit_res.fDistance,
                    hit_res.vUVs.v[0], hit_res.vUVs.v[1]);
            } else {
                printf("MISS\n");
            }
            prev_any_hit = any_hit;
        }

        // ── トリガー入力 ──
        const bool trigger_now = right_valid && laser.IsTriggerPressed(vr_system, right_hand);
        if (trigger_now && !prev_trigger && hit) {
            hit->FireTrigger();
#ifdef UI_ADJUST
            printf("=== Button fired ===\n");
            laser.PrintParams();
            printf("camera_roll rot=(%.6f, %.6f, %.6f)  offset=(%.6f, %.6f, %.6f)\n",
                camera_roll.RotX(), camera_roll.RotY(), camera_roll.RotZ(),
                camera_roll.OffX(), camera_roll.OffY(), camera_roll.OffZ());
#endif
        }
        prev_trigger = trigger_now;

        // ── イベントポーリング ──
        vr::VREvent_t event;
        if (vr::VROverlay()->PollNextOverlayEvent(
                camera_roll.MainOverlayHandle(), &event, sizeof(event))) {
            if (event.eventType == vr::VREvent_Quit) break;
        }

        Sleep(16);
    }

    vr::VR_Shutdown();
    return 0;
}
