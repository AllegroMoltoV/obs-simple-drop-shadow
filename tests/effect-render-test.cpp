#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

int main(int argc, char **argv)
{
	if (argc != 4) {
		std::cerr << "expected effect path, graphics module path, and test case\n";
		return 2;
	}
	const bool horizontal = std::strcmp(argv[3], "horizontal") == 0;
	const bool composite = std::strcmp(argv[3], "vertical-composite") == 0;
	if (!horizontal && !composite && std::strcmp(argv[3], "smoke") != 0) {
		std::cerr << "unknown test case\n";
		return 2;
	}
	const char *technique = horizontal ? "Horizontal" : "VerticalComposite";

	graphics_t *graphics = nullptr;
	if (gs_create(&graphics, argv[2], 0) != GS_SUCCESS) {
		std::cerr << "failed to initialize OBS graphics\n";
		return 2;
	}
	gs_enter_context(graphics);

	constexpr uint32_t size = 5;
	std::array<uint8_t, size * size * 4> pixels = {};
	pixels[(2 * size + 2) * 4 + 3] = 255;
	if (composite) {
		pixels[0] = 255;
		pixels[3] = 255;
		pixels[(2 * size + 2) * 4] = 255;
		pixels[(2 * size + 2) * 4 + 3] = 128;
	}
	const uint8_t *image_data = pixels.data();
	gs_texture_t *input = gs_texture_create(size, size, GS_RGBA, 1, &image_data, 0);
	std::array<uint8_t, size * size * 4> blurred_pixels = {};
	blurred_pixels[(2 * size + 2) * 4 + 3] = 255;
	const uint8_t *blurred_data = blurred_pixels.data();
	gs_texture_t *blurred = composite ? gs_texture_create(size, size, GS_RGBA, 1, &blurred_data, 0) : nullptr;
	gs_texrender_t *output = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *staging = gs_stagesurface_create(size, size, GS_RGBA);
	gs_effect_t *effect = gs_effect_create_from_file(argv[1], nullptr);
	int result = 0;

	if (!input || (composite && !blurred) || !output || !staging || !effect) {
		std::cerr << "failed to create effect test resources\n";
		result = 2;
	} else if (!gs_effect_get_technique(effect, technique)) {
		std::cerr << "missing effect technique: " << technique << '\n';
		result = 1;
	} else {
		struct vec2 zero = {};
		struct vec2 texel = {1.0f / size, 1.0f / size};
		struct vec4 color = {0.0f, 0.0f, 0.0f, 1.0f};
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), input);
		if (!horizontal) {
			gs_effect_set_texture(gs_effect_get_param_by_name(effect, "blurred_image"),
					      composite ? blurred : input);
		}
		gs_effect_set_vec2(gs_effect_get_param_by_name(effect, "shadow_offset"), &zero);
		gs_effect_set_float(gs_effect_get_param_by_name(effect, "blur_radius"),
				    horizontal || composite ? 1.0f : 0.0f);
		gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "shadow_color"), &color);
		gs_effect_set_float(gs_effect_get_param_by_name(effect, "shadow_opacity"), composite ? 1.0f : 0.0f);
		gs_effect_set_vec2(gs_effect_get_param_by_name(effect, "texel_size"), &texel);

		gs_texrender_reset(output);
		if (!gs_texrender_begin(output, size, size)) {
			std::cerr << "failed to begin render target\n";
			result = 2;
		} else {
			struct vec4 clear = {};
			gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
			gs_ortho(0.0f, static_cast<float>(size), 0.0f, static_cast<float>(size), -100.0f, 100.0f);
			gs_blend_state_push();
			gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
			while (gs_effect_loop(effect, technique)) {
				gs_draw_sprite(input, 0, size, size);
			}
			gs_blend_state_pop();
			gs_texrender_end(output);

			gs_stage_texture(staging, gs_texrender_get_texture(output));
			uint8_t *mapped = nullptr;
			uint32_t stride = 0;
			if (!gs_stagesurface_map(staging, &mapped, &stride)) {
				std::cerr << "failed to read render target\n";
				result = 2;
			} else {
				if (horizontal) {
					for (uint32_t y = 0; y < size; ++y) {
						for (uint32_t x = 0; x < size; ++x) {
							const uint8_t actual = mapped[y * stride + x * 4 + 3];
							const uint8_t expected = y == 2 && x >= 1 && x <= 3 ? 85 : 0;
							if (actual != expected) {
								std::cerr << "horizontal alpha at (" << x << ", " << y
									  << "): " << static_cast<int>(actual)
									  << " (expected " << static_cast<int>(expected)
									  << ")\n";
								result = 1;
							}
						}
					}
				} else if (composite) {
					for (uint32_t y = 0; y < size; ++y) {
						for (uint32_t x = 0; x < size; ++x) {
							const uint8_t expected = x == 0 && y == 0             ? 255
										 : x == 2 && y == 2           ? 170
										 : x == 2 && y >= 1 && y <= 3 ? 85
													      : 0;
							const uint8_t actual = mapped[y * stride + x * 4 + 3];
							if (actual != expected) {
								std::cerr << "composite alpha at (" << x << ", " << y
									  << "): " << static_cast<int>(actual)
									  << " (expected " << static_cast<int>(expected)
									  << ")\n";
								result = 1;
							}
						}
					}
					const size_t corner = 0;
					const size_t center = 2 * stride + 2 * 4;
					if (mapped[corner] != 255 || mapped[center] != 128) {
						std::cerr
							<< "composite red: corner=" << static_cast<int>(mapped[corner])
							<< " center=" << static_cast<int>(mapped[center]) << '\n';
						result = 1;
					}
				} else {
					const uint8_t center_alpha = mapped[2 * stride + 2 * 4 + 3];
					const uint8_t corner_alpha = mapped[3];
					if (center_alpha != 255 || corner_alpha != 0) {
						std::cerr << "unexpected output alpha: center="
							  << static_cast<int>(center_alpha)
							  << " corner=" << static_cast<int>(corner_alpha) << '\n';
						result = 1;
					}
				}
				gs_stagesurface_unmap(staging);
			}
		}
	}

	gs_effect_destroy(effect);
	gs_stagesurface_destroy(staging);
	gs_texrender_destroy(output);
	gs_texture_destroy(blurred);
	gs_texture_destroy(input);
	gs_leave_context();
	gs_destroy(graphics);
	return result;
}
