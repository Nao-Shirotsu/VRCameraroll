#pragma execution_character_set("utf-8")
#include <openvr.h>
#include <Windows.h>
#include <cstdio>
#include <array>
#include <vector>
#include <algorithm>
#include "ImageCollection.h"

// ハードコードされた監視フォルダ
static const char* kImageFolder = "C:\\Users\\albus\\Pictures\\VRChat\\2026-03";

// 中央正方形クロップ + ボックスフィルタ縮小 (サブ画像用)
// 出力は常に正方形 (crop_size/scale) x (crop_size/scale)
static std::vector<uint8_t> MakeCroppedThumbnail(const Image& img, int scale = 8) {
    int crop_size = min(img.width, img.height);
    int crop_x    = (img.width  - crop_size) / 2;
    int crop_y    = (img.height - crop_size) / 2;
    int tw        = max(1, crop_size / scale);
    std::vector<uint8_t> out(tw * tw * 4, 0);

    for (int y = 0; y < tw; ++y) {
        for (int x = 0; x < tw; ++x) {
            uint32_t r = 0, g = 0, b = 0, a = 0, count = 0;
            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    int sx = crop_x + x * scale + dx;
                    int sy = crop_y + y * scale + dy;
                    if (sx < img.width && sy < img.height) {
                        int idx = (sy * img.width + sx) * 4;
                        r += img.pixels[idx + 0]; g += img.pixels[idx + 1];
                        b += img.pixels[idx + 2]; a += img.pixels[idx + 3];
                        ++count;
                    }
                }
            }
            int dst = (y * tw + x) * 4;
            out[dst+0] = r/count; out[dst+1] = g/count;
            out[dst+2] = b/count; out[dst+3] = a/count;
        }
    }
    return out;
}

// グレーバッファ (ロード中表示用)
static void UploadGrey(vr::VROverlayHandle_t overlay) {
    static uint8_t grey[64 * 64 * 4];
    static bool init = false;
    if (!init) { memset(grey, 128, sizeof(grey)); init = true; }
    vr::VROverlay()->SetOverlayRaw(overlay, grey, 64, 64, 4);
}

static vr::HmdMatrix34_t MakeTransform(float x, float y, float z) {
    vr::HmdMatrix34_t t = {};
    t.m[0][0] = 1.0f; t.m[1][1] = 1.0f; t.m[2][2] = 1.0f;
    t.m[0][3] = x; t.m[1][3] = y; t.m[2][3] = z;
    return t;
}

int main() {
    vr::EVRInitError err = vr::VRInitError_None;
    vr::IVRSystem* vr_system = vr::VR_Init(&err, vr::VRApplication_Overlay);
    if (err != vr::VRInitError_None) {
        printf("VR_Init failed: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(err));
        return 1;
    }

    // --- オーバーレイ作成 ---
    // [0] メイン画像 (上部・大)
    // [1][2][3] サブ画像 (下部・小、横並び)
    constexpr int N = ImageCollection::N; // == 4
    std::array<vr::VROverlayHandle_t, N> overlays;
    for (int i = 0; i < N; ++i) {
        char key[64], name[64];
        snprintf(key,  sizeof(key),  "camera_roll.img%d", i);
        snprintf(name, sizeof(name), "Camera Roll %d", i);
        vr::VROverlay()->CreateOverlay(key, name, &overlays[i]);
        vr::VROverlay()->ShowOverlay(overlays[i]);
    }

    // レイアウト定義 (コントローラー相対、単位: メートル)
    //   メイン: 上部中央、幅 0.25m
    //   サブ x3: 下部横並び、幅 0.083m (= 0.25/3)
    const float MAIN_W = 0.25f;
    const float SUB_W  = MAIN_W / 3.0f;
    const float MAIN_Y =  0.10f;
    const float SUB_Y  = -0.03f;

    struct OverlayLayout { float x, y, z, w; };
    const std::array<OverlayLayout, N> layout = {{
        {  0.0f,    MAIN_Y, 0.0f, MAIN_W }, // [0] メイン
        { -SUB_W,   SUB_Y,  0.0f, SUB_W  }, // [1] サブ左
        {  0.0f,    SUB_Y,  0.0f, SUB_W  }, // [2] サブ中
        {  SUB_W,   SUB_Y,  0.0f, SUB_W  }, // [3] サブ右
    }};
    for (int i = 0; i < N; ++i) {
        vr::VROverlay()->SetOverlayWidthInMeters(overlays[i], layout[i].w);
        UploadGrey(overlays[i]); // 読み込み前はグレー
    }

    // --- 画像読み込み ---
    ImageCollection collection;
    collection.LoadFromFolder(kImageFolder);

    // [0] メイン: フルサイズ
    const Image& main_img = collection.Main();
    if (main_img.IsLoaded()) {
        vr::VROverlay()->SetOverlayRaw(
            overlays[0],
            (void*)main_img.pixels.data(),
            main_img.width, main_img.height, 4
        );
    }

    // [1..3] サブ: 1/8 サムネイル
    auto& images = collection.Images();
    for (int i = 1; i < N; ++i) {
        const Image& img = images[i];
        if (img.IsLoaded()) {
            auto thumb = MakeCroppedThumbnail(img, 8);
            int tw = min(img.width, img.height) / 8;
            vr::VROverlay()->SetOverlayRaw(overlays[i], thumb.data(), tw, tw, 4);
        }
    }

    printf("Loaded %d image(s). Press Enter to exit.\n", collection.LoadedCount());

    // --- メインループ ---
    while (true) {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) break;

        vr::TrackedDeviceIndex_t left_hand =
            vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);

        if (left_hand != vr::k_unTrackedDeviceIndexInvalid) {
            for (int i = 0; i < N; ++i) {
                auto t = MakeTransform(layout[i].x, layout[i].y, layout[i].z);
                vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(
                    overlays[i], left_hand, &t
                );
            }
        }

        vr::VREvent_t event;
        if (vr::VROverlay()->PollNextOverlayEvent(overlays[0], &event, sizeof(event))) {
            if (event.eventType == vr::VREvent_Quit) break;
        }

        Sleep(16);
    }

    for (int i = 0; i < N; ++i)
        vr::VROverlay()->DestroyOverlay(overlays[i]);
    vr::VR_Shutdown();
    return 0;
}
