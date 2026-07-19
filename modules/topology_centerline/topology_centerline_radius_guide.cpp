/**************************************************************************/
/*  topology_centerline_radius_guide.cpp                                  */
/**************************************************************************/

#include "topology_centerline_radius_guide.h"

#include "tcenterlinevectP.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace topology_centerline {
namespace {

CompatRaster make_raster(const Ref<Image> &p_image) {
	const int width = p_image->get_width();
	const int height = p_image->get_height();
	CompatRaster raster(width, height);
	const PackedByteArray bytes = p_image->get_data();
	const uint8_t *source = bytes.ptr();
	for (int y = 0; y < height; y++) {
		CompatPixel *row = raster.pixels(y);
		for (int x = 0; x < width; x++) {
			const uint8_t *rgba = source + (size_t(y) * size_t(width) + size_t(x)) * 4;
			row[x].r = rgba[0];
			row[x].g = rgba[1];
			row[x].b = rgba[2];
			row[x].m = rgba[3];
			row[x].value = std::max(rgba[0], std::max(rgba[1], rgba[2]));
			row[x].tone = row[x].value;
		}
	}
	return raster;
}

void delete_skeletons(SkeletonList *p_skeletons) {
	if (p_skeletons == nullptr) {
		return;
	}
	for (SkeletonGraph *skeleton : *p_skeletons) {
		delete skeleton;
	}
	delete p_skeletons;
}

void splat_radius(std::vector<float> &r_radii, std::vector<float> &r_distances,
		int p_width, int p_height, const Vector2 &p_position, float p_radius) {
	if (!p_position.is_finite() || !std::isfinite(double(p_radius)) || p_radius <= 0.0f) {
		return;
	}
	constexpr float SUPPORT = 3.0f;
	constexpr float SUPPORT_SQUARED = SUPPORT * SUPPORT;
	const int minimum_x = std::max(0, int(std::floor(p_position.x - SUPPORT - 0.5f)));
	const int maximum_x = std::min(p_width - 1, int(std::ceil(p_position.x + SUPPORT - 0.5f)));
	const int minimum_y = std::max(0, int(std::floor(p_position.y - SUPPORT - 0.5f)));
	const int maximum_y = std::min(p_height - 1, int(std::ceil(p_position.y + SUPPORT - 0.5f)));
	for (int y = minimum_y; y <= maximum_y; y++) {
		for (int x = minimum_x; x <= maximum_x; x++) {
			const float distance_squared = p_position.distance_squared_to(Vector2(float(x) + 0.5f, float(y) + 0.5f));
			if (distance_squared > SUPPORT_SQUARED) {
				continue;
			}
			const size_t pixel = size_t(y) * size_t(p_width) + size_t(x);
			if (distance_squared < r_distances[pixel]) {
				r_distances[pixel] = distance_squared;
				r_radii[pixel] = p_radius;
			}
		}
	}
}

void rasterize_stroke_guide(const TStroke &p_stroke, std::vector<float> &r_radii,
		std::vector<float> &r_distances, int p_width, int p_height) {
	const std::vector<TThickPoint> &points = p_stroke.getControlPoints();
	if (points.size() < 2) {
		return;
	}
	auto position = [&](size_t p_index) { return Vector2(float(points[p_index].x), float(points[p_index].y)); };
	auto radius = [&](size_t p_index) { return float(std::max(0.0, points[p_index].thick)); };
	if (points.size() >= 3 && points.size() % 2 == 1) {
		for (size_t control = 0; control + 2 < points.size(); control += 2) {
			const float estimated_length = position(control).distance_to(position(control + 1)) +
					position(control + 1).distance_to(position(control + 2));
			const int subdivisions = std::clamp(int(std::ceil(double(estimated_length) * 2.0)), 1, 16384);
			for (int step = 0; step <= subdivisions; step++) {
				const float t = float(step) / float(subdivisions);
				const float inverse = 1.0f - t;
				const float first_weight = inverse * inverse;
				const float middle_weight = 2.0f * inverse * t;
				const float last_weight = t * t;
				splat_radius(r_radii, r_distances, p_width, p_height,
						position(control) * first_weight + position(control + 1) * middle_weight + position(control + 2) * last_weight,
						radius(control) * first_weight + radius(control + 1) * middle_weight + radius(control + 2) * last_weight);
			}
		}
		return;
	}
	for (size_t first = 0; first + 1 < points.size(); first++) {
		const float length = position(first).distance_to(position(first + 1));
		const int subdivisions = std::clamp(int(std::ceil(double(length) * 2.0)), 1, 16384);
		for (int step = 0; step <= subdivisions; step++) {
			const float t = float(step) / float(subdivisions);
			splat_radius(r_radii, r_distances, p_width, p_height,
					position(first).lerp(position(first + 1), t),
					radius(first) + (radius(first + 1) - radius(first)) * t);
		}
	}
}

} // namespace

std::vector<float> build_radius_guide(const Ref<Image> &p_image, const Params &p_params) {
	const int width = p_image->get_width();
	const int height = p_image->get_height();
	const size_t pixel_count = size_t(width) * size_t(height);
	std::vector<float> radii(pixel_count, -1.0f);
	if (width <= 0 || height <= 0) {
		return radii;
	}

	CompatRaster raster = make_raster(p_image);
	CenterlineConfiguration configuration;
	const float coverage_threshold = std::clamp(p_params.threshold, 0.0f, 1.0f);
	const float brightness_threshold = std::clamp(
			1.0f - coverage_threshold + 1.0f / 255.0f, 1.0f / 255.0f, 1.0f);
	configuration.m_threshold = int(std::lround(brightness_threshold * 255.0f));
	configuration.m_despeckling = std::max(0, int(std::lround(p_params.despeckling)));
	configuration.m_maxThickness = double(std::max(width, height) * 2 + 1);
	configuration.m_penalty = 0.5;

	VectorizerCoreGlobals globals;
	globals.currConfig = &configuration;
	Contours polygons;
	polygonize(&raster, polygons, globals);
	VectorizerCore vectorizer;
	SkeletonList *skeletons = skeletonize(polygons, &vectorizer, globals);
	organizeGraphs(skeletons, globals);

	std::vector<TStroke *> strokes;
	conversionToStrokes(strokes, globals);
	std::vector<float> distances(pixel_count, std::numeric_limits<float>::infinity());
	for (TStroke *stroke : strokes) {
		if (stroke != nullptr) {
			rasterize_stroke_guide(*stroke, radii, distances, width, height);
		}
		delete stroke;
	}
	delete_skeletons(skeletons);
	return radii;
}

} // namespace topology_centerline
