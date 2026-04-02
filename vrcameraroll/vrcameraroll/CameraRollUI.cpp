#include <Windows.h>
#include "CameraRollUI.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

static constexpr float BAR_FRAC      = 0.04f; // iPad風グレー帯の高さ比率（画像高さに対する割合）
static constexpr float SIDE_PAD_FRAC = 0.03f; // 左右グレー帯の幅比率（画像幅に対する割合）

// ────────────────────────────────────────────
// ナビゲーション用ヘルパー
// ────────────────────────────────────────────

static const std::vector<std::wstring> kImageExts = { L".png", L".jpg", L".jpeg", L".bmp" };

static bool IsImageFile(const std::filesystem::path& p) {
    auto ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    for (const auto& e : kImageExts) if (ext == e) return true;
    return false;
}

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
            out[dst+0]=(uint8_t)(r/count); out[dst+1]=(uint8_t)(g/count);
            out[dst+2]=(uint8_t)(b/count); out[dst+3]=(uint8_t)(a/count);
        }
    }
    // 丸角（サムネイルサイズの約12.5%）
    ApplyRoundedCorners(out, tw, tw, max(1, tw / 8));
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
    int bar_h    = max(8,  (int)(img.height * BAR_FRAC));
    int side_pad = max(4,  (int)(img.width  * SIDE_PAD_FRAC));
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
    int radius = min(min(min(bar_h, side_pad), w / 20), h / 20 );
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

// フォルダアイコンテクスチャを GDI で生成する（64×64 RGBA）
// is_back=true: 青灰色（戻るフォルダ）, false: 黄色（通常フォルダ）
static std::vector<uint8_t> MakeFolderTexture(const std::wstring& name, bool is_back) {
    constexpr int W = 64, H = 64;

    // DIBSection を作成（top-down: biHeight 負値）
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = W;
    bmi.bmiHeader.biHeight      = -H; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC hdc = CreateCompatibleDC(nullptr);
    HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ hbm_old = SelectObject(hdc, hbm);

    // 背景: RGB(30,30,35)
    {
        HBRUSH hbr = CreateSolidBrush(RGB(30, 30, 35));
        RECT rc = { 0, 0, W, H };
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);
    }

    // フォルダ形状を描く
    if (!is_back) {
        // 通常フォルダ: 黄色系
        // タブ
        HBRUSH hbr_tab = CreateSolidBrush(RGB(195, 150, 30));
        RECT tab_rc = { 5, 8, 26, 17 };
        FillRect(hdc, &tab_rc, hbr_tab);
        DeleteObject(hbr_tab);
        // ボディ
        HBRUSH hbr_body = CreateSolidBrush(RGB(235, 185, 45));
        RECT body_rc = { 3, 15, 61, 47 };
        FillRect(hdc, &body_rc, hbr_body);
        DeleteObject(hbr_body);
        // ハイライト
        HBRUSH hbr_hl = CreateSolidBrush(RGB(255, 215, 90));
        RECT hl_rc = { 3, 15, 61, 20 };
        FillRect(hdc, &hl_rc, hbr_hl);
        DeleteObject(hbr_hl);
    } else {
        // 戻るフォルダ: 青灰色系
        HBRUSH hbr_tab = CreateSolidBrush(RGB(80, 100, 130));
        RECT tab_rc = { 5, 8, 26, 17 };
        FillRect(hdc, &tab_rc, hbr_tab);
        DeleteObject(hbr_tab);
        HBRUSH hbr_body = CreateSolidBrush(RGB(100, 125, 165));
        RECT body_rc = { 3, 15, 61, 47 };
        FillRect(hdc, &body_rc, hbr_body);
        DeleteObject(hbr_body);
        HBRUSH hbr_hl = CreateSolidBrush(RGB(130, 155, 195));
        RECT hl_rc = { 3, 15, 61, 20 };
        FillRect(hdc, &hl_rc, hbr_hl);
        DeleteObject(hbr_hl);
    }

    // テキスト描画
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(230, 230, 230));

    bool font_created = false;
    HFONT hfont = CreateFontW(
        -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Yu Gothic UI");
    if (hfont) {
        font_created = true;
    } else {
        hfont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    HGDIOBJ hfont_old = SelectObject(hdc, hfont);

    std::wstring label;
    if (is_back) {
        label = L"\u2191"; // ↑
    } else {
        label = name;
        if (label.size() > 7) {
            label = label.substr(0, 6) + L"..";
        }
    }

    RECT text_rc = { 1, 48, 63, 63 };
    DrawTextW(hdc, label.c_str(), -1, &text_rc,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // フォントを元に戻してから削除（ストックオブジェクトは削除しない）
    SelectObject(hdc, hfont_old);
    if (font_created) DeleteObject(hfont);

    // GDI フラッシュ
    GdiFlush();

    // BGRA → RGBA 変換、A=255 に設定
    std::vector<uint8_t> result(W * H * 4);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(bits);
    for (int i = 0; i < W * H; ++i) {
        result[i * 4 + 0] = src[i * 4 + 2]; // R ← B (GDI は BGR)
        result[i * 4 + 1] = src[i * 4 + 1]; // G
        result[i * 4 + 2] = src[i * 4 + 0]; // B ← R
        result[i * 4 + 3] = 255;             // A
    }

    // 丸角
    ApplyRoundedCorners(result, W, H, 8);

    // クリーンアップ
    SelectObject(hdc, hbm_old);
    DeleteObject(hbm);
    DeleteDC(hdc);

    return result;
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

    // サブ画像 TriggerableButton: スロットタイプに応じてナビゲーションまたはメイン選択
    for (int i = 1; i < N; ++i) {
        m_sub_btns.emplace_back(m_img_overlays[i], [this, slot = i - 1]{
            int nav_idx = m_nav_offset + slot;
            if (nav_idx >= (int)m_nav_items.size()) return;
            const NavItem& item = m_nav_items[nav_idx];
            if (item.type == SlotType::BackDir || item.type == SlotType::SubDir)
                NavigateInto(item.path);
            else if (item.type == SlotType::Image)
                SetMainImage(nav_idx);
        });
    }
}

CameraRollUI::~CameraRollUI() {
    m_observer.Stop();
    vr::VROverlay()->DestroyOverlay(m_bg_overlay);
    for (int i = 0; i < N; ++i)
        vr::VROverlay()->DestroyOverlay(m_img_overlays[i]);
    // TriggerableButton のデストラクタが overlay を破棄する
}

void CameraRollUI::LoadImages(const std::filesystem::path& folder) {
    m_observer.Stop();
    try { m_root_dir = std::filesystem::weakly_canonical(folder); }
    catch (...) { m_root_dir = folder; }
    m_current_dir  = m_root_dir;
    m_nav_offset   = 0;
    m_main_image.Unload();
    m_main_nav_idx = -1;
    RefreshAndDisplay();
}

void CameraRollUI::PollFolderChanges() {
    m_observer.Poll();
}

// ────────────────────────────────────────────
// ナビゲーション private helpers
// ────────────────────────────────────────────

void CameraRollUI::RefreshNavItems() {
    m_nav_items.clear();

    std::error_code ec;
    if (!std::filesystem::exists(m_current_dir, ec)) return;

    // 戻るスロット（root でない場合）
    if (m_current_dir != m_root_dir) {
        NavItem back;
        back.type         = SlotType::BackDir;
        back.path         = m_current_dir.parent_path();
        back.display_name = L"..";
        m_nav_items.push_back(std::move(back));
    }

    // サブディレクトリと画像を収集
    std::vector<std::filesystem::path> subdirs;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> images;

    for (const auto& entry : std::filesystem::directory_iterator(m_current_dir, ec)) {
        if (ec) break;
        std::error_code ec2;
        if (entry.is_directory(ec2)) {
            subdirs.push_back(entry.path());
        } else if (entry.is_regular_file(ec2) && IsImageFile(entry.path())) {
            std::filesystem::file_time_type mtime = entry.last_write_time(ec2);
            images.push_back({ mtime, entry.path() });
        }
    }

    // サブディレクトリを名前昇順ソート
    std::sort(subdirs.begin(), subdirs.end(), [](const auto& a, const auto& b) {
        return a.filename().wstring() < b.filename().wstring();
    });

    // 画像を更新日時の降順ソート（最新が先）
    std::sort(images.begin(), images.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    // サブディレクトリアイテムを追加
    for (const auto& sd : subdirs) {
        NavItem item;
        item.type         = SlotType::SubDir;
        item.path         = sd;
        item.display_name = sd.filename().wstring();
        m_nav_items.push_back(std::move(item));
    }

    // 画像アイテムを追加
    for (const auto& [mtime, p] : images) {
        NavItem item;
        item.type         = SlotType::Image;
        item.path         = p;
        item.display_name = L"";
        m_nav_items.push_back(std::move(item));
    }

    std::printf("NavItems: %zu items in: %s\n",
        m_nav_items.size(),
        m_current_dir.u8string().c_str());
}

void CameraRollUI::UploadNavSlots() {
    // ホバーリセット
    m_hovered_sub_idx = -1;
    constexpr float DIM = 0.35f;
    for (int i = 1; i < N; ++i)
        vr::VROverlay()->SetOverlayColor(m_img_overlays[i], DIM, DIM, DIM);

    // 既存スロット画像をアンロード
    for (auto& si : m_slot_images) si.Unload();

    for (int slot = 0; slot < N - 1; ++slot) {
        int nav_idx = m_nav_offset + slot;
        vr::VROverlayHandle_t overlay = m_img_overlays[slot + 1];

        if (nav_idx >= (int)m_nav_items.size()) {
            UploadGrey(overlay);
            continue;
        }

        const NavItem& item = m_nav_items[nav_idx];

        if (item.type == SlotType::BackDir || item.type == SlotType::SubDir) {
            bool is_back = (item.type == SlotType::BackDir);
            auto tex = MakeFolderTexture(item.display_name, is_back);
            vr::VROverlay()->SetOverlayRaw(overlay, tex.data(), 64, 64, 4);
        } else {
            // Image スロット
            m_slot_images[slot].LoadFromFile(item.path);
            if (m_slot_images[slot].IsLoaded()) {
                auto thumb = MakeCroppedThumbnail(m_slot_images[slot], 8);
                int tw = min(m_slot_images[slot].width,
                                  m_slot_images[slot].height) / 8;
                vr::VROverlay()->SetOverlayRaw(overlay, thumb.data(), tw, tw, 4);
            } else {
                UploadGrey(overlay);
            }
        }
    }

    UpdateArrowColors();
}

void CameraRollUI::RefreshAndDisplay() {
    RefreshNavItems();
    UploadNavSlots();

    // 最初の Image アイテムを探してメイン表示
    int first_img_idx = -1;
    for (int i = 0; i < (int)m_nav_items.size(); ++i) {
        if (m_nav_items[i].type == SlotType::Image) {
            first_img_idx = i;
            break;
        }
    }

    if (first_img_idx >= 0) {
        SetMainImage(first_img_idx);
        m_observer.Start(m_current_dir, 0, [this](int) { ReloadCurrentDir(); });
    } else {
        UploadGrey(m_img_overlays[0]);
        UpdateMainY();
    }
}

void CameraRollUI::NavigateInto(const std::filesystem::path& dir) {
    m_observer.Stop();
    try { m_current_dir = std::filesystem::weakly_canonical(dir); }
    catch (...) { m_current_dir = dir; }
    m_nav_offset   = 0;
    m_main_image.Unload();
    m_main_nav_idx = -1;
    RefreshAndDisplay();
}

void CameraRollUI::ReloadCurrentDir() {
    // 以前のメイン画像パスを保存
    std::filesystem::path prev_main_path;
    if (m_main_nav_idx >= 0 && m_main_nav_idx < (int)m_nav_items.size()) {
        prev_main_path = m_nav_items[m_main_nav_idx].path;
    }

    RefreshNavItems();

    // nav_offset をクランプ
    int max_offset = max(0, (int)m_nav_items.size() - (N - 1));
    m_nav_offset   = min(m_nav_offset, max_offset);

    UploadNavSlots();

    // 前のメイン画像を再探索
    int new_main = -1;
    if (!prev_main_path.empty()) {
        for (int i = 0; i < (int)m_nav_items.size(); ++i) {
            if (m_nav_items[i].type == SlotType::Image &&
                m_nav_items[i].path == prev_main_path)
            {
                new_main = i;
                break;
            }
        }
    }

    // 見つからなければ最初の Image を使用
    if (new_main < 0) {
        for (int i = 0; i < (int)m_nav_items.size(); ++i) {
            if (m_nav_items[i].type == SlotType::Image) {
                new_main = i;
                break;
            }
        }
    }

    if (new_main >= 0) {
        SetMainImage(new_main);
    } else {
        // 画像が一枚もない
        m_main_image.Unload();
        m_main_nav_idx = -1;
        UploadGrey(m_img_overlays[0]);
        UpdateMainY();
    }
}

void CameraRollUI::SetMainImage(int nav_idx) {
    m_main_nav_idx = nav_idx;
    m_main_image.LoadFromFile(m_nav_items[nav_idx].path);
    if (m_main_image.IsLoaded()) {
        auto buf = MakeMainImageTexture(m_main_image);
        vr::VROverlay()->SetOverlayRaw(
            m_img_overlays[0], buf.pixels.data(), buf.w, buf.h, 4);
    } else {
        UploadGrey(m_img_overlays[0]);
    }
    UpdateMainY();
}

void CameraRollUI::UpdateMainY() {
    float aspect;
    if (m_main_image.IsLoaded() && m_main_image.width > 0) {
        int bar_h    = max(8, (int)(m_main_image.height * BAR_FRAC));
        int side_pad = max(4, (int)(m_main_image.width  * SIDE_PAD_FRAC));
        int new_w = m_main_image.width  + 2 * side_pad;
        int new_h = m_main_image.height + 2 * bar_h;
        aspect = (float)new_h / (float)new_w;
    } else {
        aspect = 1.0f;
    }
    float sub_top     = SUB_Y + SUB_W / 2.0f;
    float main_height = MAIN_W * aspect;
    m_img_layout[0].y = sub_top + 0.005f + main_height / 2.0f;
}

void CameraRollUI::UpdateArrowColors() {
    bool can_newer = m_nav_offset > 0;
    bool can_older = m_nav_offset + (N - 1) < (int)m_nav_items.size();
    m_btn_newer.UploadTexture(MakeArrowTexture(true,  can_newer), 64, 64);
    m_btn_older.UploadTexture(MakeArrowTexture(false, can_older), 64, 64);
}

void CameraRollUI::OnNewerPage() {
    if (m_nav_offset == 0) return;
    m_nav_offset = max(0, m_nav_offset - (N - 1));
    UploadNavSlots();
}

void CameraRollUI::OnOlderPage() {
    int max_offset = max(0, (int)m_nav_items.size() - (N - 1));
    if (m_nav_offset >= max_offset) return;
    m_nav_offset = min(m_nav_offset + (N - 1), max_offset);
    UploadNavSlots();
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
