#pragma once
#include "TriggerableButton.h"
#include "Mat3.h"
#include <vector>

// 右コントローラーのレーザーライン overlay を管理し、
// TriggerableButton リストへの交差判定とトリガー検出を担う。
class LaserController {
public:
    LaserController();
    ~LaserController();

    // 毎フレーム: 右コントローラー相対にレーザーライン overlay を配置する
    void UpdatePointerTransform(vr::TrackedDeviceIndex_t right_hand);

    // 右コントローラーの pose からワールド空間の交差判定パラメータを構築する
    vr::VROverlayIntersectionParams_t BuildIntersectionParams(
        const vr::TrackedDevicePose_t& pose) const;

    // ボタンリストに対して交差判定し、最初にヒットした Button を返す（なければ nullptr）
    TriggerableButton* HitTest(
        const std::vector<TriggerableButton*>& buttons,
        const vr::VROverlayIntersectionParams_t& params,
        vr::VROverlayIntersectionResults_t* out = nullptr) const;

    bool IsTriggerPressed(vr::IVRSystem* vr_system,
                          vr::TrackedDeviceIndex_t right_hand) const;

    // DebugUI から ptr_rot / ptr_t を操作するためのアクセサ
    void RotateX(float delta) { m_rot = m_rot * Mat3::rotX(delta); }
    void RotateY(float delta) { m_rot = m_rot * Mat3::rotY(delta); }
    void RotateZ(float delta) { m_rot = m_rot * Mat3::rotZ(delta); }
    void MoveX(float delta)   { m_tx += delta; }
    void MoveY(float delta)   { m_ty += delta; }
    void MoveZ(float delta)   { m_tz += delta; }

    // 現在値を stdout に出力（"Co" ボタン用）
    void PrintParams() const;

private:
    vr::VROverlayHandle_t m_ptr_line = vr::k_ulOverlayHandleInvalid;

    Mat3  m_rot;
    float m_tx = -0.020000f;
    float m_ty = -0.090000f;
    float m_tz = -0.062000f;
};
