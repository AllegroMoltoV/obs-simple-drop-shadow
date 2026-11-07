#pragma once

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
