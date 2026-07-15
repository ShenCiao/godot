/**************************************************************************/
/*  centerline_vectorizer.cpp                                             */
/**************************************************************************/

#include "centerline_vectorizer.h"

#include "core/error/error_macros.h"
#include "core/math/math_defs.h"
#include "core/object/class_db.h"
#include "core/os/time.h"
#include "core/string/print_string.h"
#include "core/variant/array.h"

// Match the arrangement_2d Conan CGAL build (Boost multiprecision, no GMP).
#define CGAL_DISABLE_GMP true
#define CGAL_DO_NOT_USE_BOOST_MP

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Polyline_simplification_2/Squared_distance_cost.h>
#include <CGAL/Polyline_simplification_2/Stop_above_cost_threshold.h>
#include <CGAL/Polyline_simplification_2/simplify.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct Params {
	float threshold = 0.5f;
	float max_thickness = 0.0f;
	float despeckling = 0.0f;
};

struct Stroke {
	PackedVector2Array positions;
	PackedFloat32Array radii;
	std::vector<uint8_t> anchors;
};

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
	return params;
}

Ref<Image> prepare_image(const Ref<Image> &p_image) {
	ERR_FAIL_COND_V(p_image.is_null(), Ref<Image>());
	ERR_FAIL_COND_V(p_image->is_empty(), Ref<Image>());

	Ref<Image> image = p_image->duplicate();
	if (image->is_compressed()) {
		const Error error = image->decompress();
		ERR_FAIL_COND_V_MSG(error != OK, Ref<Image>(), "CenterlineVectorizer: failed to decompress image.");
	}
	if (image->get_format() != Image::FORMAT_RGBA8) {
		image->convert(Image::FORMAT_RGBA8);
	}
	return image;
}

inline bool in_bounds(int x, int y, int width, int height) {
	return x >= 0 && y >= 0 && x < width && y < height;
}

inline size_t pixel_index(int x, int y, int width) {
	return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
}

std::vector<uint8_t> build_mask(const Ref<Image> &p_image, float p_threshold) {
	const size_t pixel_count = static_cast<size_t>(p_image->get_width()) * static_cast<size_t>(p_image->get_height());
	std::vector<uint8_t> mask(pixel_count, 0);
	const PackedByteArray bytes = p_image->get_data();
	const uint8_t *source = bytes.ptr();
	for (size_t i = 0; i < pixel_count; i++) {
		const uint8_t *rgba = source + i * 4;
		const float coverage = rgba[3] < 255 ? float(rgba[3]) / 255.0f : 1.0f - (0.2126f * float(rgba[0]) + 0.7152f * float(rgba[1]) + 0.0722f * float(rgba[2])) / 255.0f;
		mask[i] = coverage >= p_threshold;
	}
	return mask;
}

void remove_small_components(std::vector<uint8_t> &r_mask, int p_width, int p_height, float p_min_area) {
	if (p_min_area <= 0.0f) {
		return;
	}

	std::vector<uint8_t> visited(r_mask.size(), 0);
	for (int y = 0; y < p_height; y++) {
		for (int x = 0; x < p_width; x++) {
			const size_t start = pixel_index(x, y, p_width);
			if (!r_mask[start] || visited[start]) {
				continue;
			}

			std::vector<size_t> component;
			std::queue<std::pair<int, int>> pending;
			pending.push({ x, y });
			visited[start] = 1;
			while (!pending.empty()) {
				const auto [current_x, current_y] = pending.front();
				pending.pop();
				component.push_back(pixel_index(current_x, current_y, p_width));

				constexpr int DX[4] = { 1, -1, 0, 0 };
				constexpr int DY[4] = { 0, 0, 1, -1 };
				for (int direction = 0; direction < 4; direction++) {
					const int next_x = current_x + DX[direction];
					const int next_y = current_y + DY[direction];
					if (!in_bounds(next_x, next_y, p_width, p_height)) {
						continue;
					}
					const size_t next = pixel_index(next_x, next_y, p_width);
					if (r_mask[next] && !visited[next]) {
						visited[next] = 1;
						pending.push({ next_x, next_y });
					}
				}
			}

			if (float(component.size()) < p_min_area) {
				for (size_t pixel : component) {
					r_mask[pixel] = 0;
				}
			}
		}
	}
}

// Felzenszwalb-Huttenlocher squared Euclidean distance transform.
void distance_transform_1d(const std::vector<float> &p_source, int p_count, std::vector<float> &r_distances,
		std::vector<int> &r_sites, std::vector<float> &r_boundaries) {
	int envelope_size = 0;
	r_sites[0] = 0;
	r_boundaries[0] = -std::numeric_limits<float>::infinity();
	r_boundaries[1] = std::numeric_limits<float>::infinity();
	for (int q = 1; q < p_count; q++) {
		float intersection = 0.0f;
		while (true) {
			const int site = r_sites[envelope_size];
			intersection = ((p_source[q] + float(q * q)) - (p_source[site] + float(site * site))) /
					float(2 * (q - site));
			if (intersection > r_boundaries[envelope_size] || envelope_size == 0) {
				break;
			}
			envelope_size--;
		}
		envelope_size++;
		r_sites[envelope_size] = q;
		r_boundaries[envelope_size] = intersection;
		r_boundaries[envelope_size + 1] = std::numeric_limits<float>::infinity();
	}

	envelope_size = 0;
	for (int q = 0; q < p_count; q++) {
		while (r_boundaries[envelope_size + 1] < q) {
			envelope_size++;
		}
		const float delta = float(q - r_sites[envelope_size]);
		r_distances[q] = delta * delta + p_source[r_sites[envelope_size]];
	}
}

std::vector<float> distance_to_background(const std::vector<uint8_t> &p_mask, int p_width, int p_height) {
	const int padded_width = p_width + 2;
	const int padded_height = p_height + 2;
	constexpr float FAR = 1.0e12f;
	std::vector<float> distances(static_cast<size_t>(padded_width) * static_cast<size_t>(padded_height), 0.0f);
	for (int y = 0; y < p_height; y++) {
		for (int x = 0; x < p_width; x++) {
			distances[pixel_index(x + 1, y + 1, padded_width)] = p_mask[pixel_index(x, y, p_width)] ? FAR : 0.0f;
		}
	}

	const int max_dimension = MAX(padded_width, padded_height);
	std::vector<float> source(max_dimension);
	std::vector<float> result(max_dimension);
	std::vector<int> sites(max_dimension);
	std::vector<float> boundaries(max_dimension + 1);
	for (int x = 0; x < padded_width; x++) {
		for (int y = 0; y < padded_height; y++) {
			source[y] = distances[pixel_index(x, y, padded_width)];
		}
		distance_transform_1d(source, padded_height, result, sites, boundaries);
		for (int y = 0; y < padded_height; y++) {
			distances[pixel_index(x, y, padded_width)] = result[y];
		}
	}
	for (int y = 0; y < padded_height; y++) {
		for (int x = 0; x < padded_width; x++) {
			source[x] = distances[pixel_index(x, y, padded_width)];
		}
		distance_transform_1d(source, padded_width, result, sites, boundaries);
		for (int x = 0; x < padded_width; x++) {
			distances[pixel_index(x, y, padded_width)] = result[x];
		}
	}

	std::vector<float> radii(p_mask.size(), 0.0f);
	for (int y = 0; y < p_height; y++) {
		for (int x = 0; x < p_width; x++) {
			const size_t pixel = pixel_index(x, y, p_width);
			if (p_mask[pixel]) {
				radii[pixel] = MAX(0.5f, Math::sqrt(distances[pixel_index(x + 1, y + 1, padded_width)]) - 0.5f);
			}
		}
	}
	return radii;
}

// Two-subiteration Zhang-Suen thinning preserves connectivity while reducing
// each ink component to a one-pixel-wide centerline.
void thin(std::vector<uint8_t> &r_pixels, int p_width, int p_height) {
	auto filled = [&](int x, int y) {
		return in_bounds(x, y, p_width, p_height) && r_pixels[pixel_index(x, y, p_width)] != 0;
	};

	std::vector<size_t> removed;
	bool changed = true;
	while (changed) {
		changed = false;
		for (int pass = 0; pass < 2; pass++) {
			removed.clear();
			for (int y = 0; y < p_height; y++) {
				for (int x = 0; x < p_width; x++) {
					if (!filled(x, y)) {
						continue;
					}

					const bool north = filled(x, y - 1);
					const bool north_east = filled(x + 1, y - 1);
					const bool east = filled(x + 1, y);
					const bool south_east = filled(x + 1, y + 1);
					const bool south = filled(x, y + 1);
					const bool south_west = filled(x - 1, y + 1);
					const bool west = filled(x - 1, y);
					const bool north_west = filled(x - 1, y - 1);
					const int neighbor_count = int(north) + int(north_east) + int(east) + int(south_east) +
							int(south) + int(south_west) + int(west) + int(north_west);
					if (neighbor_count < 2 || neighbor_count > 6) {
						continue;
					}

					const int transitions = int(!north && north_east) + int(!north_east && east) +
							int(!east && south_east) + int(!south_east && south) + int(!south && south_west) +
							int(!south_west && west) + int(!west && north_west) + int(!north_west && north);
					if (transitions != 1) {
						continue;
					}

					const bool protects_first = pass == 0 ? (north && east && south) : (north && east && west);
					const bool protects_second = pass == 0 ? (east && south && west) : (north && south && west);
					if (!protects_first && !protects_second) {
						removed.push_back(pixel_index(x, y, p_width));
					}
				}
			}
			for (size_t pixel : removed) {
				r_pixels[pixel] = 0;
			}
			changed |= !removed.empty();
		}
	}
}

float clamp_radius(float p_radius, float p_max_thickness) {
	float radius = MAX(0.0f, p_radius);
	if (p_max_thickness > 0.0f) {
		radius = MIN(radius, p_max_thickness);
	}
	return radius;
}

using SimplificationKernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using SimplificationPoint = SimplificationKernel::Point_2;

struct CuspCandidate {
	int index = -1;
	float dot = 1.0f;
	float lookahead = 0.0f;
	float arc_position = 0.0f;
};

void mark_stable_cusps(const std::vector<Vector2> &p_positions, const std::vector<float> &p_radii,
		std::vector<uint8_t> &r_anchors, bool p_closed) {
	const int count = int(p_positions.size());
	if (count < 5) {
		return;
	}

	std::vector<float> cumulative(count, 0.0f);
	for (int i = 1; i < count; i++) {
		cumulative[i] = cumulative[i - 1] + p_positions[i - 1].distance_to(p_positions[i]);
	}
	const float closing_length = p_closed ? p_positions.back().distance_to(p_positions.front()) : 0.0f;
	const float total_length = cumulative.back() + closing_length;
	std::vector<CuspCandidate> candidates;

	auto sample_side = [&](int center, int direction, float target_distance, Vector2 &r_sample, float &r_traveled) {
		int current = center;
		r_traveled = 0.0f;
		for (int step = 0; step < count - 1 && r_traveled < target_distance; step++) {
			int next = current + direction;
			if (p_closed) {
				next = (next + count) % count;
			} else if (next < 0 || next >= count) {
				break;
			}
			r_traveled += p_positions[current].distance_to(p_positions[next]);
			current = next;
			if (r_anchors[current]) {
				break;
			}
		}
		r_sample = p_positions[current];
	};

	for (int i = p_closed ? 0 : 1; i < (p_closed ? count : count - 1); i++) {
		if (r_anchors[i]) {
			continue;
		}
		const float lookahead = CLAMP(p_radii[i] * 3.0f, 6.0f, 16.0f);
		Vector2 before;
		Vector2 after;
		float before_distance = 0.0f;
		float after_distance = 0.0f;
		sample_side(i, -1, lookahead, before, before_distance);
		sample_side(i, 1, lookahead, after, after_distance);
		if (before_distance < lookahead * 0.75f || after_distance < lookahead * 0.75f) {
			continue;
		}
		const Vector2 incoming = (p_positions[i] - before).normalized();
		const Vector2 outgoing = (after - p_positions[i]).normalized();
		const float dot = incoming.dot(outgoing);
		if (dot < 0.5f) {
			candidates.push_back({ i, dot, lookahead, cumulative[i] });
		}
	}

	std::sort(candidates.begin(), candidates.end(), [](const CuspCandidate &a, const CuspCandidate &b) {
		return a.dot != b.dot ? a.dot < b.dot : a.index < b.index;
	});
	std::vector<CuspCandidate> selected;
	for (const CuspCandidate &candidate : candidates) {
		bool near_selected = false;
		for (const CuspCandidate &other : selected) {
			float distance = Math::abs(candidate.arc_position - other.arc_position);
			if (p_closed) {
				distance = MIN(distance, total_length - distance);
			}
			if (distance < MAX(candidate.lookahead, other.lookahead)) {
				near_selected = true;
				break;
			}
		}
		if (!near_selected) {
			r_anchors[candidate.index] = 1;
			selected.push_back(candidate);
		}
	}
}

template <typename Visitor>
void visit_arc_neighbors(const std::vector<Vector2> &p_positions, int p_center, float p_limit, bool p_closed,
		const std::vector<uint8_t> *p_barriers, std::vector<int> &r_visit_marks, Visitor p_visitor) {
	const int count = int(p_positions.size());
	r_visit_marks[p_center] = p_center;
	for (int side = 0; side < 2; side++) {
		const int direction = side == 0 ? -1 : 1;
		int current = p_center;
		float traveled = 0.0f;
		for (int step = 0; step < count - 1; step++) {
			int next = current + direction;
			if (p_closed) {
				next = (next + count) % count;
			} else if (next < 0 || next >= count) {
				break;
			}
			if (r_visit_marks[next] == p_center) {
				break;
			}
			traveled += p_positions[current].distance_to(p_positions[next]);
			if (traveled > p_limit) {
				break;
			}
			current = next;
			r_visit_marks[current] = p_center;
			p_visitor(current, direction * traveled, side);
			if (p_barriers != nullptr && (*p_barriers)[current]) {
				break;
			}
		}
	}
}

void smooth_stroke_radii(const std::vector<Vector2> &p_positions, std::vector<float> &r_radii, bool p_closed) {
	const int count = int(p_positions.size());
	if (count < 3) {
		return;
	}
	const std::vector<float> original_radii = r_radii;
	std::vector<float> median_radii(count);
	std::vector<int> visit_marks(count, -1);

	// The distance transform has isolated positive spikes at crossings. Remove
	// them before the low-pass fit so each paired stroke estimates its own width.
	for (int center = 0; center < count; center++) {
		std::vector<float> neighborhood = { original_radii[center] };
		const float window_radius = CLAMP(2.0f + original_radii[center] * 0.5f, 2.5f, 6.0f);
		visit_arc_neighbors(p_positions, center, window_radius, p_closed, nullptr, visit_marks,
				[&](int index, float, int) { neighborhood.push_back(original_radii[index]); });
		std::sort(neighborhood.begin(), neighborhood.end());
		const size_t middle = neighborhood.size() / 2;
		median_radii[center] = neighborhood.size() % 2 != 0 ? neighborhood[middle] :
				0.5f * (neighborhood[middle - 1] + neighborhood[middle]);
	}

	std::fill(visit_marks.begin(), visit_marks.end(), -1);
	for (int center = 0; center < count; center++) {
		const float sigma = CLAMP(3.0f + median_radii[center] * 0.75f, 3.5f, 8.0f);
		double s0 = 1.0;
		double s1 = 0.0;
		double s2 = 0.0;
		double b0 = median_radii[center];
		double b1 = 0.0;
		float minimum = median_radii[center];
		float maximum = median_radii[center];
		visit_arc_neighbors(p_positions, center, sigma * 3.0f, p_closed, nullptr, visit_marks,
				[&](int index, float offset, int) {
					const double t = double(offset / sigma);
					const double weight = std::exp(-0.5 * t * t);
					s0 += weight;
					s1 += weight * t;
					s2 += weight * t * t;
					b0 += weight * median_radii[index];
					b1 += weight * t * median_radii[index];
					minimum = MIN(minimum, median_radii[index]);
					maximum = MAX(maximum, median_radii[index]);
				});
		const double determinant = s0 * s2 - s1 * s1;
		if (determinant > 1e-8) {
			const float fitted = float((b0 * s2 - b1 * s1) / determinant);
			r_radii[center] = CLAMP(fitted, minimum, maximum);
		} else {
			r_radii[center] = median_radii[center];
		}
	}
}

void smooth_stroke_positions(std::vector<Vector2> &r_positions, const std::vector<float> &p_radii,
		const std::vector<uint8_t> &p_anchors, bool p_closed) {
	const int count = int(r_positions.size());
	if (count < 5) {
		return;
	}
	const std::vector<Vector2> original_positions = r_positions;
	std::vector<int> visit_marks(count, -1);

	// A local quadratic is a low-pass filter at the existing arc-length samples.
	// Unlike a moving average it preserves ordinary curvature without resampling.
	for (int center = 0; center < count; center++) {
		if (p_anchors[center]) {
			continue;
		}
		const float sigma = CLAMP(3.0f + p_radii[center] * 0.75f, 4.0f, 8.0f);
		double s0 = 1.0;
		double s1 = 0.0;
		double s2 = 0.0;
		double s3 = 0.0;
		double s4 = 0.0;
		Vector2 b0 = original_positions[center];
		Vector2 b1;
		Vector2 b2;
		int side_counts[2] = {};
		visit_arc_neighbors(original_positions, center, sigma * 3.0f, p_closed, &p_anchors, visit_marks,
				[&](int index, float offset, int side) {
					const double t = double(offset / sigma);
					const double t2 = t * t;
					const double weight = std::exp(-0.5 * t2);
					s0 += weight;
					s1 += weight * t;
					s2 += weight * t2;
					s3 += weight * t2 * t;
					s4 += weight * t2 * t2;
					b0 += original_positions[index] * float(weight);
					b1 += original_positions[index] * float(weight * t);
					b2 += original_positions[index] * float(weight * t2);
					side_counts[side]++;
				});
		if (side_counts[0] == 0 || side_counts[1] == 0) {
			continue;
		}

		const double minor0 = s2 * s4 - s3 * s3;
		const double minor1 = s1 * s4 - s2 * s3;
		const double minor2 = s1 * s3 - s2 * s2;
		const double determinant = s0 * minor0 - s1 * minor1 + s2 * minor2;
		if (Math::abs(determinant) <= 1e-8) {
			continue;
		}
		const Vector2 numerator = b0 * float(minor0) - b1 * float(s1 * s4 - s2 * s3) +
				b2 * float(s1 * s3 - s2 * s2);
		const Vector2 fitted = numerator / float(determinant);
		Vector2 displacement = fitted - original_positions[center];
		const float displacement_limit = CLAMP(p_radii[center] * 0.5f, 1.25f, 2.5f);
		if (displacement.length_squared() > displacement_limit * displacement_limit) {
			displacement = displacement.normalized() * displacement_limit;
		}
		r_positions[center] = original_positions[center] + displacement;
	}
}

void append_simplified_span(const std::vector<int> &p_indices, const std::vector<Vector2> &p_positions,
		const std::vector<float> &p_radii, std::vector<Vector2> &r_positions, std::vector<float> &r_radii) {
	if (p_indices.size() <= 2) {
		for (int index : p_indices) {
			if (r_positions.empty() || r_positions.back() != p_positions[index]) {
				r_positions.push_back(p_positions[index]);
				r_radii.push_back(p_radii[index]);
			}
		}
		return;
	}

	std::vector<SimplificationPoint> input;
	input.reserve(p_indices.size());
	for (int index : p_indices) {
		input.emplace_back(double(p_positions[index].x), double(p_positions[index].y));
	}
	std::vector<SimplificationPoint> simplified;
	try {
		CGAL::Polyline_simplification_2::simplify(input.begin(), input.end(),
				CGAL::Polyline_simplification_2::Squared_distance_cost(),
				CGAL::Polyline_simplification_2::Stop_above_cost_threshold(0.1 * 0.1),
				std::back_inserter(simplified), false);
	} catch (...) {
		simplified = input;
	}

	size_t search_from = 0;
	for (const SimplificationPoint &point : simplified) {
		while (search_from < input.size() && input[search_from] != point) {
			search_from++;
		}
		if (search_from == input.size()) {
			// The simplifier only deletes vertices. If that contract ever changes,
			// preserve the unsimplified span rather than losing radius alignment.
			for (int index : p_indices) {
				if (r_positions.empty() || r_positions.back() != p_positions[index]) {
					r_positions.push_back(p_positions[index]);
					r_radii.push_back(p_radii[index]);
				}
			}
			return;
		}
		const int source_index = p_indices[search_from];
		if (r_positions.empty() || r_positions.back() != p_positions[source_index]) {
			r_positions.push_back(p_positions[source_index]);
			r_radii.push_back(p_radii[source_index]);
		}
		search_from++;
	}
}

void postprocess_stroke(Stroke &r_stroke) {
	if (r_stroke.positions.size() < 3) {
		return;
	}
	const bool closed = r_stroke.positions[0] == r_stroke.positions[r_stroke.positions.size() - 1];
	int unique_count = r_stroke.positions.size() - int(closed);
	if (unique_count < 3) {
		return;
	}

	std::vector<Vector2> positions(unique_count);
	std::vector<float> radii(unique_count);
	std::vector<uint8_t> anchors(unique_count, 0);
	for (int i = 0; i < unique_count; i++) {
		positions[i] = r_stroke.positions[i];
		radii[i] = r_stroke.radii[i];
		if (i < int(r_stroke.anchors.size())) {
			anchors[i] = r_stroke.anchors[i];
		}
	}
	if (!closed) {
		anchors.front() = 1;
		anchors.back() = 1;
	}

	smooth_stroke_radii(positions, radii, closed);
	mark_stable_cusps(positions, radii, anchors, closed);
	smooth_stroke_positions(positions, radii, anchors, closed);
	anchors[0] = 1; // Stable seam for closed-stroke simplification.

	std::vector<int> anchor_indices;
	for (int i = 0; i < unique_count; i++) {
		if (anchors[i]) {
			anchor_indices.push_back(i);
		}
	}
	std::vector<Vector2> simplified_positions;
	std::vector<float> simplified_radii;
	if (!closed) {
		for (size_t anchor = 1; anchor < anchor_indices.size(); anchor++) {
			std::vector<int> span;
			for (int i = anchor_indices[anchor - 1]; i <= anchor_indices[anchor]; i++) {
				span.push_back(i);
			}
			append_simplified_span(span, positions, radii, simplified_positions, simplified_radii);
		}
	} else {
		for (size_t anchor = 0; anchor < anchor_indices.size(); anchor++) {
			const int first = anchor_indices[anchor];
			const int last = anchor_indices[(anchor + 1) % anchor_indices.size()];
			std::vector<int> span = { first };
			for (int i = (first + 1) % unique_count; i != last; i = (i + 1) % unique_count) {
				span.push_back(i);
			}
			span.push_back(last);
			append_simplified_span(span, positions, radii, simplified_positions, simplified_radii);
		}
		if (!simplified_positions.empty() && simplified_positions.back() != simplified_positions.front()) {
			simplified_positions.push_back(simplified_positions.front());
			simplified_radii.push_back(simplified_radii.front());
		}
	}

	if (simplified_positions.size() < 2) {
		return;
	}
	r_stroke.positions.clear();
	r_stroke.radii.clear();
	r_stroke.anchors.clear();
	for (size_t i = 0; i < simplified_positions.size(); i++) {
		r_stroke.positions.push_back(simplified_positions[i]);
		r_stroke.radii.push_back(simplified_radii[i]);
	}
}

std::vector<Stroke> skeleton_to_strokes(const std::vector<uint8_t> &p_mask, int p_width, int p_height,
		float p_despeckling, float p_max_thickness) {
	std::vector<uint8_t> ink = p_mask;
	remove_small_components(ink, p_width, p_height, p_despeckling);
	const std::vector<float> radii = distance_to_background(ink, p_width, p_height);
	std::vector<uint8_t> skeleton = ink;
	thin(skeleton, p_width, p_height);

	std::unordered_map<int, std::vector<int>> adjacency;
	for (int y = 0; y < p_height; y++) {
		for (int x = 0; x < p_width; x++) {
			const int vertex = int(pixel_index(x, y, p_width));
			if (!skeleton[vertex]) {
				continue;
			}
			adjacency[vertex];
			for (int offset_y = -1; offset_y <= 1; offset_y++) {
				for (int offset_x = -1; offset_x <= 1; offset_x++) {
					if ((offset_x == 0 && offset_y == 0) || !in_bounds(x + offset_x, y + offset_y, p_width, p_height)) {
						continue;
					}
					const int neighbor = int(pixel_index(x + offset_x, y + offset_y, p_width));
					if (neighbor <= vertex || !skeleton[neighbor]) {
						continue;
					}
					// Prefer orthogonal links in a 2x2 corner; the diagonal would create
					// a triangle and a false junction.
					if (offset_x != 0 && offset_y != 0 &&
							(skeleton[pixel_index(x + offset_x, y, p_width)] || skeleton[pixel_index(x, y + offset_y, p_width)])) {
						continue;
					}
					adjacency[vertex].push_back(neighbor);
					adjacency[neighbor].push_back(vertex);
				}
			}
		}
	}

	std::unordered_map<int, Vector2> synthetic_positions;
	std::unordered_map<int, float> synthetic_radii;
	const int pixel_vertex_count = p_width * p_height;
	auto position_of = [&](int p_vertex) -> Vector2 {
		if (p_vertex < pixel_vertex_count) {
			return Vector2(float(p_vertex % p_width) + 0.5f, float(p_vertex / p_width) + 0.5f);
		}
		return synthetic_positions[p_vertex];
	};
	auto radius_of = [&](int p_vertex) -> float {
		return p_vertex < pixel_vertex_count ? radii[p_vertex] : synthetic_radii[p_vertex];
	};
	auto degree = [&](int p_vertex) -> int {
		auto found = adjacency.find(p_vertex);
		return found == adjacency.end() ? 0 : int(found->second.size());
	};

	// A raster crossing is usually a small cluster of high-degree pixels. Replace
	// each cluster with one semantic junction before pairing its branches.
	std::unordered_set<int> unclustered;
	for (const auto &entry : adjacency) {
		if (entry.second.size() >= 3) {
			unclustered.insert(entry.first);
		}
	}
	std::vector<std::vector<int>> clusters;
	while (!unclustered.empty()) {
		const int seed = *unclustered.begin();
		unclustered.erase(seed);
		std::vector<int> cluster;
		std::queue<int> pending;
		pending.push(seed);
		while (!pending.empty()) {
			const int current = pending.front();
			pending.pop();
			cluster.push_back(current);
			for (int neighbor : adjacency[current]) {
				if (unclustered.erase(neighbor)) {
					pending.push(neighbor);
				}
			}
		}
		if (cluster.size() >= 2) {
			clusters.push_back(std::move(cluster));
		}
	}

	int next_synthetic_vertex = pixel_vertex_count;
	for (const std::vector<int> &cluster : clusters) {
		std::unordered_set<int> core(cluster.begin(), cluster.end());
		std::unordered_set<int> external;
		Vector2 center;
		double total_weight = 0.0;
		float center_radius = 0.0f;
		for (int vertex : cluster) {
			const double weight = MAX(0.25, double(radius_of(vertex)) * double(radius_of(vertex)));
			center += position_of(vertex) * float(weight);
			total_weight += weight;
			center_radius = MAX(center_radius, radius_of(vertex));
			for (int neighbor : adjacency[vertex]) {
				if (!core.count(neighbor)) {
					external.insert(neighbor);
				}
			}
		}
		if (external.size() < 3) {
			continue;
		}

		const int synthetic = next_synthetic_vertex++;
		synthetic_positions[synthetic] = center / float(total_weight);
		synthetic_radii[synthetic] = center_radius;
		adjacency[synthetic] = std::vector<int>(external.begin(), external.end());
		for (int neighbor : external) {
			auto &neighbors = adjacency[neighbor];
			neighbors.erase(std::remove_if(neighbors.begin(), neighbors.end(), [&](int candidate) {
				return core.count(candidate);
			}),
					neighbors.end());
			neighbors.push_back(synthetic);
		}
		for (int vertex : cluster) {
			adjacency.erase(vertex);
		}
	}
	for (auto &entry : adjacency) {
		std::sort(entry.second.begin(), entry.second.end());
		entry.second.erase(std::unique(entry.second.begin(), entry.second.end()), entry.second.end());
	}

	std::unordered_map<int, std::unordered_map<int, int>> continuation;
	for (const auto &entry : adjacency) {
		const int junction = entry.first;
		if (entry.second.size() == 2) {
			continuation[junction][entry.second[0]] = entry.second[1];
			continuation[junction][entry.second[1]] = entry.second[0];
			continue;
		}
		if (entry.second.size() < 3) {
			continue;
		}

		struct Branch {
			int neighbor = -1;
			Vector2 direction;
		};
		std::vector<Branch> branches;
		for (int neighbor : entry.second) {
			const Vector2 origin = position_of(junction);
			const float lookahead = MAX(2.0f, radius_of(junction) * 1.5f);
			float traveled = 0.0f;
			int previous = junction;
			int current = neighbor;
			Vector2 sample = position_of(current);
			while (degree(current) == 2 && traveled < lookahead) {
				const Vector2 previous_position = position_of(previous);
				const Vector2 current_position = position_of(current);
				const float segment_length = previous_position.distance_to(current_position);
				if (traveled + segment_length >= lookahead && segment_length > 0.0f) {
					sample = previous_position.lerp(current_position, (lookahead - traveled) / segment_length);
					break;
				}
				traveled += segment_length;
				sample = current_position;
				const int next = adjacency[current][0] == previous ? adjacency[current][1] : adjacency[current][0];
				previous = current;
				current = next;
			}
			branches.push_back({ neighbor, (sample - origin).normalized() });
		}

		struct Candidate {
			int first = -1;
			int second = -1;
			float dot = 0.0f;
		};
		std::vector<Candidate> candidates;
		for (int first = 0; first < int(branches.size()); first++) {
			for (int second = first + 1; second < int(branches.size()); second++) {
				const float dot = branches[first].direction.dot(branches[second].direction);
				if (dot <= -0.65f) {
					candidates.push_back({ first, second, dot });
				}
			}
		}
		std::sort(candidates.begin(), candidates.end(), [&](const Candidate &a, const Candidate &b) {
			if (a.dot != b.dot) {
				return a.dot < b.dot;
			}
			return std::pair<int, int>(branches[a.first].neighbor, branches[a.second].neighbor) <
					std::pair<int, int>(branches[b.first].neighbor, branches[b.second].neighbor);
		});
		std::vector<uint8_t> paired(branches.size(), 0);
		for (const Candidate &candidate : candidates) {
			if (paired[candidate.first] || paired[candidate.second]) {
				continue;
			}
			paired[candidate.first] = paired[candidate.second] = 1;
			const int first = branches[candidate.first].neighbor;
			const int second = branches[candidate.second].neighbor;
			continuation[junction][first] = second;
			continuation[junction][second] = first;
		}
	}

	auto edge_key = [](int p_first, int p_second) -> uint64_t {
		if (p_first > p_second) {
			std::swap(p_first, p_second);
		}
		return (uint64_t(uint32_t(p_first)) << 32) | uint32_t(p_second);
	};
	auto continuation_of = [&](int p_vertex, int p_incoming) -> int {
		auto vertex = continuation.find(p_vertex);
		if (vertex == continuation.end()) {
			return -1;
		}
		auto incoming = vertex->second.find(p_incoming);
		return incoming == vertex->second.end() ? -1 : incoming->second;
	};

	std::vector<int> vertices;
	vertices.reserve(adjacency.size());
	for (const auto &entry : adjacency) {
		vertices.push_back(entry.first);
	}
	std::sort(vertices.begin(), vertices.end());

	std::unordered_set<uint64_t> used_edges;
	std::vector<Stroke> strokes;
	auto append_vertex = [&](Stroke &r_stroke, int p_vertex) {
		r_stroke.positions.push_back(position_of(p_vertex));
		r_stroke.radii.push_back(clamp_radius(radius_of(p_vertex), p_max_thickness));
		r_stroke.anchors.push_back(degree(p_vertex) != 2);
	};
	auto walk = [&](int p_start, int p_first_neighbor) {
		Stroke stroke;
		int current = p_start;
		int next = p_first_neighbor;
		append_vertex(stroke, current);
		while (next >= 0) {
			if (!used_edges.insert(edge_key(current, next)).second) {
				break;
			}
			const int previous = current;
			current = next;
			append_vertex(stroke, current);
			next = continuation_of(current, previous);
		}
		if (stroke.positions.size() >= 2) {
			strokes.push_back(std::move(stroke));
		}
	};

	// Open strokes start at unmatched ports, then any remaining edges are cycles.
	for (int vertex : vertices) {
		if (!adjacency.count(vertex)) {
			continue;
		}
		for (int neighbor : adjacency[vertex]) {
			if (continuation_of(vertex, neighbor) < 0 && !used_edges.count(edge_key(vertex, neighbor))) {
				walk(vertex, neighbor);
			}
		}
	}
	for (int vertex : vertices) {
		if (!adjacency.count(vertex)) {
			continue;
		}
		for (int neighbor : adjacency[vertex]) {
			if (!used_edges.count(edge_key(vertex, neighbor))) {
				walk(vertex, neighbor);
			}
		}
	}
	for (Stroke &stroke : strokes) {
		postprocess_stroke(stroke);
	}
	return strokes;
}

TypedArray<Dictionary> strokes_to_array(const std::vector<Stroke> &p_strokes) {
	TypedArray<Dictionary> result;
	result.resize(int(p_strokes.size()));
	for (int i = 0; i < int(p_strokes.size()); i++) {
		Dictionary stroke;
		stroke["positions"] = p_strokes[i].positions;
		stroke["radii"] = p_strokes[i].radii;
		result[i] = stroke;
	}
	return result;
}

} // namespace

void CenterlineVectorizer::_bind_methods() {
	ClassDB::bind_static_method("CenterlineVectorizer", D_METHOD("vectorize", "source", "params"), &CenterlineVectorizer::vectorize, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("CenterlineVectorizer", D_METHOD("vectorize_image", "image", "params"), &CenterlineVectorizer::vectorize_image, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("CenterlineVectorizer", D_METHOD("vectorize_texture", "texture", "params"), &CenterlineVectorizer::vectorize_texture, DEFVAL(Dictionary()));
}

TypedArray<Dictionary> CenterlineVectorizer::vectorize(const Variant &p_source, const Dictionary &p_params) {
	if (p_source.get_type() != Variant::OBJECT) {
		ERR_FAIL_V_MSG(TypedArray<Dictionary>(), "CenterlineVectorizer.vectorize: source must be Image or Texture2D.");
	}
	Object *object = Object::cast_to<Object>(p_source.get_validated_object());
	if (object == nullptr) {
		ERR_FAIL_V_MSG(TypedArray<Dictionary>(), "CenterlineVectorizer.vectorize: source is null.");
	}
	if (Image *image = Object::cast_to<Image>(object)) {
		return vectorize_image(Ref<Image>(image), p_params);
	}
	if (Texture2D *texture = Object::cast_to<Texture2D>(object)) {
		return vectorize_texture(Ref<Texture2D>(texture), p_params);
	}
	ERR_FAIL_V_MSG(TypedArray<Dictionary>(), "CenterlineVectorizer.vectorize: source must be Image or Texture2D.");
}

TypedArray<Dictionary> CenterlineVectorizer::vectorize_texture(const Ref<Texture2D> &p_texture, const Dictionary &p_params) {
	ERR_FAIL_COND_V_MSG(p_texture.is_null(), TypedArray<Dictionary>(), "CenterlineVectorizer: texture is null.");
	const Ref<Image> image = p_texture->get_image();
	ERR_FAIL_COND_V_MSG(image.is_null() || image->is_empty(), TypedArray<Dictionary>(),
			"CenterlineVectorizer: texture->get_image() failed (GPU-only textures need a readable image).");
	return vectorize_image(image, p_params);
}

TypedArray<Dictionary> CenterlineVectorizer::vectorize_image(const Ref<Image> &p_image, const Dictionary &p_params) {
	const uint64_t started = Time::get_singleton()->get_ticks_usec();
	const Ref<Image> image = prepare_image(p_image);
	ERR_FAIL_COND_V(image.is_null(), TypedArray<Dictionary>());

	const int width = image->get_width();
	const int height = image->get_height();
	ERR_FAIL_COND_V(width <= 0 || height <= 0, TypedArray<Dictionary>());
	const Params params = parse_params(p_params);
	const std::vector<uint8_t> mask = build_mask(image, params.threshold);
	const std::vector<Stroke> strokes = skeleton_to_strokes(mask, width, height, params.despeckling, params.max_thickness);
	const uint64_t finished = Time::get_singleton()->get_ticks_usec();
	print_verbose(vformat("CenterlineVectorizer: image=%dx%d strokes=%d total=%.3fms", width, height, int(strokes.size()),
			double(finished - started) / 1000.0));
	return strokes_to_array(strokes);
}
