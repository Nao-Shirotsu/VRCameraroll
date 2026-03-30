#pragma once
#ifdef _DEBUG

#include "TriggerableButton.h"
#include "Mat3.h"
#include <vector>

class CameraRollUI;
class LaserController;

// デバッグビルド専用。
// CameraRollUI の rot/offset 調整ボタンと LaserController の
// ポインター調整ボタン群を管理する。
class DebugUI {
public:
    // camera_roll / laser への参照をコンストラクタで受け取る
    DebugUI(CameraRollUI& camera_roll, LaserController& laser);
    ~DebugUI() = default;

    // 毎フレーム: 左コントローラー相対に全ボタンを配置する
    // rot/offset は CameraRollUI から取得して渡す
    void UpdateTransforms(vr::TrackedDeviceIndex_t left_hand,
                          float rot_x, float rot_y, float rot_z,
                          float off_x, float off_y, float off_z);

    // 当たり判定ループに渡すボタンリスト
    std::vector<TriggerableButton*> Buttons();

private:
    // 12 個の回転/移動ボタン (カメラロール UI 用)
    std::vector<TriggerableButton> m_adj;
    // 12 個のポインター回転/移動ボタン + 1 個の Co ボタン
    std::vector<TriggerableButton> m_ptr_adj;
    TriggerableButton              m_btn_cout;

    // レイアウト定数
    static constexpr float ADJ_W   = 0.025f;
    static constexpr float ADJ_GAP = 0.003f;
    static constexpr float ADJ_GRP = 0.008f;

    float m_adj_xs[6] = {};
    float m_row1_y = 0.f; // 回転ボタン行
    float m_row2_y = 0.f; // 移動ボタン行
    float m_row3_y = 0.f; // ポインター回転行
    float m_row4_y = 0.f; // ポインター移動行
    float m_cout_y = 0.f;
};

#endif // _DEBUG
