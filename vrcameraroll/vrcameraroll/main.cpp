#pragma execution_character_set("utf-8")
#include <openvr.h>
#include <Windows.h>

#include "CameraRollUI.h"
#include "LaserController.h"
#ifdef _DEBUG
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

#ifdef _DEBUG
    DebugUI debug_ui(camera_roll, laser);
#endif

    // ── テスト用ナビボタン（main.cpp で作成、m_btn_newer と同位置） ──
    // CameraRollUI の m_btn_newer と全く同じ位置に置く。
    // これが HIT すれば「CameraRollUI 内のボタン作成に問題あり」と確定。
    TriggerableButton test_nav_btn(
        "debug.test_nav", "TestNav",
        0.10f,  // 見つけやすいよう少し大きく
        [] { printf(">>> TEST NAV TRIGGERED! <<<\n"); }
    );
    {
        constexpr int S = 32;
        std::vector<uint8_t> green(S * S * 4);
        for (int i = 0; i < S * S; ++i) {
            green[i*4+0]=0; green[i*4+1]=255; green[i*4+2]=0; green[i*4+3]=255;
        }
        test_nav_btn.UploadTexture(green, S, S);
    }
    test_nav_btn.Show();

    // ── デバッグボタン（最小構成） ──
    // CameraRollUI の仕組みを一切使わない独立したボタン。
    // このボタンに対してレーザーを当て、HIT/TRIGGEREDが出るか確認する。
    TriggerableButton debug_btn(
        "debug.test_btn", "DEBUG",
        0.12f,
        [] { printf(">>> DEBUG BUTTON TRIGGERED! <<<\n"); }
    );
    // 目視確認用に真っ赤なテクスチャをアップロード
    {
        constexpr int S = 32;
        std::vector<uint8_t> red(S * S * 4);
        for (int i = 0; i < S * S; ++i) {
            red[i*4+0]=255; red[i*4+1]=0; red[i*4+2]=0; red[i*4+3]=255;
        }
        debug_btn.UploadTexture(red, S, S);
    }
    debug_btn.Show();

    // 起動時: 全ボタンのハンドルと可視状態を表示
    {
        auto btns = camera_roll.Buttons();
        btns.push_back(&test_nav_btn);
        btns.push_back(&debug_btn);
        printf("=== Button state (%zu buttons) ===\n", btns.size());
        for (size_t i = 0; i < btns.size(); ++i) {
            vr::VROverlayHandle_t h = btns[i]->Handle();
            bool visible = vr::VROverlay()->IsOverlayVisible(h);
            printf("  [%zu] handle=%llu  visible=%s\n",
                i, (unsigned long long)h, visible ? "YES" : "NO");
        }
        printf("==================================\n");
    }

    bool prev_trigger = false;
    bool prev_any_hit = false;

    while (true) {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) break;

        // ── 左コントローラー取得 ──
        vr::TrackedDeviceIndex_t left_hand =
            vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);

        camera_roll.UpdateTransforms(left_hand);

        // btn[0](m_btn_newer)に test_nav_btn と全く同じ transform を強制適用してヒットするか確認
        // → ヒットすれば UpdateTransforms の SetTransformTrackedDeviceRelative が効いていない証拠
        if (left_hand != vr::k_unTrackedDeviceIndexInvalid) {
            constexpr float MAIN_W_ = 0.25f, BTN_GAP_ = 0.01f, BTN_W_ = 0.05f;
            constexpr float BTN_L_X_ = -(MAIN_W_ / 2.0f + BTN_GAP_ + BTN_W_ / 2.0f);
            constexpr float SUB_Y_   = -0.03f;
            auto t_force = MakeTransform(BTN_L_X_, SUB_Y_, 0.f,
                camera_roll.RotX(), camera_roll.RotY(), camera_roll.RotZ(),
                camera_roll.OffX(), camera_roll.OffY(), camera_roll.OffZ());
            auto btns0 = camera_roll.Buttons();
            // 直接 OpenVR API を呼ぶ（TriggerableButton 経由でなく）
            auto err = vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(
                btns0[0]->Handle(), left_hand, &t_force);
            static bool once_err = true;
            if (once_err && err != vr::VROverlayError_None) {
                printf("FORCE btn[0] transform err=%d\n", (int)err);
                once_err = false;
            }
        }

        // test_nav_btn を m_btn_newer と全く同じ位置に配置
        // (BTN_L_X = -(MAIN_W/2 + BTN_GAP + BTN_W/2), SUB_Y = -0.03)
        if (left_hand != vr::k_unTrackedDeviceIndexInvalid) {
            constexpr float MAIN_W = 0.25f, BTN_GAP = 0.01f, BTN_W = 0.05f;
            constexpr float BTN_L_X = -(MAIN_W / 2.0f + BTN_GAP + BTN_W / 2.0f);
            constexpr float SUB_Y   = -0.03f;
            auto t = MakeTransform(BTN_L_X, SUB_Y, 0.f,
                camera_roll.RotX(), camera_roll.RotY(), camera_roll.RotZ(),
                camera_roll.OffX(), camera_roll.OffY(), camera_roll.OffZ());
            test_nav_btn.SetTransformTrackedDeviceRelative(left_hand, t);
        }

        // デバッグボタンを左コントローラー上部に配置。
        // 既存UIと同じ rot_y=π/2 を使って向きを揃える。
        if (left_hand != vr::k_unTrackedDeviceIndexInvalid) {
            auto dbg_t = MakeTransform(
                0.0f, 0.2f, 0.0f,          // レイアウト位置: メインUIより上 0.2m
                0.0f, 3.14159265f/2.0f, 0.0f, // rot_y = π/2（既存UIと同じ向き）
                camera_roll.OffX(), camera_roll.OffY(), camera_roll.OffZ());
            debug_btn.SetTransformTrackedDeviceRelative(left_hand, dbg_t);
        }

#ifdef _DEBUG
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
        buttons.push_back(&test_nav_btn); // テストナビボタン
        buttons.push_back(&debug_btn);    // デバッグボタン
#ifdef _DEBUG
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
        if (trigger_now && !prev_trigger && hit)
            hit->FireTrigger();
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
