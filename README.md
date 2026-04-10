# ぶいちゃフォトビュー

HMD をかぶったままPC内の画像を閲覧できる SteamVR オーバーレイアプリケーションです。 

※「ぶいちゃ」と名乗ってはいますが、画像フォルダをよみこんで表示するアプリケーションですので、読み込み対象フォルダをconfigで変えれば他の用途にも使えます。

[!['ぶいちゃフォトビュー紹介動画']('https://img.youtube.com/vi/V83oOGgfafQ/0.jpg')]('https://youtube.com/shorts/V83oOGgfafQ')

## ライセンスについて

**本リポジトリを利用する前に [LICENSE.md](LICENSE.md) を必ずお読みください。**

- ソースコードの閲覧・学習・個人利用目的のビルドは自由に行えます。
- **ビルド済みバイナリの再配布は禁止です。** 正規バイナリは [BOOTH - しろつ's Shader STUDIO](https://4rotsugd.booth.pm) でのみ配布（有料）しています。
- 商用利用には事前連絡が必要です。

このリポジトリは、改良・改変を歓迎する目的で開発者向けにオープンにしています。バグ修正や機能追加を目的としたソースコードの再配布・プルリクエストは積極的に許可しています。ご自由にどうぞ。

## 特徴

- **ワンアクションで展開** — VR 中に左グリップをダブルタップでカメラロール UI を表示
- **省設定** — config.yml で指定したフォルダ以下の画像を表示、子フォルダを再帰的に選択できるUIあり
- **Easy Anti Cheat に検知されない** — OpenVR Overlay を使用しており、VRChat 公式が認めている方式（OVR・XSOverlay 等と同様）

## ビルド

### 依存ライブラリ

| ライブラリ | 入手方法 |
|---|---|
| OpenVR SDK | SteamVR インストール済み環境に含まれる（`openvr_api.dll` / `openvr.h`） |
| stb_image | ヘッダオンリー、リポジトリに同梱 |
| OpenGL | Windows 標準（`opengl32.lib`） |

### 動作確認環境

- Windows 11
- Visual Studio 2026（C++ デスクトップ開発ワークロード）
- SteamVR 2.15.6

### ビルド手順

1. `vrcameraroll/vrcameraroll.slnx` を Visual Studio で開く
2. 構成を `Release / x64` に設定
3. ビルド実行（`Ctrl+Shift+B`）

### 実行時に必要なファイル

ビルド後、実行ファイルと同じディレクトリに以下が必要です：

- `openvr_api.dll`（SteamVR インストール先 or OpenVR SDK から取得）
- `config.yml`（設定ファイル、初回起動で自動生成またはサンプルをコピー）

## 設定ファイル (`config.yml`)

```yaml
# 監視するフォルダパス（省略時は VRChat デフォルトフォルダ）
folder: "C:/Users/YourName/Pictures/VRChat"

# 表示する最新画像枚数
max_images: 20
```

## アーキテクチャ概要

```
main.cpp
├── ImageFolderObserver   # ReadDirectoryChangesW でフォルダ変更を監視
├── ImageCollection       # 最新N枚の画像リストを管理
├── Image                 # stb_image による画像読み込み・OpenGL テクスチャ管理
├── CameraRollUI          # オーバーレイ上の UI レンダリング
├── LaserController       # VRコントローラーのレーザーポインター入力処理
└── TriggerableButton     # グリップ 2 回押し検出などのボタンロジック
```

フレームループは約 60fps でポーリングし、フォルダ変更検出時のみ画像リストを更新します。

## 連絡先

不具合報告・ご質問は以下へどうぞ。

- X (Twitter): [@462shaderstudio](https://x.com/462shaderstudio)
- メール: 4rotsugd@gmail.com
