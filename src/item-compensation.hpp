#pragma once

#include <obs.h>

#include <cstdint>

// 影の分だけ広げる出力の余白 (画素)
struct ShadowMargins {
	int left = 0;
	int right = 0;
	int top = 0;
	int bottom = 0;

	bool operator==(const ShadowMargins &other) const
	{
		return left == other.left && right == other.right && top == other.top && bottom == other.bottom;
	}
	bool operator!=(const ShadowMargins &other) const { return !(*this == other); }
};

// 影がソースの外へはみ出す分だけ、辺ごとに余白を取る
ShadowMargins compute_shadow_margins(float radius, float offset_x, float offset_y);

// 余白が before から after へ変わったとき、parent を参照する全シーンのアイテムの位置とバウンディングボックスを
// 補正し、シーン上の元画像の位置と大きさを保つ。source_width と source_height は元画像の大きさ。補正したアイテムの数を返す
size_t compensate_scene_items(obs_source_t *parent, uint32_t source_width, uint32_t source_height,
			      const ShadowMargins &before, const ShadowMargins &after);
