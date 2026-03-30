#include "TriggerableButton.h"
#include <utility>

TriggerableButton::TriggerableButton(const char* overlay_key,
                                     const char* overlay_name,
                                     float width_m,
                                     std::function<void()> on_trigger)
    : m_on_trigger(std::move(on_trigger))
{
    vr::VROverlay()->CreateOverlay(overlay_key, overlay_name, &m_handle);
    vr::VROverlay()->SetOverlayWidthInMeters(m_handle, width_m);
    vr::VROverlay()->SetOverlayInputMethod(m_handle, vr::VROverlayInputMethod_None);
}

TriggerableButton::TriggerableButton(vr::VROverlayHandle_t existing_handle,
                                     std::function<void()> on_trigger)
    : m_handle(existing_handle)
    , m_on_trigger(std::move(on_trigger))
    , m_owns_handle(false)
{}

TriggerableButton::~TriggerableButton() {
    if (m_owns_handle && m_handle != vr::k_ulOverlayHandleInvalid)
        vr::VROverlay()->DestroyOverlay(m_handle);
}

TriggerableButton::TriggerableButton(TriggerableButton&& o) noexcept
    : m_handle(o.m_handle)
    , m_on_trigger(std::move(o.m_on_trigger))
    , m_owns_handle(o.m_owns_handle)
{
    o.m_handle = vr::k_ulOverlayHandleInvalid;
}

TriggerableButton& TriggerableButton::operator=(TriggerableButton&& o) noexcept {
    if (this != &o) {
        if (m_owns_handle && m_handle != vr::k_ulOverlayHandleInvalid)
            vr::VROverlay()->DestroyOverlay(m_handle);
        m_handle      = o.m_handle;
        m_on_trigger  = std::move(o.m_on_trigger);
        m_owns_handle = o.m_owns_handle;
        o.m_handle    = vr::k_ulOverlayHandleInvalid;
    }
    return *this;
}

bool TriggerableButton::Intersected(const vr::VROverlayIntersectionParams_t& params,
                                     vr::VROverlayIntersectionResults_t* out) const
{
    vr::VROverlayIntersectionResults_t tmp;
    return vr::VROverlay()->ComputeOverlayIntersection(m_handle, &params,
                                                        out ? out : &tmp);
}

void TriggerableButton::FireTrigger() const {
    if (m_on_trigger) m_on_trigger();
}

void TriggerableButton::SetTransformTrackedDeviceRelative(
    vr::TrackedDeviceIndex_t device, const vr::HmdMatrix34_t& transform)
{
    auto err = vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(m_handle, device, &transform);
    if (err != vr::VROverlayError_None) {
        printf("SetOverlayTransformTrackedDeviceRelative handle=%llu err=%d\n",
               (unsigned long long)m_handle, (int)err);
    }
}

void TriggerableButton::UploadTexture(const std::vector<uint8_t>& pixels, int w, int h) {
    vr::VROverlay()->SetOverlayRaw(m_handle,
        const_cast<uint8_t*>(pixels.data()), w, h, 4);
}

void TriggerableButton::Show() { vr::VROverlay()->ShowOverlay(m_handle); }
void TriggerableButton::Hide() { vr::VROverlay()->HideOverlay(m_handle); }
