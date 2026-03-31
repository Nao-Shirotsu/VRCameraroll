#pragma once
#include "ImageCollection.h"
#include "TriggerableButton.h"
#include "Mat3.h"
#include <array>
#include <vector>
#include <filesystem>

// 左コントローラーに追従するカメラロール UI。
// ImageCollection の保持・画像オーバーレイ・ページ送りボタンを管理する。
// rot/offset パラメータも内部で保持し、DebugUI から RotateX() 等で操作される。
class CameraRollUI {
public:
    CameraRollUI();
    ~CameraRollUI();

    void LoadImages(const std::filesystem::path& folder);

    // 毎フレーム: 左コントローラー相対にオーバーレイを配置する
    void UpdateTransforms(vr::TrackedDeviceIndex_t left_hand);

    // DebugUI から呼ばれる rot/offset 操作
    void RotateX(float delta) { m_rot_x += delta; }
    void RotateY(float delta) { m_rot_y += delta; }
    void RotateZ(float delta) { m_rot_z += delta; }
    void MoveX(float delta)   { m_offset_x += delta; }
    void MoveY(float delta)   { m_offset_y += delta; }
    void MoveZ(float delta)   { m_offset_z += delta; }

    // DebugUI が UpdateTransforms に渡すための rot/offset 読み取り
    float RotX() const { return m_rot_x; }
    float RotY() const { return m_rot_y; }
    float RotZ() const { return m_rot_z; }
    float OffX() const { return m_offset_x; }
    float OffY() const { return m_offset_y; }
    float OffZ() const { return m_offset_z; }

    // 当たり判定ループに渡すボタンリスト（ページ送り + サブ画像）
    std::vector<TriggerableButton*> Buttons();

    void SetActive(bool active);
    bool IsActive() const { return m_active; }

    // イベントポーリング用（メインオーバーレイのハンドル）
    vr::VROverlayHandle_t MainOverlayHandle() const { return m_img_overlays[0]; }

    // 定数
    static constexpr int N      = ImageCollection::N;
    static constexpr float MAIN_W = 0.25f;
    static constexpr float SUB_W  = MAIN_W / (N - 1);
    static constexpr float SUB_Y  = -0.03f;

private:
    bool m_active = true;

    ImageCollection m_collection;

    // 画像オーバーレイ (overlays[0]=メイン, [1..N-1]=サブ)
    std::array<vr::VROverlayHandle_t, N> m_img_overlays;

    // ページ送りボタン
    TriggerableButton m_btn_newer;
    TriggerableButton m_btn_older;

    // サブ画像ボタン（クリックでメイン選択）
    std::vector<TriggerableButton> m_sub_btns;

    // コントローラー相対の配置パラメータ
    float m_rot_x    = -1.675515f;
    float m_rot_y    =  0.209440f;
    float m_rot_z    = -0.994838f;
    float m_offset_x =  0.080000f;
    float m_offset_y = -0.090000f;
    float m_offset_z =  0.050000f;

    // レイアウト
    struct Layout { float x, y; };
    std::array<Layout, N> m_img_layout;

    void UploadImages();
    void UpdateMainY();
    void OnNewerPage();
    void OnOlderPage();
};
