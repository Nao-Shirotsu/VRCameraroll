#include "DebugUI.h"
#ifdef UI_ADJUST

#include "CameraRollUI.h"
#include "LaserController.h"
#include <cstring>
#include <cstdio>
#include <cmath>

// ────────────────────────────────────────────
// 内部テクスチャ生成
// ────────────────────────────────────────────

struct GlyphDef { char c; uint8_t rows[5]; };
static const GlyphDef kFont[] = {
    {'R', {0xE, 0x9, 0xE, 0xA, 0x9}},
    {'x', {0x9, 0x6, 0x0, 0x6, 0x9}},
    {'y', {0x9, 0x9, 0x7, 0x1, 0xE}},
    {'z', {0xF, 0x1, 0x6, 0x8, 0xF}},
    {'T', {0xF, 0x6, 0x6, 0x6, 0x6}},
    {'C', {0xE, 0x8, 0x8, 0x8, 0xE}},
    {'o', {0x6, 0x9, 0x9, 0x9, 0x6}},
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

static std::vector<uint8_t> MakeLabelTexture(const char* label,
    uint8_t bg_r = 60, uint8_t bg_g = 100, uint8_t bg_b = 60)
{
    constexpr int S = 64, scale = 2, char_w = 4*scale, gap = 2;
    std::vector<uint8_t> pixels(S * S * 4);
    for (int i = 0; i < S * S; ++i) {
        pixels[i*4+0]=bg_r; pixels[i*4+1]=bg_g;
        pixels[i*4+2]=bg_b; pixels[i*4+3]=180;
    }
    int n = (int)strlen(label);
    int lx = (S - (n*char_w + (n-1)*gap)) / 2;
    int ly = (S - 5*scale) / 2;
    for (int i = 0; i < n; ++i)
        DrawGlyph(pixels, S, lx + i*(char_w+gap), ly, label[i], scale);
    return pixels;
}

static std::vector<uint8_t> MakeArrowWithLabel(bool left_arrow, const char* label,
    uint8_t bg_r, uint8_t bg_g, uint8_t bg_b)
{
    constexpr int S = 64;
    std::vector<uint8_t> pixels(S * S * 4);
    for (int i = 0; i < S * S; ++i) {
        pixels[i*4+0]=bg_r; pixels[i*4+1]=bg_g;
        pixels[i*4+2]=bg_b; pixels[i*4+3]=180;
    }
    const int cy = 18, half = 13;
    for (int y = 0; y < 40; ++y) {
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
    if (label && label[0]) {
        constexpr int scale = 2, char_w = 4 * scale, gap = 2;
        int n = (int)strlen(label);
        int lx = (S - (n * char_w + (n - 1) * gap)) / 2;
        for (int i = 0; i < n; ++i)
            DrawGlyph(pixels, S, lx + i * (char_w + gap), 46, label[i], scale);
    }
    return pixels;
}

// ────────────────────────────────────────────
// DebugUI
// ────────────────────────────────────────────

namespace {
    constexpr float ROT_STEP  = 3.0f * 3.14159265f / 180.0f;
    constexpr float MOVE_STEP = 0.01f;

    struct AdjInfo { const char* label; uint8_t r, g, b; };
    static const AdjInfo kAdjInfo[12] = {
        {"Rx",120,50, 50}, {"Rx",120,50, 50},
        {"Ry", 50,50,120}, {"Ry", 50,50,120},
        {"Rz", 50,110,50}, {"Rz", 50,110,50},
        {"Tx",120,50, 50}, {"Tx",120,50, 50},
        {"Ty", 50,50,120}, {"Ty", 50,50,120},
        {"Tz", 50,110,50}, {"Tz", 50,110,50},
    };
}

DebugUI::DebugUI(CameraRollUI& cam, LaserController& laser)
    : m_btn_cout("cr.btn_cout", "Cout", ADJ_W, [&laser]{ laser.PrintParams(); })
{
    // adj_xs 計算
    const float total = 6*ADJ_W + 4*ADJ_GAP + 2*ADJ_GRP;
    float cur = -total / 2.0f + ADJ_W / 2.0f;
    m_adj_xs[0] = cur;
    m_adj_xs[1] = (cur += ADJ_W + ADJ_GAP);
    m_adj_xs[2] = (cur += ADJ_W + ADJ_GRP);
    m_adj_xs[3] = (cur += ADJ_W + ADJ_GAP);
    m_adj_xs[4] = (cur += ADJ_W + ADJ_GRP);
    m_adj_xs[5] = (cur += ADJ_W + ADJ_GAP);

    const float SUB_Y = CameraRollUI::SUB_Y;
    const float SUB_W = CameraRollUI::SUB_W;
    m_row1_y = SUB_Y - SUB_W / 2.0f - ADJ_GAP - ADJ_W / 2.0f;
    m_row2_y = m_row1_y - ADJ_W - ADJ_GAP;
    m_row3_y = m_row2_y - ADJ_W - ADJ_GAP;
    m_row4_y = m_row3_y - ADJ_W - ADJ_GAP;
    m_cout_y = m_row4_y - ADJ_W - ADJ_GAP;

    // カメラロール UI 調整ボタン 12 個
    const char* adj_keys[12] = {
        "cr.rx_m","cr.rx_p","cr.ry_m","cr.ry_p","cr.rz_m","cr.rz_p",
        "cr.tx_m","cr.tx_p","cr.ty_m","cr.ty_p","cr.tz_m","cr.tz_p",
    };
    const char* adj_names[12] = {
        "Rx-","Rx+","Ry-","Ry+","Rz-","Rz+",
        "Tx-","Tx+","Ty-","Ty+","Tz-","Tz+",
    };
    for (int i = 0; i < 12; ++i) {
        bool left = (i % 2 == 0);
        float delta = left ? -1.f : +1.f;
        std::function<void()> cb;
        if (i < 6) {
            // 回転
            float step = ROT_STEP * delta;
            int axis = i / 2;
            cb = [&cam, axis, step]{
                if      (axis == 0) cam.RotateX(step);
                else if (axis == 1) cam.RotateY(step);
                else                cam.RotateZ(step);
            };
        } else {
            // 移動
            float step = MOVE_STEP * delta;
            int axis = (i - 6) / 2;
            cb = [&cam, axis, step]{
                if      (axis == 0) cam.MoveX(step);
                else if (axis == 1) cam.MoveY(step);
                else                cam.MoveZ(step);
            };
        }
        m_adj.emplace_back(adj_keys[i], adj_names[i], ADJ_W, std::move(cb));
        auto tex = MakeArrowWithLabel(left, kAdjInfo[i].label,
            kAdjInfo[i].r, kAdjInfo[i].g, kAdjInfo[i].b);
        m_adj.back().UploadTexture(tex, 64, 64);
        m_adj.back().Show();
    }

    // ポインター調整ボタン 12 個
    const char* ptr_keys[12] = {
        "cr.prx_m","cr.prx_p","cr.pry_m","cr.pry_p","cr.prz_m","cr.prz_p",
        "cr.ptx_m","cr.ptx_p","cr.pty_m","cr.pty_p","cr.ptz_m","cr.ptz_p",
    };
    const char* ptr_names[12] = {
        "PRx-","PRx+","PRy-","PRy+","PRz-","PRz+",
        "PTx-","PTx+","PTy-","PTy+","PTz-","PTz+",
    };
    for (int i = 0; i < 12; ++i) {
        bool left = (i % 2 == 0);
        float delta = left ? -1.f : +1.f;
        std::function<void()> cb;
        if (i < 6) {
            float step = ROT_STEP * delta;
            int axis = i / 2;
            cb = [&laser, axis, step]{
                if      (axis == 0) laser.RotateX(step);
                else if (axis == 1) laser.RotateY(step);
                else                laser.RotateZ(step);
            };
        } else {
            float step = MOVE_STEP * delta;
            int axis = (i - 6) / 2;
            cb = [&laser, axis, step]{
                if      (axis == 0) laser.MoveX(step);
                else if (axis == 1) laser.MoveY(step);
                else                laser.MoveZ(step);
            };
        }
        m_ptr_adj.emplace_back(ptr_keys[i], ptr_names[i], ADJ_W, std::move(cb));
        auto tex = MakeArrowWithLabel(left, kAdjInfo[i].label,
            kAdjInfo[i].r, kAdjInfo[i].g, kAdjInfo[i].b);
        m_ptr_adj.back().UploadTexture(tex, 64, 64);
        m_ptr_adj.back().Show();
    }

    // Co ボタン
    m_btn_cout.UploadTexture(MakeLabelTexture("Co"), 64, 64);
    m_btn_cout.Show();
}

void DebugUI::UpdateTransforms(vr::TrackedDeviceIndex_t left_hand,
    float rot_x, float rot_y, float rot_z,
    float off_x, float off_y, float off_z)
{
    if (left_hand == vr::k_unTrackedDeviceIndexInvalid) return;

    auto make = [&](float x, float y) {
        return MakeTransform(x, y, 0.f, rot_x, rot_y, rot_z, off_x, off_y, off_z);
    };

    for (int i = 0; i < 6; ++i)
        m_adj[i].SetTransformTrackedDeviceRelative(left_hand, make(m_adj_xs[i], m_row1_y));
    for (int i = 6; i < 12; ++i)
        m_adj[i].SetTransformTrackedDeviceRelative(left_hand, make(m_adj_xs[i-6], m_row2_y));
    for (int i = 0; i < 6; ++i)
        m_ptr_adj[i].SetTransformTrackedDeviceRelative(left_hand, make(m_adj_xs[i], m_row3_y));
    for (int i = 6; i < 12; ++i)
        m_ptr_adj[i].SetTransformTrackedDeviceRelative(left_hand, make(m_adj_xs[i-6], m_row4_y));
    m_btn_cout.SetTransformTrackedDeviceRelative(left_hand, make(0.f, m_cout_y));
}

std::vector<TriggerableButton*> DebugUI::Buttons() {
    std::vector<TriggerableButton*> result;
    for (auto& b : m_adj)     result.push_back(&b);
    for (auto& b : m_ptr_adj) result.push_back(&b);
    result.push_back(&m_btn_cout);
    return result;
}

#endif // UI_ADJUST
