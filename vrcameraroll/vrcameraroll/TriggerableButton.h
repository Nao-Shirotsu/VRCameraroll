#pragma once
#include <openvr.h>
#include <functional>
#include <vector>
#include <cstdint>

// レーザーポイントしてトリガーを押すことで操作できるボタン。
// 挙動は std::function コールバックで注入する（コンポーネントモデル）。
class TriggerableButton {
public:
    TriggerableButton(const char* overlay_key,
                      const char* overlay_name,
                      float width_m,
                      std::function<void()> on_trigger);

    // 既存の overlay ハンドルを引き受けるコンストラクタ。
    // デストラクタでは overlay を破棄しない（所有権は呼び出し元が持つ）。
    TriggerableButton(vr::VROverlayHandle_t existing_handle,
                      std::function<void()> on_trigger);
    ~TriggerableButton();

    // コピー不可、ムーブのみ
    TriggerableButton(const TriggerableButton&) = delete;
    TriggerableButton& operator=(const TriggerableButton&) = delete;
    TriggerableButton(TriggerableButton&&) noexcept;
    TriggerableButton& operator=(TriggerableButton&&) noexcept;

    // コントローラー forward との交差判定。
    // out が非 nullptr なら交差結果を書き込む。
    bool Intersected(const vr::VROverlayIntersectionParams_t& params,
                     vr::VROverlayIntersectionResults_t* out = nullptr) const;

    // トリガーが押された瞬間に外部から呼ぶ
    void FireTrigger() const;

    void SetTransformTrackedDeviceRelative(vr::TrackedDeviceIndex_t device,
                                           const vr::HmdMatrix34_t& transform);

    void UploadTexture(const std::vector<uint8_t>& pixels, int w, int h);
    void Show();
    void Hide();

    vr::VROverlayHandle_t Handle() const { return m_handle; }

private:
    vr::VROverlayHandle_t m_handle      = vr::k_ulOverlayHandleInvalid;
    std::function<void()> m_on_trigger;
    bool                  m_owns_handle = true; // false のときデストラクタで破棄しない
};
