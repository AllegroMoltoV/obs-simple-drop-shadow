#include "filter-render-fixture.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QTimer>

OBS_DECLARE_MODULE()

namespace {
QString result_dir;
QString phase;
QJsonObject evidence;
constexpr const char *source_name = "Drop shadow install impulse";
constexpr const char *legacy_filter_name = "Drop Shadow";
constexpr const char *current_filter_name = "Simple Drop Shadow";

// 32x32 のソースの (10, 10) にある赤い 1 画素に、オフセット 0・半径 1 の影を付けた出力を検査する。
// v0.1.1 は出力がソースと同じ大きさで、影は 3x3 のボックス (アルファ 28)。
// 0.2.0 以降は出力の四辺に 1 画素の余白が付き、影は σ = 1/3 のガウシアンで、上下左右の 4 画素だけに薄く出る
int check_pixels(obs_source_t *source, bool legacy)
{
	fixture::Image image;
	if (!fixture::render(source, image)) {
		return 2;
	}
	fixture::Checker c;
	const int size = legacy ? 32 : 34;
	const int cx = legacy ? 10 : 11;
	const int cy = cx;
	c.expect(image.width == static_cast<uint32_t>(size) && image.height == static_cast<uint32_t>(size),
		 "unexpected output size");
	if (c.failures) {
		return 1;
	}
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			const int dx = std::abs(x - cx);
			const int dy = std::abs(y - cy);
			const int a = image.alpha(x, y);
			if (dx == 0 && dy == 0) {
				c.expect(a == 255, "original pixel is not opaque");
			} else if (dx > 1 || dy > 1) {
				c.expect(a == 0, "shadow outside the radius");
			} else if (legacy) {
				c.expect_near(a, 28, 1, "legacy box shadow");
			} else if (dx + dy == 1) {
				c.expect(a >= 8 && a <= 20 && a == image.alpha(cx - (x - cx), cy - (y - cy)),
					 "gaussian shadow next to the original");
			} else {
				c.expect(a <= 3, "gaussian shadow on the diagonal");
			}
		}
	}
	return c.failures ? 1 : 0;
}

void finish(bool passed, const QString &reason)
{
	QJsonObject result{{"passed", passed},
			   {"reason", reason},
			   {"obs", obs_get_version_string()},
			   {"phase", phase},
			   {"evidence", evidence}};
	if (auto *module = obs_get_module("simple-drop-shadow"))
		result.insert("product_binary_path", QString::fromUtf8(obs_get_module_binary_path(module)));
	QFile file(result_dir + "/result.json");
	const auto json = QJsonDocument(result).toJson();
	if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.flush())
		passed = false;
	file.close();
	blog(passed ? LOG_INFO : LOG_ERROR, "DROP_SHADOW_INSTALL %s phase=%s %s", passed ? "PASS" : "FAIL",
	     phase.toUtf8().constData(), reason.toUtf8().constData());
	auto *window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	QTimer::singleShot(0, window, [window] { window->close(); });
}

void inspect()
{
	auto *module = obs_get_module("simple-drop-shadow");
	if (!module || QString(obs_get_version_string()) != "33.0.1") {
		finish(false, "product module missing or OBS version mismatch");
		return;
	}
	int registrations = 0;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_filter_types(i, &id); ++i)
		registrations += std::strcmp(id, "drop_shadow_filter") == 0;
	evidence.insert("filter_registrations", registrations);
	if (registrations != 1) {
		finish(false, "filter registration count mismatch");
		return;
	}
	char *effect = obs_find_module_file(module, "effects/drop-shadow.effect");
	char *locale = obs_find_module_file(module, "locale/en-US.ini");
	const QString translated = QString::fromUtf8(obs_module_get_locale_text(module, "DropShadowFilterName"));
	evidence.insert("effect_path", effect ? QString::fromUtf8(effect) : QString());
	evidence.insert("locale_path", locale ? QString::fromUtf8(locale) : QString());
	evidence.insert("filter_name", translated);
	const bool legacy = translated == legacy_filter_name;
	const bool resources = effect && locale && (legacy || translated == current_filter_name);
	bfree(effect);
	bfree(locale);
	if (!resources) {
		finish(false, "installed effect or translated locale unavailable");
		return;
	}
	obs_source_t *source = nullptr;
	obs_source_t *filter = nullptr;
	if (phase == "setup") {
		source = obs_source_create("drop_shadow_test_source", source_name, nullptr, nullptr);
		obs_data_t *settings = obs_data_create();
		obs_data_set_double(settings, "offset_x", 0.0);
		obs_data_set_double(settings, "offset_y", 0.0);
		obs_data_set_double(settings, "blur_radius", 1.0);
		obs_data_set_double(settings, "opacity", 1.0);
		obs_data_set_int(settings, "color", 0xff000000);
		filter = obs_source_create("drop_shadow_filter", "install shadow", settings, nullptr);
		obs_data_release(settings);
		auto *scene_source = obs_frontend_get_current_scene();
		auto *scene = scene_source ? obs_scene_from_source(scene_source) : nullptr;
		const bool attached = source && filter && scene && obs_scene_add(scene, source);
		obs_source_release(scene_source);
		if (!attached) {
			obs_source_release(filter);
			obs_source_release(source);
			finish(false, "source, filter or scene attachment failed");
			return;
		}
		obs_source_filter_add(source, filter);
	} else {
		source = obs_get_source_by_name(source_name);
		if (source)
			filter = obs_source_get_filter_by_name(source, "install shadow");
	}
	if (!source || !filter || std::strcmp(obs_source_get_id(source), "drop_shadow_test_source") != 0 ||
	    std::strcmp(obs_source_get_id(filter), "drop_shadow_filter") != 0) {
		obs_source_release(filter);
		obs_source_release(source);
		finish(false, "persisted source or filter missing");
		return;
	}
	auto *settings = obs_source_get_settings(filter);
	const bool retained = obs_source_enabled(filter) && obs_data_get_double(settings, "offset_x") == 0.0 &&
			      obs_data_get_double(settings, "offset_y") == 0.0 &&
			      obs_data_get_double(settings, "blur_radius") == 1.0 &&
			      obs_data_get_double(settings, "opacity") == 1.0 &&
			      obs_data_get_int(settings, "color") == 0xff000000;
	evidence.insert("filter_settings", QJsonDocument::fromJson(obs_data_get_json(settings)).object());
	obs_data_release(settings);
	const int pixels = retained ? check_pixels(source, legacy) : -1;
	evidence.insert("pixel_result", pixels);
	evidence.insert("scenario", legacy ? "legacy: 32x32 output, center alpha 255, surrounding 3x3 alpha 28"
					   : "current: 34x34 output, center alpha 255, 4 neighbours alpha 8-20, "
					     "diagonals at most 3");
	obs_source_release(filter);
	obs_source_release(source);
	if (retained && pixels == 0)
		obs_frontend_save();
	finish(retained && pixels == 0, "installed resources, persisted filter settings and shadow pixels checked");
}

void frontend_event(obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		QTimer::singleShot(1000, static_cast<QMainWindow *>(obs_frontend_get_main_window()), inspect);
}
} // namespace

bool obs_module_load()
{
	result_dir = qEnvironmentVariable("DROP_SHADOW_INSTALL_RESULT_DIR");
	phase = qEnvironmentVariable("DROP_SHADOW_INSTALL_PHASE");
	if (result_dir.isEmpty() || (phase != "setup" && phase != "restore"))
		return false;
	fixture::current_spec = {32, 32, fixture::single_pixel(10, 10, 255, 0, 0)};
	fixture::register_source();
	obs_frontend_add_event_callback(frontend_event, nullptr);
	return true;
}

void obs_module_unload()
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
}
