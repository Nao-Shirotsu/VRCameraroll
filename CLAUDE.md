# SteamVR Overlay - Camera Roll Viewer

## プロジェクト概要

SteamVRのワールドスペースオーバーレイとして、PC内の特定フォルダから最新N枚の画像をカメラロール的に表示するアプリケーション。

## 最終目標

- 指定フォルダ（VRChat のスクリーンショットフォルダなど）を監視
- 最新N枚の画像をVR空間内の3Dパネルとして表示
- カメラロール風のUI（横スクロール or グリッド）

## 技術スタック

- **言語**: C++17
- **VR SDK**: OpenVR SDK (via SteamVR)
- **ビルド**: Visual Studio (`.sln` / `.vcxproj`)
- **画像読み込み**: stb_image (ヘッダオンリー)
- **レンダリング**: OpenGL (オーバーレイテクスチャへの描画)
- **オーバーレイ種別**: ワールドスペースオーバーレイ (`VROverlayType_Absolute`)

## ディレクトリ構成

```
SteamVROverlay/
├── CLAUDE.md
├── SteamVROverlay.sln
├── SteamVROverlay/
│   ├── SteamVROverlay.vcxproj
│   ├── src/
│   │   ├── main.cpp
│   │   ├── Overlay.h / Overlay.cpp       # OpenVRオーバーレイ管理
│   │   ├── ImageLoader.h / ImageLoader.cpp  # stb_imageによる画像読み込み
│   │   ├── FolderWatcher.h / FolderWatcher.cpp  # フォルダ監視
│   │   └── Renderer.h / Renderer.cpp     # OpenGLテクスチャ描画
│   └── third_party/
│       ├── openvr/          # OpenVR SDK (headers + lib)
│       └── stb/             # stb_image.h
└── resources/
    └── manifest.vrmanifest  # SteamVRアプリマニフェスト
```

## OpenVR オーバーレイの基本方針

- `vr::VROverlay()->CreateOverlay()` でワールドスペースオーバーレイを作成
- 画像をOpenGLテクスチャに描画し `SetOverlayTexture()` でオーバーレイに反映
- 位置は `SetOverlayTransformAbsolute()` でHMD前方に配置
- フレームループは `~60fps` でポーリング、画像更新は差分検出時のみ

## フォルダ監視

- Windows API の `ReadDirectoryChangesW` を使用
- 監視対象フォルダはアプリ起動時に引数またはコンフィグファイルで指定
- 新規ファイル検出時に画像リストを更新し、テクスチャを再生成

## コーディング規約

- インクルードガードは `#pragma once`
- クラス名はPascalCase、メンバ変数はプレフィックス `m_`
- OpenVRのエラーチェックは必ず行い、失敗時はログ出力して継続 or 終了を判断
- `std::filesystem` (C++17) でパス操作
- リソース管理はRAIIで行う（OpenVRハンドルのラッパークラスを使う）

## 開発ステップ

1. Visual Studioプロジェクトのセットアップ（OpenVR SDKリンク、OpenGLリンク）
2. OpenVRの初期化とワールドスペースオーバーレイの表示確認（単色テクスチャ）
3. stb_imageで画像読み込み → OpenGLテクスチャ → オーバーレイ表示
4. フォルダから最新N枚を列挙してグリッド/横スクロール表示
5. `ReadDirectoryChangesW` でフォルダ変更を検知してリアルタイム更新
6. `.vrmanifest` を整備してSteamVRに登録

## 注意事項

- SteamVRが起動していない場合は `vr::VR_Init()` が失敗する。起動待ちループを実装すること
- オーバーレイのテクスチャサイズは2のべき乗にする（パフォーマンス上の推奨）
- VRChatのスクリーンショットフォルダはデフォルト: `%USERPROFILE%\Pictures\VRChat`

## VRChat AFK 問題と今後の入力方針

### 問題
`VROverlayFlags_MakeOverlaysInteractiveIfVisible` を常時 ON にすると、SteamVR がシステム全体のレーザーマウスモードを有効化し、VRChat が「ダッシュボード開放中」と同等の状態と見なして AFK 判定される。

### 目標挙動（XSOverlay 参考）
- **通常時**: `MakeOverlaysInteractiveIfVisible = false` → VRChat への入力は通常通り、AFK なし
- **右コントローラーのレイが overlay にヒットした瞬間**: 動的に `true` に切り替え → SteamVR がレーザーを表示 & トリガー検出を肩代わり
- **レイが外れたら**: 即座に `false` に戻す

### 実装前提条件
上記を実現するには **`ComputeOverlayIntersection` が正しく動作すること** が必要。
現在デバッグ中（`#ifdef _DEBUG` ブロックで毎秒ログ出力）。

### `ComputeOverlayIntersection` 動作確認チェックリスト
1. `pose_valid=1` になっているか（右コントローラーのトラッキングが有効か）
2. `src` 座標がリアルなワールド座標か（数メートル以内の値）
3. `dir` ベクトルが正規化されているか（各成分が -1〜1 の範囲）
4. hit が `true` になるか（コントローラーをボタンに正対させた状態で）
5. もし hit しない場合: コントローラーの forward ベクトルが `-m[*][2]` で正しいか、`+m[*][2]` を試す
