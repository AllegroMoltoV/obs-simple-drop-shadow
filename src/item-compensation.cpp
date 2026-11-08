#include "item-compensation.hpp"

#include <graphics/math-defs.h>

#include <algorithm>
#include <cmath>
#include <vector>

ShadowMargins compute_shadow_margins(float radius, float offset_x, float offset_y)
{
	const float r = std::ceil(radius);
	ShadowMargins m;
	m.left = static_cast<int>(std::ceil(std::max(0.0f, r - offset_x)));
	m.right = static_cast<int>(std::ceil(std::max(0.0f, r + offset_x)));
	m.top = static_cast<int>(std::ceil(std::max(0.0f, r - offset_y)));
	m.bottom = static_cast<int>(std::ceil(std::max(0.0f, r + offset_y)));
	return m;
}

namespace {

struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;
};

// libobs/obs-scene.c の add_alignment と同じ計算。中央揃えは整数の半分を使う
void add_alignment(Vec2 &v, uint32_t align, int cx, int cy)
{
	if (align & OBS_ALIGN_RIGHT) {
		v.x += static_cast<float>(cx);
	} else if ((align & OBS_ALIGN_LEFT) == 0) {
		v.x += static_cast<float>(cx / 2);
	}
	if (align & OBS_ALIGN_BOTTOM) {
		v.y += static_cast<float>(cy);
	} else if ((align & OBS_ALIGN_TOP) == 0) {
		v.y += static_cast<float>(cy / 2);
	}
}

// 浮動小数点から OBS と同じく 0 方向へ切り捨てた整数にする
int truncate(float v)
{
	return static_cast<int>(static_cast<int64_t>(v));
}

struct Placement {
	Vec2 origin;
	Vec2 scale;
};

// libobs/obs-scene.c の update_item_transform と calculate_bounds_data を再現し、出力 (width x height) を
// 描くときの拡大率と原点を求める。アイテムの描画は「位置 + 回転 × (拡大率 × 出力上の座標 - 原点)」になる。
// crop_to_bounds による切り取りは再現しない
Placement place(const obs_transform_info &info, const obs_sceneitem_crop &crop, uint32_t width, uint32_t height)
{
	const uint32_t crop_cx = static_cast<uint32_t>(crop.left + crop.right);
	const uint32_t crop_cy = static_cast<uint32_t>(crop.top + crop.bottom);
	const uint32_t cx = crop_cx > width ? 2 : width - crop_cx;
	const uint32_t cy = crop_cy > height ? 2 : height - crop_cy;

	Placement p;
	p.scale = {info.scale.x, info.scale.y};
	if (info.bounds_type == OBS_BOUNDS_NONE) {
		add_alignment(p.origin, info.alignment, truncate(static_cast<float>(cx) * p.scale.x),
			      truncate(static_cast<float>(cy) * p.scale.y));
		return p;
	}

	const Vec2 bounds = {info.bounds.x, info.bounds.y};
	float w = static_cast<float>(cx) * std::fabs(p.scale.x);
	float h = static_cast<float>(cy) * std::fabs(p.scale.y);
	const float item_aspect = w / h;
	const float bounds_aspect = bounds.x / bounds.y;
	uint32_t type = info.bounds_type;
	if (info.bounds_type == OBS_BOUNDS_MAX_ONLY && (w > bounds.x || h > bounds.y)) {
		type = OBS_BOUNDS_SCALE_INNER;
	}
	if (type == OBS_BOUNDS_SCALE_INNER || type == OBS_BOUNDS_SCALE_OUTER) {
		bool use_width = bounds_aspect < item_aspect;
		if (info.bounds_type == OBS_BOUNDS_SCALE_OUTER) {
			use_width = !use_width;
		}
		const float mul = use_width ? bounds.x / w : bounds.y / h;
		p.scale.x *= mul;
		p.scale.y *= mul;
	} else if (type == OBS_BOUNDS_SCALE_TO_WIDTH) {
		p.scale.x *= bounds.x / w;
		p.scale.y *= bounds.x / w;
	} else if (type == OBS_BOUNDS_SCALE_TO_HEIGHT) {
		p.scale.x *= bounds.y / h;
		p.scale.y *= bounds.y / h;
	} else if (type == OBS_BOUNDS_STRETCH) {
		p.scale.x = std::copysign(bounds.x / static_cast<float>(cx), p.scale.x);
		p.scale.y = std::copysign(bounds.y / static_cast<float>(cy), p.scale.y);
	}

	w = static_cast<float>(cx) * p.scale.x;
	h = static_cast<float>(cy) * p.scale.y;
	const float width_diff = bounds.x - std::fabs(w);
	const float height_diff = bounds.y - std::fabs(h);
	add_alignment(p.origin, info.bounds_alignment, truncate(-width_diff), truncate(-height_diff));
	p.origin.x += w < 0.0f ? w : 0.0f;
	p.origin.y += h < 0.0f ? h : 0.0f;
	add_alignment(p.origin, info.alignment, static_cast<int>(static_cast<uint32_t>(bounds.x)),
		      static_cast<int>(static_cast<uint32_t>(bounds.y)));
	return p;
}

// アイテムの回転 (度) で v を回す。OBS の描画と同じく、画面の下向きを y の正とする
Vec2 rotate(const Vec2 &v, float degrees)
{
	const float rad = RAD(degrees);
	const float c = std::cos(rad);
	const float s = std::sin(rad);
	return {v.x * c - v.y * s, v.x * s + v.y * c};
}

// 出力上の点 u を、拡大率と原点で写した位置 (回転と平行移動の前)
Vec2 local_point(const Placement &p, const Vec2 &u)
{
	return {p.scale.x * u.x - p.origin.x, p.scale.y * u.y - p.origin.y};
}

// OBS はアイテムの位置を読み出すときと描画するときに 0.5 画素単位へ丸める (libobs/obs-scene.c の nudge_abs_values)。
// 丸めた値を基準に補正を重ねると誤差が積み上がるため、補正で書き込んだ丸める前の位置と、そのとき読み出せた丸めた位置を
// アイテムの非公開設定に残す。次の補正で読み出した位置が記録と同じなら、利用者は動かしていないとみなし、丸める前の位置を基準にする
constexpr const char *kExactPosX = "simple_drop_shadow_exact_pos_x";
constexpr const char *kExactPosY = "simple_drop_shadow_exact_pos_y";
constexpr const char *kShownPosX = "simple_drop_shadow_shown_pos_x";
constexpr const char *kShownPosY = "simple_drop_shadow_shown_pos_y";

Vec2 base_position(obs_sceneitem_t *item, const vec2 &shown)
{
	Vec2 base = {shown.x, shown.y};
	obs_data_t *data = obs_sceneitem_get_private_settings(item);
	if (data && obs_data_has_user_value(data, kExactPosX) && obs_data_has_user_value(data, kShownPosX) &&
	    std::fabs(obs_data_get_double(data, kShownPosX) - shown.x) < 1e-3 &&
	    std::fabs(obs_data_get_double(data, kShownPosY) - shown.y) < 1e-3) {
		base = {static_cast<float>(obs_data_get_double(data, kExactPosX)),
			static_cast<float>(obs_data_get_double(data, kExactPosY))};
	}
	obs_data_release(data);
	return base;
}

void remember_position(obs_sceneitem_t *item, const Vec2 &exact)
{
	vec2 shown;
	obs_sceneitem_get_pos(item, &shown);
	obs_data_t *data = obs_sceneitem_get_private_settings(item);
	if (data) {
		obs_data_set_double(data, kExactPosX, exact.x);
		obs_data_set_double(data, kExactPosY, exact.y);
		obs_data_set_double(data, kShownPosX, shown.x);
		obs_data_set_double(data, kShownPosY, shown.y);
	}
	obs_data_release(data);
}

// クロップは出力の端からの切り取りとして扱い、変えない。余白が変わると、切り取られる部分は影の余白と元画像の間で移る
void compensate_item(obs_sceneitem_t *item, uint32_t source_width, uint32_t source_height, const ShadowMargins &before,
		     const ShadowMargins &after)
{
	obs_transform_info info = {};
	obs_sceneitem_get_info2(item, &info);
	obs_sceneitem_crop crop = {};
	obs_sceneitem_get_crop(item, &crop);

	const uint32_t width0 = source_width + static_cast<uint32_t>(before.left + before.right);
	const uint32_t height0 = source_height + static_cast<uint32_t>(before.top + before.bottom);
	const uint32_t width1 = source_width + static_cast<uint32_t>(after.left + after.right);
	const uint32_t height1 = source_height + static_cast<uint32_t>(after.top + after.bottom);

	obs_transform_info new_info = info;
	if (info.bounds_type != OBS_BOUNDS_NONE) {
		// バウンディングボックスは出力 (クロップ後) と同じ比率で広げ、元画像の拡大率を保つ
		const float crop_x = static_cast<float>(crop.left + crop.right);
		const float crop_y = static_cast<float>(crop.top + crop.bottom);
		const float cx0 = static_cast<float>(width0) - crop_x;
		const float cy0 = static_cast<float>(height0) - crop_y;
		const float cx1 = static_cast<float>(width1) - crop_x;
		const float cy1 = static_cast<float>(height1) - crop_y;
		if (cx0 > 0.0f && cy0 > 0.0f && cx1 > 0.0f && cy1 > 0.0f) {
			new_info.bounds.x = info.bounds.x * cx1 / cx0;
			new_info.bounds.y = info.bounds.y * cy1 / cy0;
		}
	}

	// 元画像の左上の点が、変更の前後でシーン上の同じ位置に来るよう位置を決める
	const Vec2 u0 = {static_cast<float>(before.left - crop.left), static_cast<float>(before.top - crop.top)};
	const Vec2 u1 = {static_cast<float>(after.left - crop.left), static_cast<float>(after.top - crop.top)};
	const Vec2 p0 = local_point(place(info, crop, width0, height0), u0);
	const Vec2 p1 = local_point(place(new_info, crop, width1, height1), u1);
	const Vec2 delta = rotate({p0.x - p1.x, p0.y - p1.y}, info.rot);
	const Vec2 base = base_position(item, info.pos);
	const Vec2 exact = {base.x + delta.x, base.y + delta.y};
	new_info.pos.x = exact.x;
	new_info.pos.y = exact.y;

	obs_sceneitem_set_info2(item, &new_info);
	remember_position(item, exact);
}

struct CollectContext {
	obs_source_t *parent = nullptr;
	std::vector<obs_sceneitem_t *> items;
};

void add_unique(CollectContext &ctx, obs_sceneitem_t *item)
{
	if (std::find(ctx.items.begin(), ctx.items.end(), item) == ctx.items.end()) {
		obs_sceneitem_addref(item);
		ctx.items.push_back(item);
	}
}

bool collect_item(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto &ctx = *static_cast<CollectContext *>(param);
	if (obs_sceneitem_get_source(item) == ctx.parent) {
		add_unique(ctx, item);
	}
	if (obs_sceneitem_is_group(item)) {
		obs_sceneitem_group_enum_items(item, collect_item, param);
	}
	return true;
}

bool collect_scene(void *param, obs_source_t *scene_source)
{
	// キャンバスはシーンだけを列挙する。グループの中のアイテムは collect_item で辿る
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (scene) {
		obs_scene_enum_items(scene, collect_item, param);
	}
	return true;
}

bool collect_canvas(void *param, obs_canvas_t *canvas)
{
	obs_canvas_enum_scenes(canvas, collect_scene, param);
	return true;
}

} // namespace

void compensate_scene_items(obs_source_t *parent, uint32_t source_width, uint32_t source_height,
			    const ShadowMargins &before, const ShadowMargins &after)
{
	if (!parent || source_width == 0 || source_height == 0 || before == after) {
		return;
	}
	// グループは複数の経路から辿れるため、重複を除いてから 1 回ずつ補正する
	CollectContext ctx;
	ctx.parent = parent;
	obs_enum_canvases(collect_canvas, &ctx);
	for (obs_sceneitem_t *item : ctx.items) {
		compensate_item(item, source_width, source_height, before, after);
		obs_sceneitem_release(item);
	}
}
