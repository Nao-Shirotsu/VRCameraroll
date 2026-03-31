#include "CameraRollUI.h"
#include <algorithm>
#include <cstring>

// ────────────────────────────────────────────
// 内部テクスチャ生成
// ────────────────────────────────────────────

static std::vector<uint8_t> MakeCroppedThumbnail(const Image& img, int scale = 8) {
    int crop_size = std::min(img.width, img.height);
    int crop_x    = (img.width  - crop_size) / 2;
    int crop_y    = (img.height - crop_size) / 2;
    int tw        = std::max(1, crop_size / scale);
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
            out[dst+0]=(uint8_t)(r/count); out[dst+1]=(uint8_t)(g/count);
            out[dst+2]=(uint8_t)(b/count); out[dst+3]=(uint8_t)(a/count);
        }
    }
    return out;
}

static std::vector<uint8_t> MakeArrowTexture(bool left_arrow,
    uint8_t bg_r = 80, uint8_t bg_g = 80, uint8_t bg_b = 80)
{
    constexpr int S = 64;
    std::vector<uint8_t> pixels(S * S * 4);
    for (int i = 0; i < S * S; ++i) {
        pixels[i*4+0]=bg_r; pixels[i*4+1]=bg_g;
        pixels[i*4+2]=bg_b; pixels[i*4+3]=180;
    }
    const int cy = S / 2, half = S / 4;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            int dx = x - S/2, dy = y - cy, ady = std::abs(dy);
            bool tri = left_arrow
                ? (dx >= -half && dx <= 0 && ady <= half + dx)
                : (dx <=  half && dx >= 0 && ady <= half - dx);
            if (tri) {
                int idx = (y * S + x) * 4;
                pixels[idx+0]=220; pixels[idx+1]=220;
                pixels[idx+2]=220; pixels[idx+3]=255;
            }
        }
    }
    return pixels;
}

static void UploadGrey(vr::VROverlayHandle_t overlay) {
    static uint8_t grey[64 * 64 * 4];
    static bool init = false;
    if (!init) { std::memset(grey, 128, sizeof(grey)); init = true; }
    vr::VROverlay()->SetOverlayRaw(overlay, grey, 64, 64, 4);
}

// ────────────────────────────────────────────
// CameraRollUI
// ────────────────────────────────────────────

CameraRollUI::CameraRollUI()
    : m_btn_newer("camera_roll.btn_newer", "Newer", 0.05f, [this]{ OnNewerPage(); })
    , m_btn_older("camera_roll.btn_older", "Older", 0.05f, [this]{ OnOlderPage(); })
{
    // 画像オーバーレイ生成
    for (int i = 0; i < N; ++i) {
        char key[64], name[64];
        std::snprintf(key,  sizeof(key),  "camera_roll.img%d", i);
        std::snprintf(name, sizeof(name), "Camera Roll %d", i);
        vr::VROverlay()->CreateOverlay(key, name, &m_img_overlays[i]);
        vr::VROverlay()->ShowOverlay(m_img_overlays[i]);
    }

    // サブ画像はマウス入力不要
    for (int i = 1; i < N; ++i)
        vr::VROverlay()->SetOverlayInputMethod(m_img_overlays[i], vr::VROverlayInputMethod_None);

    // レイアウト計算
    const float SUB_X0 = -MAIN_W / 2.0f + SUB_W / 2.0f;
    m_img_layout[0] = { 0.0f, 0.0f };
    for (int i = 1; i < N; ++i)
        m_img_layout[i] = { SUB_X0 + (i - 1) * SUB_W, SUB_Y };

    for (int i = 0; i < N; ++i) {
        vr::VROverlay()->SetOverlayWidthInMeters(m_img_overlays[i],
            i == 0 ? MAIN_W : SUB_W);
        UploadGrey(m_img_overlays[i]);
    }

    // ページ送りボタンのテクスチャ
    m_btn_newer.UploadTexture(MakeArrowTexture(true), 64, 64);
    m_btn_older.UploadTexture(MakeArrowTexture(false), 64, 64);
    m_btn_newer.Show();
    m_btn_older.Show();

    // サブ画像 TriggerableButton: m_img_overlays[i] を所有権なしで引き受ける。
    // 別 overlay を作らないことで同位置の二重 overlay を避ける。
    for (int i = 1; i < N; ++i) {
        m_sub_btns.emplace_back(m_img_overlays[i], [this, idx = i - 1]{
            m_collection.SetMain(m_collection.Images().begin() + idx);
            const Image& mi = m_collection.Main();
            if (mi.IsLoaded())
                vr::VROverlay()->SetOverlayRaw(m_img_overlays[0],
                    (void*)mi.pixels.data(), mi.width, mi.height, 4);
            UpdateMainY();
        });
    }
}

CameraRollUI::~CameraRollUI() {
    for (int i = 0; i < N; ++i)
        vr::VROverlay()->DestroyOverlay(m_img_overlays[i]);
    // TriggerableButton のデストラクタが overlay を破棄する
}

void CameraRollUI::LoadImages(const std::filesystem::path& folder) {
    m_collection.LoadFromFolder(folder);
    UploadImages();
    UpdateMainY();
}

void CameraRollUI::UploadImages() {
    const Image& main_img = m_collection.Main();
    if (main_img.IsLoaded()) {
        vr::VROverlay()->SetOverlayRaw(
            m_img_overlays[0], (void*)main_img.pixels.data(),
            main_img.width, main_img.height, 4);
    } else {
        UploadGrey(m_img_overlays[0]);
    }
    auto& images = m_collection.Images();
    for (int i = 1; i < N; ++i) {
        const Image& img = images[i - 1];
        if (img.IsLoaded()) {
            auto thumb = MakeCroppedThumbnail(img, 8);
            int tw = std::min(img.width, img.height) / 8;
            vr::VROverlay()->SetOverlayRaw(m_img_overlays[i], thumb.data(), tw, tw, 4);
        } else {
            UploadGrey(m_img_overlays[i]);
        }
    }
}

void CameraRollUI::UpdateMainY() {
    const Image& img = m_collection.Main();
    float aspect      = (img.IsLoaded() && img.width > 0)
                        ? (float)img.height / (float)img.width : 1.0f;
    float sub_top     = SUB_Y + SUB_W / 2.0f;
    float main_height = MAIN_W * aspect;
    m_img_layout[0].y = sub_top + 0.005f + main_height / 2.0f;
}

void CameraRollUI::SetActive(bool active) {
    m_active = active;
    auto* ovr = vr::VROverlay();
    for (auto h : m_img_overlays) {
        if (active) ovr->ShowOverlay(h);
        else        ovr->HideOverlay(h);
    }
    if (active) { m_btn_newer.Show(); m_btn_older.Show(); }
    else        { m_btn_newer.Hide(); m_btn_older.Hide(); }
}

void CameraRollUI::UpdateTransforms(vr::TrackedDeviceIndex_t left_hand) {
    if (!m_active) return;
    if (left_hand == vr::k_unTrackedDeviceIndexInvalid) return;

    auto make = [&](float x, float y) {
        return MakeTransform(x, y, 0.f,
            m_rot_x, m_rot_y, m_rot_z,
            m_offset_x, m_offset_y, m_offset_z);
    };

    // 画像オーバーレイ
    for (int i = 0; i < N; ++i) {
        auto t = make(m_img_layout[i].x, m_img_layout[i].y);
        vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(
            m_img_overlays[i], left_hand, &t);
    }

    // ページ送りボタン
    const float BTN_GAP = 0.01f, BTN_W = 0.05f;
    const float BTN_L_X = -(MAIN_W / 2.0f + BTN_GAP + BTN_W / 2.0f);
    const float BTN_R_X =  (MAIN_W / 2.0f + BTN_GAP + BTN_W / 2.0f);
    m_btn_newer.SetTransformTrackedDeviceRelative(left_hand, make(BTN_L_X, SUB_Y));
    m_btn_older.SetTransformTrackedDeviceRelative(left_hand, make(BTN_R_X, SUB_Y));
    // サブ画像ボタンは m_img_overlays[i] と同じハンドルなので位置は上のループで設定済み
}

std::vector<TriggerableButton*> CameraRollUI::Buttons() {
    if (!m_active) return {};
    std::vector<TriggerableButton*> result;
    result.push_back(&m_btn_newer);
    result.push_back(&m_btn_older);
    for (auto& b : m_sub_btns) result.push_back(&b);
    return result;
}

// private helpers called from lambdas
void CameraRollUI::OnNewerPage() {
    m_collection.LoadNewerPage();
    UploadImages();
    UpdateMainY();
}

void CameraRollUI::OnOlderPage() {
    m_collection.LoadOlderPage();
    UploadImages();
    UpdateMainY();
}
