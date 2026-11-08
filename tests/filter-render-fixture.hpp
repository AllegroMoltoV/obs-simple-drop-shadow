#pragma once

// 統合テストの共通部品。テスト用ソースの登録と、フィルタ付きソースの出力全体の読み出しを行う。

#include "obs-test-env.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace fixture {

// 出力の画素。色はアルファを掛けた値で、フィルタの出力と同じ表現
struct Image {
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint8_t> rgba;

	const uint8_t *at(int x, int y) const { return &rgba[(static_cast<size_t>(y) * width + x) * 4]; }
	int alpha(int x, int y) const
	{
		if (x < 0 || y < 0 || x >= static_cast<int>(width) || y >= static_cast<int>(height)) {
			return 0;
		}
		return at(x, y)[3];
	}
};

// テスト用ソースの大きさと描き方。create のたびに参照する
struct SourceSpec {
	uint32_t width = 32;
	uint32_t height = 32;
	std::function<void(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height)> paint;
};

inline SourceSpec current_spec;

struct TestSource {
	gs_texture_t *texture = nullptr;
	uint32_t width = 0;
	uint32_t height = 0;
};

inline void *create_source(obs_data_t *, obs_source_t *)
{
	auto *source = new TestSource;
	source->width = current_spec.width;
	source->height = current_spec.height;
	std::vector<uint8_t> pixels(static_cast<size_t>(source->width) * source->height * 4, 0);
	if (current_spec.paint) {
		current_spec.paint(pixels, source->width, source->height);
	}
	const uint8_t *data = pixels.data();
	obs_enter_graphics();
	source->texture = gs_texture_create(source->width, source->height, GS_RGBA, 1, &data, 0);
	obs_leave_graphics();
	if (!source->texture) {
		delete source;
		return nullptr;
	}
	return source;
}

inline void destroy_source(void *data)
{
	auto *source = static_cast<TestSource *>(data);
	obs_enter_graphics();
	gs_texture_destroy(source->texture);
	obs_leave_graphics();
	delete source;
}

inline uint32_t source_width(void *data)
{
	return static_cast<TestSource *>(data)->width;
}

inline uint32_t source_height(void *data)
{
	return static_cast<TestSource *>(data)->height;
}

inline void render_source(void *data, gs_effect_t *effect)
{
	auto *source = static_cast<TestSource *>(data);
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), source->texture);
	gs_draw_sprite(source->texture, 0, source->width, source->height);
}

inline void register_source()
{
	obs_source_info info = {};
	info.id = "drop_shadow_test_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = [](void *) {
		return "Drop shadow test source";
	};
	info.create = create_source;
	info.destroy = destroy_source;
	info.get_width = source_width;
	info.get_height = source_height;
	info.video_render = render_source;
	obs_register_source(&info);
}

// 指定した位置の 1 画素を不透明にする描き方
inline std::function<void(std::vector<uint8_t> &, uint32_t, uint32_t)> single_pixel(uint32_t px, uint32_t py, uint8_t r,
										    uint8_t g, uint8_t b)
{
	return [=](std::vector<uint8_t> &pixels, uint32_t width, uint32_t) {
		uint8_t *p = &pixels[(static_cast<size_t>(py) * width + px) * 4];
		p[0] = r;
		p[1] = g;
		p[2] = b;
		p[3] = 255;
	};
}

// 全面を不透明にする描き方
inline void fill_opaque(std::vector<uint8_t> &pixels, uint32_t, uint32_t)
{
	for (size_t i = 0; i < pixels.size(); i += 4) {
		pixels[i] = 40;
		pixels[i + 1] = 80;
		pixels[i + 2] = 120;
		pixels[i + 3] = 255;
	}
}

// フィルタを通したソースの出力全体を、出力サイズ (フィルタの get_width/get_height) のまま読み出す
inline bool render(obs_source_t *source, Image &image)
{
	bool ok = false;
	obs_enter_graphics();
	image.width = obs_source_get_width(source);
	image.height = obs_source_get_height(source);
	gs_texrender_t *output = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *staging = gs_stagesurface_create(image.width, image.height, GS_RGBA);
	if (output && staging && gs_texrender_begin(output, image.width, image.height)) {
		struct vec4 clear = {};
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(image.width), 0.0f, static_cast<float>(image.height), -100.0f,
			 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_video_render(source);
		gs_blend_state_pop();
		gs_texrender_end(output);
		gs_stage_texture(staging, gs_texrender_get_texture(output));
		uint8_t *mapped = nullptr;
		uint32_t stride = 0;
		if (gs_stagesurface_map(staging, &mapped, &stride)) {
			image.rgba.resize(static_cast<size_t>(image.width) * image.height * 4);
			for (uint32_t y = 0; y < image.height; ++y) {
				std::copy(mapped + static_cast<size_t>(y) * stride,
					  mapped + static_cast<size_t>(y) * stride + image.width * 4,
					  image.rgba.begin() + static_cast<size_t>(y) * image.width * 4);
			}
			gs_stagesurface_unmap(staging);
			ok = true;
		}
	}
	if (!ok) {
		std::cerr << "failed to render filter output\n";
	}
	gs_stagesurface_destroy(staging);
	gs_texrender_destroy(output);
	obs_leave_graphics();
	return ok;
}

// 失敗した検査を数えて報告する
struct Checker {
	int failures = 0;

	void expect(bool condition, const std::string &message)
	{
		if (!condition) {
			std::cerr << message << '\n';
			++failures;
		}
	}

	void expect_near(int actual, int expected, int tolerance, const std::string &what)
	{
		if (std::abs(actual - expected) > tolerance) {
			std::cerr << what << ": " << actual << " (expected " << expected << " +/- " << tolerance
				  << ")\n";
			++failures;
		}
	}
};

} // namespace fixture
