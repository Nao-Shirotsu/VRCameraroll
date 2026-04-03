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

// SteamVR の起動を最大 timeout_ms ミリ秒待つ。
// 起動したら true、タイムアウトしたら false を返す。
static bool WaitForSteamVR(DWORD timeout_ms = INFINITE) {
    const DWORD interval = 3000;
    DWORD elapsed = 0;
    while (true) {
        vr::EVRInitError err = vr::VRInitError_None;
        vr::IVRSystem* sys = vr::VR_Init(&err, vr::VRApplication_Overlay);
        if (err == vr::VRInitError_None) {
            vr::VR_Shutdown();  // 一旦切る（外側の VR_Init でまた繋ぐ）
            return true;
        }
        if (timeout_ms != INFINITE && elapsed >= timeout_ms) return false;
        Sleep(interval);
        elapsed += interval;
    }
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    const std::string image_folder = LoadImageFolderFromConfig();
    if (image_folder.empty()) {
        printf("image_folder の設定が見つかりませんでした。config.yml を確認してください。\n");
        return 1;
    }
    printf("画像フォルダ: %s\n", image_folder.c_str());
    printf("起動しました。終了するには Enter キーを押すか、このウィンドウを閉じてください。\n");

    // ── 外側ループ: SteamVR が切れても再接続して常駐し続ける ──
    while (true) {
        // SteamVR に接続
        vr::EVRInitError err = vr::VRInitError_None;
        vr::IVRSystem* vr_system = vr::VR_Init(&err, vr::VRApplication_Overlay);
        if (err != vr::VRInitError_None) {
            printf("SteamVR 接続失敗 (%d)。再試行します…\n", (int)err);
            Sleep(3000);
            continue;
        }

        // LaserController を先に作る:
        // コンストラクタ内で globalInputOverlaysEnabled=false を設定するため、
        // CameraRollUI の overlay 作成より前に呼ぶ必要がある。
        LaserController laser;

        CameraRollUI camera_roll;
        camera_roll.LoadImages(image_folder.c_str());

#ifdef UI_ADJUST
        DebugUI debug_ui(camera_roll, laser);
#endif

        bool     prev_trigger          = false;
        bool     prev_any_hit          = false;
        bool     ui_visible            = true;   // 初期状態は表示
        bool     prev_stick_pressed    = false;
        ULONGLONG left_trigger_click_ms = 0;     // 直前クリックの時刻（0=未クリック）

        bool user_quit    = false;  // Enter キー等でユーザーが明示的に終了
        bool vr_lost      = false;  // SteamVR 側の終了 → 再接続へ

        // ── 内側ループ: メインフレームループ ──
        while (true) {
            if (GetAsyncKeyState(VK_RETURN) & 0x8000) { user_quit = true; break; }

            // ── フォルダ変更ポーリング ──
            camera_roll.PollFolderChanges();

            // ── 全コントローラー pose 取得（左右共通）──
            vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
            vr_system->GetDeviceToAbsoluteTrackingPose(
                vr::TrackingUniverseStanding, 0.0f, poses, vr::k_unMaxTrackedDeviceCount);

            // ── 左コントローラー取得 ──
            vr::TrackedDeviceIndex_t left_hand =
                vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);

            // ── 左トリガーダブルクリックでUI表示トグル（0.375秒以内）──
            if (left_hand != vr::k_unTrackedDeviceIndexInvalid) {
                vr::VRControllerState_t ctrl = {};
                vr_system->GetControllerState(left_hand, &ctrl, sizeof(ctrl));
                const bool stick_now = (ctrl.ulButtonPressed &
                    vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) != 0;
                if (stick_now && !prev_stick_pressed) {
                    const ULONGLONG now = GetTickCount64();
                    if (left_trigger_click_ms != 0 && (now - left_trigger_click_ms) <= 375) {
                        ui_visible = !ui_visible;
                        camera_roll.SetActive(ui_visible);
                        laser.SetActive(ui_visible);
                        printf("カメラロール: %s\n", ui_visible ? "表示" : "非表示");
                        left_trigger_click_ms = 0;
                    } else {
                        left_trigger_click_ms = now;
                    }
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
                if (event.eventType == vr::VREvent_Quit) {
                    // SteamVR がシャットダウンを要求（AFK・省電力・手動終了など）。
                    // プロセスは終了せず、SteamVR の再起動を待って再接続する。
                    vr::VRSystem()->AcknowledgeQuit_Exiting();
                    vr_lost = true;
                    break;
                }
            }

            Sleep(16);
        }

        // VR 接続を切断（オブジェクトのデストラクタより前に呼ぶ）
        vr::VR_Shutdown();

        if (user_quit) break;  // 明示的な終了 → プロセス終了

        // SteamVR が切れた → 再接続待ち
        printf("SteamVR が終了しました。再起動を待機中…（Enter で中止）\n");
        while (true) {
            // Enter が押されたら待機自体をキャンセルして終了
            if (GetAsyncKeyState(VK_RETURN) & 0x8000) { user_quit = true; break; }

            vr::EVRInitError test_err = vr::VRInitError_None;
            vr::IVRSystem* test_sys = vr::VR_Init(&test_err, vr::VRApplication_Overlay);
            if (test_err == vr::VRInitError_None) {
                vr::VR_Shutdown();
                printf("SteamVR が再起動しました。再接続します…\n");
                break;
            }
            Sleep(3000);
        }
        if (user_quit) break;
    }

    return 0;
}
