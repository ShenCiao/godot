/**************************************************************************/
/*  topology_centerline_polyline_fitting.cpp                              */
/**************************************************************************/

#include "topology_centerline_polyline_fitting.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace topology_centerline {
namespace {

constexpr double POSITION_EPSILON = 1e-6;
constexpr double POLYGON_RADIUS_ERROR_MULTIPLIER = 0.75;
constexpr double RADIUS_MAX_ERROR = 0.25;
constexpr double EVIDENCE_SPACING = 1.0;
constexpr double TWO_PI = 6.28318530717958647692;
constexpr double INTERSECTION_CELL_SIZE = 4.0;

struct WeightedMoments {
	double weight = 0.0;
	double x = 0.0;
	double y = 0.0;
	double xx = 0.0;
	double xy = 0.0;
	double yy = 0.0;
};

struct SegmentRef {
	int stroke = -1;
	size_t sample = 0;
	Vector2 start;
	Vector2 end;
};

struct SegmentHit {
	double first_parameter = 0.0;
	double second_parameter = 0.0;
	Vector2 position;
};

struct Insertion {
	size_t segment = 0;
	double parameter = 0.0;
	Vector2 position;
	bool prepend = false;
	bool append = false;
};

struct AnchoredStroke {
	std::vector<Sample> samples;
	std::vector<uint8_t> anchors;
};

bool is_finite(float p_value) {
	return std::isfinite(double(p_value));
}

bool is_finite(const Vector2 &p_value) {
	return is_finite(p_value.x) && is_finite(p_value.y);
}

double sample_tolerance(const Sample &p_sample, double p_max_error) {
	const double radius = is_finite(p_sample.radius) ? std::max(0.0, double(p_sample.radius)) : 0.0;
	return std::max(POSITION_EPSILON, std::min(p_max_error, radius * POLYGON_RADIUS_ERROR_MULTIPLIER));
}

std::vector<Sample> compact_samples(const std::vector<Sample> &p_samples) {
	std::vector<Sample> result;
	result.reserve(p_samples.size());
	for (const Sample &sample : p_samples) {
		if (!is_finite(sample.position)) {
			continue;
		}
		if (!result.empty() &&
				result.back().position.distance_squared_to(sample.position) <= POSITION_EPSILON * POSITION_EPSILON) {
			result.back() = sample;
		} else {
			result.push_back(sample);
		}
	}
	return result;
}

std::vector<double> cumulative_lengths(const std::vector<Sample> &p_samples) {
	std::vector<double> cumulative(p_samples.size(), 0.0);
	for (size_t i = 1; i < p_samples.size(); i++) {
		cumulative[i] = cumulative[i - 1] + p_samples[i - 1].position.distance_to(p_samples[i].position);
	}
	return cumulative;
}

Sample sample_at_length(const std::vector<Sample> &p_samples, const std::vector<double> &p_cumulative, double p_length,
		size_t &r_second) {
	if (p_length <= 0.0 || p_samples.size() < 2) {
		return p_samples.front();
	}
	if (p_length >= p_cumulative.back()) {
		return p_samples.back();
	}
	while (r_second + 1 < p_cumulative.size() && p_cumulative[r_second] < p_length) {
		r_second++;
	}
	const size_t first = r_second - 1;
	const double segment_length = p_cumulative[r_second] - p_cumulative[first];
	if (segment_length <= POSITION_EPSILON) {
		return p_samples[r_second];
	}
	const float weight = float((p_length - p_cumulative[first]) / segment_length);
	return {
		p_samples[first].position.lerp(p_samples[r_second].position, weight),
		p_samples[first].radius + (p_samples[r_second].radius - p_samples[first].radius) * weight,
	};
}

std::vector<Sample> resample_evidence(const std::vector<Sample> &p_samples) {
	if (p_samples.size() < 3) {
		return p_samples;
	}
	const std::vector<double> cumulative = cumulative_lengths(p_samples);
	const double total_length = cumulative.back();
	if (total_length <= EVIDENCE_SPACING) {
		return p_samples;
	}
	const size_t segment_count = std::max<size_t>(1, size_t(std::ceil(total_length / EVIDENCE_SPACING)));
	std::vector<Sample> result;
	result.reserve(segment_count + 1);
	result.push_back(p_samples.front());
	size_t second = 1;
	for (size_t segment = 1; segment < segment_count; segment++) {
		const double target_length = total_length * double(segment) / double(segment_count);
		result.push_back(sample_at_length(p_samples, cumulative, target_length, second));
	}
	result.push_back(p_samples.back());
	return compact_samples(result);
}

bool shortcut_preserves_radius(const std::vector<Sample> &p_samples, const std::vector<double> &p_cumulative,
		size_t p_first, size_t p_last) {
	if (p_last <= p_first + 1) {
		return true;
	}
	const double arc_length = p_cumulative[p_last] - p_cumulative[p_first];
	if (arc_length <= POSITION_EPSILON) {
		return false;
	}
	for (size_t sample = p_first + 1; sample < p_last; sample++) {
		const double weight = (p_cumulative[sample] - p_cumulative[p_first]) / arc_length;
		const double fitted_radius = double(p_samples[p_first].radius) +
				(double(p_samples[p_last].radius) - double(p_samples[p_first].radius)) * weight;
		if (std::abs(double(p_samples[sample].radius) - fitted_radius) > RADIUS_MAX_ERROR + POSITION_EPSILON) {
			return false;
		}
	}
	return true;
}

bool shortcut_is_valid(const std::vector<Sample> &p_samples, const std::vector<double> &p_cumulative,
		size_t p_first, size_t p_last, double p_max_error) {
	if (p_last <= p_first || p_last >= p_samples.size()) {
		return false;
	}
	if (p_last == p_first + 1) {
		return true;
	}
	const Vector2 chord_vector = p_samples[p_last].position - p_samples[p_first].position;
	const double chord = chord_vector.length();
	if (!std::isfinite(chord) || chord <= POSITION_EPSILON) {
		return false;
	}
	const Vector2 tangent = chord_vector / float(chord);
	for (size_t sample = p_first + 1; sample < p_last; sample++) {
		const Vector2 offset = p_samples[sample].position - p_samples[p_first].position;
		const double projection = offset.dot(tangent);
		const double normal_distance = std::abs(double(offset.x) * double(tangent.y) - double(offset.y) * double(tangent.x));
		const double tolerance = sample_tolerance(p_samples[sample], p_max_error);
		if (normal_distance > tolerance + POSITION_EPSILON || projection < -tolerance || projection > chord + tolerance) {
			return false;
		}
	}
	return shortcut_preserves_radius(p_samples, p_cumulative, p_first, p_last);
}

WeightedMoments operator+(const WeightedMoments &p_a, const WeightedMoments &p_b) {
	return {
		p_a.weight + p_b.weight,
		p_a.x + p_b.x,
		p_a.y + p_b.y,
		p_a.xx + p_b.xx,
		p_a.xy + p_b.xy,
		p_a.yy + p_b.yy,
	};
}

WeightedMoments operator-(const WeightedMoments &p_a, const WeightedMoments &p_b) {
	return {
		p_a.weight - p_b.weight,
		p_a.x - p_b.x,
		p_a.y - p_b.y,
		p_a.xx - p_b.xx,
		p_a.xy - p_b.xy,
		p_a.yy - p_b.yy,
	};
}

std::vector<WeightedMoments> build_moment_prefix(const std::vector<Sample> &p_samples, double p_max_error) {
	std::vector<WeightedMoments> prefix(p_samples.size() + 1);
	for (size_t i = 0; i < p_samples.size(); i++) {
		const double previous_length = i > 0 ? p_samples[i - 1].position.distance_to(p_samples[i].position) : 0.0;
		const double next_length = i + 1 < p_samples.size() ?
				p_samples[i].position.distance_to(p_samples[i + 1].position) : 0.0;
		const double arc_mass = std::max(POSITION_EPSILON, 0.5 * (previous_length + next_length));
		const double tolerance = sample_tolerance(p_samples[i], p_max_error);
		const double weight = arc_mass / (tolerance * tolerance);
		const double x = p_samples[i].position.x;
		const double y = p_samples[i].position.y;
		const WeightedMoments sample = { weight, weight * x, weight * y, weight * x * x, weight * x * y, weight * y * y };
		prefix[i + 1] = prefix[i] + sample;
	}
	return prefix;
}

double shortcut_error(const std::vector<Sample> &p_samples, const std::vector<WeightedMoments> &p_prefix,
		size_t p_first, size_t p_last) {
	if (p_last <= p_first + 1) {
		return 0.0;
	}
	const Vector2 chord = p_samples[p_last].position - p_samples[p_first].position;
	const double chord_length = chord.length();
	if (chord_length <= POSITION_EPSILON) {
		return std::numeric_limits<double>::infinity();
	}
	const double nx = -double(chord.y) / chord_length;
	const double ny = double(chord.x) / chord_length;
	const double line_offset = nx * double(p_samples[p_first].position.x) + ny * double(p_samples[p_first].position.y);
	const WeightedMoments moments = p_prefix[p_last] - p_prefix[p_first + 1];
	const double first_order = nx * moments.x + ny * moments.y;
	const double second_order = nx * nx * moments.xx + 2.0 * nx * ny * moments.xy + ny * ny * moments.yy;
	return std::max(0.0, second_order - 2.0 * line_offset * first_order + line_offset * line_offset * moments.weight);
}

double unwrap_near(double p_angle, double p_reference) {
	while (p_angle - p_reference > 0.5 * TWO_PI) {
		p_angle -= TWO_PI;
	}
	while (p_angle - p_reference < -0.5 * TWO_PI) {
		p_angle += TWO_PI;
	}
	return p_angle;
}

double cross_product(const Vector2 &p_a, const Vector2 &p_b) {
	return double(p_a.x) * double(p_b.y) - double(p_a.y) * double(p_b.x);
}

double segment_parameter(const Vector2 &p_point, const Vector2 &p_start, const Vector2 &p_end) {
	const Vector2 delta = p_end - p_start;
	const double length_squared = delta.length_squared();
	return length_squared <= POSITION_EPSILON * POSITION_EPSILON ? 0.0 :
			double((p_point - p_start).dot(delta)) / length_squared;
}

void append_segment_hit(std::vector<SegmentHit> &r_hits, double p_first_parameter, double p_second_parameter,
		const Vector2 &p_position) {
	for (const SegmentHit &hit : r_hits) {
		if (hit.position.distance_squared_to(p_position) <= POSITION_EPSILON * POSITION_EPSILON) {
			return;
		}
	}
	r_hits.push_back({
		std::clamp(p_first_parameter, 0.0, 1.0),
		std::clamp(p_second_parameter, 0.0, 1.0),
		p_position,
	});
}

std::vector<SegmentHit> segment_intersections(const SegmentRef &p_first, const SegmentRef &p_second) {
	std::vector<SegmentHit> hits;
	const Vector2 first_delta = p_first.end - p_first.start;
	const Vector2 second_delta = p_second.end - p_second.start;
	const Vector2 offset = p_second.start - p_first.start;
	const double denominator = cross_product(first_delta, second_delta);
	const double scale = std::max(1.0, double(first_delta.length()) * double(second_delta.length()));
	if (std::abs(denominator) > 1e-10 * scale) {
		const double first_parameter = cross_product(offset, second_delta) / denominator;
		const double second_parameter = cross_product(offset, first_delta) / denominator;
		if (first_parameter >= -POSITION_EPSILON && first_parameter <= 1.0 + POSITION_EPSILON &&
				second_parameter >= -POSITION_EPSILON && second_parameter <= 1.0 + POSITION_EPSILON) {
			const Vector2 first_position = p_first.start.lerp(p_first.end, float(first_parameter));
			const Vector2 second_position = p_second.start.lerp(p_second.end, float(second_parameter));
			append_segment_hit(hits, first_parameter, second_parameter, (first_position + second_position) * 0.5f);
		}
		return hits;
	}

	if (std::abs(cross_product(offset, first_delta)) > 1e-10 * scale) {
		return hits;
	}
	const Vector2 first_points[2] = { p_first.start, p_first.end };
	for (int endpoint = 0; endpoint < 2; endpoint++) {
		const double second_parameter = segment_parameter(first_points[endpoint], p_second.start, p_second.end);
		if (second_parameter >= -POSITION_EPSILON && second_parameter <= 1.0 + POSITION_EPSILON) {
			const Vector2 projected = p_second.start.lerp(p_second.end, float(second_parameter));
			if (projected.distance_squared_to(first_points[endpoint]) <= POSITION_EPSILON * POSITION_EPSILON) {
				append_segment_hit(hits, double(endpoint), second_parameter, first_points[endpoint]);
			}
		}
	}
	const Vector2 second_points[2] = { p_second.start, p_second.end };
	for (int endpoint = 0; endpoint < 2; endpoint++) {
		const double first_parameter = segment_parameter(second_points[endpoint], p_first.start, p_first.end);
		if (first_parameter >= -POSITION_EPSILON && first_parameter <= 1.0 + POSITION_EPSILON) {
			const Vector2 projected = p_first.start.lerp(p_first.end, float(first_parameter));
			if (projected.distance_squared_to(second_points[endpoint]) <= POSITION_EPSILON * POSITION_EPSILON) {
				append_segment_hit(hits, first_parameter, double(endpoint), second_points[endpoint]);
			}
		}
	}
	return hits;
}

uint64_t cell_key(int p_x, int p_y) {
	return (uint64_t(uint32_t(p_x)) << 32) | uint32_t(p_y);
}

bool adjacent_segments(const SegmentRef &p_first, const SegmentRef &p_second, const std::vector<Stroke> &p_strokes) {
	if (p_first.stroke != p_second.stroke) {
		return false;
	}
	const size_t first = std::min(p_first.sample, p_second.sample);
	const size_t second = std::max(p_first.sample, p_second.sample);
	if (second == first + 1) {
		return true;
	}
	const std::vector<Sample> &samples = p_strokes[p_first.stroke].samples;
	return samples.size() >= 3 && samples.front().position == samples.back().position &&
			first == 0 && second + 2 == samples.size();
}

std::vector<std::vector<Insertion>> collect_intersections(const std::vector<Stroke> &p_strokes,
		bool p_include_self_intersections) {
	std::vector<std::vector<Insertion>> insertions(p_strokes.size());
	std::vector<SegmentRef> segments;
	std::unordered_map<uint64_t, std::vector<int>> cells;
	for (int stroke = 0; stroke < int(p_strokes.size()); stroke++) {
		const std::vector<Sample> &samples = p_strokes[stroke].samples;
		for (size_t sample = 0; sample + 1 < samples.size(); sample++) {
			const SegmentRef segment = { stroke, sample, samples[sample].position, samples[sample + 1].position };
			if (segment.start.distance_squared_to(segment.end) <= POSITION_EPSILON * POSITION_EPSILON) {
				continue;
			}
			const int minimum_x = int(std::floor(double(std::min(segment.start.x, segment.end.x)) / INTERSECTION_CELL_SIZE));
			const int maximum_x = int(std::floor(double(std::max(segment.start.x, segment.end.x)) / INTERSECTION_CELL_SIZE));
			const int minimum_y = int(std::floor(double(std::min(segment.start.y, segment.end.y)) / INTERSECTION_CELL_SIZE));
			const int maximum_y = int(std::floor(double(std::max(segment.start.y, segment.end.y)) / INTERSECTION_CELL_SIZE));
			std::unordered_set<int> candidates;
			for (int cell_y = minimum_y; cell_y <= maximum_y; cell_y++) {
				for (int cell_x = minimum_x; cell_x <= maximum_x; cell_x++) {
					const auto found = cells.find(cell_key(cell_x, cell_y));
					if (found != cells.end()) {
						candidates.insert(found->second.begin(), found->second.end());
					}
				}
			}
			for (int candidate : candidates) {
				const SegmentRef &other = segments[candidate];
				if (!p_include_self_intersections && segment.stroke == other.stroke) {
					continue;
				}
				if (adjacent_segments(segment, other, p_strokes)) {
					continue;
				}
				for (const SegmentHit &hit : segment_intersections(segment, other)) {
					insertions[segment.stroke].push_back({ segment.sample, hit.first_parameter, hit.position, false, false });
					insertions[other.stroke].push_back({ other.sample, hit.second_parameter, hit.position, false, false });
				}
			}
			const int segment_index = int(segments.size());
			segments.push_back(segment);
			for (int cell_y = minimum_y; cell_y <= maximum_y; cell_y++) {
				for (int cell_x = minimum_x; cell_x <= maximum_x; cell_x++) {
					cells[cell_key(cell_x, cell_y)].push_back(segment_index);
				}
			}
		}
	}
	return insertions;
}

std::vector<std::vector<Insertion>> collect_mandatory_insertions(const std::vector<Stroke> &p_strokes) {
	std::vector<std::vector<Insertion>> insertions(p_strokes.size());
	for (int stroke = 0; stroke < int(p_strokes.size()); stroke++) {
		const std::vector<Sample> &samples = p_strokes[stroke].samples;
		if (samples.size() < 2) {
			continue;
		}
		for (const Vector2 &point : p_strokes[stroke].mandatory_points) {
			if (!is_finite(point)) {
				continue;
			}
			double best_distance = std::numeric_limits<double>::infinity();
			size_t best_segment = 0;
			double best_parameter = 0.0;
			for (size_t segment = 0; segment + 1 < samples.size(); segment++) {
				const Vector2 start = samples[segment].position;
				const Vector2 end = samples[segment + 1].position;
				const double parameter = std::clamp(segment_parameter(point, start, end), 0.0, 1.0);
				const double distance = point.distance_to(start.lerp(end, float(parameter)));
				if (distance < best_distance) {
					best_distance = distance;
					best_segment = segment;
					best_parameter = parameter;
				}
			}
			// Reverse drawing already associated this key vertex with this path.
			// Projection determines only its order along the polyline; it is not a
			// geometric guess that needs a proximity threshold.
			const bool at_start = best_segment == 0 && best_parameter <= POSITION_EPSILON;
			const bool at_end = best_segment + 2 == samples.size() && best_parameter >= 1.0 - POSITION_EPSILON;
			if (at_start) {
				insertions[stroke].push_back({ 0, 0.0, point, true, false });
			} else if (at_end) {
				insertions[stroke].push_back({ samples.size() - 2, 1.0, point, false, true });
			} else {
				insertions[stroke].push_back({ best_segment, best_parameter, point, false, false });
			}
		}
	}
	return insertions;
}

void append_anchored_sample(AnchoredStroke &r_stroke, const Sample &p_sample, bool p_anchor) {
	if (!r_stroke.samples.empty() &&
			r_stroke.samples.back().position.distance_squared_to(p_sample.position) <= POSITION_EPSILON * POSITION_EPSILON) {
		if (p_anchor) {
			r_stroke.samples.back() = p_sample;
			r_stroke.anchors.back() = 1;
		}
		return;
	}
	r_stroke.samples.push_back(p_sample);
	r_stroke.anchors.push_back(p_anchor ? 1 : 0);
}

std::vector<AnchoredStroke> build_anchored_strokes(const std::vector<Stroke> &p_strokes,
		bool p_include_self_intersections) {
	std::vector<std::vector<Insertion>> insertions = collect_intersections(p_strokes, p_include_self_intersections);
	const std::vector<std::vector<Insertion>> mandatory_insertions = collect_mandatory_insertions(p_strokes);
	for (size_t stroke = 0; stroke < insertions.size(); stroke++) {
		insertions[stroke].insert(insertions[stroke].end(), mandatory_insertions[stroke].begin(), mandatory_insertions[stroke].end());
	}
	std::vector<AnchoredStroke> result(p_strokes.size());
	for (size_t stroke = 0; stroke < p_strokes.size(); stroke++) {
		const std::vector<Sample> &samples = p_strokes[stroke].samples;
		if (samples.empty()) {
			continue;
		}
		std::stable_sort(insertions[stroke].begin(), insertions[stroke].end(), [](const Insertion &p_a, const Insertion &p_b) {
			if (p_a.segment != p_b.segment) {
				return p_a.segment < p_b.segment;
			}
			return p_a.parameter < p_b.parameter;
		});
		AnchoredStroke &anchored = result[stroke];
		auto is_explicit_self_vertex = [&](const Vector2 &p_position) {
			for (const Vector2 &vertex : p_strokes[stroke].explicit_self_vertices) {
				if (vertex.distance_squared_to(p_position) <= POSITION_EPSILON * POSITION_EPSILON) {
					return true;
				}
			}
			return false;
		};
		anchored.samples.reserve(samples.size() + insertions[stroke].size());
		anchored.anchors.reserve(samples.size() + insertions[stroke].size());
		for (const Insertion &item : insertions[stroke]) {
			if (item.prepend) {
				const float weight = float(std::clamp(item.parameter, 0.0, 1.0));
				append_anchored_sample(anchored, { item.position, samples.front().radius * (1.0f - weight) + samples[1].radius * weight }, true);
			}
		}
		append_anchored_sample(anchored, samples.front(), true);
		size_t insertion = 0;
		for (size_t segment = 0; segment + 1 < samples.size(); segment++) {
			while (insertion < insertions[stroke].size() && insertions[stroke][insertion].segment == segment) {
				const Insertion &item = insertions[stroke][insertion++];
				if (item.prepend || item.append) {
					continue;
				}
				const float weight = float(std::clamp(item.parameter, 0.0, 1.0));
				Sample sample = {
					item.position,
					samples[segment].radius + (samples[segment + 1].radius - samples[segment].radius) * weight,
				};
				append_anchored_sample(anchored, sample, true);
			}
			append_anchored_sample(anchored, samples[segment + 1],
					segment + 2 == samples.size() || is_explicit_self_vertex(samples[segment + 1].position));
			for (const Insertion &item : insertions[stroke]) {
				if (item.append && item.segment == segment) {
					append_anchored_sample(anchored, { item.position, samples.back().radius }, true);
				}
			}
		}
	}
	return result;
}

std::vector<Sample> minimum_link_fit(const std::vector<Sample> &p_input, double p_max_error) {
	const std::vector<Sample> compact = compact_samples(p_input);
	const std::vector<double> compact_cumulative = cumulative_lengths(compact);
	if (compact.size() < 3 || shortcut_is_valid(compact, compact_cumulative, 0, compact.size() - 1, p_max_error)) {
		return compact.size() < 2 ? compact : std::vector<Sample>{ compact.front(), compact.back() };
	}

	const std::vector<Sample> samples = resample_evidence(compact);
	const size_t sample_count = samples.size();
	if (sample_count < 3) {
		return samples;
	}
	const std::vector<double> sample_cumulative = cumulative_lengths(samples);
	const std::vector<WeightedMoments> moment_prefix = build_moment_prefix(samples, p_max_error);
	const int infinity = std::numeric_limits<int>::max();
	std::vector<int> link_count(sample_count, infinity);
	std::vector<double> fitting_error(sample_count, std::numeric_limits<double>::infinity());
	std::vector<int> predecessor(sample_count, -1);
	link_count[0] = 0;
	fitting_error[0] = 0.0;

	// Build the shortcut DAG implicitly. Each evidence disk contributes an
	// allowed direction interval, while radius samples constrain the linear
	// radius slope. Empty intersections cannot become feasible farther ahead.
	for (size_t first = 0; first + 1 < sample_count; first++) {
		if (link_count[first] == infinity) {
			continue;
		}
		bool has_interval = false;
		double interval_low = 0.0;
		double interval_high = 0.0;
		double radius_slope_low = -std::numeric_limits<double>::infinity();
		double radius_slope_high = std::numeric_limits<double>::infinity();
		double minimum_endpoint_distance = 0.0;
		for (size_t last = first + 1; last < sample_count; last++) {
			const Vector2 offset = samples[last].position - samples[first].position;
			const double distance = offset.length();
			const double arc_distance = sample_cumulative[last] - sample_cumulative[first];
			const double radius_delta = double(samples[last].radius) - double(samples[first].radius);
			const double radius_slope = arc_distance > POSITION_EPSILON ? radius_delta / arc_distance : 0.0;
			const bool radius_allowed = radius_slope >= radius_slope_low - POSITION_EPSILON &&
					radius_slope <= radius_slope_high + POSITION_EPSILON;
			bool corridor_empty = false;
			if (distance > POSITION_EPSILON) {
				double angle = std::atan2(double(offset.y), double(offset.x));
				if (has_interval) {
					angle = unwrap_near(angle, 0.5 * (interval_low + interval_high));
				}
				const bool direction_allowed = !has_interval ||
						(angle >= interval_low - POSITION_EPSILON && angle <= interval_high + POSITION_EPSILON);
				if (direction_allowed && radius_allowed && distance + POSITION_EPSILON >= minimum_endpoint_distance) {
					const int candidate_links = link_count[first] + 1;
					if (candidate_links <= link_count[last]) {
						const double candidate_error = fitting_error[first] + shortcut_error(samples, moment_prefix, first, last);
						const bool fewer_links = candidate_links < link_count[last];
						const bool lower_error = candidate_links == link_count[last] &&
								candidate_error < fitting_error[last] - POSITION_EPSILON;
						const bool stable_tie = candidate_links == link_count[last] &&
								std::abs(candidate_error - fitting_error[last]) <= POSITION_EPSILON &&
								(predecessor[last] < 0 || int(first) < predecessor[last]);
						if (fewer_links || lower_error || stable_tie) {
							link_count[last] = candidate_links;
							fitting_error[last] = candidate_error;
							predecessor[last] = int(first);
						}
					}
				}

				const double tolerance = sample_tolerance(samples[last], p_max_error);
				minimum_endpoint_distance = std::max(minimum_endpoint_distance, distance - tolerance);
				if (distance > tolerance) {
					const double half_width = std::asin(std::clamp(tolerance / distance, 0.0, 1.0));
					if (!has_interval) {
						interval_low = angle - half_width;
						interval_high = angle + half_width;
						has_interval = true;
					} else {
						interval_low = std::max(interval_low, angle - half_width);
						interval_high = std::min(interval_high, angle + half_width);
						if (interval_low > interval_high + POSITION_EPSILON) {
							corridor_empty = true;
						}
					}
				}
			}
			if (arc_distance > POSITION_EPSILON) {
				radius_slope_low = std::max(radius_slope_low, (radius_delta - RADIUS_MAX_ERROR) / arc_distance);
				radius_slope_high = std::min(radius_slope_high, (radius_delta + RADIUS_MAX_ERROR) / arc_distance);
				corridor_empty |= radius_slope_low > radius_slope_high + POSITION_EPSILON;
			}
			if (corridor_empty) {
				break;
			}
		}
	}

	if (predecessor.back() < 0) {
		return samples;
	}
	std::vector<size_t> reverse_indices;
	for (int sample = int(sample_count) - 1; sample >= 0; sample = predecessor[sample]) {
		reverse_indices.push_back(size_t(sample));
		if (sample == 0) {
			break;
		}
		if (predecessor[sample] < 0) {
			return samples;
		}
	}
	std::reverse(reverse_indices.begin(), reverse_indices.end());
	std::vector<Sample> result;
	result.reserve(reverse_indices.size());
	for (size_t sample : reverse_indices) {
		result.push_back(samples[sample]);
	}
	return result;
}

void append_output_sample(std::vector<Sample> &r_samples, const Sample &p_sample) {
	if (!r_samples.empty() &&
			r_samples.back().position.distance_squared_to(p_sample.position) <= POSITION_EPSILON * POSITION_EPSILON) {
		r_samples.back() = p_sample;
		return;
	}
	r_samples.push_back(p_sample);
}

std::vector<Sample> fit_between_anchors(const AnchoredStroke &p_stroke, double p_max_error) {
	if (p_stroke.samples.size() < 2 || p_stroke.samples.size() != p_stroke.anchors.size()) {
		return p_stroke.samples;
	}
	std::vector<Sample> result;
	size_t first = 0;
	while (first + 1 < p_stroke.samples.size()) {
		size_t last = first + 1;
		while (last + 1 < p_stroke.samples.size() && !p_stroke.anchors[last]) {
			last++;
		}
		const std::vector<Sample> interval(p_stroke.samples.begin() + std::ptrdiff_t(first),
				p_stroke.samples.begin() + std::ptrdiff_t(last + 1));
		for (const Sample &sample : minimum_link_fit(interval, p_max_error)) {
			append_output_sample(result, sample);
		}
		first = last;
	}
	return result;
}

std::vector<Sample> enforce_max_segment_length(const std::vector<Sample> &p_samples, double p_max_length) {
	if (p_samples.size() < 2 || p_max_length <= POSITION_EPSILON) {
		return p_samples;
	}
	std::vector<Sample> result;
	result.reserve(p_samples.size());
	result.push_back(p_samples.front());
	for (size_t segment = 0; segment + 1 < p_samples.size(); segment++) {
		const double length = p_samples[segment].position.distance_to(p_samples[segment + 1].position);
		const int subdivisions = std::max(1, int(std::ceil(length / p_max_length)));
		for (int subdivision = 1; subdivision <= subdivisions; subdivision++) {
			const float weight = float(subdivision) / float(subdivisions);
			append_output_sample(result, {
				p_samples[segment].position.lerp(p_samples[segment + 1].position, weight),
				p_samples[segment].radius + (p_samples[segment + 1].radius - p_samples[segment].radius) * weight,
			});
		}
	}
	return result;
}

void fair_anchored_stroke(AnchoredStroke &r_stroke, double p_max_error) {
	if (r_stroke.samples.size() < 3 || r_stroke.samples.size() != r_stroke.anchors.size()) {
		return;
	}
	const std::vector<Sample> evidence = r_stroke.samples;
	const std::vector<double> cumulative = cumulative_lengths(evidence);
	std::vector<size_t> previous_anchor(evidence.size(), 0);
	std::vector<size_t> next_anchor(evidence.size(), evidence.size() - 1);
	size_t anchor = 0;
	for (size_t sample = 0; sample < evidence.size(); sample++) {
		if (r_stroke.anchors[sample]) {
			anchor = sample;
		}
		previous_anchor[sample] = anchor;
	}
	anchor = evidence.size() - 1;
	for (size_t sample = evidence.size(); sample-- > 0;) {
		if (r_stroke.anchors[sample]) {
			anchor = sample;
		}
		next_anchor[sample] = anchor;
	}

	for (size_t center = 1; center + 1 < evidence.size(); center++) {
		if (r_stroke.anchors[center]) {
			continue;
		}
		const double window = std::clamp(2.0 * double(std::max(0.5f, evidence[center].radius)), 2.0, 8.0);
		const size_t first = size_t(std::lower_bound(cumulative.begin() + std::ptrdiff_t(previous_anchor[center]),
				cumulative.begin() + std::ptrdiff_t(center + 1), cumulative[center] - window) - cumulative.begin());
		const size_t last = size_t(std::upper_bound(cumulative.begin() + std::ptrdiff_t(center),
				cumulative.begin() + std::ptrdiff_t(next_anchor[center] + 1), cumulative[center] + window) - cumulative.begin());
		double weight_sum = 0.0;
		double weighted_arc = 0.0;
		double weighted_arc_squared = 0.0;
		Vector2 weighted_position;
		Vector2 weighted_arc_position;
		const double denominator = 2.0 * window * window;
		for (size_t sample = first; sample < last; sample++) {
			const double arc = cumulative[sample] - cumulative[center];
			const double weight = std::exp(-(arc * arc) / denominator);
			weight_sum += weight;
			weighted_arc += weight * arc;
			weighted_arc_squared += weight * arc * arc;
			weighted_position += evidence[sample].position * float(weight);
			weighted_arc_position += evidence[sample].position * float(weight * arc);
		}
		const double determinant = weight_sum * weighted_arc_squared - weighted_arc * weighted_arc;
		if (determinant <= POSITION_EPSILON) {
			continue;
		}
		const Vector2 fitted = (weighted_position * float(weighted_arc_squared) -
				weighted_arc_position * float(weighted_arc)) / float(determinant);
		Vector2 displacement = fitted - evidence[center].position;
		const double limit = sample_tolerance(evidence[center], p_max_error);
		const double displacement_length = displacement.length();
		if (displacement_length > limit && displacement_length > POSITION_EPSILON) {
			displacement *= float(limit / displacement_length);
		}
		r_stroke.samples[center].position = evidence[center].position + displacement;
	}
}

double point_stroke_distance(const Vector2 &p_point, const Stroke &p_stroke) {
	double closest = std::numeric_limits<double>::infinity();
	for (size_t segment = 0; segment + 1 < p_stroke.samples.size(); segment++) {
		const Vector2 start = p_stroke.samples[segment].position;
		const Vector2 end = p_stroke.samples[segment + 1].position;
		const double parameter = std::clamp(segment_parameter(p_point, start, end), 0.0, 1.0);
		closest = std::min(closest, double(p_point.distance_to(start.lerp(end, float(parameter)))));
	}
	return closest;
}

void remove_redundant_micro_strokes(std::vector<Stroke> &r_strokes) {
	constexpr double MAXIMUM_LENGTH = 1.5;
	constexpr double MAXIMUM_REDUNDANCY_DISTANCE = 1.0;
	std::vector<uint8_t> remove(r_strokes.size(), 0);
	for (size_t stroke = 0; stroke < r_strokes.size(); stroke++) {
		const Stroke &candidate = r_strokes[stroke];
		if (candidate.samples.size() < 2 || !candidate.explicit_self_vertices.empty()) {
			continue;
		}
		const std::vector<double> cumulative = cumulative_lengths(candidate.samples);
		if (cumulative.back() > MAXIMUM_LENGTH) {
			continue;
		}
		for (size_t owner = 0; owner < r_strokes.size(); owner++) {
			if (owner == stroke || r_strokes[owner].samples.size() < 2) {
				continue;
			}
			const std::vector<double> owner_cumulative = cumulative_lengths(r_strokes[owner].samples);
			if (owner_cumulative.back() <= cumulative.back() + POSITION_EPSILON) {
				continue;
			}
			bool covered = true;
			for (const Vector2 &mandatory : candidate.mandatory_points) {
				if (point_stroke_distance(mandatory, r_strokes[owner]) > POSITION_EPSILON) {
					covered = false;
					break;
				}
			}
			for (size_t sample = 0; sample < candidate.samples.size(); sample++) {
				if (point_stroke_distance(candidate.samples[sample].position, r_strokes[owner]) >
						MAXIMUM_REDUNDANCY_DISTANCE) {
					covered = false;
					break;
				}
				if (sample + 1 < candidate.samples.size()) {
					const Vector2 midpoint = candidate.samples[sample].position.lerp(
							candidate.samples[sample + 1].position, 0.5f);
					if (point_stroke_distance(midpoint, r_strokes[owner]) > MAXIMUM_REDUNDANCY_DISTANCE) {
						covered = false;
						break;
					}
				}
			}
			if (covered) {
				remove[stroke] = 1;
				break;
			}
		}
	}
	std::vector<Stroke> retained;
	retained.reserve(r_strokes.size());
	for (size_t stroke = 0; stroke < r_strokes.size(); stroke++) {
		if (!remove[stroke]) {
			retained.push_back(std::move(r_strokes[stroke]));
		}
	}
	r_strokes = std::move(retained);
}

} // namespace

void fit_stroke_polylines(std::vector<Stroke> &r_strokes, const Params &p_params) {
	const double maximum_error = is_finite(p_params.polyline_max_error) ?
			std::max(POSITION_EPSILON, double(p_params.polyline_max_error)) : 0.35;
	const double maximum_segment_length = is_finite(p_params.polyline_max_segment_length) ?
			std::max(0.0, double(p_params.polyline_max_segment_length)) : 8.0;
	for (Stroke &stroke : r_strokes) {
		stroke.samples = compact_samples(stroke.samples);
	}
	remove_redundant_micro_strokes(r_strokes);
	// Same-stroke crossings in the raw medial path are not source topology:
	// fitting may remove them unless reverse drawing supplied a mandatory node.
	std::vector<AnchoredStroke> anchored = build_anchored_strokes(r_strokes, false);
	for (AnchoredStroke &stroke : anchored) {
		fair_anchored_stroke(stroke, maximum_error);
	}
	for (size_t stroke = 0; stroke < r_strokes.size(); stroke++) {
		std::vector<Sample> fitted = fit_between_anchors(anchored[stroke], maximum_error);
		if (fitted.size() >= 2) {
			r_strokes[stroke].samples = std::move(fitted);
		}
	}

	// Splitting any intersections introduced by fitting makes the returned
	// polylines directly consumable by an arrangement builder.
	const std::vector<AnchoredStroke> final_intersections = build_anchored_strokes(r_strokes, true);
	for (size_t stroke = 0; stroke < r_strokes.size(); stroke++) {
		if (final_intersections[stroke].samples.size() >= 2) {
			r_strokes[stroke].samples = enforce_max_segment_length(
					final_intersections[stroke].samples, maximum_segment_length);
		}
	}
	remove_redundant_micro_strokes(r_strokes);
}

} // namespace topology_centerline
