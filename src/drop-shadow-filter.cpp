#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/effect.h>
#include <graphics/vec2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <new>

namespace {

constexpr double kMaxBlurRadius = 1024.0;
constexpr double kMaxOffset = 1024.0;

// 影のぼかしは σ = 半径 / 3 のガウシアン相当にする。半径の位置で裾がほぼ 0 になり、余白の外へ切れにくい
constexpr float kSigmaPerRadius = 1.0f / 3.0f;
constexpr int kMaxLevels = 12;
// 最も小さい段の一辺がこれを下回る段数は使わない
constexpr uint32_t kMinLevelSize = 2;

// Dual Kawase を n 段行ったときの分散 (元の解像度の画素の 2 乗) を kKawaseVarianceScale * (4^n - 1) と近似する。
// 段 k の画素は元の 2^k 倍なので、各段の分散は 4^k に比例して積み上がる。係数は縮小・拡大のサンプル位置から
// 求めた 0.944 を出発点に、段数が切り替わる半径の前後でぼけ幅が連続するよう実測で合わせた
constexpr float kKawaseVarianceScale = 0.87f;

float kawase_variance(int levels)
{
	return kKawaseVarianceScale * (std::ldexp(1.0f, 2 * levels) - 1.0f);
}

struct Margins {
	int left = 0;
	int right = 0;
	int top = 0;
	int bottom = 0;
};

// 影がソースの外へはみ出す分だけ、辺ごとに余白を取る
Margins compute_margins(float radius, float offset_x, float offset_y)
{
	const float r = std::ceil(radius);
	Margins m;
	m.left = static_cast<int>(std::ceil(std::max(0.0f, r - offset_x)));
	m.right = static_cast<int>(std::ceil(std::max(0.0f, r + offset_x)));
	m.top = static_cast<int>(std::ceil(std::max(0.0f, r - offset_y)));
	m.bottom = static_cast<int>(std::ceil(std::max(0.0f, r + offset_y)));
	return m;
}

} // namespace

class DropShadowFilter final {
public:
	DropShadowFilter(obs_data_t *settings, obs_source_t *source) : context_(source)
	{
		obs_enter_graphics();

		char *path = obs_module_file("effects/drop-shadow.effect");
		if (path) {
			effect_ = gs_effect_create_from_file(path, nullptr);
			bfree(path);
		} else {
			obs_log(LOG_WARNING, "[drop_shadow] obs_module_file() failed: effects/drop-shadow.effect");
		}

		if (effect_) {
			param_image_ = gs_effect_get_param_by_name(effect_, "image");
			param_mask_image_ = gs_effect_get_param_by_name(effect_, "mask_image");
			param_canvas_size_ = gs_effect_get_param_by_name(effect_, "canvas_size");
			param_source_size_ = gs_effect_get_param_by_name(effect_, "source_size");
			param_canvas_origin_ = gs_effect_get_param_by_name(effect_, "canvas_origin");
			param_shadow_origin_ = gs_effect_get_param_by_name(effect_, "shadow_origin");
			param_texel_size_ = gs_effect_get_param_by_name(effect_, "texel_size");
			param_gauss_sigma_ = gs_effect_get_param_by_name(effect_, "gauss_sigma");
			param_gauss_taps_ = gs_effect_get_param_by_name(effect_, "gauss_taps");
			param_shadow_color_ = gs_effect_get_param_by_name(effect_, "shadow_color");
			param_shadow_opacity_ = gs_effect_get_param_by_name(effect_, "shadow_opacity");
			source_render_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
			gauss_render_ = gs_texrender_create(GS_R8, GS_ZS_NONE);
			for (auto &level : levels_) {
				level = gs_texrender_create(GS_R8, GS_ZS_NONE);
			}
		} else {
			obs_log(LOG_WARNING, "[drop_shadow] Failed to load effect: effects/drop-shadow.effect");
		}

		obs_leave_graphics();

		Update(settings);
	}

	~DropShadowFilter()
	{
		obs_enter_graphics();
		for (auto &level : levels_) {
			gs_texrender_destroy(level);
		}
		gs_texrender_destroy(gauss_render_);
		gs_texrender_destroy(source_render_);
		if (effect_) {
			gs_effect_destroy(effect_);
			effect_ = nullptr;
		}
		obs_leave_graphics();
	}

	void Update(obs_data_t *settings)
	{
		offset_x_ = static_cast<float>(
			std::clamp(obs_data_get_double(settings, "offset_x"), -kMaxOffset, kMaxOffset));
		offset_y_ = static_cast<float>(
			std::clamp(obs_data_get_double(settings, "offset_y"), -kMaxOffset, kMaxOffset));
		blur_radius_ = static_cast<float>(
			std::clamp(obs_data_get_double(settings, "blur_radius"), 0.0, kMaxBlurRadius));
		opacity_ = static_cast<float>(obs_data_get_double(settings, "opacity"));

		const std::uint32_t rgba = static_cast<std::uint32_t>(obs_data_get_int(settings, "color"));
		vec4_from_rgba(&shadow_color_, rgba);
	}

	uint32_t Width()
	{
		const uint32_t base = TargetWidth();
		if (base == 0) {
			return 0;
		}
		const Margins m = compute_margins(blur_radius_, offset_x_, offset_y_);
		return base + static_cast<uint32_t>(m.left + m.right);
	}

	uint32_t Height()
	{
		const uint32_t base = TargetHeight();
		if (base == 0) {
			return 0;
		}
		const Margins m = compute_margins(blur_radius_, offset_x_, offset_y_);
		return base + static_cast<uint32_t>(m.top + m.bottom);
	}

	void Render()
	{
		if (!Ready()) {
			obs_source_skip_video_filter(context_);
			return;
		}

		obs_source_t *target = obs_filter_get_target(context_);
		obs_source_t *parent = obs_filter_get_parent(context_);
		if (!target || !parent) {
			return;
		}
		const uint32_t width = obs_source_get_base_width(target);
		const uint32_t height = obs_source_get_base_height(target);
		if (width == 0 || height == 0) {
			obs_source_skip_video_filter(context_);
			return;
		}

		const Margins m = compute_margins(blur_radius_, offset_x_, offset_y_);
		const uint32_t canvas_w = width + static_cast<uint32_t>(m.left + m.right);
		const uint32_t canvas_h = height + static_cast<uint32_t>(m.top + m.bottom);

		gs_texture_t *source_texture = RenderSource(target, parent, width, height);
		if (!source_texture) {
			obs_source_skip_video_filter(context_);
			return;
		}

		struct vec2 canvas_size = {static_cast<float>(canvas_w), static_cast<float>(canvas_h)};
		struct vec2 source_size = {static_cast<float>(width), static_cast<float>(height)};
		struct vec2 canvas_origin = {static_cast<float>(m.left), static_cast<float>(m.top)};
		struct vec2 shadow_origin = {static_cast<float>(m.left) + offset_x_,
					     static_cast<float>(m.top) + offset_y_};

		gs_effect_set_texture(param_image_, source_texture);
		gs_effect_set_vec2(param_canvas_size_, &canvas_size);
		gs_effect_set_vec2(param_source_size_, &source_size);
		gs_effect_set_vec2(param_shadow_origin_, &shadow_origin);
		if (!DrawPass(levels_[0], "Mask", nullptr, canvas_w, canvas_h)) {
			obs_source_skip_video_filter(context_);
			return;
		}

		gs_texture_t *shadow = Blur(canvas_w, canvas_h);
		if (!shadow) {
			obs_source_skip_video_filter(context_);
			return;
		}

		gs_effect_set_texture(param_image_, source_texture);
		gs_effect_set_texture(param_mask_image_, shadow);
		gs_effect_set_vec2(param_canvas_size_, &canvas_size);
		gs_effect_set_vec2(param_source_size_, &source_size);
		gs_effect_set_vec2(param_canvas_origin_, &canvas_origin);
		gs_effect_set_vec4(param_shadow_color_, &shadow_color_);
		gs_effect_set_float(param_shadow_opacity_, opacity_);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		while (gs_effect_loop(effect_, "Composite")) {
			gs_draw_sprite(shadow, 0, canvas_w, canvas_h);
		}
		gs_blend_state_pop();
	}

	static const char *GetName(void *unused)
	{
		UNUSED_PARAMETER(unused);
		return obs_module_text("DropShadowFilterName");
	}

	static void *Create(obs_data_t *settings, obs_source_t *source)
	{
		return new (std::nothrow) DropShadowFilter(settings, source);
	}

	static void Destroy(void *data) { delete static_cast<DropShadowFilter *>(data); }

	static void UpdateCallback(void *data, obs_data_t *settings)
	{
		static_cast<DropShadowFilter *>(data)->Update(settings);
	}

	static void GetDefaults(obs_data_t *settings)
	{
		obs_data_set_default_double(settings, "offset_x", 4.0);
		obs_data_set_default_double(settings, "offset_y", 4.0);
		obs_data_set_default_double(settings, "blur_radius", 4.0);
		obs_data_set_default_int(settings, "color", 0x80000000);
		obs_data_set_default_double(settings, "opacity", 0.6);
	}

	static obs_properties_t *Properties(void *unused)
	{
		UNUSED_PARAMETER(unused);

		obs_properties_t *props = obs_properties_create();

		obs_properties_add_float_slider(props, "offset_x", obs_module_text("OffsetX"), -kMaxOffset, kMaxOffset,
						1.0);
		obs_properties_add_float_slider(props, "offset_y", obs_module_text("OffsetY"), -kMaxOffset, kMaxOffset,
						1.0);
		obs_properties_add_float_slider(props, "blur_radius", obs_module_text("BlurRadius"), 0.0,
						kMaxBlurRadius, 0.5);
		obs_properties_add_color(props, "color", obs_module_text("Color"));
		obs_properties_add_float_slider(props, "opacity", obs_module_text("Opacity"), 0.0, 1.0, 0.01);

		return props;
	}

	static void VideoRender(void *data, gs_effect_t *unused)
	{
		UNUSED_PARAMETER(unused);
		static_cast<DropShadowFilter *>(data)->Render();
	}

	static uint32_t GetWidth(void *data) { return static_cast<DropShadowFilter *>(data)->Width(); }

	static uint32_t GetHeight(void *data) { return static_cast<DropShadowFilter *>(data)->Height(); }

private:
	uint32_t TargetWidth() const
	{
		obs_source_t *target = obs_filter_get_target(context_);
		return target ? obs_source_get_base_width(target) : 0;
	}

	uint32_t TargetHeight() const
	{
		obs_source_t *target = obs_filter_get_target(context_);
		return target ? obs_source_get_base_height(target) : 0;
	}

	bool Ready() const
	{
		if (!effect_ || !source_render_ || !gauss_render_ || !param_image_ || !param_mask_image_ ||
		    !param_canvas_size_ || !param_source_size_ || !param_canvas_origin_ || !param_shadow_origin_ ||
		    !param_texel_size_ || !param_gauss_sigma_ || !param_gauss_taps_ || !param_shadow_color_ ||
		    !param_shadow_opacity_) {
			return false;
		}
		for (const char *technique : {"Mask", "Down", "Up", "Gauss", "Composite"}) {
			if (!gs_effect_get_technique(effect_, technique)) {
				return false;
			}
		}
		return std::all_of(levels_.begin(), levels_.end(), [](gs_texrender_t *level) { return level; });
	}

	gs_texture_t *RenderSource(obs_source_t *target, obs_source_t *parent, uint32_t width, uint32_t height)
	{
		gs_texrender_reset(source_render_);
		if (!gs_texrender_begin_with_color_space(source_render_, width, height, GS_CS_SRGB)) {
			return nullptr;
		}
		gs_blend_state_push();
		gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		struct vec4 clear = {};
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
		const uint32_t parent_flags = obs_source_get_output_flags(parent);
		if (target == parent && !(parent_flags & OBS_SOURCE_CUSTOM_DRAW) &&
		    !(parent_flags & OBS_SOURCE_ASYNC)) {
			obs_source_default_render(target);
		} else {
			obs_source_video_render(target);
		}
		gs_blend_state_pop();
		gs_texrender_end(source_render_);
		return gs_texrender_get_texture(source_render_);
	}

	// input を technique で描画し、output (cx x cy) に書く。input が nullptr なら事前に設定した image を使う
	bool DrawPass(gs_texrender_t *output, const char *technique, gs_texture_t *input, uint32_t cx, uint32_t cy)
	{
		gs_texrender_reset(output);
		if (!gs_texrender_begin(output, cx, cy)) {
			return false;
		}
		struct vec4 clear = {};
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(cx), 0.0f, static_cast<float>(cy), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		if (input) {
			gs_effect_set_texture(param_mask_image_, input);
		}
		while (gs_effect_loop(effect_, technique)) {
			gs_draw_sprite(input, 0, cx, cy);
		}
		gs_blend_state_pop();
		gs_texrender_end(output);
		return true;
	}

	// levels_[0] のマスクをぼかし、結果のテクスチャを返す。
	// 目標の分散を超えない段数まで Dual Kawase で縮小し、残りの分散を最も小さい段の分離型ガウシアンで足してから
	// 同じ段数だけ拡大する。分散は足し算になるため、半径に対してぼけ幅が連続かつ単調に変わる。
	// 段数 0 のときは縮小せず、元の解像度のガウシアンだけで処理する
	gs_texture_t *Blur(uint32_t canvas_w, uint32_t canvas_h)
	{
		gs_texture_t *mask = gs_texrender_get_texture(levels_[0]);
		const float sigma = blur_radius_ * kSigmaPerRadius;
		if (!mask || sigma <= 0.0f) {
			return mask;
		}

		const float target_variance = sigma * sigma;
		int levels = 0;
		while (levels < kMaxLevels && kawase_variance(levels + 1) <= target_variance &&
		       (canvas_w >> (levels + 1)) >= kMinLevelSize && (canvas_h >> (levels + 1)) >= kMinLevelSize) {
			++levels;
		}

		// gs_technique_end がパスごとに全パラメータの値を消すため、パスの直前に毎回設定する
		for (int n = 1; n <= levels; ++n) {
			const uint32_t out_w = canvas_w >> n;
			const uint32_t out_h = canvas_h >> n;
			struct vec2 texel = {1.0f / static_cast<float>(out_w), 1.0f / static_cast<float>(out_h)};
			gs_effect_set_vec2(param_texel_size_, &texel);
			if (!DrawPass(levels_[n], "Down", gs_texrender_get_texture(levels_[n - 1]), out_w, out_h)) {
				return nullptr;
			}
		}

		const uint32_t level_w = canvas_w >> levels;
		const uint32_t level_h = canvas_h >> levels;
		const float residual = std::sqrt(std::max(0.0f, target_variance - kawase_variance(levels))) /
				       static_cast<float>(1 << levels);
		if (residual > 0.01f) {
			const int taps = static_cast<int>(std::ceil(residual * 3.0f));
			struct vec2 horizontal = {1.0f / static_cast<float>(level_w), 0.0f};
			gs_effect_set_float(param_gauss_sigma_, residual);
			gs_effect_set_int(param_gauss_taps_, taps);
			gs_effect_set_vec2(param_texel_size_, &horizontal);
			if (!DrawPass(gauss_render_, "Gauss", gs_texrender_get_texture(levels_[levels]), level_w,
				      level_h)) {
				return nullptr;
			}
			struct vec2 vertical = {0.0f, 1.0f / static_cast<float>(level_h)};
			gs_effect_set_float(param_gauss_sigma_, residual);
			gs_effect_set_int(param_gauss_taps_, taps);
			gs_effect_set_vec2(param_texel_size_, &vertical);
			if (!DrawPass(levels_[levels], "Gauss", gs_texrender_get_texture(gauss_render_), level_w,
				      level_h)) {
				return nullptr;
			}
		}

		for (int n = levels - 1; n >= 0; --n) {
			gs_texture_t *input = gs_texrender_get_texture(levels_[n + 1]);
			struct vec2 texel = {1.0f / static_cast<float>(gs_texture_get_width(input)),
					     1.0f / static_cast<float>(gs_texture_get_height(input))};
			gs_effect_set_vec2(param_texel_size_, &texel);
			if (!DrawPass(levels_[n], "Up", input, canvas_w >> n, canvas_h >> n)) {
				return nullptr;
			}
		}
		return gs_texrender_get_texture(levels_[0]);
	}

	obs_source_t *context_ = nullptr;

	gs_effect_t *effect_ = nullptr;
	gs_texrender_t *source_render_ = nullptr;
	gs_texrender_t *gauss_render_ = nullptr;
	std::array<gs_texrender_t *, kMaxLevels + 1> levels_ = {};
	gs_eparam_t *param_image_ = nullptr;
	gs_eparam_t *param_mask_image_ = nullptr;
	gs_eparam_t *param_canvas_size_ = nullptr;
	gs_eparam_t *param_source_size_ = nullptr;
	gs_eparam_t *param_canvas_origin_ = nullptr;
	gs_eparam_t *param_shadow_origin_ = nullptr;
	gs_eparam_t *param_texel_size_ = nullptr;
	gs_eparam_t *param_gauss_sigma_ = nullptr;
	gs_eparam_t *param_gauss_taps_ = nullptr;
	gs_eparam_t *param_shadow_color_ = nullptr;
	gs_eparam_t *param_shadow_opacity_ = nullptr;

	float offset_x_ = 4.0f;
	float offset_y_ = 4.0f;
	float blur_radius_ = 4.0f;
	float opacity_ = 0.6f;
	struct vec4 shadow_color_ = {};
};

void register_drop_shadow_filter(void)
{
	static obs_source_info info = {};

	info.id = "drop_shadow_filter";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;

	info.get_name = DropShadowFilter::GetName;
	info.create = DropShadowFilter::Create;
	info.destroy = DropShadowFilter::Destroy;
	info.update = DropShadowFilter::UpdateCallback;
	info.get_defaults = DropShadowFilter::GetDefaults;
	info.get_properties = DropShadowFilter::Properties;
	info.video_render = DropShadowFilter::VideoRender;
	info.get_width = DropShadowFilter::GetWidth;
	info.get_height = DropShadowFilter::GetHeight;

	obs_register_source(&info);
}
