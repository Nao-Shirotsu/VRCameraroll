#pragma once
#include "Image.h"
#include "ImageFolderObserver.h"
#include "TriggerableButton.h"
#include "Mat3.h"
#include <array>
#include <vector>
#include <filesystem>
#include <string>

// 左コントローラーに追従するカメラロール UI。
// フォルダナビゲーション付き。サブスロットにはサブディレクトリ・画像を表示する。
// rot/offset パラメータも内部で保持し、DebugUI から RotateX() 等で操作される。
class CameraRollUI {
public:
    CameraRollUI();
    ~CameraRollUI();

    void LoadImages(const std::filesystem::path& folder);

    // メインループから毎フレーム呼ぶ。フォルダ変更があれば自動的に再読み込みする。
    void PollFolderChanges();

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

    // 毎フレーム: ホバー中のボタンを渡してサブ画像の明暗を更新する（nullptr=非ホバー）
    void UpdateHover(TriggerableButton* hit);

    void SetActive(bool active);
    bool IsActive() const { return m_active; }

    // イベントポーリング用（メインオーバーレイのハンドル）
    vr::VROverlayHandle_t MainOverlayHandle() const { return m_img_overlays[0]; }

    // 定数
    static constexpr int N        = 7;
    static constexpr float MAIN_W = 0.25f;
    static constexpr float SUB_W  = MAIN_W / (N - 1);
    static constexpr float SUB_Y  = -0.03f;
    static constexpr float BTN_GAP = 0.01f;
    static constexpr float BTN_W   = 0.05f;

private:
    bool m_active = true;

    // ナビゲーション
    enum class SlotType { Empty, BackDir, SubDir, Image };
    struct NavItem {
        SlotType type;
        std::filesystem::path path;
        std::wstring display_name;
    };

    std::filesystem::path m_root_dir;
    std::filesystem::path m_current_dir;
    std::vector<NavItem>  m_nav_items;
    int                   m_nav_offset   = 0;

    // スロット画像（サムネイル用）
    std::array<Image, N - 1> m_slot_images;
    Image                    m_main_image;
    int                      m_main_nav_idx = -1;

    ImageFolderObserver m_observer;

    // 背景オーバーレイ（サブ画像ストリップの背面）
    vr::VROverlayHandle_t m_bg_overlay;

    // 画像オーバーレイ (overlays[0]=メイン, [1..N-1]=サブ)
    std::array<vr::VROverlayHandle_t, N> m_img_overlays;

    // ページ送りボタン
    TriggerableButton m_btn_newer;
    TriggerableButton m_btn_older;

    // サブ画像ボタン（クリックでメイン選択 or フォルダナビゲーション）
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

    // ホバー状態（-1=なし、0..N-2=サブ画像インデックス）
    int m_hovered_sub_idx = -1;

    void RefreshNavItems();
    void UploadNavSlots();
    void RefreshAndDisplay();
    void NavigateInto(const std::filesystem::path& dir);
    void ReloadCurrentDir();
    void SetMainImage(int nav_idx);

    void UpdateMainY();
    void UpdateArrowColors();
    void OnNewerPage();
    void OnOlderPage();
};
