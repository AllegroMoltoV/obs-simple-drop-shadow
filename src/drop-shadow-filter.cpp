#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/effect.h>
#include <graphics/vec2.h>

#include <cstdint>
#include <new>

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
			param_shadow_offset_ = gs_effect_get_param_by_name(effect_, "shadow_offset");
			param_blur_radius_ = gs_effect_get_param_by_name(effect_, "blur_radius");
			param_shadow_color_ = gs_effect_get_param_by_name(effect_, "shadow_color");
			param_shadow_opacity_ = gs_effect_get_param_by_name(effect_, "shadow_opacity");
			param_texel_size_ = gs_effect_get_param_by_name(effect_, "texel_size");
		} else {
			obs_log(LOG_WARNING, "[drop_shadow] Failed to load effect: effects/drop-shadow.effect");
		}

		obs_leave_graphics();

		Update(settings);
	}

	~DropShadowFilter()
	{
		obs_enter_graphics();
		if (effect_) {
			gs_effect_destroy(effect_);
			effect_ = nullptr;
		}
		obs_leave_graphics();
	}

	void Update(obs_data_t *settings)
	{
		offset_x_ = static_cast<float>(obs_data_get_double(settings, "offset_x"));
		offset_y_ = static_cast<float>(obs_data_get_double(settings, "offset_y"));
		blur_radius_ = static_cast<float>(obs_data_get_double(settings, "blur_radius"));
		opacity_ = static_cast<float>(obs_data_get_double(settings, "opacity"));

		const std::uint32_t rgba = static_cast<std::uint32_t>(obs_data_get_int(settings, "color"));
		vec4_from_rgba(&shadow_color_, rgba);
	}

	void Render()
	{
		if (!effect_ || !param_shadow_offset_ || !param_blur_radius_ || !param_shadow_color_ ||
		    !param_shadow_opacity_ || !param_texel_size_) {
			obs_source_skip_video_filter(context_);
			return;
		}

		if (!obs_source_process_filter_begin(context_, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING)) {
			return;
		}

		obs_source_t *target = obs_filter_get_target(context_);
		uint32_t width = obs_source_get_base_width(target);
		uint32_t height = obs_source_get_base_height(target);

		if (width == 0 || height == 0) {
			obs_source_skip_video_filter(context_);
			return;
		}

		struct vec2 shadow_offset;
		shadow_offset.x = offset_x_;
		shadow_offset.y = offset_y_;

		struct vec2 texel_size;
		texel_size.x = 1.0f / static_cast<float>(width);
		texel_size.y = 1.0f / static_cast<float>(height);

		gs_effect_set_vec2(param_shadow_offset_, &shadow_offset);
		gs_effect_set_float(param_blur_radius_, blur_radius_);
		gs_effect_set_vec4(param_shadow_color_, &shadow_color_);
		gs_effect_set_float(param_shadow_opacity_, opacity_);
		gs_effect_set_vec2(param_texel_size_, &texel_size);

		obs_source_process_filter_end(context_, effect_, 0, 0);
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

		obs_properties_add_float(props, "offset_x", obs_module_text("OffsetX"), -100.0, 100.0, 1.0);
		obs_properties_add_float(props, "offset_y", obs_module_text("OffsetY"), -100.0, 100.0, 1.0);
		obs_properties_add_float(props, "blur_radius", obs_module_text("BlurRadius"), 0.0, 20.0, 0.5);
		obs_properties_add_color(props, "color", obs_module_text("Color"));
		obs_properties_add_float_slider(props, "opacity", obs_module_text("Opacity"), 0.0, 1.0, 0.01);

		return props;
	}

	static void VideoRender(void *data, gs_effect_t *unused)
	{
		UNUSED_PARAMETER(unused);
		static_cast<DropShadowFilter *>(data)->Render();
	}

private:
	obs_source_t *context_ = nullptr;

	gs_effect_t *effect_ = nullptr;
	gs_eparam_t *param_shadow_offset_ = nullptr;
	gs_eparam_t *param_blur_radius_ = nullptr;
	gs_eparam_t *param_shadow_color_ = nullptr;
	gs_eparam_t *param_shadow_opacity_ = nullptr;
	gs_eparam_t *param_texel_size_ = nullptr;

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

	obs_register_source(&info);
}
