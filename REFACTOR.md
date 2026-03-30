# リファクタリング計画

作成日: 2026-03-30

---

## 全体方針

`main.cpp` の巨大な手続き的コードを、責務が明確な複数クラスに分解する。
新規ファイルは `vrcameraroll/` ディレクトリに `.h/.cpp` ペアで追加する。

---

## 1. TriggerableButton クラス

### 設計判断: 継承 vs コンポーネント（std::function）

**推奨: コンポーネントモデル（std::function）**

理由:
- ボタン種類ごとにサブクラスを作るより、コールバックで挙動を差し込む方が柔軟。
- サブ画像ボタン（インデックス i が必要）のように、ローカル変数をキャプチャしやすい。
- `std::function<void()>` で十分に型安全。virtual dispatch のオーバーヘッドも不要。
- DebugUI から CameraRoll への依存を逆転できる（DebugUI が CameraRoll を知らなくてよい）。

もし将来「ボタン種別ごとに描画を変えたい」等の要件が出たら継承に切り替えを検討。

### インターフェース案

```cpp
// TriggerableButton.h
class TriggerableButton {
public:
    TriggerableButton(const char* key, const char* name,
                      float width_m,
                      std::function<void()> on_trigger);
    ~TriggerableButton();

    // コントローラーの forward との交差判定
    // params: ComputeOverlayIntersection に渡す光線情報
    // out   : ヒット結果（不要なら nullptr）
    bool Intersected(const vr::VROverlayIntersectionParams_t& params,
                     vr::VROverlayIntersectionResults_t* out = nullptr) const;

    // トリガーが押された瞬間に呼ぶ（hit している前提）
    void FireTrigger() const;

    // オーバーレイ位置を左コントローラー相対に更新
    void SetTransformRelative(vr::TrackedDeviceIndex_t device,
                              const vr::HmdMatrix34_t& transform);

    void UploadTexture(const std::vector<uint8_t>& pixels, int w, int h);
    void Show();
    void Hide();

    vr::VROverlayHandle_t Handle() const { return m_handle; }

private:
    vr::VROverlayHandle_t       m_handle = vr::k_ulOverlayHandleInvalid;
    std::function<void()>       m_on_trigger;
};
```

---

## 2. CameraRollUI クラス（左手側）

`ImageCollection` の保持・画像アップロード・ページ送りボタンを一括管理。
`TriggerableButton` をメンバとして所有する。

### 責務
- `ImageCollection` の保持と操作（ページ送り、サブ→メイン選択）
- オーバーレイハンドル（img_overlays + btn_newer/btn_older）の生成・破棄
- `UpdateTransforms(left_hand)` でレイアウトを毎フレーム適用
- `CollectButtons()` で `TriggerableButton*` リストを返し、LaserController の当たり判定に提供

### 主要メンバ関数案

```cpp
class CameraRollUI {
public:
    CameraRollUI();
    ~CameraRollUI();

    void LoadImages(const std::filesystem::path& folder);
    void UpdateTransforms(vr::TrackedDeviceIndex_t left_hand,
                          float rot_x, float rot_y, float rot_z,
                          float off_x, float off_y, float off_z);

    // TriggerableButton のポインタリストを返す（当たり判定に使用）
    std::vector<TriggerableButton*> Buttons();

private:
    ImageCollection m_collection;
    std::array<vr::VROverlayHandle_t, ImageCollection::N> m_img_overlays;
    TriggerableButton m_btn_newer;
    TriggerableButton m_btn_older;
    std::array<TriggerableButton, ImageCollection::N - 1> m_sub_btns;

    void UploadImages();
    void UpdateMainY();

    // レイアウトパラメータ
    float m_main_w, m_sub_w, m_sub_y;
    std::array<float, ImageCollection::N> m_layout_x;
    std::array<float, ImageCollection::N> m_layout_y;
};
```

---

## 3. DebugUI クラス

デバッグビルド専用。`TriggerableButton` のベクターを管理し、
`rot_x/y/z` や `offset_x/y/z` の変更コールバックを std::function で受け取る。

`#ifdef _DEBUG` は DebugUI.h/.cpp 内に閉じ込め、main.cpp 側のプリプロセッサを減らす。

### 責務
- 調整ボタン 12 個（回転/移動）の作成・テクスチャアップロード
- `#ifdef LASER_UI` のポインター調整ボタン 12 個 + "Co" ボタン
- `UpdateTransforms(left_hand, ...)` で毎フレーム位置適用
- `Buttons()` で全 TriggerableButton* を返す

---

## 4. LaserController クラス（右手側）

レーザーライン overlay の管理、交差判定ループ、トリガー検出をカプセル化。

### 責務
- `ptr_line` オーバーレイの生成・破棄
- `ptr_rot`, `ptr_tx/ty/tz` の保持と更新
- `UpdatePointerTransform(right_hand)` でラインを毎フレーム配置
- `BuildIntersectionParams(poses)` → `vr::VROverlayIntersectionParams_t` を返す
- `HitTest(buttons_list, params)` → ヒットした TriggerableButton* を返す
- `IsTriggerPressed(right_hand)` → bool

### 主要メンバ関数案

```cpp
class LaserController {
public:
    LaserController();
    ~LaserController();

    void UpdatePointerTransform(vr::TrackedDeviceIndex_t right_hand);

    // ワールド空間の光線パラメータを構築
    vr::VROverlayIntersectionParams_t BuildIntersectionParams(
        const vr::TrackedDevicePose_t& pose) const;

    // ボタンリストに対して交差判定し、最初にヒットしたものを返す
    TriggerableButton* HitTest(
        const std::vector<TriggerableButton*>& buttons,
        const vr::VROverlayIntersectionParams_t& params,
        vr::VROverlayIntersectionResults_t* out = nullptr) const;

    bool IsTriggerPressed(vr::IVRSystem* vr_system,
                          vr::TrackedDeviceIndex_t right_hand) const;

    // デバッグ: ptr_rot / ptr_t を stdout 出力
    void PrintParams() const;

    // ptr_rot / ptr_t の直接変更（DebugUI のコールバック用）
    Mat3& RotMat()  { return m_ptr_rot; }
    float& OffsetX() { return m_ptr_tx; }
    float& OffsetY() { return m_ptr_ty; }
    float& OffsetZ() { return m_ptr_tz; }

private:
    vr::VROverlayHandle_t m_ptr_line = vr::k_ulOverlayHandleInvalid;
    Mat3  m_ptr_rot;
    float m_ptr_tx, m_ptr_ty, m_ptr_tz;
};
```

---

## 5. main.cpp の姿（リファクタリング後）

```cpp
int main() {
    // VR 初期化
    // ...

    // UI パラメータ（rot_x/y/z, offset_x/y/z）
    // → CameraRollUI コンストラクタ引数か、専用 struct にまとめる

    CameraRollUI camera_roll;
    camera_roll.LoadImages(kImageFolder);

    LaserController laser;

#ifdef _DEBUG
    DebugUI debug_ui(/* rot/offset/ptr refs */);
#endif

    bool prev_trigger = false;

    while (true) {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) break;

        // 左コン取得 → UpdateTransforms
        camera_roll.UpdateTransforms(left_hand, rot_x, ...);
#ifdef _DEBUG
        debug_ui.UpdateTransforms(left_hand, rot_x, ...);
#endif

        // 右コン pose 取得
        laser.UpdatePointerTransform(right_hand);

        // 全ボタン収集 → HitTest
        auto buttons = camera_roll.Buttons();
#ifdef _DEBUG
        auto db = debug_ui.Buttons();
        buttons.insert(buttons.end(), db.begin(), db.end());
#endif

        auto params = laser.BuildIntersectionParams(poses[right_hand]);
        TriggerableButton* hit = laser.HitTest(buttons, params);

        // トリガー押下時
        bool trigger_now = laser.IsTriggerPressed(vr_system, right_hand);
        if (trigger_now && !prev_trigger && hit)
            hit->FireTrigger();
        prev_trigger = trigger_now;

        // イベントポーリング
        // ...
        Sleep(16);
    }
}
```

---

## 6. ファイル構成（追加分）

```
vrcameraroll/
├── main.cpp              (大幅簡素化)
├── Image.h / Image.cpp   (変更なし)
├── ImageCollection.h / ImageCollection.cpp  (変更なし)
├── Mat3.h                (MakeTransform, Mat3 を分離)
├── TriggerableButton.h / TriggerableButton.cpp
├── CameraRollUI.h / CameraRollUI.cpp
├── LaserController.h / LaserController.cpp
└── DebugUI.h / DebugUI.cpp
```

---

## 7. 実装順序

1. `Mat3.h` + `MakeTransform` の分離（依存なし・安全）
2. `TriggerableButton` 実装
3. `LaserController` 実装（TriggerableButton に依存）
4. `CameraRollUI` 実装（TriggerableButton + LaserController に依存なし）
5. `DebugUI` 実装（TriggerableButton に依存）
6. `main.cpp` をリファクタリング

---

## 決定事項（2026-03-30 追記）

- **rot/offset の ownership**: `CameraRollUI` が持つ。外部から操作する API は `RotateX(amount)` / `MoveX(amount)` 等のメンバ関数。DebugUI は CameraRollUI への参照をコンストラクタで受け取り、ボタンの std::function から呼び出す。
- **テクスチャ生成関数**（`MakeArrowTexture`, `MakeCroppedThumbnail`, `MakeLabelTexture`, `MakePointerTexture`）: 各クラスの `.cpp` 内 static 関数として隠蔽する。shared header には出さない。
- **global overlay input blocking の無効化**: 起動時に以下を呼ぶ。`LaserController` のコンストラクタ内に置く。
  ```cpp
  vr::EVRSettingsError se = vr::VRSettingsError_None;
  vr::VRSettings()->SetBool("steamvr", "globalInputOverlaysEnabled", false, &se);
  ```
  これにより VRChat の AFK トリガーを抑制しつつ、overlay の input 受付を制御できる可能性がある。（既存の動的 MakeOverlaysInteractiveIfVisible 切り替えと併用）
