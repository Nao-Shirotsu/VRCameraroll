#pragma execution_character_set("utf-8")
#define LASER_UI
#include <openvr.h>
#include <Windows.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <array>
#include <vector>
#include <algorithm>
#include "ImageCollection.h"

static const char* kImageFolder = "C:\\Users\\albus\\Pictures\\VRChat\\2026-03";

// ────────────────────────────────────────────
// テクスチャ生成
// ────────────────────────────────────────────

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
            out[dst+0]=r/count; out[dst+1]=g/count; out[dst+2]=b/count; out[dst+3]=a/count;
        }
    }
    return out;
}

// ────────────────────────────────────────────
// ピクセルフォント (4x5 ビットマップ, 2x2スケール描画)
// ビット3が左端、ビット0が右端
// ────────────────────────────────────────────

struct GlyphDef { char c; uint8_t rows[5]; };
static const GlyphDef kFont[] = {
    {'R', {0xE, 0x9, 0xE, 0xA, 0x9}},  // 1110 1001 1110 1010 1001
    {'x', {0x9, 0x6, 0x0, 0x6, 0x9}},  // 1001 0110 0000 0110 1001
    {'y', {0x9, 0x9, 0x7, 0x1, 0xE}},  // 1001 1001 0111 0001 1110
    {'z', {0xF, 0x1, 0x6, 0x8, 0xF}},  // 1111 0001 0110 1000 1111
    {'T', {0xF, 0x6, 0x6, 0x6, 0x6}},  // 1111 0110 0110 0110 0110
    {'C', {0xE, 0x8, 0x8, 0x8, 0xE}},  // 1110 1000 1000 1000 1110
    {'o', {0x6, 0x9, 0x9, 0x9, 0x6}},  // 0110 1001 1001 1001 0110
};

static void DrawGlyph(std::vector<uint8_t>& pixels, int S,
    int left_x, int top_y, char ch, int scale = 2)
{
    for (const auto& g : kFont) {
        if (g.c != ch) continue;
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 4; ++col) {
                if (!((g.rows[row] >> (3 - col)) & 1)) continue;
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        int px = left_x + col * scale + dx;
                        int py = top_y  + row * scale + dy;
                        if (px < 0 || px >= S || py < 0 || py >= S) continue;
                        int idx = (py * S + px) * 4;
                        pixels[idx+0]=255; pixels[idx+1]=255;
                        pixels[idx+2]=255; pixels[idx+3]=255;
                    }
                }
            }
        }
        return;
    }
}

// 64x64 ラベルのみボタンテクスチャ（矢印なし、ラベル中央揃え）
static std::vector<uint8_t> MakeLabelTexture(const char* label,
    uint8_t bg_r = 60, uint8_t bg_g = 100, uint8_t bg_b = 60)
{
    constexpr int S = 64, scale = 2, char_w = 4*scale, gap = 2;
    std::vector<uint8_t> pixels(S * S * 4);
    for (int i = 0; i < S * S; ++i) {
        pixels[i*4+0]=bg_r; pixels[i*4+1]=bg_g; pixels[i*4+2]=bg_b; pixels[i*4+3]=180;
    }
    int n = (int)strlen(label);
    int lx = (S - (n*char_w + (n-1)*gap)) / 2;
    int ly = (S - 5*scale) / 2;
    for (int i = 0; i < n; ++i)
        DrawGlyph(pixels, S, lx + i*(char_w+gap), ly, label[i], scale);
    return pixels;
}

// 64x64 矢印テクスチャ。label が非空なら下部にラベル描画。
static std::vector<uint8_t> MakeArrowTexture(bool left_arrow, const char* label = "",
    uint8_t bg_r = 80, uint8_t bg_g = 80, uint8_t bg_b = 80)
{
    constexpr int S = 64;
    std::vector<uint8_t> pixels(S * S * 4, 0);
    for (int i = 0; i < S * S; ++i) {
        pixels[i*4+0]=bg_r; pixels[i*4+1]=bg_g; pixels[i*4+2]=bg_b; pixels[i*4+3]=180;
    }

    const bool has_label = label && label[0] != '\0';
    const int cy   = has_label ? 18 : S / 2;
    const int half = has_label ? 13 : S / 4;
    const int ymax = has_label ? 40 : S;

    for (int y = 0; y < ymax; ++y) {
        for (int x = 0; x < S; ++x) {
            int dx = x - S/2, dy = y - cy;
            int ady = abs(dy);
            bool tri = left_arrow
                ? (dx >= -half && dx <= 0 && ady <= half + dx)
                : (dx <=  half && dx >= 0 && ady <= half - dx);
            if (tri) {
                int idx = (y * S + x) * 4;
                pixels[idx+0]=220; pixels[idx+1]=220; pixels[idx+2]=220; pixels[idx+3]=255;
            }
        }
    }

    // ラベル: 2文字 × (4*2=8px) + 2px gap = 18px幅, y=46 から描画
    if (has_label) {
        constexpr int scale = 2, char_w = 4 * scale, gap = 2;
        int n = (int)strlen(label);
        int lx = (S - (n * char_w + (n - 1) * gap)) / 2;
        for (int i = 0; i < n; ++i)
            DrawGlyph(pixels, S, lx + i * (char_w + gap), 46, label[i], scale);
    }

    return pixels;
}

// 4x256 px の単色ライン用テクスチャ（赤/半透明）
static std::vector<uint8_t> MakePointerTexture() {
    constexpr int W = 4, H = 256;
    std::vector<uint8_t> pixels(W * H * 4);
    for (int i = 0; i < W * H; ++i) {
        pixels[i*4+0] = 255; pixels[i*4+1] = 60;
        pixels[i*4+2] = 60;  pixels[i*4+3] = 220;
    }
    return pixels;
}

static void UploadGrey(vr::VROverlayHandle_t overlay) {
    static uint8_t grey[64 * 64 * 4];
    static bool init = false;
    if (!init) { memset(grey, 128, sizeof(grey)); init = true; }
    vr::VROverlay()->SetOverlayRaw(overlay, grey, 64, 64, 4);
}

// ────────────────────────────────────────────
// 3x3 回転行列（ポインターラインのローカル軸回転用）
// ────────────────────────────────────────────
struct Mat3 {
    float m[3][3] = {};
    static Mat3 identity() {
        Mat3 r; r.m[0][0]=r.m[1][1]=r.m[2][2]=1.f; return r;
    }
    static Mat3 rotX(float a) {
        Mat3 r = identity();
        r.m[1][1]= cosf(a); r.m[1][2]=-sinf(a);
        r.m[2][1]= sinf(a); r.m[2][2]= cosf(a);
        return r;
    }
    static Mat3 rotY(float a) {
        Mat3 r = identity();
        r.m[0][0]= cosf(a); r.m[0][2]= sinf(a);
        r.m[2][0]=-sinf(a); r.m[2][2]= cosf(a);
        return r;
    }
    static Mat3 rotZ(float a) {
        Mat3 r = identity();
        r.m[0][0]= cosf(a); r.m[0][1]=-sinf(a);
        r.m[1][0]= sinf(a); r.m[1][1]= cosf(a);
        return r;
    }
    Mat3 operator*(const Mat3& o) const {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }
};

// ────────────────────────────────────────────
// トランスフォーム計算
//
// 左コントローラーの元々の座標軸を基底とした外因的回転 + 平行移動
//
// 外因的回転 (extrinsic): コントローラー固有軸周りに X→Y→Z の順で独立適用
//   R = Rz(rotZ) * Ry(rotY) * Rx(rotX)
//
// overlay の中心位置 (コントローラー空間):
//   t = R * [x, y, z]^T + [offsetX, offsetY, offsetZ]^T
//
// R = Rz*Ry*Rx 展開:
//   [[cz*cy,  cz*sy*sx - sz*cx,  cz*sy*cx + sz*sx],
//    [sz*cy,  sz*sy*sx + cz*cx,  sz*sy*cx - cz*sx],
//    [  -sy,           cy*sx,             cy*cx  ]]
// ────────────────────────────────────────────
static vr::HmdMatrix34_t MakeTransform(
    float x,  float y,  float z,
    float rotX, float rotY, float rotZ,
    float offsetX, float offsetY, float offsetZ)
{
    const float cx = cosf(rotX), sx = sinf(rotX);
    const float cy = cosf(rotY), sy = sinf(rotY);
    const float cz = cosf(rotZ), sz = sinf(rotZ);

    const float r00 = cz*cy,              r01 = cz*sy*sx - sz*cx,  r02 = cz*sy*cx + sz*sx;
    const float r10 = sz*cy,              r11 = sz*sy*sx + cz*cx,  r12 = sz*sy*cx - cz*sx;
    const float r20 = -sy,                r21 = cy*sx,              r22 = cy*cx;

    vr::HmdMatrix34_t t = {};
    t.m[0][0]=r00; t.m[0][1]=r01; t.m[0][2]=r02;
    t.m[1][0]=r10; t.m[1][1]=r11; t.m[1][2]=r12;
    t.m[2][0]=r20; t.m[2][1]=r21; t.m[2][2]=r22;
    t.m[0][3] = r00*x + r01*y + r02*z + offsetX;
    t.m[1][3] = r10*x + r11*y + r12*z + offsetY;
    t.m[2][3] = r20*x + r21*y + r22*z + offsetZ;
    return t;
}

static float CalcMainCenterY(float sub_center_y, float sub_w, float main_w, const Image& img) {
    float aspect      = (img.IsLoaded() && img.width > 0)
                        ? (float)img.height / (float)img.width : 1.0f;
    float sub_top     = sub_center_y + sub_w / 2.0f;
    float gap         = 0.005f;
    float main_height = main_w * aspect;
    return sub_top + gap + main_height / 2.0f;
}

static void UploadImages(
    const std::array<vr::VROverlayHandle_t, ImageCollection::N>& overlays,
    ImageCollection& col)
{
    const Image& main_img = col.Main();
    if (main_img.IsLoaded()) {
        vr::VROverlay()->SetOverlayRaw(
            overlays[0], (void*)main_img.pixels.data(), main_img.width, main_img.height, 4);
    } else {
        UploadGrey(overlays[0]);
    }
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
        return 1;
    }

    constexpr int N = ImageCollection::N;

    // UI パラメータ (全ビルド共通)
    float rot_x    = 0.0f;
    float rot_y    = 3.14159265f / 2.0f; // 90°
    float rot_z    = 0.0f;
    float offset_x = -0.1f;
    float offset_y = 0.0f;
    float offset_z = 0.0f;

    // ── 画像オーバーレイ ──
    std::array<vr::VROverlayHandle_t, N> img_overlays;
    for (int i = 0; i < N; ++i) {
        char key[64], name[64];
        snprintf(key,  sizeof(key),  "camera_roll.img%d", i);
        snprintf(name, sizeof(name), "Camera Roll %d", i);
        vr::VROverlay()->CreateOverlay(key, name, &img_overlays[i]);
        vr::VROverlay()->ShowOverlay(img_overlays[i]);
    }

    const float MAIN_W = 0.25f;
    const float SUB_W  = MAIN_W / (N - 1);
    const float SUB_Y  = -0.03f;
    const float SUB_X0 = -MAIN_W / 2.0f + SUB_W / 2.0f;

    struct Layout { float x, y, z, w; };
    std::array<Layout, N> img_layout;
    img_layout[0] = { 0.0f, 0.0f, 0.0f, MAIN_W };
    for (int i = 1; i < N; ++i)
        img_layout[i] = { SUB_X0 + (i - 1) * SUB_W, SUB_Y, 0.0f, SUB_W };
    for (int i = 0; i < N; ++i) {
        vr::VROverlay()->SetOverlayWidthInMeters(img_overlays[i], img_layout[i].w);
        UploadGrey(img_overlays[i]);
    }
    for (int i = 1; i < N; ++i)
        vr::VROverlay()->SetOverlayInputMethod(img_overlays[i], vr::VROverlayInputMethod_None);

    // ── ページ送りボタン (←・→) ──
    const float BTN_W   = 0.05f;
    const float BTN_GAP = 0.01f;
    const float BTN_L_X = -(MAIN_W / 2.0f + BTN_GAP + BTN_W / 2.0f);
    const float BTN_R_X =  (MAIN_W / 2.0f + BTN_GAP + BTN_W / 2.0f);

    vr::VROverlayHandle_t btn_newer, btn_older;
    vr::VROverlay()->CreateOverlay("camera_roll.btn_newer", "Newer", &btn_newer);
    vr::VROverlay()->CreateOverlay("camera_roll.btn_older", "Older", &btn_older);
    vr::VROverlay()->SetOverlayWidthInMeters(btn_newer, BTN_W);
    vr::VROverlay()->SetOverlayWidthInMeters(btn_older, BTN_W);
    {
        auto al = MakeArrowTexture(true);
        auto ar = MakeArrowTexture(false);
        vr::VROverlay()->SetOverlayRaw(btn_newer, al.data(), 64, 64, 4);
        vr::VROverlay()->SetOverlayRaw(btn_older, ar.data(), 64, 64, 4);
    }
    for (auto h : { btn_newer, btn_older }) {
        vr::VROverlay()->SetOverlayInputMethod(h, vr::VROverlayInputMethod_None);
        vr::VROverlay()->ShowOverlay(h);
    }

    // ────────────────────────────────────────────
    // [DEBUG] 調整ボタン: 上段 Rx/Ry/Rz, 下段 Tx/Ty/Tz
    //
    // 各段: [X-][X+] [Y-][Y+] [Z-][Z+] を横並び
    // 背景色: X=赤, Y=緑, Z=青
    // ────────────────────────────────────────────
#ifdef _DEBUG
    constexpr float ROT_STEP  = 3.0f * 3.14159265f / 180.0f; // 3°
    constexpr float MOVE_STEP = 0.01f;                         // 1cm

    constexpr float ADJ_W   = 0.025f;
    constexpr float ADJ_GAP = 0.003f;
    constexpr float ADJ_GRP = 0.008f;

    // 6ボタン1行の中心X座標を計算するラムダ
    float adj_xs[6];
    {
        const float total = 6*ADJ_W + 4*ADJ_GAP + 2*ADJ_GRP;
        float cur = -total / 2.0f + ADJ_W / 2.0f;
        adj_xs[0] = cur;
        adj_xs[1] = (cur += ADJ_W + ADJ_GAP);
        adj_xs[2] = (cur += ADJ_W + ADJ_GRP);
        adj_xs[3] = (cur += ADJ_W + ADJ_GAP);
        adj_xs[4] = (cur += ADJ_W + ADJ_GRP);
        adj_xs[5] = (cur += ADJ_W + ADJ_GAP);
    }
    const float ADJ_ROW1_Y = SUB_Y - SUB_W / 2.0f - ADJ_GAP - ADJ_W / 2.0f; // 上段(回転)
    const float ADJ_ROW2_Y = ADJ_ROW1_Y - ADJ_W - ADJ_GAP;                   // 下段(移動)

    // ハンドル: [0]=Rx-, [1]=Rx+, [2]=Ry-, [3]=Ry+, [4]=Rz-, [5]=Rz+
    //           [6]=Tx-, [7]=Tx+, [8]=Ty-, [9]=Ty+, [10]=Tz-,[11]=Tz+
    vr::VROverlayHandle_t adj[12];
    const char* adj_keys[12] = {
        "cr.rx_m","cr.rx_p","cr.ry_m","cr.ry_p","cr.rz_m","cr.rz_p",
        "cr.tx_m","cr.tx_p","cr.ty_m","cr.ty_p","cr.tz_m","cr.tz_p",
    };
    const char* adj_names[12] = {
        "Rx-","Rx+","Ry-","Ry+","Rz-","Rz+",
        "Tx-","Tx+","Ty-","Ty+","Tz-","Tz+",
    };
    for (int i = 0; i < 12; ++i)
        vr::VROverlay()->CreateOverlay(adj_keys[i], adj_names[i], &adj[i]);
    for (int i = 0; i < 12; ++i)
        vr::VROverlay()->SetOverlayWidthInMeters(adj[i], ADJ_W);

    // テクスチャ: X=赤(120,50,50), Y=緑(50,110,50), Z=青(50,50,120)
    struct { const char* label; uint8_t r,g,b; } adj_info[12] = {
        {"Rx",120,50, 50}, {"Rx",120,50, 50},
        {"Ry", 50,50,120}, {"Ry", 50,50,120},
        {"Rz", 50,110,50}, {"Rz", 50,110,50},
        {"Tx",120,50, 50}, {"Tx",120,50, 50},
        {"Ty", 50,50,120}, {"Ty", 50,50,120},
        {"Tz", 50,110,50}, {"Tz", 50,110,50},
    };
    for (int i = 0; i < 12; ++i) {
        bool left = (i % 2 == 0);
        auto tex = MakeArrowTexture(left, adj_info[i].label,
            adj_info[i].r, adj_info[i].g, adj_info[i].b);
        vr::VROverlay()->SetOverlayRaw(adj[i], tex.data(), 64, 64, 4);
    }
    for (int i = 0; i < 12; ++i) {
        vr::VROverlay()->SetOverlayInputMethod(adj[i], vr::VROverlayInputMethod_None);
        vr::VROverlay()->ShowOverlay(adj[i]);
    }

#ifdef LASER_UI
    // ── ポインターライン調整ボタン (第3・4行) ──
    const float ADJ_ROW3_Y = ADJ_ROW2_Y - ADJ_W - ADJ_GAP; // ポインター回転
    const float ADJ_ROW4_Y = ADJ_ROW3_Y - ADJ_W - ADJ_GAP; // ポインター移動

    vr::VROverlayHandle_t ptr_adj[12];
    const char* ptr_adj_keys[12] = {
        "cr.prx_m","cr.prx_p","cr.pry_m","cr.pry_p","cr.prz_m","cr.prz_p",
        "cr.ptx_m","cr.ptx_p","cr.pty_m","cr.pty_p","cr.ptz_m","cr.ptz_p",
    };
    const char* ptr_adj_names[12] = {
        "PRx-","PRx+","PRy-","PRy+","PRz-","PRz+",
        "PTx-","PTx+","PTy-","PTy+","PTz-","PTz+",
    };
    for (int i = 0; i < 12; ++i)
        vr::VROverlay()->CreateOverlay(ptr_adj_keys[i], ptr_adj_names[i], &ptr_adj[i]);
    for (int i = 0; i < 12; ++i)
        vr::VROverlay()->SetOverlayWidthInMeters(ptr_adj[i], ADJ_W);
    for (int i = 0; i < 12; ++i) {
        bool left = (i % 2 == 0);
        auto tex = MakeArrowTexture(left, adj_info[i].label,
            adj_info[i].r, adj_info[i].g, adj_info[i].b);
        vr::VROverlay()->SetOverlayRaw(ptr_adj[i], tex.data(), 64, 64, 4);
    }
    for (int i = 0; i < 12; ++i) {
        vr::VROverlay()->SetOverlayInputMethod(ptr_adj[i], vr::VROverlayInputMethod_None);
        vr::VROverlay()->ShowOverlay(ptr_adj[i]);
    }

    // "Co" ボタン: 現在の ptr_rot / ptr_t を stdout に出力
    const float BTN_COUT_Y = ADJ_ROW4_Y - ADJ_W - ADJ_GAP;
    vr::VROverlayHandle_t btn_cout;
    vr::VROverlay()->CreateOverlay("cr.btn_cout", "Cout", &btn_cout);
    vr::VROverlay()->SetOverlayWidthInMeters(btn_cout, ADJ_W);
    {
        auto tex = MakeLabelTexture("Co");
        vr::VROverlay()->SetOverlayRaw(btn_cout, tex.data(), 64, 64, 4);
    }
    {
        vr::VROverlay()->SetOverlayInputMethod(btn_cout, vr::VROverlayInputMethod_None);
        vr::VROverlay()->ShowOverlay(btn_cout);
    }
#endif // LASER_UI
#endif // _DEBUG

    // ── 右コントローラー正面方向ライン ──
    // Rx(+90°) で overlay の height 方向をコントローラーの +Z（正面）に揃える
    // 幅 0.004m × 高さ 0.256m (4x256 テクスチャ)、中心を z=0.128m に配置
    vr::VROverlayHandle_t ptr_line;
    vr::VROverlay()->CreateOverlay("camera_roll.ptr_line", "Pointer Line", &ptr_line);
    {
        auto ptex = MakePointerTexture();
        vr::VROverlay()->SetOverlayRaw(ptr_line, ptex.data(), 4, 256, 4);
        vr::VROverlay()->SetOverlayWidthInMeters(ptr_line, 0.004f);
        vr::VROverlay()->ShowOverlay(ptr_line);
    }

    // ── 画像読み込み ──
    ImageCollection collection;
    collection.LoadFromFolder(kImageFolder);
    UploadImages(img_overlays, collection);
    img_layout[0].y = CalcMainCenterY(SUB_Y, SUB_W, MAIN_W, collection.Main());


    // ── ポインターライン調整パラメータ ──
    Mat3 ptr_rot;
    ptr_rot.m[0][0]=-0.996330f; ptr_rot.m[0][1]=-0.010861f; ptr_rot.m[0][2]=-0.084879f;
    ptr_rot.m[1][0]=-0.074455f; ptr_rot.m[1][1]=+0.598924f; ptr_rot.m[1][2]=+0.797335f;
    ptr_rot.m[2][0]=+0.042176f; ptr_rot.m[2][1]=+0.800731f; ptr_rot.m[2][2]=-0.597535f;
    float ptr_tx = -0.020000f;
    float ptr_ty = -0.090000f;
    float ptr_tz = -0.062000f;

    bool prev_any_hit = false;
    bool prev_trigger = false;

    while (true) {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) break;

        vr::TrackedDeviceIndex_t left_hand =
            vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);

        if (left_hand != vr::k_unTrackedDeviceIndexInvalid) {
            // 画像オーバーレイ
            for (int i = 0; i < N; ++i) {
                auto t = MakeTransform(img_layout[i].x, img_layout[i].y, img_layout[i].z,
                    rot_x, rot_y, rot_z, offset_x, offset_y, offset_z);
                vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(img_overlays[i], left_hand, &t);
            }
            // ページ送りボタン
            {
                auto t = MakeTransform(BTN_L_X, SUB_Y, 0.f, rot_x, rot_y, rot_z, offset_x, offset_y, offset_z);
                vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(btn_newer, left_hand, &t);
            }
            {
                auto t = MakeTransform(BTN_R_X, SUB_Y, 0.f, rot_x, rot_y, rot_z, offset_x, offset_y, offset_z);
                vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(btn_older, left_hand, &t);
            }
#ifdef _DEBUG
            // 上段: 回転ボタン (Rx/Ry/Rz)
            for (int i = 0; i < 6; ++i) {
                auto t = MakeTransform(adj_xs[i], ADJ_ROW1_Y, 0.f,
                    rot_x, rot_y, rot_z, offset_x, offset_y, offset_z);
                vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(adj[i], left_hand, &t);
            }
            // 下段: 移動ボタン (Tx/Ty/Tz)
            for (int i = 6; i < 12; ++i) {
                auto t = MakeTransform(adj_xs[i - 6], ADJ_ROW2_Y, 0.f,
                    rot_x, rot_y, rot_z, offset_x, offset_y, offset_z);
                vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(adj[i], left_hand, &t);
            }
#ifdef LASER_UI
            // 第3行: ポインター回転 (PRx/PRy/PRz)
            for (int i = 0; i < 6; ++i) {
                auto t = MakeTransform(adj_xs[i], ADJ_ROW3_Y, 0.f,
                    rot_x, rot_y, rot_z, offset_x, offset_y, offset_z);
                vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(ptr_adj[i], left_hand, &t);
            }
            // 第4行: ポインター移動 (PTx/PTy/PTz)
            for (int i = 6; i < 12; ++i) {
                auto t = MakeTransform(adj_xs[i - 6], ADJ_ROW4_Y, 0.f,
                    rot_x, rot_y, rot_z, offset_x, offset_y, offset_z);
                vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(ptr_adj[i], left_hand, &t);
            }
            // Co ボタン
            {
                auto t = MakeTransform(0.f, BTN_COUT_Y, 0.f,
                    rot_x, rot_y, rot_z, offset_x, offset_y, offset_z);
                vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(btn_cout, left_hand, &t);
            }
#endif // LASER_UI
#endif // _DEBUG
        }

        // 右コントローラーの raw pose
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
        vr_system->GetDeviceToAbsoluteTrackingPose(
            vr::TrackingUniverseStanding, 0.0f, poses, vr::k_unMaxTrackedDeviceCount);

        vr::TrackedDeviceIndex_t right_hand =
            vr_system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);
        const bool right_valid = (right_hand != vr::k_unTrackedDeviceIndexInvalid)
                               && poses[right_hand].bPoseIsValid;

        // ── 右コントローラー正面ライン更新 ──
        if (right_valid) {
            vr::HmdMatrix34_t line_t = {};
            line_t.m[0][0]=ptr_rot.m[0][0]; line_t.m[0][1]=ptr_rot.m[0][1]; line_t.m[0][2]=ptr_rot.m[0][2]; line_t.m[0][3]=ptr_tx;
            line_t.m[1][0]=ptr_rot.m[1][0]; line_t.m[1][1]=ptr_rot.m[1][1]; line_t.m[1][2]=ptr_rot.m[1][2]; line_t.m[1][3]=ptr_ty;
            line_t.m[2][0]=ptr_rot.m[2][0]; line_t.m[2][1]=ptr_rot.m[2][1]; line_t.m[2][2]=ptr_rot.m[2][2]; line_t.m[2][3]=ptr_tz;
            vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(ptr_line, right_hand, &line_t);
        }

        // ── 動的 MakeOverlaysInteractiveIfVisible 切り替え ──
        bool any_hit = false;
        vr::VROverlayHandle_t hit_overlay = vr::k_ulOverlayHandleInvalid;
        const char* hit_name = nullptr;
        vr::VROverlayIntersectionResults_t hit_res = {};
        if (right_valid) {
            auto& m = poses[right_hand].mDeviceToAbsoluteTracking.m;
            vr::VROverlayIntersectionParams_t params;
            params.eOrigin    = vr::TrackingUniverseStanding;
            params.vSource    = { m[0][0]*ptr_tx + m[0][1]*ptr_ty + m[0][2]*ptr_tz + m[0][3],
                                  m[1][0]*ptr_tx + m[1][1]*ptr_ty + m[1][2]*ptr_tz + m[1][3],
                                  m[2][0]*ptr_tx + m[2][1]*ptr_ty + m[2][2]*ptr_tz + m[2][3] };
            params.vDirection = { m[0][0]*ptr_rot.m[0][2] + m[0][1]*ptr_rot.m[1][2] + m[0][2]*ptr_rot.m[2][2],
                                  m[1][0]*ptr_rot.m[0][2] + m[1][1]*ptr_rot.m[1][2] + m[1][2]*ptr_rot.m[2][2],
                                  m[2][0]*ptr_rot.m[0][2] + m[2][1]*ptr_rot.m[1][2] + m[2][2]*ptr_rot.m[2][2] };
            vr::VROverlayIntersectionResults_t res;

            auto test = [&](vr::VROverlayHandle_t h, const char* name) {
                if (!any_hit && vr::VROverlay()->ComputeOverlayIntersection(h, &params, &res)) {
                    any_hit = true; hit_overlay = h; hit_name = name; hit_res = res;
                }
            };

            test(btn_newer, "btn_newer");
            test(btn_older, "btn_older");
#ifdef _DEBUG
            for (int i = 0; i < 12; ++i) test(adj[i], adj_names[i]);
#ifdef LASER_UI
            for (int i = 0; i < 12; ++i) test(ptr_adj[i], ptr_adj_names[i]);
            test(btn_cout, "btn_cout");
#endif // LASER_UI
#endif
            for (int i = 1; i < N; ++i) test(img_overlays[i], "img");
        }
        if (any_hit != prev_any_hit) {
            if (any_hit)
                printf("HIT: %s  dist=%.3fm  uv=(%.3f, %.3f)  pt=(%.3f, %.3f, %.3f)\n",
                    hit_name, hit_res.fDistance,
                    hit_res.vUVs.v[0], hit_res.vUVs.v[1],
                    hit_res.vPoint.v[0], hit_res.vPoint.v[1], hit_res.vPoint.v[2]);
            else
                printf("MISS\n");
            prev_any_hit = any_hit;
        }

        // ── トリガー入力・UI アクション ──
        {
            vr::VRControllerState_t ctrl = {};
            const bool trigger_now = right_valid &&
                vr_system->GetControllerState(right_hand, &ctrl, sizeof(ctrl)) &&
                (ctrl.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) != 0;

            if (trigger_now && !prev_trigger && any_hit) {
                if (hit_overlay == btn_newer) {
                    collection.LoadNewerPage();
                    UploadImages(img_overlays, collection);
                    img_layout[0].y = CalcMainCenterY(SUB_Y, SUB_W, MAIN_W, collection.Main());
                } else if (hit_overlay == btn_older) {
                    collection.LoadOlderPage();
                    UploadImages(img_overlays, collection);
                    img_layout[0].y = CalcMainCenterY(SUB_Y, SUB_W, MAIN_W, collection.Main());
                } else {
                    for (int i = 1; i < N; ++i) {
                        if (hit_overlay != img_overlays[i]) continue;
                        collection.SetMain(collection.Images().begin() + (i - 1));
                        const Image& mi = collection.Main();
                        if (mi.IsLoaded())
                            vr::VROverlay()->SetOverlayRaw(img_overlays[0], (void*)mi.pixels.data(), mi.width, mi.height, 4);
                        img_layout[0].y = CalcMainCenterY(SUB_Y, SUB_W, MAIN_W, mi);
                        break;
                    }
#ifdef _DEBUG
                    float* rot_ptrs[3] = { &rot_x, &rot_y, &rot_z };
                    float* off_ptrs[3] = { &offset_x, &offset_y, &offset_z };
                    for (int i = 0; i < 6; ++i) {
                        if (hit_overlay == adj[i])
                            *rot_ptrs[i/2] += (i % 2 == 0) ? -ROT_STEP : +ROT_STEP;
                        else if (hit_overlay == adj[i + 6])
                            *off_ptrs[i/2] += (i % 2 == 0) ? -MOVE_STEP : +MOVE_STEP;
                    }
#ifdef LASER_UI
                    for (int i = 0; i < 6; ++i) {
                        if (hit_overlay == ptr_adj[i]) {
                            float delta = (i % 2 == 0) ? -ROT_STEP : +ROT_STEP;
                            if      (i < 2) ptr_rot = ptr_rot * Mat3::rotX(delta);
                            else if (i < 4) ptr_rot = ptr_rot * Mat3::rotY(delta);
                            else            ptr_rot = ptr_rot * Mat3::rotZ(delta);
                        } else if (hit_overlay == ptr_adj[i + 6]) {
                            float* ptr_offs[3] = { &ptr_tx, &ptr_ty, &ptr_tz };
                            *ptr_offs[i/2] += (i % 2 == 0) ? -MOVE_STEP : +MOVE_STEP;
                        }
                    }
#endif // LASER_UI
#endif // _DEBUG
                }
            }
            prev_trigger = trigger_now;
        }

        vr::VREvent_t event;

        if (vr::VROverlay()->PollNextOverlayEvent(img_overlays[0], &event, sizeof(event))) {
            if (event.eventType == vr::VREvent_Quit) break;
        }

        Sleep(16);
    }

    for (int i = 0; i < N; ++i) vr::VROverlay()->DestroyOverlay(img_overlays[i]);
    vr::VROverlay()->DestroyOverlay(btn_newer);
    vr::VROverlay()->DestroyOverlay(btn_older);
    vr::VROverlay()->DestroyOverlay(ptr_line);
#ifdef _DEBUG
    for (int i = 0; i < 12; ++i) vr::VROverlay()->DestroyOverlay(adj[i]);
#ifdef LASER_UI
    for (int i = 0; i < 12; ++i) vr::VROverlay()->DestroyOverlay(ptr_adj[i]);
    vr::VROverlay()->DestroyOverlay(btn_cout);
#endif // LASER_UI
#endif
    vr::VR_Shutdown();
    return 0;
}
