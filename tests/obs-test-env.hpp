#pragma once

// テスト用に、フロントエンドなしで libobs を起動し、描画できる状態にする。
// Windows は D3D11 と OpenGL、Linux は X11 + EGL の OpenGL、macOS は OpenGL の描画モジュールを渡す。

#include <obs.h>
#if defined(__linux__)
#include <obs-nix-platform.h>
#include <X11/Xlib.h>
#endif

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

namespace obs_test {

// 成功すれば true。失敗したときは obs_shutdown まで済ませて false を返す
inline bool start(const char *graphics_module, const char *core_data, uint32_t width, uint32_t height)
{
#if defined(__linux__)
	// Linux の OpenGL 描画は X11 のディスプレイと EGL を使う。CI では xvfb のディスプレイを開く
	Display *display = XOpenDisplay(nullptr);
	if (!display) {
		std::cerr << "failed to open X display\n";
		return false;
	}
	obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
	obs_set_nix_platform_display(display);
#endif
	if (!obs_startup("en-US", nullptr, nullptr)) {
		std::cerr << "failed to start OBS\n";
		return false;
	}
	// obs_add_data_path は非推奨だが、フロントエンドなしで libobs の既定エフェクトを読ませる手段がほかにない
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	std::string core_data_path = core_data;
	core_data_path += '/';
	obs_add_data_path(core_data_path.c_str());
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif
	obs_video_info video = {};
	video.graphics_module = graphics_module;
	video.fps_num = 60;
	video.fps_den = 1;
	video.base_width = width;
	video.base_height = height;
	video.output_width = width;
	video.output_height = height;
	video.output_format = VIDEO_FORMAT_RGBA;
	if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS) {
		std::cerr << "failed to initialize OBS video\n";
		obs_shutdown();
		return false;
	}
	// OpenGL では、OBS の描画スレッドが最初のフレームを進める前にテストのスレッドから描画すると、
	// 出力全体が空になることがある。描画スレッドが 2 フレーム進んだのを確かめてから戻る
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (obs_get_total_frames() < 2) {
		if (std::chrono::steady_clock::now() > deadline) {
			std::cerr << "OBS graphics thread did not start rendering within five seconds\n";
			obs_shutdown();
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return true;
}

// プラグインを読み込んで初期化する。失敗したときは false を返す
inline bool load_plugin(const char *plugin_path, const char *module_data)
{
	obs_module_t *module = nullptr;
	if (obs_open_module(&module, plugin_path, module_data) != MODULE_SUCCESS || !obs_init_module(module)) {
		std::cerr << "failed to load plugin module\n";
		return false;
	}
	return true;
}

} // namespace obs_test
