#pragma execution_character_set("utf-8")
#include <openvr.h>
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "CameraRollUI.h"
#include "LaserController.h"
#ifdef UI_ADJUST
#include "DebugUI.h"
#endif

// 実行ファイルと同じフォルダの config.yml から image_folder の値を読む。
// 該当キーが見つからない場合は空文字を返す。
static std::string LoadImageFolderFromConfig() {
    char exe_path[MAX_PATH];
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::filesystem::path config_path =
        std::filesystem::path(exe_path).parent_path() / "config.yml";

    std::ifstream file(config_path);
    if (!file.is_open()) {
        printf("config.yml が見つかりません: %s\n", config_path.string().c_str());
        return {};
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;  // コメント・空行をスキップ
        const std::string key = "image_folder:";
        if (line.rfind(key, 0) == 0) {
            std::string value = line.substr(key.size());
            size_t start = value.find_first_not_of(" \t\r\n");
            size_t end   = value.find_last_not_of(" \t\r\n");
            if (start == std::string::npos) return {};
            std::string raw = value.substr(start, end - start + 1);
            // %USERPROFILE% などの環境変数を展開する
            char expanded[32767];  // ExpandEnvironmentStrings の最大バッファサイズ
            DWORD n = ExpandEnvironmentStringsA(raw.c_str(), expanded, (DWORD)sizeof(expanded));
            if (n == 0 || n > sizeof(expanded)) {
                return raw;
            }
            return expanded;
        }
    }

    printf("config.yml に image_folder が見つかりません\n");
    return {};
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    vr::EVRInitError err = vr::VRInitError_None;
    vr::IVRSystem* vr_system = vr::VR_Init(&err, vr::VRApplication_Overlay);
    if (err != vr::VRInitError_None) return 1;

    // LaserController を先に作る:
    // コンストラクタ内で globalInputOverlaysEnabled=false を設定するため、
    // CameraRollUI の overlay 作成より前に呼ぶ必要がある。
    LaserController laser;

    const std::string image_folder = LoadImageFolderFromConfig();
    if (image_folder.empty()) {
        printf("image_folder の設定が見つかりませんでした。config.yml を確認してください。\n");
        vr::VR_Shutdown();
        return 1;
    }
    printf("画像フォルダ: %s\n", image_folder.c_str());
    printf("起動しました。終了するには Enter キーを押すか、このウィンドウを閉じてください。\n");

    CameraRollUI camera_roll;
    camera_roll.LoadImages(image_folder.c_str());

#ifdef UI_ADJUST
    DebugUI debug_ui(camera_roll, laser);
#endif

    // ── IVRInput 初期化 (現在無効: SetActionManifestPath がレガシー GetControllerState を妨害するため) ──
    // {
    //     char exe_path_buf[MAX_PATH];
    //     GetModuleFileNameA(nullptr, exe_path_buf, MAX_PATH);
    //     std::filesystem::path manifest =
    //         std::filesystem::path(exe_path_buf).parent_path() / "actions" / "actions.json";
    //     vr::VRInput()->SetActionManifestPath(manifest.string().c_str());
    // }
    // vr::VRActionHandle_t    h_toggle_ui  = vr::k_ulInvalidActionHandle;
    // vr::VRActionSetHandle_t h_action_set = vr::k_ulInvalidActionSetHandle;
    // vr::VRInput()->GetActionHandle("/actions/camera_roll/in/toggle_ui", &h_toggle_ui);
    // vr::VRInput()->GetActionSetHandle("/actions/camera_roll", &h_action_set);

    bool prev_trigger        = false;
    bool prev_any_hit        = false;
    bool ui_visible          = true;   // 初期状態は表示
    bool prev_stick_pressed  = false;

    while (true) {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) break;

        // ── フォルダ変更ポーリング ──
        camera_roll.PollFolderChanges();

        // ── 全コントローラー pose 取得（左右共通）──
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
        vr_system->GetDeviceToAbsoluteTrackingPose(
            vr::TrackingUniverseStanding, 0.0f, poses, vr::k_unMaxTrackedDeviceCount);

        // ── 左コントローラー取得 ──
        vr::TrackedDeviceIndex_t left_hand =
            vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);

        // ── 左スティック押し込みでUI表示トグル ──
        if (left_hand != vr::k_unTrackedDeviceIndexInvalid) {
            vr::VRControllerState_t ctrl = {};
            vr_system->GetControllerState(left_hand, &ctrl, sizeof(ctrl));
            const bool stick_now = (ctrl.ulButtonPressed &
                vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) != 0;
            if (stick_now && !prev_stick_pressed) {
                ui_visible = !ui_visible;
                camera_roll.SetActive(ui_visible);
                laser.SetActive(ui_visible);
                printf("カメラロール: %s\n", ui_visible ? "表示" : "非表示");
            }
            prev_stick_pressed = stick_now;
        }

        camera_roll.UpdateTransforms(left_hand);

#ifdef UI_ADJUST
        debug_ui.UpdateTransforms(left_hand,
            camera_roll.RotX(), camera_roll.RotY(), camera_roll.RotZ(),
            camera_roll.OffX(), camera_roll.OffY(), camera_roll.OffZ());
#endif

        // ── 右コントローラー ──
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

        camera_roll.UpdateHover(hit);

        const bool any_hit = (hit != nullptr);
        if (any_hit != prev_any_hit) {
            prev_any_hit = any_hit;
        }

        // ── トリガー入力 ──
        const bool trigger_now = right_valid && laser.IsTriggerPressed(vr_system, right_hand);
        if (trigger_now && !prev_trigger && hit) {
            hit->FireTrigger();
#ifdef UI_ADJUST
            printf("=== Button fired ===\n");
            laser.PrintParams();
            printf("camera_roll translate=(%.6f, %.6f, %.6f)  rotate=(%.6f, %.6f, %.6f)\n",
                camera_roll.OffX(), camera_roll.OffY(), camera_roll.OffZ(),
                camera_roll.RotX(), camera_roll.RotY(), camera_roll.RotZ());
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
