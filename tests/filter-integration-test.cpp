// 影フィルタを実際の描画モジュールで動かし、出力の画素を検査する統合テスト。
// 引数: プラグイン、プラグインのデータ、描画モジュール、libobs のデータ、シナリオ名

#include "filter-render-fixture.hpp"

#include <graphics/matrix4.h>
#include <graphics/vec3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

using fixture::Checker;
using fixture::Image;

struct ShadowSettings {
	double offset_x = 0.0;
	double offset_y = 0.0;
	double blur_radius = 0.0;
	double opacity = 1.0;
	long long color = 0xff000000; // OBS の色は 0xAABBGGRR
};

obs_data_t *make_settings(const ShadowSettings &s)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_double(settings, "offset_x", s.offset_x);
	obs_data_set_double(settings, "offset_y", s.offset_y);
	obs_data_set_double(settings, "blur_radius", s.blur_radius);
	obs_data_set_double(settings, "opacity", s.opacity);
	obs_data_set_int(settings, "color", s.color);
	return settings;
}

std::string at(int x, int y)
{
	return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
}

void expect_size(Checker &c, const Image &image, uint32_t width, uint32_t height)
{
	c.expect(image.width == width && image.height == height,
		 "output size " + std::to_string(image.width) + "x" + std::to_string(image.height) + " (expected " +
			 std::to_string(width) + "x" + std::to_string(height) + ")");
}

// 半径 0 の影: 元画像の 1 画素と、オフセット先の影の 1 画素だけが描かれる
void expect_hard_shadow(Checker &c, const Image &image, int original_x, int original_y, int shadow_x, int shadow_y,
			int shadow_alpha, int shadow_rgb)
{
	for (int y = 0; y < static_cast<int>(image.height); ++y) {
		for (int x = 0; x < static_cast<int>(image.width); ++x) {
			const uint8_t *p = image.at(x, y);
			if (x == original_x && y == original_y) {
				c.expect(p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255,
					 "original pixel at " + at(x, y) + " is not opaque red");
			} else if (x == shadow_x && y == shadow_y) {
				c.expect_near(p[3], shadow_alpha, 1, "shadow alpha at " + at(x, y));
				c.expect_near(p[0], shadow_rgb, 1, "shadow red at " + at(x, y));
				c.expect_near(p[1], shadow_rgb, 1, "shadow green at " + at(x, y));
				c.expect_near(p[2], shadow_rgb, 1, "shadow blue at " + at(x, y));
			} else {
				c.expect(p[3] == 0, "unexpected alpha " + std::to_string(p[3]) + " at " + at(x, y));
			}
		}
	}
}

// 1 画素の影をぼかした結果: 中心について上下左右に対称で、軸に沿って外へ単調に減り、半径の外では 0 になる。
// 元解像度のガウシアンは重みを正規化しているため、総量は元の 1 画素分 (255) に近い
void expect_point_blur(Checker &c, const Image &image, int cx, int cy, int radius)
{
	int total = 0;
	for (int dy = -radius - 3; dy <= radius + 3; ++dy) {
		for (int dx = -radius - 3; dx <= radius + 3; ++dx) {
			const int a = image.alpha(cx + dx, cy + dy);
			total += a;
			if (std::abs(dx) > radius || std::abs(dy) > radius) {
				c.expect(a == 0,
					 "alpha " + std::to_string(a) + " outside radius at " + at(cx + dx, cy + dy));
			}
			c.expect_near(image.alpha(cx - dx, cy + dy), a, 1,
				      "horizontal symmetry at " + at(cx + dx, cy + dy));
			c.expect_near(image.alpha(cx + dx, cy - dy), a, 1,
				      "vertical symmetry at " + at(cx + dx, cy + dy));
		}
	}
	c.expect(image.alpha(cx, cy) > 0, "no shadow at the center " + at(cx, cy));
	for (int d = 1; d <= radius + 1; ++d) {
		c.expect(image.alpha(cx + d, cy) <= image.alpha(cx + d - 1, cy) + 1,
			 "shadow increases outward at " + at(cx + d, cy));
		c.expect(image.alpha(cx, cy + d) <= image.alpha(cx, cy + d - 1) + 1,
			 "shadow increases outward at " + at(cx, cy + d));
	}
	c.expect_near(total, 255, 26, "total shadow alpha");
}

// 全面が不透明な正方形のソースの影を、オフセット 0 でぼかした結果を検査する。
// 出力は中心について対称で、ソースの縁から外へ単調に減り、縁から半径だけ離れるとほぼ 0 になる
void expect_block_blur(Checker &c, const Image &image, int margin, int block, int min_edge_alpha, int max_edge_alpha)
{
	const int size = margin * 2 + block;
	expect_size(c, image, static_cast<uint32_t>(size), static_cast<uint32_t>(size));
	if (image.width != static_cast<uint32_t>(size) || image.height != static_cast<uint32_t>(size)) {
		return;
	}
	const int center = margin + block / 2;
	const int edge = margin + block; // ソースの右端の 1 つ外側
	c.expect(image.alpha(center, center) == 255, "source is not opaque at the center");
	const int edge_alpha = image.alpha(edge, center);
	c.expect(edge_alpha >= min_edge_alpha && edge_alpha <= max_edge_alpha,
		 "shadow alpha just outside the source " + std::to_string(edge_alpha) + " (expected " +
			 std::to_string(min_edge_alpha) + "-" + std::to_string(max_edge_alpha) + ")");
	for (int x = edge + 1; x < size; ++x) {
		c.expect(image.alpha(x, center) <= image.alpha(x - 1, center) + 1,
			 "shadow increases outward at " + at(x, center));
		c.expect(image.alpha(center, x) <= image.alpha(center, x - 1) + 1,
			 "shadow increases outward at " + at(center, x));
	}
	c.expect(image.alpha(size - 1, center) <= 2,
		 "shadow alpha at the outer edge " + std::to_string(image.alpha(size - 1, center)));
	for (int i = 0; i < size; ++i) {
		c.expect_near(image.alpha(size - 1 - i, center), image.alpha(i, center), 3,
			      "horizontal symmetry at " + at(i, center));
		c.expect_near(image.alpha(center, size - 1 - i), image.alpha(center, i), 3,
			      "vertical symmetry at " + at(center, i));
	}
}

void mark_updated(void *data, calldata_t *)
{
	static_cast<std::atomic_bool *>(data)->store(true);
}

bool update_and_wait(obs_source_t *filter, const ShadowSettings &s)
{
	std::atomic_bool applied = false;
	signal_handler_t *signals = obs_source_get_signal_handler(filter);
	signal_handler_connect(signals, "update", mark_updated, &applied);
	obs_data_t *settings = make_settings(s);
	obs_source_update(filter, settings);
	obs_data_release(settings);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!applied.load() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	signal_handler_disconnect(signals, "update", mark_updated, &applied);
	return applied.load();
}

// 描画スレッドが n フレーム進むのを待つ
bool wait_frames(uint32_t n)
{
	const uint32_t start = obs_get_total_frames();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (obs_get_total_frames() < start + n) {
		if (std::chrono::steady_clock::now() > deadline) {
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return true;
}

// シーンを 1 回描画する。グループの中のアイテムの変換と、ソースの大きさの変化の反映は、OBS がシーンを
// 描画するときに行うため、実際の OBS と同じく描画してから変換を読む
void render_scenes(std::initializer_list<obs_scene_t *> scenes)
{
	obs_enter_graphics();
	gs_texrender_t *target = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	for (obs_scene_t *scene : scenes) {
		gs_texrender_reset(target);
		if (gs_texrender_begin(target, 64, 64)) {
			gs_ortho(0.0f, 64.0f, 0.0f, 64.0f, -100.0f, 100.0f);
			obs_source_video_render(obs_scene_get_source(scene));
			gs_texrender_end(target);
		}
	}
	gs_texrender_destroy(target);
	obs_leave_graphics();
}

// フィルターと同じ規則で、影がはみ出す辺の余白を求める (左・上)
int margin_before_origin(double radius, double offset)
{
	return static_cast<int>(std::ceil(std::max(0.0, std::ceil(radius) - offset)));
}

struct SceneCorners {
	vec3 top_left = {};
	vec3 bottom_right = {};
};

// アイテムに描かれる元画像の左上と右下を、シーンの座標で求める。グループの中のアイテムは、グループの変換も掛ける
SceneCorners original_corners(obs_sceneitem_t *item, obs_sceneitem_t *group, const ShadowSettings &s,
			      uint32_t source_width, uint32_t source_height)
{
	obs_sceneitem_crop crop = {};
	obs_sceneitem_get_crop(item, &crop);
	matrix4 transform;
	obs_sceneitem_get_draw_transform(item, &transform);
	vec3 top_left;
	vec3_set(&top_left, static_cast<float>(margin_before_origin(s.blur_radius, s.offset_x) - crop.left),
		 static_cast<float>(margin_before_origin(s.blur_radius, s.offset_y) - crop.top), 0.0f);
	vec3 bottom_right;
	vec3_set(&bottom_right, top_left.x + static_cast<float>(source_width),
		 top_left.y + static_cast<float>(source_height), 0.0f);
	SceneCorners corners;
	vec3_transform(&corners.top_left, &top_left, &transform);
	vec3_transform(&corners.bottom_right, &bottom_right, &transform);
	if (group) {
		matrix4 group_transform;
		obs_sceneitem_get_draw_transform(group, &group_transform);
		vec3_transform(&corners.top_left, &corners.top_left, &group_transform);
		vec3_transform(&corners.bottom_right, &corners.bottom_right, &group_transform);
	}
	return corners;
}

void set_transform(obs_sceneitem_t *item, float x, float y, float rot, float scale_x, float scale_y, uint32_t align,
		   obs_bounds_type bounds_type, float bounds_x, float bounds_y)
{
	obs_transform_info info = {};
	obs_sceneitem_get_info2(item, &info);
	info.pos = {x, y};
	info.rot = rot;
	info.scale = {scale_x, scale_y};
	info.alignment = align;
	info.bounds_type = bounds_type;
	info.bounds_alignment = OBS_ALIGN_CENTER;
	info.bounds = {bounds_x, bounds_y};
	obs_sceneitem_set_info2(item, &info);
}

// 影の設定を変えても、シーン上の元画像の四隅が動かず、設定を戻すと位置とクロップも戻ることを確かめる
void check_scene_compensation(Checker &c, obs_source_t *source, obs_source_t *filter, const ShadowSettings &initial)
{
	obs_scene_t *scene1 = obs_scene_create("compensation scene 1");
	obs_scene_t *scene2 = obs_scene_create("compensation scene 2");
	struct Placed {
		const char *name;
		obs_sceneitem_t *item;
		obs_sceneitem_t *group;
	};
	std::vector<Placed> placed;

	obs_sceneitem_t *top_left = obs_scene_add(scene1, source);
	set_transform(top_left, 100.0f, 50.0f, 0.0f, 1.5f, 1.5f, OBS_ALIGN_TOP | OBS_ALIGN_LEFT, OBS_BOUNDS_NONE, 0.0f,
		      0.0f);
	placed.push_back({"top-left aligned, scaled", top_left, nullptr});

	obs_sceneitem_t *rotated = obs_scene_add(scene1, source);
	set_transform(rotated, 400.0f, 300.0f, 30.0f, 0.75f, 1.25f, OBS_ALIGN_CENTER, OBS_BOUNDS_NONE, 0.0f, 0.0f);
	const obs_sceneitem_crop rotated_crop = {3, 2, 0, 4};
	obs_sceneitem_set_crop(rotated, &rotated_crop);
	placed.push_back({"center aligned, rotated, cropped", rotated, nullptr});

	obs_sceneitem_t *stretched = obs_scene_add(scene1, source);
	set_transform(stretched, 900.0f, 700.0f, 0.0f, 1.0f, 1.0f, OBS_ALIGN_BOTTOM | OBS_ALIGN_RIGHT,
		      OBS_BOUNDS_STRETCH, 200.0f, 120.0f);
	placed.push_back({"bottom-right aligned, stretch bounds", stretched, nullptr});

	obs_sceneitem_t *inner = obs_scene_add(scene2, source);
	set_transform(inner, 50.0f, 60.0f, -15.0f, 1.0f, 1.0f, OBS_ALIGN_TOP | OBS_ALIGN_LEFT, OBS_BOUNDS_SCALE_INNER,
		      300.0f, 300.0f);
	placed.push_back({"scale-inner bounds, rotated", inner, nullptr});

	obs_sceneitem_t *group = obs_scene_add_group2(scene2, "compensation group", false);
	obs_sceneitem_t *grouped = obs_scene_add(scene2, source);
	set_transform(grouped, 500.0f, 100.0f, 0.0f, 1.0f, 1.0f, OBS_ALIGN_TOP | OBS_ALIGN_LEFT, OBS_BOUNDS_NONE, 0.0f,
		      0.0f);
	obs_sceneitem_group_add_item(group, grouped);
	placed.push_back({"inside a group", grouped, group});

	const uint32_t width = fixture::current_spec.width;
	const uint32_t height = fixture::current_spec.height;
	if (!wait_frames(3)) {
		c.expect(false, "OBS did not advance frames");
	}
	render_scenes({scene1, scene2});
	std::vector<SceneCorners> expected;
	std::vector<obs_transform_info> initial_info;
	std::vector<obs_sceneitem_crop> initial_crop;
	for (const Placed &p : placed) {
		expected.push_back(original_corners(p.item, p.group, initial, width, height));
		obs_transform_info info = {};
		obs_sceneitem_get_info2(p.item, &info);
		initial_info.push_back(info);
		obs_sceneitem_crop crop = {};
		obs_sceneitem_get_crop(p.item, &crop);
		initial_crop.push_back(crop);
	}

	const ShadowSettings steps[] = {
		{-10.0, 6.0, 30.0, 1.0, 0xff000000},
		{12.0, -20.0, 3.0, 1.0, 0xff000000},
		{0.0, 0.0, 64.0, 1.0, 0xff000000},
		initial,
	};
	for (const ShadowSettings &s : steps) {
		const std::string label = "offset (" + std::to_string(s.offset_x) + ", " + std::to_string(s.offset_y) +
					  ") radius " + std::to_string(s.blur_radius);
		if (!update_and_wait(filter, s) || !wait_frames(3)) {
			c.expect(false, "settings were not applied: " + label);
			continue;
		}
		render_scenes({scene1, scene2});
		for (size_t i = 0; i < placed.size(); ++i) {
			const SceneCorners now = original_corners(placed[i].item, placed[i].group, s, width, height);
			const float error = std::max({std::fabs(now.top_left.x - expected[i].top_left.x),
						      std::fabs(now.top_left.y - expected[i].top_left.y),
						      std::fabs(now.bottom_right.x - expected[i].bottom_right.x),
						      std::fabs(now.bottom_right.y - expected[i].bottom_right.y)});
			// OBS はアイテムの位置を 0.5 画素単位に丸めて描くため、各軸 0.25 画素までのずれは避けられない
			c.expect(error <= 0.26f, std::string(placed[i].name) + " moved by " + std::to_string(error) +
							 " px after " + label);
		}
	}

	// 最初の設定に戻すと、位置・バウンディングボックス・クロップも最初の値に戻る
	for (size_t i = 0; i < placed.size(); ++i) {
		obs_transform_info info = {};
		obs_sceneitem_get_info2(placed[i].item, &info);
		obs_sceneitem_crop crop = {};
		obs_sceneitem_get_crop(placed[i].item, &crop);
		c.expect(std::fabs(info.pos.x - initial_info[i].pos.x) <= 0.05f &&
				 std::fabs(info.pos.y - initial_info[i].pos.y) <= 0.05f &&
				 std::fabs(info.bounds.x - initial_info[i].bounds.x) <= 0.05f &&
				 std::fabs(info.bounds.y - initial_info[i].bounds.y) <= 0.05f,
			 std::string(placed[i].name) + " did not return to its initial transform");
		c.expect(crop.left == initial_crop[i].left && crop.top == initial_crop[i].top &&
				 crop.right == initial_crop[i].right && crop.bottom == initial_crop[i].bottom,
			 std::string(placed[i].name) + " did not return to its initial crop");
	}

	// グループを含むシーンを解放するだけで終了すると、補正を行わない場合でも obs_shutdown で
	// "Double destroy" のエラーとともに異常終了した。グループを先にシーンから取り除く
	obs_sceneitem_remove(group);
	obs_scene_release(scene2);
	obs_scene_release(scene1);
}

// 32x32 のソースの (10, 10) にある赤い 1 画素
const fixture::SourceSpec red_pixel = {32, 32, fixture::single_pixel(10, 10, 255, 0, 0)};

struct Scenario {
	const char *name;
	fixture::SourceSpec source;
	ShadowSettings settings;
	std::function<void(Checker &, obs_source_t *source, obs_source_t *filter)> check;
};

const Scenario scenarios[] = {
	// 正のオフセット: 余白は右と下だけに付き、元画像は動かない
	{"hard-shadow",
	 red_pixel,
	 {3.0, 2.0, 0.0, 1.0, 0xff000000},
	 [](Checker &c, obs_source_t *source, obs_source_t *) {
		 Image image;
		 if (fixture::render(source, image)) {
			 expect_size(c, image, 35, 34);
			 expect_hard_shadow(c, image, 10, 10, 13, 12, 255, 0);
		 } else {
			 c.expect(false, "render failed");
		 }
	 }},
	// 負のオフセット: 余白は左と上に付き、元画像はその分だけ右下へ動く
	{"negative-offset",
	 red_pixel,
	 {-4.0, -3.0, 0.0, 1.0, 0xff000000},
	 [](Checker &c, obs_source_t *source, obs_source_t *) {
		 Image image;
		 if (fixture::render(source, image)) {
			 expect_size(c, image, 36, 35);
			 expect_hard_shadow(c, image, 14, 13, 10, 10, 255, 0);
		 } else {
			 c.expect(false, "render failed");
		 }
	 }},
	// 影の色 (白) と不透明度 (0.5) は、アルファを掛けた色として出力される
	{"color-opacity",
	 red_pixel,
	 {3.0, 2.0, 0.0, 0.5, 0xffffffff},
	 [](Checker &c, obs_source_t *source, obs_source_t *) {
		 Image image;
		 if (fixture::render(source, image)) {
			 expect_size(c, image, 35, 34);
			 expect_hard_shadow(c, image, 10, 10, 13, 12, 128, 128);
		 } else {
			 c.expect(false, "render failed");
		 }
	 }},
	// 小さい半径 (σ = 1) は元の解像度のガウシアンで処理する。余白は左 0・右 11・上下 3
	{"blur-small",
	 red_pixel,
	 {8.0, 0.0, 3.0, 1.0, 0xff000000},
	 [](Checker &c, obs_source_t *source, obs_source_t *) {
		 Image image;
		 if (fixture::render(source, image)) {
			 expect_size(c, image, 43, 38);
			 expect_point_blur(c, image, 18, 13, 3);
			 c.expect(image.alpha(10, 13) == 255, "original pixel moved or is not opaque");
		 } else {
			 c.expect(false, "render failed");
		 }
	 }},
	// 大きい半径 (σ = 16) は Dual Kawase の 4 段と残差のガウシアンで処理する
	{"blur-large",
	 {64, 64, fixture::fill_opaque},
	 {0.0, 0.0, 48.0, 1.0, 0xff000000},
	 [](Checker &c, obs_source_t *source, obs_source_t *) {
		 Image image;
		 if (fixture::render(source, image)) {
			 expect_block_blur(c, image, 48, 64, 90, 170);
		 } else {
			 c.expect(false, "render failed");
		 }
	 }},
	// 上限の半径 1024 (σ ≈ 341) で、出力の大きさと影の形が保たれる
	{"blur-max",
	 {256, 256, fixture::fill_opaque},
	 {0.0, 0.0, 1024.0, 1.0, 0xff000000},
	 [](Checker &c, obs_source_t *source, obs_source_t *) {
		 Image image;
		 if (fixture::render(source, image)) {
			 expect_block_blur(c, image, 1024, 256, 15, 80);
		 } else {
			 c.expect(false, "render failed");
		 }
	 }},
	// 設定の変更が、出力の大きさと影の形に反映される
	{"settings-update",
	 red_pixel,
	 {3.0, 2.0, 0.0, 1.0, 0xff000000},
	 [](Checker &c, obs_source_t *source, obs_source_t *filter) {
		 Image before;
		 if (!fixture::render(source, before)) {
			 c.expect(false, "render failed");
			 return;
		 }
		 expect_size(c, before, 35, 34);
		 if (!update_and_wait(filter, {8.0, 0.0, 3.0, 1.0, 0xff000000})) {
			 c.expect(false, "OBS did not apply filter settings within two seconds");
			 return;
		 }
		 Image after;
		 if (fixture::render(source, after)) {
			 expect_size(c, after, 43, 38);
			 expect_point_blur(c, after, 18, 13, 3);
		 } else {
			 c.expect(false, "render failed");
		 }
	 }},
	// 影の設定を変えて出力の大きさが変わっても、シーン上の元画像の位置と大きさが保たれる
	{"scene-compensation",
	 red_pixel,
	 {4.0, 4.0, 4.0, 1.0, 0xff000000},
	 [](Checker &c, obs_source_t *source, obs_source_t *filter) {
		 check_scene_compensation(c, source, filter, {4.0, 4.0, 4.0, 1.0, 0xff000000});
	 }},
};

} // namespace

int main(int argc, char **argv)
{
	if (argc != 6) {
		std::cerr << "expected plugin, module data, graphics module, core data, and scenario\n";
		return 2;
	}
	const Scenario *scenario = nullptr;
	for (const Scenario &s : scenarios) {
		if (std::strcmp(argv[5], s.name) == 0) {
			scenario = &s;
		}
	}
	if (!scenario) {
		std::cerr << "unknown scenario\n";
		return 2;
	}
	if (!obs_test::start(argv[3], argv[4], 64, 64)) {
		return 2;
	}
	fixture::register_source();
	fixture::current_spec = scenario->source;

	int result = 2;
	if (obs_test::load_plugin(argv[1], argv[2])) {
		obs_source_t *source = obs_source_create("drop_shadow_test_source", "source", nullptr, nullptr);
		obs_data_t *settings = make_settings(scenario->settings);
		obs_source_t *filter = obs_source_create("drop_shadow_filter", "shadow", settings, nullptr);
		obs_data_release(settings);
		if (!source || !filter) {
			std::cerr << "failed to create test source or filter\n";
		} else {
			obs_source_filter_add(source, filter);
			Checker checker;
			scenario->check(checker, source, filter);
			result = checker.failures == 0 ? 0 : 1;
			if (result != 0) {
				std::cerr << checker.failures << " check(s) failed in " << scenario->name << '\n';
			}
		}
		obs_source_release(filter);
		obs_source_release(source);
	}
	obs_shutdown();
	return result;
}
