# OBS 33 の Windows 配置移行

## 対象

Simple Drop Shadow 0.1.2 の Windows DLL/PDB を製品フォルダー直下へ移した。effect と locale の data 配置、描画ロジック、設定形式を維持した。計画の正本は obs-loudness-balance-monitor-142、実行課題は obs-simple-drop-shadow-rm8。

## 検証結果

[通常版インストール検証](https://github.com/AllegroMoltoV/obs-simple-drop-shadow/actions/runs/42372074038)と[3OS ビルド・整形チェック](https://github.com/AllegroMoltoV/obs-simple-drop-shadow/actions/runs/42372074002)に成功した。対象コミットは 19a8e02。

通常版の新規環境へ配布 ZIP を導入し、新 DLL のロードパス、配布 effect と locale、フィルター登録数 1、影の実画素を確認した。公式 0.1.1 で保存した設定とシーンを保持し、新版を上書きした。旧 bin/64bit を残した状態でも新配置を読み込み、設定と描画が一致した。

代表画像は 5×5、中央の不透明画素と、その周囲の影を検査する。既存の画素検査を共用し、全パラメーターの試験を増やしていない。既存 CTest 11 件も成功した。

公式旧版 ZIP の SHA256 は b744e120a498aed68e1f6f42b8cbfaebc29f48eee9146b03dd3005aed5547e25。ログと結果は .deps/install-rm8-ci-success に保存した。

ローカルの隔離ポータブル版でも新配置からの読み込みと影の画素検査に成功した。通常版用の旧配置をポータブル版へ適用した試験は探索対象外で失敗したため、旧版更新の判定に使用していない。旧版更新を通常版 CI で確認する方針を依頼者 AllegroMoltoV が承認した。

## 配布と公開結果

README にフォルダー未作成時の対応、通常版とポータブル版の配置、旧版更新、OBS 32 以前向けの 0.1.1 固定リンクを記載した。既存の公開版を差し替えない。

依頼者 AllegroMoltoV が通常環境で目視確認し、公開を承認した。PR #3 をマージする操作中だけ ruleset 11550302 を無効化し、直後に全設定を復元・照合した。この限定操作も依頼者が明示承認した。

[タグ CI 42372855952](https://github.com/AllegroMoltoV/obs-simple-drop-shadow/actions/runs/42372855952) の 3 OS ビルドが成功した。最終 Windows ZIP を隔離 OBS 33.0.1 の新配置から読み込み、effect・locale・影の画素と設定保存を確認した。

[0.1.2 Release](https://github.com/AllegroMoltoV/obs-simple-drop-shadow/releases/tag/v0.1.2) を公開し、再取得した ZIP と検証品の SHA256 が一致した。

- ZIP: 340C040FAB47853BEE1B1B06BFF0F1894515A7DE3163972998FD3B4CF96F7749
- DLL: 80EBB4F79F5C015B2A1FFB7E08E7E1071564585141E7B42B252835B91EBC592C
- 証拠: .deps/install-rm8-final-load/results

Forum の概要、最低 OBS バージョン、導入先、フォルダー作成手順、旧 OBS 向けリンクを更新した。AI 利用開示と GIF を保持した。Go to download が 0.1.2 Release を開くことを Brave で確認した。

[更新投稿 8821](https://obsproject.com/forum/resources/simple-drop-shadow.2326/update/8821/) を登録した。モデレーター承認待ちと表示され、一般訪問者には非表示。承認反映の確認を残す。

macOS/Linux は既存のビルド確認を維持し、OBS 実機動作の保証を追加していない。通常環境には目視確認済みの CI 候補を保持している。
