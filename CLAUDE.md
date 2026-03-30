# SteamVR Overlay - Camera Roll Viewer

SteamVRのワールドスペースオーバーレイとして、PC内の特定フォルダから最新N枚の画像をカメラロール的に表示するアプリケーション。

## 目標

- 指定フォルダ（VRChat のスクリーンショットフォルダなど）を監視
- 最新N枚の画像をVR空間内の3Dパネルとして表示
- カメラロール風のUI（横スクロール or グリッド）

## 技術スタック
- C++17
- OpenVR SDK (via SteamVR)
- stb_image (ヘッダオンリー)
- OpenGL (オーバーレイテクスチャへの描画)
- ワールドスペースオーバーレイ (`VROverlayType_Absolute`)

## OpenVR オーバーレイの基本方針

- `vr::VROverlay()->CreateOverlay()` でワールドスペースオーバーレイを作成
- 画像をOpenGLテクスチャに描画し `SetOverlayTexture()` でオーバーレイに反映
- 位置は `SetOverlayTransformAbsolute()` でHMD前方に配置
- フレームループは `~60fps` でポーリング、画像更新は差分検出時のみ

## フォルダ監視

- Windows API の `ReadDirectoryChangesW` を使用
- 監視対象フォルダはコンフィグファイルで指定
- VRChatのスクリーンショットフォルダはデフォルト: `%USERPROFILE%\Pictures\VRChat`