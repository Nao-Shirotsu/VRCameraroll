#pragma execution_character_set("utf-8")
#include <openvr.h>
#include <Windows.h>
#include <cstdio>
#include <array>
#include <vector>
#include <algorithm>
#include "ImageCollection.h"

static const char* kImageFolder = "C:\\Users\\albus\\Pictures\\VRChat\\2026-03";

// ────────────────────────────────────────────
// テクスチャ生成
// ────────────────────────────────────────────

// 中央正方形クロップ + ボックスフィルタ縮小
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
                        r += img.pixels[idx+0]; g += img.pixels[idx+1];
                        b += img.pixels[idx+2]; a += img.pixels[idx+3];
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

// 64x64 の矢印テクスチャ (left=true で ←、false で →)
static std::vector<uint8_t> MakeArrowTexture(bool left_arrow) {
    constexpr int S = 64;
    std::vector<uint8_t> pixels(S * S * 4, 0);
    for (int i = 0; i < S * S; ++i) {
        pixels[i*4+0] = 80; pixels[i*4+1] = 80;
        pixels[i*4+2] = 80; pixels[i*4+3] = 180;
    }
    int cx = S / 2, cy = S / 2, half = S / 4;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            int dx = x - cx, dy = y - cy;
            int ady = abs(dy);
            bool tri = left_arrow
                ? (dx >= -half && dx <= 0 && ady <= half + dx)
                : (dx <= half  && dx >= 0 && ady <= half - dx);
            if (tri) {
                int idx = (y * S + x) * 4;
                pixels[idx+0] = 220; pixels[idx+1] = 220;
                pixels[idx+2] = 220; pixels[idx+3] = 255;
            }
        }
    }
    return pixels;
}

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

// メイン overlay の中心 Y を計算する。
// サブ画像の上端を下限として、画像のアスペクト比に応じて上方向に広げる。
// sub_center_y: サブ overlay の中心 Y
// sub_w:        サブ overlay の幅 (= 高さ、正方形)
// main_w:       メイン overlay の幅
// img:          表示する画像
static float CalcMainCenterY(float sub_center_y, float sub_w, float main_w, const Image& img) {
    float aspect      = (img.IsLoaded() && img.width > 0)
                        ? (float)img.height / (float)img.width
                        : 1.0f;
    float sub_top     = sub_center_y + sub_w / 2.0f; // サブ上端
    float gap         = 0.005f;                        // 隙間 5mm
    float main_height = main_w * aspect;
    return sub_top + gap + main_height / 2.0f;         // メイン中心
}

// ────────────────────────────────────────────
// 画像オーバーレイの再描画
// overlays[0]  = メイン (collection.Main() をフルサイズ)
// overlays[i]  = サブ   (images[i-1] をサムネイル, i=1..N-1)
// ────────────────────────────────────────────

static void UploadImages(
    const std::array<vr::VROverlayHandle_t, ImageCollection::N>& overlays,
    ImageCollection& col)
{
    // [0] メイン: 選択中画像をフルサイズ
    const Image& main_img = col.Main();
    if (main_img.IsLoaded()) {
        vr::VROverlay()->SetOverlayRaw(
            overlays[0], (void*)main_img.pixels.data(),
            main_img.width, main_img.height, 4);
    } else {
        UploadGrey(overlays[0]);
    }

    // [1..N-1] サブ: images[i-1] をサムネイル表示
    // → overlays[1]=images[0], overlays[2]=images[1], ..., overlays[N-1]=images[N-2]
    auto& images = col.Images();
    for (int i = 1; i < ImageCollection::N; ++i) {
        const Image& img = images[i - 1];
        if (img.IsLoaded()) {
            auto thumb = MakeCroppedThumbnail(img, 8);
            int tw = min(img.width, img.height) / 8;
            vr::VROverlay()->SetOverlayRaw(overlays[i], thumb.data(), tw, tw, 4);
        } else {
            UploadGrey(overlays[i]);
        }
    }
}

// ────────────────────────────────────────────
// main
// ────────────────────────────────────────────

int main() {
    vr::EVRInitError err = vr::VRInitError_None;
    vr::IVRSystem* vr_system = vr::VR_Init(&err, vr::VRApplication_Overlay);
    if (err != vr::VRInitError_None) {
        printf("VR_Init failed: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(err));
        return 1;
    }

    constexpr int N = ImageCollection::N;

    // ── 画像オーバーレイ (N枚) ──
    std::array<vr::VROverlayHandle_t, N> img_overlays;
    for (int i = 0; i < N; ++i) {
        char key[64], name[64];
        snprintf(key,  sizeof(key),  "camera_roll.img%d", i);
        snprintf(name, sizeof(name), "Camera Roll %d", i);
        vr::VROverlay()->CreateOverlay(key, name, &img_overlays[i]);
        vr::VROverlay()->ShowOverlay(img_overlays[i]);
    }

    // レイアウト: [0]=メイン上部（Y は動的計算）、[1..N-1]=サブ下部横並び
    const float MAIN_W = 0.25f;
    const float SUB_W  = MAIN_W / (N - 1);
    const float SUB_Y  = -0.03f;
    const float SUB_X0 = -MAIN_W / 2.0f + SUB_W / 2.0f;

    struct Layout { float x, y, z, w; };
    std::array<Layout, N> img_layout;
    img_layout[0] = { 0.0f, 0.0f, 0.0f, MAIN_W }; // y は後で更新
    for (int i = 1; i < N; ++i) {
        img_layout[i] = { SUB_X0 + (i - 1) * SUB_W, SUB_Y, 0.0f, SUB_W };
    }
    for (int i = 0; i < N; ++i) {
        vr::VROverlay()->SetOverlayWidthInMeters(img_overlays[i], img_layout[i].w);
        UploadGrey(img_overlays[i]);
    }

    // サブ画像にマウス入力を有効化 (クリックでメイン切り替え)
    for (int i = 1; i < N; ++i) {
        vr::HmdVector2_t ms = { 64.f, 64.f };
        vr::VROverlay()->SetOverlayInputMethod(img_overlays[i], vr::VROverlayInputMethod_Mouse);
        vr::VROverlay()->SetOverlayMouseScale(img_overlays[i], &ms);
        vr::VROverlay()->SetOverlayFlag(img_overlays[i], vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, true);
        vr::VROverlay()->SetOverlayFlag(img_overlays[i], vr::VROverlayFlags_EnableClickStabilization, true);
    }

    // ── ボタンオーバーレイ (←・→) ──
    const float BTN_W   = 0.05f;
    const float BTN_GAP = 0.01f;
    const float BTN_L_X = -(MAIN_W / 2.0f + BTN_GAP + BTN_W / 2.0f);
    const float BTN_R_X =  (MAIN_W / 2.0f + BTN_GAP + BTN_W / 2.0f);

    vr::VROverlayHandle_t btn_newer, btn_older;
    vr::VROverlay()->CreateOverlay("camera_roll.btn_newer", "Newer", &btn_newer);
    vr::VROverlay()->CreateOverlay("camera_roll.btn_older", "Older", &btn_older);
    vr::VROverlay()->SetOverlayWidthInMeters(btn_newer, BTN_W);
    vr::VROverlay()->SetOverlayWidthInMeters(btn_older, BTN_W);

    auto arrow_l = MakeArrowTexture(true);
    auto arrow_r = MakeArrowTexture(false);
    vr::VROverlay()->SetOverlayRaw(btn_newer, arrow_l.data(), 64, 64, 4);
    vr::VROverlay()->SetOverlayRaw(btn_older, arrow_r.data(), 64, 64, 4);

    vr::HmdVector2_t btn_ms = { 64.f, 64.f };
    vr::VROverlay()->SetOverlayInputMethod(btn_newer, vr::VROverlayInputMethod_Mouse);
    vr::VROverlay()->SetOverlayInputMethod(btn_older, vr::VROverlayInputMethod_Mouse);
    vr::VROverlay()->SetOverlayMouseScale(btn_newer, &btn_ms);
    vr::VROverlay()->SetOverlayMouseScale(btn_older, &btn_ms);
    vr::VROverlay()->SetOverlayFlag(btn_newer, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, true);
    vr::VROverlay()->SetOverlayFlag(btn_older, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, true);
    vr::VROverlay()->SetOverlayFlag(btn_newer, vr::VROverlayFlags_EnableClickStabilization, true);
    vr::VROverlay()->SetOverlayFlag(btn_older, vr::VROverlayFlags_EnableClickStabilization, true);

    vr::VROverlay()->ShowOverlay(btn_newer);
    vr::VROverlay()->ShowOverlay(btn_older);

    // ── 画像読み込み ──
    ImageCollection collection;
    collection.LoadFromFolder(kImageFolder);
    UploadImages(img_overlays, collection);
    img_layout[0].y = CalcMainCenterY(SUB_Y, SUB_W, MAIN_W, collection.Main());

    printf("Loaded %d image(s). Press Enter to exit.\n", collection.LoadedCount());

    while (true) {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) break;

        // 左コントローラーにオーバーレイを追従させる
        vr::TrackedDeviceIndex_t left_hand =
            vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);

        if (left_hand != vr::k_unTrackedDeviceIndexInvalid) {
            for (int i = 0; i < N; ++i) {
                auto t = MakeTransform(img_layout[i].x, img_layout[i].y, img_layout[i].z);
                vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(img_overlays[i], left_hand, &t);
            }
            auto t_newer = MakeTransform(BTN_L_X, SUB_Y, 0.0f);
            auto t_older = MakeTransform(BTN_R_X, SUB_Y, 0.0f);
            vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(btn_newer, left_hand, &t_newer);
            vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(btn_older, left_hand, &t_older);
        }

        // ── イベント処理 ──
        vr::VREvent_t event;

        // ← ボタン: 新しいページへ
        while (vr::VROverlay()->PollNextOverlayEvent(btn_newer, &event, sizeof(event))) {
            if (event.eventType == vr::VREvent_MouseButtonDown) {
                collection.LoadNewerPage();
                UploadImages(img_overlays, collection);
                img_layout[0].y = CalcMainCenterY(SUB_Y, SUB_W, MAIN_W, collection.Main());
                printf("Load newer page. Main: %s\n", collection.Main().path.filename().string().c_str());
            }
        }
        // → ボタン: 古いページへ
        while (vr::VROverlay()->PollNextOverlayEvent(btn_older, &event, sizeof(event))) {
            if (event.eventType == vr::VREvent_MouseButtonDown) {
                collection.LoadOlderPage();
                UploadImages(img_overlays, collection);
                img_layout[0].y = CalcMainCenterY(SUB_Y, SUB_W, MAIN_W, collection.Main());
                printf("Load older page. Main: %s\n", collection.Main().path.filename().string().c_str());
            }
        }

        // サブ画像クリック: overlays[i] (i=1..N-1) → images[i-1] をメインに
        for (int i = 1; i < N; ++i) {
            while (vr::VROverlay()->PollNextOverlayEvent(img_overlays[i], &event, sizeof(event))) {
                if (event.eventType == vr::VREvent_MouseButtonDown) {
                    collection.SetMain(collection.Images().begin() + (i - 1));
                    const Image& m = collection.Main();
                    if (m.IsLoaded()) {
                        vr::VROverlay()->SetOverlayRaw(
                            img_overlays[0], (void*)m.pixels.data(),
                            m.width, m.height, 4);
                    }
                    img_layout[0].y = CalcMainCenterY(SUB_Y, SUB_W, MAIN_W, m);
                    printf("Select sub[%d]: %s\n", i - 1, m.path.filename().string().c_str());
                }
            }
        }

        // Quit
        if (vr::VROverlay()->PollNextOverlayEvent(img_overlays[0], &event, sizeof(event))) {
            if (event.eventType == vr::VREvent_Quit) break;
        }

        Sleep(16);
    }

    for (int i = 0; i < N; ++i) vr::VROverlay()->DestroyOverlay(img_overlays[i]);
    vr::VROverlay()->DestroyOverlay(btn_newer);
    vr::VROverlay()->DestroyOverlay(btn_older);
    vr::VR_Shutdown();
    return 0;
}
