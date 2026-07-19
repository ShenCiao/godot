/**************************************************************************/
/*  topology_centerline_vectorizer.cpp                                    */
/**************************************************************************/

#include "topology_centerline_vectorizer.h"

#include "topology_centerline_pipeline.h"
#include "topology_centerline_polyline_fitting.h"
#include "topology_centerline_radius_guide.h"
#include "topology_centerline_reverse_drawing.h"

#include "core/error/error_macros.h"
#include "core/object/class_db.h"
#include "core/os/time.h"
#include "core/string/print_string.h"
#include "core/variant/array.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using topology_centerline::Params;
using topology_centerline::PipelineResult;
using topology_centerline::Stroke;

Params parse_params(const Dictionary &p_params) {
	Params params;
	if (p_params.has("threshold")) {
		params.threshold = float(p_params["threshold"]);
	}
	if (p_params.has("max_thickness")) {
		params.max_thickness = float(p_params["max_thickness"]);
	}
	if (p_params.has("despeckling")) {
		params.despeckling = float(p_params["despeckling"]);
	}
	if (p_params.has("polyline_max_error")) {
		params.polyline_max_error = float(p_params["polyline_max_error"]);
	}
	if (p_params.has("polyline_max_segment_length")) {
		params.polyline_max_segment_length = float(p_params["polyline_max_segment_length"]);
	}
	return params;
}

Ref<Image> prepare_image(const Ref<Image> &p_image) {
	ERR_FAIL_COND_V(p_image.is_null(), Ref<Image>());
	ERR_FAIL_COND_V(p_image->is_empty(), Ref<Image>());

	Ref<Image> image = p_image->duplicate();
	if (image->is_compressed()) {
		const Error error = image->decompress();
		ERR_FAIL_COND_V_MSG(error != OK, Ref<Image>(), "TopologyCenterlineVectorizer: failed to decompress image.");
	}
	if (image->get_format() != Image::FORMAT_RGBA8) {
		image->convert(Image::FORMAT_RGBA8);
	}
	return image;
}

std::vector<float> build_coverage(const Ref<Image> &p_image) {
	const size_t pixel_count = static_cast<size_t>(p_image->get_width()) * static_cast<size_t>(p_image->get_height());
	std::vector<float> coverage(pixel_count);
	const PackedByteArray bytes = p_image->get_data();
	const uint8_t *source = bytes.ptr();
	for (size_t i = 0; i < pixel_count; i++) {
		const uint8_t *rgba = source + i * 4;
		coverage[i] = rgba[3] < 255 ? float(rgba[3]) / 255.0f : 1.0f - (0.2126f * float(rgba[0]) + 0.7152f * float(rgba[1]) + 0.0722f * float(rgba[2])) / 255.0f;
	}
	return coverage;
}

TypedArray<Dictionary> strokes_to_array(const std::vector<Stroke> &p_strokes, float p_max_thickness) {
	TypedArray<Dictionary> result;
	for (const Stroke &source_stroke : p_strokes) {
		if (source_stroke.samples.size() < 2) {
			continue;
		}

		PackedVector2Array positions;
		PackedFloat32Array radii;
		positions.resize(int(source_stroke.samples.size()));
		radii.resize(int(source_stroke.samples.size()));
		Vector2 *positions_write = positions.ptrw();
		float *radii_write = radii.ptrw();
		for (size_t i = 0; i < source_stroke.samples.size(); i++) {
			positions_write[i] = source_stroke.samples[i].position;
			const float radius = std::max(0.0f, source_stroke.samples[i].radius);
			radii_write[i] = p_max_thickness > 0.0f ? std::min(radius, p_max_thickness) : radius;
		}

		Dictionary stroke;
		stroke["positions"] = positions;
		stroke["radii"] = radii;
		result.push_back(stroke);
	}
	return result;
}

} // namespace

void TopologyCenterlineVectorizer::_bind_methods() {
	ClassDB::bind_static_method("TopologyCenterlineVectorizer", D_METHOD("vectorize", "source", "params"), &TopologyCenterlineVectorizer::vectorize, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("TopologyCenterlineVectorizer", D_METHOD("vectorize_image", "image", "params"), &TopologyCenterlineVectorizer::vectorize_image, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("TopologyCenterlineVectorizer", D_METHOD("vectorize_texture", "texture", "params"), &TopologyCenterlineVectorizer::vectorize_texture, DEFVAL(Dictionary()));
}

TypedArray<Dictionary> TopologyCenterlineVectorizer::vectorize(const Variant &p_source, const Dictionary &p_params) {
	if (p_source.get_type() != Variant::OBJECT) {
		ERR_FAIL_V_MSG(TypedArray<Dictionary>(), "TopologyCenterlineVectorizer.vectorize: source must be Image or Texture2D.");
	}
	Object *object = Object::cast_to<Object>(p_source.get_validated_object());
	if (object == nullptr) {
		ERR_FAIL_V_MSG(TypedArray<Dictionary>(), "TopologyCenterlineVectorizer.vectorize: source is null.");
	}
	if (Image *image = Object::cast_to<Image>(object)) {
		return vectorize_image(Ref<Image>(image), p_params);
	}
	if (Texture2D *texture = Object::cast_to<Texture2D>(object)) {
		return vectorize_texture(Ref<Texture2D>(texture), p_params);
	}
	ERR_FAIL_V_MSG(TypedArray<Dictionary>(), "TopologyCenterlineVectorizer.vectorize: source must be Image or Texture2D.");
}

TypedArray<Dictionary> TopologyCenterlineVectorizer::vectorize_texture(const Ref<Texture2D> &p_texture, const Dictionary &p_params) {
	ERR_FAIL_COND_V_MSG(p_texture.is_null(), TypedArray<Dictionary>(), "TopologyCenterlineVectorizer: texture is null.");
	const Ref<Image> image = p_texture->get_image();
	ERR_FAIL_COND_V_MSG(image.is_null() || image->is_empty(), TypedArray<Dictionary>(),
			"TopologyCenterlineVectorizer: texture->get_image() failed (GPU-only textures need a readable image).");
	return vectorize_image(image, p_params);
}

TypedArray<Dictionary> TopologyCenterlineVectorizer::vectorize_image(const Ref<Image> &p_image, const Dictionary &p_params) {
	const uint64_t started = Time::get_singleton()->get_ticks_usec();
	const Ref<Image> image = prepare_image(p_image);
	ERR_FAIL_COND_V(image.is_null(), TypedArray<Dictionary>());

	const int width = image->get_width();
	const int height = image->get_height();
	ERR_FAIL_COND_V(width <= 0 || height <= 0, TypedArray<Dictionary>());
	const Params params = parse_params(p_params);
	const std::vector<float> coverage = build_coverage(image);
	const uint64_t coverage_finished = Time::get_singleton()->get_ticks_usec();
	const std::vector<float> radius_guide = topology_centerline::build_radius_guide(image, params);
	const uint64_t radius_finished = Time::get_singleton()->get_ticks_usec();

	Params algorithm_params = params;
	algorithm_params.max_thickness = 0.0f;
	const PipelineResult pipeline = topology_centerline::extract_base_centerlines(coverage, width, height, algorithm_params, &radius_guide);
	const uint64_t base_finished = Time::get_singleton()->get_ticks_usec();
	std::vector<Stroke> strokes = topology_centerline::reverse_draw(pipeline, algorithm_params, coverage, width, height);
	const uint64_t reverse_finished = Time::get_singleton()->get_ticks_usec();
	topology_centerline::fit_stroke_polylines(strokes, algorithm_params);
	const uint64_t fitting_finished = Time::get_singleton()->get_ticks_usec();
	const TypedArray<Dictionary> result = strokes_to_array(strokes, params.max_thickness);
	const uint64_t finished = Time::get_singleton()->get_ticks_usec();

	print_verbose(vformat("TopologyCenterlineVectorizer: image=%dx%d coverage=%.3fms", width, height,
			double(coverage_finished - started) / 1000.0));
	print_verbose(vformat("TopologyCenterlineVectorizer: tahoma_radius_guide=%.3fms",
			double(radius_finished - coverage_finished) / 1000.0));
	print_verbose(vformat("TopologyCenterlineVectorizer: base=%.3fms clustered_pixels=%d topology_nodes=%d cluster_edges=%d centerlines=%d",
			double(base_finished - radius_finished) / 1000.0, pipeline.clustered_pixel_count, int(pipeline.nodes.size()),
			pipeline.cluster_edge_count, int(pipeline.centerlines.size())));
	print_verbose(vformat("TopologyCenterlineVectorizer: reverse=%.3fms strokes=%d", double(reverse_finished - base_finished) / 1000.0,
			int(strokes.size())));
	print_verbose(vformat("TopologyCenterlineVectorizer: polyline_fitting=%.3fms", double(fitting_finished - reverse_finished) / 1000.0));
	print_verbose(vformat("TopologyCenterlineVectorizer: output=%.3fms output_strokes=%d total=%.3fms",
			double(finished - fitting_finished) / 1000.0, result.size(), double(finished - started) / 1000.0));
	return result;
}
