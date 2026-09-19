#include <obs.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

namespace {

constexpr uint32_t image_size = 5;

enum class Scenario {
	Blur,
	RadiusZero,
	EdgeOffset,
	VerticalEdgeOffset,
	NegativeEdgeOffset,
	RadiusMax,
	SettingsUpdate,
	ColorOpacity
};
Scenario scenario = Scenario::Blur;

struct TestSource {
	gs_texture_t *texture = nullptr;
};

void mark_updated(void *data, calldata_t *)
{
	static_cast<std::atomic_bool *>(data)->store(true);
}

void *create_source(obs_data_t *, obs_source_t *)
{
	auto *source = new TestSource;
	std::array<uint8_t, image_size * image_size * 4> pixels = {};
	if (scenario == Scenario::RadiusMax) {
		for (uint32_t y = 0; y < image_size; ++y) {
			pixels[y * image_size * 4 + 3] = 255;
		}
	} else {
		const uint32_t source_x = scenario == Scenario::EdgeOffset           ? 0
					  : scenario == Scenario::NegativeEdgeOffset ? 4
										     : 2;
		const uint32_t source_y = scenario == Scenario::VerticalEdgeOffset ? 0 : 2;
		pixels[(source_y * image_size + source_x) * 4 + 3] = 255;
		if (scenario == Scenario::ColorOpacity) {
			pixels[(source_y * image_size + source_x) * 4] = 255;
		}
	}
	const uint8_t *data = pixels.data();
	obs_enter_graphics();
	source->texture = gs_texture_create(image_size, image_size, GS_RGBA, 1, &data, 0);
	obs_leave_graphics();
	if (!source->texture) {
		delete source;
		return nullptr;
	}
	return source;
}

void destroy_source(void *data)
{
	auto *source = static_cast<TestSource *>(data);
	obs_enter_graphics();
	gs_texture_destroy(source->texture);
	obs_leave_graphics();
	delete source;
}

uint32_t source_size(void *)
{
	return image_size;
}

void render_source(void *data, gs_effect_t *effect)
{
	auto *source = static_cast<TestSource *>(data);
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), source->texture);
	gs_draw_sprite(source->texture, 0, image_size, image_size);
}

bool prepare_effect(const std::filesystem::path &effect_path, const std::filesystem::path &data_path)
{
	std::ifstream input(effect_path, std::ios::binary);
	std::string effect((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	if (!input || effect.empty()) {
		std::cerr << "failed to read effect\n";
		return false;
	}
	std::filesystem::create_directories(data_path / "effects");
	std::ofstream output(data_path / "effects" / "drop-shadow.effect", std::ios::binary | std::ios::trunc);
	output << effect;
	return static_cast<bool>(output);
}

int render_and_check(obs_source_t *source, Scenario expected_scenario)
{
	int result = 0;
	obs_enter_graphics();
	gs_texrender_t *output = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *staging = gs_stagesurface_create(image_size, image_size, GS_RGBA);
	if (!output || !staging || !gs_texrender_begin(output, image_size, image_size)) {
		std::cerr << "failed to create output render target\n";
		result = 2;
	} else {
		struct vec4 clear = {};
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(image_size), 0.0f, static_cast<float>(image_size), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_video_render(source);
		gs_blend_state_pop();
		gs_texrender_end(output);
		gs_stage_texture(staging, gs_texrender_get_texture(output));
		uint8_t *mapped = nullptr;
		uint32_t stride = 0;
		if (!gs_stagesurface_map(staging, &mapped, &stride)) {
			std::cerr << "failed to read filter output\n";
			result = 2;
		} else {
			for (uint32_t y = 0; y < image_size; ++y) {
				for (uint32_t x = 0; x < image_size; ++x) {
					uint8_t expected = 0;
					if (expected_scenario == Scenario::EdgeOffset) {
						if (x == 0 && y == 2) {
							expected = 255;
						} else if (y >= 1 && y <= 3) {
							expected = x <= 1 ? 85 : x == 2 ? 57 : x == 3 ? 28 : 0;
						}
					} else if (expected_scenario == Scenario::VerticalEdgeOffset) {
						if (x == 2 && y == 0) {
							expected = 255;
						} else if (x >= 1 && x <= 3) {
							expected = y <= 1 ? 85 : y == 2 ? 57 : y == 3 ? 28 : 0;
						}
					} else if (expected_scenario == Scenario::NegativeEdgeOffset) {
						if (x == 4 && y == 2) {
							expected = 255;
						} else if (y >= 1 && y <= 3) {
							expected = x >= 3 ? 85 : x == 2 ? 57 : x == 1 ? 28 : 0;
						}
					} else if (expected_scenario == Scenario::RadiusMax) {
						constexpr std::array<uint8_t, image_size> max_radius_alpha = {
							255, 124, 118, 112, 106};
						expected = max_radius_alpha[x];
					} else if (expected_scenario == Scenario::ColorOpacity) {
						expected = x == 2 && y == 2                       ? 255
							   : x >= 1 && x <= 3 && y >= 1 && y <= 3 ? 14
												  : 0;
					} else {
						expected = x == 2 && y == 2 ? 255
							   : expected_scenario == Scenario::Blur && x >= 1 && x <= 3 &&
									   y >= 1 && y <= 3
								   ? 28
								   : 0;
					}
					const uint8_t actual = mapped[y * stride + x * 4 + 3];
					if (actual != expected) {
						std::cerr << "filter alpha at (" << x << ", " << y
							  << "): " << static_cast<int>(actual) << " (expected "
							  << static_cast<int>(expected) << ")\n";
						result = 1;
					}
					if (expected_scenario == Scenario::ColorOpacity &&
					    ((x == 1 && y == 1 &&
					      (mapped[y * stride + x * 4] != 14 ||
					       mapped[y * stride + x * 4 + 1] != 14 ||
					       mapped[y * stride + x * 4 + 2] != 14)) ||
					     (x == 2 && y == 2 && mapped[y * stride + x * 4] != 255))) {
						std::cerr << "unexpected original or shadow color at (" << x << ", "
							  << y << ")\n";
						result = 1;
					}
				}
			}
			gs_stagesurface_unmap(staging);
		}
	}
	gs_stagesurface_destroy(staging);
	gs_texrender_destroy(output);
	obs_leave_graphics();
	return result;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc != 7) {
		std::cerr << "expected plugin, effect, module data, graphics module, core data, and scenario\n";
		return 2;
	}
	if (std::strcmp(argv[6], "blur") == 0) {
		scenario = Scenario::Blur;
	} else if (std::strcmp(argv[6], "radius-zero") == 0) {
		scenario = Scenario::RadiusZero;
	} else if (std::strcmp(argv[6], "edge-offset") == 0) {
		scenario = Scenario::EdgeOffset;
	} else if (std::strcmp(argv[6], "vertical-edge-offset") == 0) {
		scenario = Scenario::VerticalEdgeOffset;
	} else if (std::strcmp(argv[6], "negative-edge-offset") == 0) {
		scenario = Scenario::NegativeEdgeOffset;
	} else if (std::strcmp(argv[6], "radius-max") == 0) {
		scenario = Scenario::RadiusMax;
	} else if (std::strcmp(argv[6], "settings-update") == 0) {
		scenario = Scenario::SettingsUpdate;
	} else if (std::strcmp(argv[6], "color-opacity") == 0) {
		scenario = Scenario::ColorOpacity;
	} else {
		std::cerr << "unknown scenario\n";
		return 2;
	}
	if (!prepare_effect(argv[2], argv[3])) {
		return 2;
	}
	if (!obs_startup("en-US", nullptr, nullptr)) {
		std::cerr << "failed to start OBS\n";
		return 2;
	}
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
	std::string core_data_path = argv[5];
	core_data_path += '/';
	obs_add_data_path(core_data_path.c_str());
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	obs_video_info video = {};
	video.graphics_module = argv[4];
	video.fps_num = 30;
	video.fps_den = 1;
	video.base_width = 16;
	video.base_height = 16;
	video.output_width = 16;
	video.output_height = 16;
	video.output_format = VIDEO_FORMAT_RGBA;
	if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS) {
		std::cerr << "failed to initialize OBS video\n";
		obs_shutdown();
		return 2;
	}

	obs_source_info info = {};
	info.id = "drop_shadow_test_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = [](void *) {
		return "Drop shadow test source";
	};
	info.create = create_source;
	info.destroy = destroy_source;
	info.get_width = source_size;
	info.get_height = source_size;
	info.video_render = render_source;
	obs_register_source(&info);

	obs_module_t *module = nullptr;
	int result = 2;
	if (obs_open_module(&module, argv[1], argv[3]) != MODULE_SUCCESS || !obs_init_module(module)) {
		std::cerr << "failed to load plugin module\n";
	} else {
		obs_source_t *source = obs_source_create("drop_shadow_test_source", "impulse", nullptr, nullptr);
		obs_data_t *settings = obs_data_create();
		obs_data_set_double(settings, "offset_x",
				    scenario == Scenario::EdgeOffset           ? 2.0
				    : scenario == Scenario::NegativeEdgeOffset ? -2.0
									       : 0.0);
		obs_data_set_double(settings, "offset_y", scenario == Scenario::VerticalEdgeOffset ? 2.0 : 0.0);
		obs_data_set_double(settings, "blur_radius",
				    scenario == Scenario::RadiusZero || scenario == Scenario::SettingsUpdate ? 0.0
				    : scenario == Scenario::RadiusMax                                        ? 20.0
													     : 1.0);
		obs_data_set_double(settings, "opacity", scenario == Scenario::ColorOpacity ? 0.5 : 1.0);
		obs_data_set_int(settings, "color", scenario == Scenario::ColorOpacity ? 0xffffffff : 0xff000000);
		obs_source_t *filter = obs_source_create("drop_shadow_filter", "shadow", settings, nullptr);
		obs_data_release(settings);
		if (!source || !filter) {
			std::cerr << "failed to create test source or filter\n";
		} else {
			obs_source_filter_add(source, filter);
			if (scenario == Scenario::SettingsUpdate) {
				result = render_and_check(source, Scenario::RadiusZero);
				if (result == 0) {
					obs_data_t *updated = obs_data_create();
					obs_data_set_double(updated, "offset_x", 0.0);
					obs_data_set_double(updated, "offset_y", 0.0);
					obs_data_set_double(updated, "blur_radius", 1.0);
					obs_data_set_double(updated, "opacity", 1.0);
					obs_data_set_int(updated, "color", 0xff000000);
					std::atomic_bool update_applied = false;
					signal_handler_t *signals = obs_source_get_signal_handler(filter);
					signal_handler_connect(signals, "update", mark_updated, &update_applied);
					obs_source_update(filter, updated);
					obs_data_release(updated);
					const auto deadline =
						std::chrono::steady_clock::now() + std::chrono::seconds(2);
					while (!update_applied.load() && std::chrono::steady_clock::now() < deadline) {
						std::this_thread::sleep_for(std::chrono::milliseconds(10));
					}
					signal_handler_disconnect(signals, "update", mark_updated, &update_applied);
					if (!update_applied.load()) {
						std::cerr << "OBS did not apply filter settings within two seconds\n";
						result = 2;
					} else {
						result = render_and_check(source, Scenario::Blur);
					}
				}
			} else {
				result = render_and_check(source, scenario);
			}
		}
		obs_source_release(filter);
		obs_source_release(source);
	}
	obs_shutdown();
	return result;
}
