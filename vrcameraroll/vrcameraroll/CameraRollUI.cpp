#include "CameraRollUI.h"
#include <algorithm>
#include <cstring>

static constexpr float BAR_FRAC      = 0.04f; // iPad風グレー帯の高さ比率（画像高さに対する割合）
static constexpr float SIDE_PAD_FRAC = 0.03f; // 左右グレー帯の幅比率（画像幅に対する割合）

// ────────────────────────────────────────────
// 内部テクスチャ生成ヘルパー
// ────────────────────────────────────────────

// 四隅を丸角にする（アルファチャンネルを透明化）
static void ApplyRoundedCorners(std::vector<uint8_t>& pixels, int w, int h, int radius) {
    if (radius <= 0) return;
    for (int y = 0; y < radius; ++y) {
        for (int x = 0; x < radius; ++x) {
            int dx = x - radius, dy = y - radius;
            if (dx*dx + dy*dy > radius*radius) {
                auto zero_alpha = [&](int px, int py) {
                    pixels[(py * w + px) * 4 + 3] = 0;
                };
                zero_alpha(x,       y);       // 左上
                zero_alpha(w-1-x,   y);       // 右上
                zero_alpha(x,       h-1-y);   // 左下
                zero_alpha(w-1-x,   h-1-y);   // 右下
            }
        }
    }
}

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
    // 丸角（サムネイルサイズの約12.5%）
    ApplyRoundedCorners(out, tw, tw, std::max(1, tw / 8));
    return out;
}

// active=true: 緑, active=false: グレーアウト
static std::vector<uint8_t> MakeArrowTexture(bool left_arrow, bool active = true) {
    constexpr int S = 64;
    std::vector<uint8_t> pixels(S * S * 4);
    uint8_t bg_r, bg_g, bg_b, bg_a, arr_r, arr_g, arr_b;
    if (active) {
        bg_r=40;  bg_g=110; bg_b=70;  bg_a=220;
        arr_r=110; arr_g=235; arr_b=150;
    } else {
        bg_r=65;  bg_g=65;  bg_b=68;  bg_a=160;
        arr_r=105; arr_g=105; arr_b=108;
    }
    for (int i = 0; i < S * S; ++i) {
        pixels[i*4+0]=bg_r; pixels[i*4+1]=bg_g;
        pixels[i*4+2]=bg_b; pixels[i*4+3]=bg_a;
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
                pixels[idx+0]=arr_r; pixels[idx+1]=arr_g;
                pixels[idx+2]=arr_b; pixels[idx+3]=255;
            }
        }
    }
    // 丸角
    ApplyRoundedCorners(pixels, S, S, S / 6);
    return pixels;
}

static void UploadGrey(vr::VROverlayHandle_t overlay) {
    static uint8_t grey[64 * 64 * 4];
    static bool init = false;
    if (!init) { std::memset(grey, 192, sizeof(grey)); init = true; }
    vr::VROverlay()->SetOverlayRaw(overlay, grey, 64, 64, 4);
}

// メイン画像をiPad風グレー帯付き・丸角でラップしたテクスチャを返す
// 上下に bar_h px、左右に side_pad px のグレー帯を追加する
struct PixelBuffer { std::vector<uint8_t> pixels; int w, h; };

static PixelBuffer MakeMainImageTexture(const Image& img) {
    int bar_h    = std::max(8,  (int)(img.height * BAR_FRAC));
    int side_pad = std::max(4,  (int)(img.width  * SIDE_PAD_FRAC));
    int w = img.width  + 2 * side_pad;
    int h = img.height + 2 * bar_h;
    std::vector<uint8_t> out(w * h * 4, 0);

    // キャンバス全体をグレーで塗る（上下帯 + 左右帯を一括）
    constexpr uint8_t BR = 55, BG = 55, BB = 58;
    for (int i = 0; i < w * h; ++i) {
        out[i*4+0]=BR; out[i*4+1]=BG; out[i*4+2]=BB; out[i*4+3]=255;
    }

    // 画像をキャンバス中央に貼り付け
    for (int y = 0; y < img.height; ++y) {
        const uint8_t* src = img.pixels.data() + y * img.width * 4;
        uint8_t*       dst = out.data() + (y + bar_h) * w * 4 + side_pad * 4;
        std::memcpy(dst, src, img.width * 4);
    }

    // 外枠に丸角
    int radius = std::min({ bar_h, side_pad, w / 20, h / 20 });
    ApplyRoundedCorners(out, w, h, radius);

    return { std::move(out), w, h };
}

// サブ画像ストリップの背景テクスチャ（ダーク角丸矩形）
static std::vector<uint8_t> MakeStripBgTexture(int w, int h) {
    std::vector<uint8_t> pixels(w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        pixels[i*4+0]=30; pixels[i*4+1]=30;
        pixels[i*4+2]=35; pixels[i*4+3]=190;
    }
    ApplyRoundedCorners(pixels, w, h, h / 3);
    return pixels;
}

// ────────────────────────────────────────────
// CameraRollUI
// ────────────────────────────────────────────

CameraRollUI::CameraRollUI()
    : m_btn_newer("camera_roll.btn_newer", "Newer", BTN_W, [this]{ OnNewerPage(); })
    , m_btn_older("camera_roll.btn_older", "Older", BTN_W, [this]{ OnOlderPage(); })
{
    // 背景オーバーレイ（sort order 0 = 最背面）
    vr::VROverlay()->CreateOverlay("camera_roll.bg", "Camera Roll BG", &m_bg_overlay);
    vr::VROverlay()->ShowOverlay(m_bg_overlay);
    vr::VROverlay()->SetOverlayInputMethod(m_bg_overlay, vr::VROverlayInputMethod_None);
    vr::VROverlay()->SetOverlaySortOrder(m_bg_overlay, 0);
    constexpr float BG_W = MAIN_W + 2.0f * (BTN_GAP + BTN_W);
    vr::VROverlay()->SetOverlayWidthInMeters(m_bg_overlay, BG_W);
    {
        constexpr int BG_TW = 512, BG_TH = 72;
        auto bg_tex = MakeStripBgTexture(BG_TW, BG_TH);
        vr::VROverlay()->SetOverlayRaw(m_bg_overlay, bg_tex.data(), BG_TW, BG_TH, 4);
    }

    // 画像オーバーレイ生成（sort order 1 = 中間）
    for (int i = 0; i < N; ++i) {
        char key[64], name[64];
        std::snprintf(key,  sizeof(key),  "camera_roll.img%d", i);
        std::snprintf(name, sizeof(name), "Camera Roll %d", i);
        vr::VROverlay()->CreateOverlay(key, name, &m_img_overlays[i]);
        vr::VROverlay()->ShowOverlay(m_img_overlays[i]);
        vr::VROverlay()->SetOverlaySortOrder(m_img_overlays[i], 1);
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

    // ページ送りボタンのテクスチャ（sort order 2 = 最前面、初期状態: 両方グレーアウト）
    m_btn_newer.UploadTexture(MakeArrowTexture(true,  false), 64, 64);
    m_btn_older.UploadTexture(MakeArrowTexture(false, false), 64, 64);
    vr::VROverlay()->SetOverlaySortOrder(m_btn_newer.Handle(), 2);
    vr::VROverlay()->SetOverlaySortOrder(m_btn_older.Handle(), 2);
    m_btn_newer.Show();
    m_btn_older.Show();

    // サブ画像 TriggerableButton
    for (int i = 1; i < N; ++i) {
        m_sub_btns.emplace_back(m_img_overlays[i], [this, idx = i - 1]{
            m_collection.SetMain(m_collection.Images().begin() + idx);
            const Image& mi = m_collection.Main();
            if (mi.IsLoaded()) {
                auto buf = MakeMainImageTexture(mi);
                vr::VROverlay()->SetOverlayRaw(m_img_overlays[0],
                    buf.pixels.data(), buf.w, buf.h, 4);
            }
            UpdateMainY();
        });
    }
}

CameraRollUI::~CameraRollUI() {
    vr::VROverlay()->DestroyOverlay(m_bg_overlay);
    for (int i = 0; i < N; ++i)
        vr::VROverlay()->DestroyOverlay(m_img_overlays[i]);
    // TriggerableButton のデストラクタが overlay を破棄する
}

void CameraRollUI::LoadImages(const std::filesystem::path& folder) {
    m_folder = folder;
    m_collection.LoadFromFolder(folder);
    UploadImages();
    UpdateMainY();
    UpdateArrowColors();
    m_observer.Start(folder, 0, [this](int offset) { ReloadAtOffset(offset); });
}

void CameraRollUI::PollFolderChanges() {
    m_observer.Poll();
}

void CameraRollUI::UploadImages() {
    // 画像が切り替わったらホバー状態をリセット（デフォルトはグレーアウト）
    m_hovered_sub_idx = -1;
    constexpr float DIM = 0.35f;
    for (int i = 1; i < N; ++i)
        vr::VROverlay()->SetOverlayColor(m_img_overlays[i], DIM, DIM, DIM);

    const Image& main_img = m_collection.Main();
    if (main_img.IsLoaded()) {
        auto buf = MakeMainImageTexture(main_img);
        vr::VROverlay()->SetOverlayRaw(
            m_img_overlays[0], buf.pixels.data(), buf.w, buf.h, 4);
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
    float aspect;
    if (img.IsLoaded() && img.width > 0) {
        int bar_h    = std::max(8, (int)(img.height * BAR_FRAC));
        int side_pad = std::max(4, (int)(img.width  * SIDE_PAD_FRAC));
        int new_w = img.width  + 2 * side_pad;
        int new_h = img.height + 2 * bar_h;
        aspect = (float)new_h / (float)new_w;
    } else {
        aspect = 1.0f;
    }
    float sub_top     = SUB_Y + SUB_W / 2.0f;
    float main_height = MAIN_W * aspect;
    m_img_layout[0].y = sub_top + 0.005f + main_height / 2.0f;
}

void CameraRollUI::UpdateArrowColors() {
    bool can_newer = !m_collection.IsAtNewest();
    bool can_older = !m_collection.IsAtOldest();
    m_btn_newer.UploadTexture(MakeArrowTexture(true,  can_newer), 64, 64);
    m_btn_older.UploadTexture(MakeArrowTexture(false, can_older), 64, 64);
}

void CameraRollUI::SetActive(bool active) {
    m_active = active;
    auto* ovr = vr::VROverlay();
    if (active) ovr->ShowOverlay(m_bg_overlay);
    else        ovr->HideOverlay(m_bg_overlay);
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

    // 背景オーバーレイ（sort order で前後制御するため z=0）
    {
        auto t = make(0.f, SUB_Y);
        vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(
            m_bg_overlay, left_hand, &t);
    }

    // ページ送りボタン（sort order 2 で常に前面）
    const float BTN_L_X = -(MAIN_W / 2.0f + BTN_GAP + BTN_W / 2.0f);
    const float BTN_R_X =  (MAIN_W / 2.0f + BTN_GAP + BTN_W / 2.0f);
    m_btn_newer.SetTransformTrackedDeviceRelative(left_hand, make(BTN_L_X, SUB_Y));
    m_btn_older.SetTransformTrackedDeviceRelative(left_hand, make(BTN_R_X, SUB_Y));
    // サブ画像ボタンは m_img_overlays[i] と同じハンドルなので位置は上のループで設定済み
}

void CameraRollUI::UpdateHover(TriggerableButton* hit) {
    // どのサブ画像がホバーされているか特定
    int new_hover = -1;
    for (int i = 0; i < (int)m_sub_btns.size(); ++i) {
        if (hit == &m_sub_btns[i]) { new_hover = i; break; }
    }
    if (new_hover == m_hovered_sub_idx) return; // 変化なし
    m_hovered_sub_idx = new_hover;

    // 常にグレーアウト基準。ホバー中の1枚だけ通常輝度にする。
    constexpr float DIM = 0.35f;
    for (int i = 1; i < N; ++i) {
        float c = (i - 1 == new_hover) ? 1.0f : DIM;
        vr::VROverlay()->SetOverlayColor(m_img_overlays[i], c, c, c);
    }
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

void CameraRollUI::ReloadAtOffset(int offset) {
    m_collection.LoadFromFolder(m_folder, offset);
    UploadImages();
    UpdateMainY();
    UpdateArrowColors();
}

void CameraRollUI::OnNewerPage() {
    m_collection.LoadNewerPage();
    m_observer.SetOffset(m_collection.GetOffset());
    UploadImages();
    UpdateMainY();
    UpdateArrowColors();
}

void CameraRollUI::OnOlderPage() {
    m_collection.LoadOlderPage();
    m_observer.SetOffset(m_collection.GetOffset());
    UploadImages();
    UpdateMainY();
    UpdateArrowColors();
}
