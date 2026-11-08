# UI 改善要求の現状調査

確認日: 2026-11-08

## はじめに

依頼者から、フィルタの UI について次の 4 点の要求を受けました。本書では、各要求に関係する現在の実装を調べた結果を記録します。

1. X座標・Y座標・ぼかし半径をスライダーで操作できるようにする
2. ぼかし半径の最大値を 1024 にする
3. 最も低負荷でドロップシャドウを描画する方法を、imozuru スキルで検討する
4. フィルタ一覧に表示される名前を「シンプルドロップシャドウ」にする

## 検証方法

`src/drop-shadow-filter.cpp`・`data/effects/drop-shadow.effect`・`data/locale/*.ini`・`buildspec.json` を通読しました。スライダー表示の挙動は、ビルド依存として取得済みの OBS 31.1.1 のソース `.deps/obs-studio-31.1.1/shared/properties-view/properties-view.cpp` で確認しました。

## 検証結果

### プロパティの定義

`src/drop-shadow-filter.cpp` の `Properties()` では、X座標・Y座標・ぼかし半径を `obs_properties_add_float` で数値入力欄として定義しています。不透明度だけが `obs_properties_add_float_slider` です。

```cpp
obs_properties_add_float(props, "offset_x", obs_module_text("OffsetX"), -100.0, 100.0, 1.0);
obs_properties_add_float(props, "offset_y", obs_module_text("OffsetY"), -100.0, 100.0, 1.0);
obs_properties_add_float(props, "blur_radius", obs_module_text("BlurRadius"), 0.0, 20.0, 0.5);
obs_properties_add_color(props, "color", obs_module_text("Color"));
obs_properties_add_float_slider(props, "opacity", obs_module_text("Opacity"), 0.0, 1.0, 0.01);
```

OBS 31.1.1 の `OBSPropertiesView::AddFloat` では、スライダー型の数値プロパティに対し、スライダーと数値入力欄の両方を並べて表示します。両者の値は相互に連動します。

```cpp
if (type == OBS_NUMBER_SLIDER) {
	DoubleSlider *slider = new DoubleSlider();
	slider->setDoubleConstraints(minVal, maxVal, stepVal, val);
	(中略)
	connect(slider, &DoubleSlider::doubleValChanged, spin, &QDoubleSpinBox::setValue);
	connect(spin, &QDoubleSpinBox::valueChanged, slider, &DoubleSlider::setDoubleVal);
}
(中略)
subLayout->addWidget(spin);
```

設定キー `offset_x`・`offset_y`・`blur_radius` を変えなければ、既存シーンに保存された値はそのまま読み込まれます。

### ぼかしの描画方式と負荷

`data/effects/drop-shadow.effect` のぼかしは、横方向と縦方向の 2 パスに分けたボックスブラーです。各パスは半径を `r` とすると 1 画素当たり `2r + 1` 回テクスチャを読みます。ループ回数は `ceil(blur_radius)` から決まります。

半径の上限を 1024 にすると、1 画素当たりの読み取りは 1 パス 2,049 回、2 パス合計 4,098 回になります。1920×1080 の画像では 1 フレーム当たり約 85 億回です。現在の上限 20 では 82 回です。実機のフレーム時間は測定していません。

中間テクスチャと元画像のテクスチャは、ソースと同じ解像度の `GS_RGBA` です。影は元画像の範囲内に描画され、ソースの外側には広がりません。

### フィルタの表示名

`GetName()` は `obs_module_text("DropShadowFilterName")` を返します。ロケールの値は次のとおりです。

```ini
; data/locale/ja-JP.ini
DropShadowFilterName="ドロップシャドウ"
; data/locale/en-US.ini
DropShadowFilterName="Drop Shadow"
```

フィルタの内部 ID は `drop_shadow_filter` です。`buildspec.json` の `displayName` は「シンプルなドロップシャドウプラグイン」です。

### 公開状況

0.1.2 は GitHub で公開済みです。OBS Forum の更新 8821 はモデレーターの承認待ちです。課題 `obs-simple-drop-shadow-rm8` と `obs-simple-drop-shadow-29q` で管理しています。

## 考察

要求 2 は要求 3 に依存すると考えています。現在のボックスブラーのまま上限を 1024 に上げると、大きな半径で GPU 負荷が実用範囲を超えると推測します。縮小してからぼかす方式や、読み取り回数が半径に比例しない方式を要求 3 で選んでから、上限を上げる順序が妥当だと考えています。

半径 1024 の影は、ソースの範囲内にしか描画されない現在の仕様では大部分が画面外へ切れると推測します。影をソースの外側へ広げる要否は、要求の範囲を左右するため依頼者に確認します。

表示名の変更は、内部 ID を変えないため既存シーンの読み込みに影響しないと考えています。既に追加済みのフィルタはインスタンス名を保存しているため、名前は変わらないと推測します。この挙動は実機で確認していません。
