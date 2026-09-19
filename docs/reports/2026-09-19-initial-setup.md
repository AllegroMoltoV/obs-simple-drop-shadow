# 初回セットアップと issue #1 の現状

確認日: 2026-09-19

## 初期設定

既存の Git リポジトリは `main` ブランチを使用している。`project-bootstrap` のスクリプトで、ローカル作業用ディレクトリ、`.git/info/exclude`、文書索引、Beads を初期設定した。Beads は stealth モードで初期化され、初期化直後の `bd ready` に登録済み課題はなかった。

既存の `.gitignore` は `/*` でルート直下を除外していたため、新設した `docs` も Git の対象外だった。共有する調査報告と索引を追跡できるよう、`!/docs` を追加した。`.prompts` などのローカル資料は引き続き除外される。

## 対応対象

[GitHub issue #1](https://github.com/AllegroMoltoV/obs-simple-drop-shadow/issues/1) が、確認時点で唯一の公開中の issue だった。本文は、現行のボックスブラーを分離可能な複数パスへ変更し、画素当たりのサンプル数を半径の二乗から半径に比例する量へ減らす案を示している。コメントはなかった。タイトルはガウスブラーと呼ぶが、本文と実装はボックスブラーを指している。

現行の [`drop-shadow.effect`](../../data/effects/drop-shadow.effect) は半径の縦横で二重ループを回し、元画像のアルファ値を平均している。[`drop-shadow-filter.cpp`](../../src/drop-shadow-filter.cpp) は単一のエフェクトでフィルタを描画する。半径の設定範囲は `0` から `20`。ファイル名に `test` または `Test` を含むテストは、`rg --files` による検索では見つからなかった。

Windows 開発環境には Visual Studio 2022 Community の MSBuild と CMake 4.2.1 がある。リポジトリの `windows-x64` プリセットは `Visual Studio 17 2022` を指定している。ローカルビルドと OBS 実機での動作は未確認。

## リリースと OBS Forum

確認時点の最新 GitHub release は [v0.1.0](https://github.com/AllegroMoltoV/obs-simple-drop-shadow/releases/tag/v0.1.0) で、公開済みの Windows 用 ZIP がある。`push.yaml` はバージョン番号形式のタグに対し、成果物を添付したドラフト release を作成する設定になっている。

ChatGPT 拡張から接続した Brave Browser で [OBS Forum のリソースページ](https://obsproject.com/forum/resources/simple-drop-shadow.2326/) を閲覧できた。`AllegroMoltoV` のログイン後メニューが表示され、掲載版は `0.1.0` だった。ページには、リソースの投稿・更新に2要素認証が必要との案内が表示されている。現アカウントで更新操作を実行できるかは未確認。

## 後続作業

issue #1 の実装と検証、利用者による OBS 実機確認、新しい GitHub release の作成、OBS Forum の掲載更新が必要である。依存関係と進捗は Beads に登録する。公開前に、ビルド結果、実機確認結果、release の配布物、Forum の更新画面をそれぞれ確認する。
