// 影フィルタの GPU 時間と、ぼかしの広がりを測るベンチマーク。
// time モード: 1920x1080 のソースに影フィルタを付けて描画し、フィルタなしで同じソースを描画した時間との差を出す。
//   計測は gs_timer (D3D11 のタイムスタンプクエリ) で行い、描画先はフィルタの出力サイズに合わせる。
// profile モード: 左半分だけ不透明なソースの影を右へずらして描き、影の縁を横切る 1 行のアルファから、
//   縁のぼけ幅 (平坦部の 84% から 16% までの距離の半分) を σ として出す。
//   あわせて、影の縁からアルファが 0 になるまでの距離 (extent) を出す。

#include <obs.h>
#if defined(__linux__)
#include <obs-nix-platform.h>
#include <X11/Xlib.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t source_width = 1920;
constexpr uint32_t source_height = 1080;
constexpr int warmup_frames = 10;
constexpr int measured_frames = 60;
constexpr int repeats = 5;

bool profile_mode = false;

struct BenchSource {
	gs_texture_t *texture = nullptr;
};

void *create_source(obs_data_t *, obs_source_t *)
{
	std::vector<uint8_t> pixels(static_cast<size_t>(source_width) * source_height * 4, 0);
	for (uint32_t y = 0; profile_mode && y < source_height; ++y) {
		for (uint32_t x = 0; x < source_width / 2; ++x) {
			pixels[(static_cast<size_t>(y) * source_width + x) * 4 + 3] = 255;
		}
	}
	// 配信の重ね合わせ素材を想定し、中央に不透明な矩形、周囲を透明にする
	for (uint32_t y = source_height / 4; !profile_mode && y < source_height * 3 / 4; ++y) {
		for (uint32_t x = source_width / 4; x < source_width * 3 / 4; ++x) {
			uint8_t *p = &pixels[(static_cast<size_t>(y) * source_width + x) * 4];
			p[0] = 200;
			p[1] = 120;
			p[2] = 40;
			p[3] = 255;
		}
	}
	auto *source = new BenchSource;
	const uint8_t *data = pixels.data();
	obs_enter_graphics();
	source->texture = gs_texture_create(source_width, source_height, GS_RGBA, 1, &data, 0);
	obs_leave_graphics();
	if (!source->texture) {
		delete source;
		return nullptr;
	}
	return source;
}

void destroy_source(void *data)
{
	auto *source = static_cast<BenchSource *>(data);
	obs_enter_graphics();
	gs_texture_destroy(source->texture);
	obs_leave_graphics();
	delete source;
}

uint32_t get_width(void *)
{
	return source_width;
}

uint32_t get_height(void *)
{
	return source_height;
}

void render_source(void *data, gs_effect_t *effect)
{
	auto *source = static_cast<BenchSource *>(data);
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), source->texture);
	gs_draw_sprite(source->texture, 0, source_width, source_height);
}

// 1 フレームあたりの GPU 時間 (ミリ秒) を返す。失敗時は負の値。
double measure(obs_source_t *source)
{
	double result = -1.0;
	obs_enter_graphics();
	const uint32_t width = obs_source_get_width(source);
	const uint32_t height = obs_source_get_height(source);
	gs_texrender_t *target = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_timer_range_t *range = gs_timer_range_create();
	gs_timer_t *timer = gs_timer_create();

	auto render_frame = [&]() {
		gs_texrender_reset(target);
		if (!gs_texrender_begin(target, width, height)) {
			return false;
		}
		struct vec4 clear = {};
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
		obs_source_video_render(source);
		gs_texrender_end(target);
		return true;
	};

	bool ok = target && range && timer;
	for (int i = 0; ok && i < warmup_frames; ++i) {
		ok = render_frame();
	}
	if (ok) {
		gs_timer_range_begin(range);
		gs_timer_begin(timer);
		for (int i = 0; ok && i < measured_frames; ++i) {
			ok = render_frame();
		}
		gs_timer_end(timer);
		gs_timer_range_end(range);
		gs_flush();
	}
	if (ok) {
		bool disjoint = true;
		uint64_t frequency = 0;
		uint64_t ticks = 0;
		if (gs_timer_range_get_data(range, &disjoint, &frequency) && gs_timer_get_data(timer, &ticks) &&
		    !disjoint && frequency > 0) {
			result = static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency) / measured_frames;
		}
	}
	gs_timer_destroy(timer);
	gs_timer_range_destroy(range);
	gs_texrender_destroy(target);
	obs_leave_graphics();
	return result;
}

// 行 (row) のうち、元画像の右端 (start_x) より右だけを読む。そこには影だけが描かれ、影の縁は edge_x にある
bool profile_row(obs_source_t *source, uint32_t row, uint32_t start_x, uint32_t edge_x, double &sigma, int &extent,
		 int &peak)
{
	bool ok = false;
	obs_enter_graphics();
	const uint32_t width = obs_source_get_width(source);
	const uint32_t height = obs_source_get_height(source);
	gs_texrender_t *target = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *staging = gs_stagesurface_create(width, height, GS_RGBA);
	if (target && staging && gs_texrender_begin(target, width, height)) {
		struct vec4 clear = {};
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_video_render(source);
		gs_blend_state_pop();
		gs_texrender_end(target);
		gs_stage_texture(staging, gs_texrender_get_texture(target));
		uint8_t *mapped = nullptr;
		uint32_t stride = 0;
		if (row < height && start_x < width && gs_stagesurface_map(staging, &mapped, &stride)) {
			peak = 0;
			for (uint32_t x = start_x; x < width; ++x) {
				peak = std::max(peak, static_cast<int>(mapped[row * stride + x * 4 + 3]));
			}
			// 平坦部の 84% と 16% を下回る最初の位置を探す。ガウシアンではこの距離が 2σ になる
			double x84 = -1.0;
			double x16 = -1.0;
			extent = -1;
			for (uint32_t x = start_x; x < width; ++x) {
				const int a = mapped[row * stride + x * 4 + 3];
				if (x84 < 0.0 && a < peak * 0.84) {
					x84 = x;
				}
				if (x16 < 0.0 && a < peak * 0.16) {
					x16 = x;
				}
				if (a > 0) {
					extent = static_cast<int>(x) - static_cast<int>(edge_x);
				}
			}
			sigma = (x84 >= 0.0 && x16 >= 0.0) ? (x16 - x84) / 2.0 : -1.0;
			gs_stagesurface_unmap(staging);
			ok = peak > 0;
		}
	}
	gs_stagesurface_destroy(staging);
	gs_texrender_destroy(target);
	obs_leave_graphics();
	return ok;
}

double median_of_repeats(obs_source_t *source)
{
	std::vector<double> samples;
	for (int i = 0; i < repeats; ++i) {
		const double ms = measure(source);
		if (ms < 0.0) {
			return -1.0;
		}
		samples.push_back(ms);
	}
	std::sort(samples.begin(), samples.end());
	return samples[samples.size() / 2];
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 7 || (std::strcmp(argv[5], "time") != 0 && std::strcmp(argv[5], "profile") != 0)) {
		std::cerr << "expected plugin, module data, graphics module, core data, time|profile, and radii\n";
		return 2;
	}
	profile_mode = std::strcmp(argv[5], "profile") == 0;
#if defined(__linux__)
	// Linux の OpenGL 描画は X11 のディスプレイと EGL を使う。CI では xvfb のディスプレイを開く
	Display *display = XOpenDisplay(nullptr);
	if (!display) {
		std::cerr << "failed to open X display\n";
		return 2;
	}
	obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
	obs_set_nix_platform_display(display);
#endif
	if (!obs_startup("en-US", nullptr, nullptr)) {
		std::cerr << "failed to start OBS\n";
		return 2;
	}
	// obs_add_data_path は非推奨だが、フロントエンドなしで libobs の既定エフェクトを読ませる手段がほかにない
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	std::string core_data_path = argv[4];
	core_data_path += '/';
	obs_add_data_path(core_data_path.c_str());
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif
	obs_video_info video = {};
	video.graphics_module = argv[3];
	video.fps_num = 60;
	video.fps_den = 1;
	video.base_width = source_width;
	video.base_height = source_height;
	video.output_width = source_width;
	video.output_height = source_height;
	video.output_format = VIDEO_FORMAT_RGBA;
	if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS) {
		std::cerr << "failed to initialize OBS video\n";
		obs_shutdown();
		return 2;
	}

	obs_source_info info = {};
	info.id = "drop_shadow_bench_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = [](void *) {
		return "Drop shadow benchmark source";
	};
	info.create = create_source;
	info.destroy = destroy_source;
	info.get_width = get_width;
	info.get_height = get_height;
	info.video_render = render_source;
	obs_register_source(&info);

	int exit_code = 0;
	obs_module_t *module = nullptr;
	if (obs_open_module(&module, argv[1], argv[2]) != MODULE_SUCCESS || !obs_init_module(module)) {
		std::cerr << "failed to load plugin module\n";
		obs_shutdown();
		return 2;
	}

	double baseline = 0.0;
	if (!profile_mode) {
		obs_source_t *plain = obs_source_create("drop_shadow_bench_source", "plain", nullptr, nullptr);
		baseline = median_of_repeats(plain);
		obs_source_release(plain);
		if (baseline < 0.0) {
			std::cerr << "failed to measure baseline\n";
			obs_shutdown();
			return 2;
		}
	}

	std::cout << std::fixed << std::setprecision(3);
	std::cout << (profile_mode ? "radius,sigma,sigma_per_radius,extent,peak\n"
				   : "radius,output_width,output_height,frame_ms,baseline_ms,filter_ms\n");
	for (int i = 6; i < argc; ++i) {
		const double radius = std::atof(argv[i]);
		const double ceil_radius = std::ceil(radius);
		// profile では影を右へずらし、影の縁のぼけが元画像の右端より右に収まるようにする
		const double offset_x = profile_mode ? ceil_radius + 2.0 : 4.0;
		const double offset_y = profile_mode ? 0.0 : 4.0;
		obs_source_t *source = obs_source_create("drop_shadow_bench_source", "bench", nullptr, nullptr);
		obs_data_t *settings = obs_data_create();
		obs_data_set_double(settings, "offset_x", offset_x);
		obs_data_set_double(settings, "offset_y", offset_y);
		obs_data_set_double(settings, "blur_radius", radius);
		obs_data_set_double(settings, "opacity", profile_mode ? 1.0 : 0.6);
		obs_data_set_int(settings, "color", 0xff000000);
		obs_source_t *filter = obs_source_create("drop_shadow_filter", "shadow", settings, nullptr);
		obs_data_release(settings);
		obs_source_filter_add(source, filter);
		if (profile_mode) {
			// 余白は左 0、上は半径分。元画像の右端はキャンバスの source_width / 2
			const uint32_t top = static_cast<uint32_t>(ceil_radius);
			const uint32_t row = top + source_height / 2;
			const uint32_t start_x = source_width / 2;
			const uint32_t edge_x = start_x + static_cast<uint32_t>(offset_x);
			double sigma = 0.0;
			int extent = 0;
			int peak = 0;
			if (profile_row(source, row, start_x, edge_x, sigma, extent, peak)) {
				std::cout << radius << ',' << sigma << ',' << (radius > 0.0 ? sigma / radius : 0.0)
					  << ',' << extent << ',' << peak << '\n';
				// 影が描かれ、σ が半径の約 1/3 で、裾が半径の内側で 0 になることを確かめる。
				// 測定の分解能が 0.5 画素なので、小さい半径では σ / 半径の幅を広く取る
				const double ratio = radius > 0.0 ? sigma / radius : 0.0;
				const bool shadow_drawn = peak >= 200;
				const bool sigma_ok = radius <= 0.0 ? sigma == 0.0 : (ratio > 0.25 && ratio < 0.42);
				const bool extent_ok = extent <= static_cast<int>(ceil_radius);
				if (!shadow_drawn || !sigma_ok || !extent_ok) {
					std::cerr << "unexpected shadow profile at radius " << radius << '\n';
					exit_code = 1;
				}
			} else {
				std::cerr << "failed to read shadow profile at radius " << radius << '\n';
				exit_code = 1;
			}
		} else {
			const double frame = median_of_repeats(source);
			std::cout << radius << ',' << obs_source_get_width(source) << ','
				  << obs_source_get_height(source) << ',' << frame << ',' << baseline << ','
				  << (frame - baseline) << '\n';
			if (frame < 0.0) {
				exit_code = 1;
			}
		}
		obs_source_release(filter);
		obs_source_release(source);
	}

	obs_shutdown();
	return exit_code;
}
