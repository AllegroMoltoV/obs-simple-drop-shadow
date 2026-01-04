# Simple Drop Shadow Plugin for OBS Studio

OBS Studio 向けのシンプルなドロップシャドウフィルタプラグインです。

A simple drop shadow filter plugin for OBS Studio.

## Features / 機能

映像ソースにドロップシャドウ（影）を追加するエフェクトフィルタです。

- **X Position / X座標** - 影の水平方向オフセット
- **Y Position / Y座標** - 影の垂直方向オフセット
- **Blur Radius / ぼかし半径** - 影のぼかし量
- **Color / 色** - 影の色
- **Opacity / 不透明度** - 影の透明度

## Installation / インストール

### Windows

1. [Releases](../../releases) から最新の zip ファイルをダウンロード
2. zip を展開し、`simple-drop-shadow` フォルダを `%PROGRAMDATA%\obs-studio\plugins\` にコピー
3. OBS Studio を再起動

### macOS / Linux

現在 Windows のみ動作確認済みです。macOS / Linux 向けビルドは GitHub Actions で生成されますが、動作未検証です。

## Usage / 使い方

1. OBS Studio を起動
2. 映像ソースを右クリック → **フィルタ**
3. エフェクトフィルタの **+** ボタン → **Drop Shadow** (または **ドロップシャドウ**) を追加
4. パラメータを調整

## Parameters / パラメータ

| Parameter | 日本語 | Range | Default |
|-----------|--------|-------|---------|
| X Position | X座標 | -100 ~ 100 | 4.0 |
| Y Position | Y座標 | -100 ~ 100 | 4.0 |
| Blur Radius | ぼかし半径 | 0 ~ 20 | 4.0 |
| Color | 色 | - | Black |
| Opacity | 不透明度 | 0 ~ 1 | 0.6 |

## Build / ビルド

### Requirements / 必要環境

| Platform | Tool |
|----------|------|
| Windows | Visual Studio 17 2022 |
| Windows | CMake 3.28+ |
| macOS | Xcode 16.0, CMake 3.28+ |
| Ubuntu 24.04 | ninja-build, pkg-config, build-essential, CMake 3.28+ |

### Build Commands / ビルドコマンド

```bash
# Windows
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo

# macOS
cmake --preset macos
cmake --build build_macos --config RelWithDebInfo

# Ubuntu
cmake --preset ubuntu-x86_64
cmake --build build_x86_64 --config RelWithDebInfo
```

ローカル開発時は `CMakeUserPresets.json` で `CMAKE_INSTALL_PREFIX` を設定すると、ビルド後に自動インストールされます。

## License / ライセンス

GPL-2.0 License

このプラグインは [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) をベースにしています。

## Author / 作者

AllegroMoltoV - https://www.allegromoltov.jp
