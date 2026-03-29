#pragma execution_character_set("utf-8")

#include <openvr.h>
#include <Windows.h>
#include <cstdio>

int main() {
  vr::EVRInitError err = vr::VRInitError_None;
  vr::IVRSystem* vr_system = vr::VR_Init(&err, vr::VRApplication_Overlay);

  if (err != vr::VRInitError_None) {
    printf("VR_Init failed: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(err));
    return 1;
  }

  vr::VROverlayHandle_t handle;
  vr::VROverlay()->CreateOverlay("camera_roll.main", "Camera Roll", &handle);
  vr::VROverlay()->SetOverlayWidthInMeters(handle, 0.2f); // 手首サイズ
  vr::VROverlay()->ShowOverlay(handle);

  // 64x64 の白いテクスチャをオーバーレイに貼って表示確認
  uint8_t pixels[64 * 64 * 4];
  memset(pixels, 255, sizeof(pixels)); // 白 (RGBA all 255)
  vr::VROverlay()->SetOverlayRaw(handle, pixels, 64, 64, 4);

  // コントローラー相対オフセット: 手の甲の少し上に表示
  // X: 左右、Y: 上下、Z: 前後（マイナスが手前 = 手の甲方向）
  vr::HmdMatrix34_t transform = {};
  transform.m[0][0] = 1.0f; transform.m[1][1] = 1.0f; transform.m[2][2] = 1.0f;
  transform.m[0][3] = 0.0f;  // 左右オフセットなし
  transform.m[1][3] = 0.05f; // 手の甲から 5cm 上
  transform.m[2][3] = 0.0f;  // 前後オフセットなし

  printf("Overlay created. Press Enter to exit.\n");

  // メインループ: Enterで終了、毎フレームコントローラーへの追従を更新
  while (true) {
    // Enterキーが押されたら終了
    if (GetAsyncKeyState(VK_RETURN) & 0x8000) break;

    // 左コントローラーのデバイスインデックスを取得
    vr::TrackedDeviceIndex_t left_hand =
      vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);

    if (left_hand != vr::k_unTrackedDeviceIndexInvalid) {
      vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(handle, left_hand, &transform);
    } else {
      printf("Left controller not found...\n");
    }

    // SteamVRイベント処理
    vr::VREvent_t event;
    while (vr::VROverlay()->PollNextOverlayEvent(handle, &event, sizeof(event))) {
      if (event.eventType == vr::VREvent_Quit) goto cleanup;
    }

    Sleep(16); // ~60fps
  }

cleanup:
  vr::VR_Shutdown();
  return 0;
}
