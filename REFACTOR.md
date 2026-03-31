# 設計メモ

## クラス構成

```
TriggerableButton   — オーバーレイ1枚 + コールバック（std::function<void()>）
CameraRollUI        — 左手UI。ImageCollection + ボタン群を所有
LaserController     — 右手レーザー。交差判定・トリガー検出
DebugUI             — デバッグビルド専用。rot/offset 調整ボタン群
```

## 主な設計判断

- **TriggerableButton はコンポーネントモデル**（継承なし）。コールバックでキャプチャすることで DebugUI → CameraRollUI の依存を逆転。
- **rot/offset の ownership は CameraRollUI**。DebugUI はコンストラクタで参照を受け取り `RotateX(amount)` 等を呼ぶ。
- **テクスチャ生成関数**（MakeArrowTexture 等）は各 `.cpp` 内 static 関数に隠蔽。
- **global overlay input blocking**: `LaserController` コンストラクタ内で `globalInputOverlaysEnabled = false` を設定し VRChat の AFK トリガーを抑制。
