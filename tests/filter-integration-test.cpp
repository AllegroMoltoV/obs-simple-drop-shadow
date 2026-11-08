// 影フィルタを実際の描画モジュールで動かし、出力の画素を検査する統合テスト。
// 引数: プラグイン、プラグインのデータ、描画モジュール、libobs のデータ、シナリオ名

#include "filter-render-fixture.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

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
