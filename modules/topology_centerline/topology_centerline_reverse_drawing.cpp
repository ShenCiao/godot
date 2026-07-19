/**************************************************************************/
/*  topology_centerline_reverse_drawing.cpp                               */
/**************************************************************************/

#include "topology_centerline_reverse_drawing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace topology_centerline {
namespace {

constexpr float POSITION_EPSILON = 1e-5f;
constexpr float MINIMUM_RADIUS = 0.05f;
constexpr float MINIMUM_AMBIGUITY_STEP = 0.1f;
constexpr int MAXIMUM_AMBIGUITY_STEPS = 8192;
constexpr int AMBIGUITY_REFINEMENT_STEPS = 14;
constexpr int MAXIMUM_CURVE_SUBDIVISIONS = 8192;
constexpr double MAXIMUM_LATENT_JUNCTION_RADII = 6.0;
constexpr double PI = 3.14159265358979323846;
constexpr double CONTINUATION_ANGLE_THRESHOLD_DEGREES = 140.0;

enum class BaseEnd : uint8_t {
	FRONT,
	BACK,
};

struct RuntimeBase {
	const BaseCenterline *source = nullptr;
	int input_index = -1;
	std::vector<Sample> samples;
	std::vector<float> cumulative;
	float length = 0.0f;
	int front_port = -1;
	int back_port = -1;
	bool core_confined = false;
};

struct PortState {
	Sample sample;
	float distance_from_end = 0.0f;
	bool capped = false;
};

struct PortCircleCache {
	std::vector<Sample> samples;
	std::vector<double> prefix_maximum_radius_squared;
	float radial_extent = 0.0f;
};

struct Port {
	int base_index = -1;
	BaseEnd end = BaseEnd::FRONT;
	int junction = -1;
	float max_arc = 0.0f;
	float distance_from_end = 0.0f;
	float trend_margin = 0.0f;
	float trim_s = 0.0f;
	Sample sample;
	Vector2 outward_tangent;
	std::vector<float> knots;
};

struct JunctionWork {
	int node = -1;
	Sample junction_sample;
	std::vector<int> ports;
	float consensus_radius = 0.0f;
	bool disks_separated = false;
	bool geometry_reconstructed = false;
};

struct CurveGeometry {
	std::vector<Sample> samples;
	std::vector<uint8_t> split_points;
	std::vector<int> shared_nodes;
};

struct JunctionConnection {
	int port_a = -1;
	int port_b = -1;
	int shared_node_a = -1;
	int shared_node_b = -1;
	bool is_candidate = false;
};

struct OutputEdge {
	CurveGeometry curve;
	int node_a = -1;
	int node_b = -1;
};

struct PendingContinuation {
	int node = -1;
	int edge_a = -1;
	int edge_b = -1;
};

struct HermiteCandidate {
	int port_a = -1;
	int port_b = -1;
	double max_alpha = std::numeric_limits<double>::infinity();
	double continuity_angle_degrees = 0.0;
	bool strong_continuation = false;
	Sample endpoint_a;
	Sample endpoint_b;
	Vector2 outward_tangent_a;
	Vector2 outward_tangent_b;
	CurveGeometry curve;
};

struct EndpointJet {
	Sample origin;
	Vector2 linear;
	Vector2 quadratic;
	double support = 0.0;
	bool valid = false;
};

struct CurveAttachment {
	bool found = false;
	int curve = -1;
	size_t segment = 0;
	double source_distance = 0.0;
	double curve_t = 0.0;
	double separation = std::numeric_limits<double>::infinity();
};

struct ChordContact {
	bool found = false;
	double source_t = 0.0;
	double target_t = 0.0;
	double separation = std::numeric_limits<double>::infinity();
};

struct InsertedHit {
	Sample sample;
	int shared_node = -1;
};

bool is_finite(float p_value) {
	return std::isfinite(double(p_value));
}

bool is_finite(const Vector2 &p_value) {
	return is_finite(p_value.x) && is_finite(p_value.y);
}

float clamp_value(float p_value, float p_minimum, float p_maximum) {
	return std::min(p_maximum, std::max(p_minimum, p_value));
}

double clamp_value(double p_value, double p_minimum, double p_maximum) {
	return std::min(p_maximum, std::max(p_minimum, p_value));
}

float sanitize_radius(float p_radius, const Params &p_params) {
	float radius = is_finite(p_radius) ? p_radius : 0.5f;
	radius = std::max(MINIMUM_RADIUS, radius);
	if (is_finite(p_params.max_thickness) && p_params.max_thickness > 0.0f) {
		radius = std::min(radius, p_params.max_thickness);
	}
	return std::max(MINIMUM_RADIUS, radius);
}

bool same_position(const Vector2 &p_a, const Vector2 &p_b) {
	return p_a.distance_squared_to(p_b) <= POSITION_EPSILON * POSITION_EPSILON;
}

Sample interpolate_sample(const Sample &p_a, const Sample &p_b, float p_weight) {
	const float weight = clamp_value(p_weight, 0.0f, 1.0f);
	return { p_a.position.lerp(p_b.position, weight), p_a.radius + (p_b.radius - p_a.radius) * weight };
}

void append_distinct(std::vector<Sample> &r_samples, const Sample &p_sample) {
	if (r_samples.empty() || !same_position(r_samples.back().position, p_sample.position)) {
		r_samples.push_back(p_sample);
	} else {
		r_samples.back() = p_sample;
	}
}

void compact_samples(std::vector<Sample> &r_samples) {
	std::vector<Sample> compact;
	compact.reserve(r_samples.size());
	for (const Sample &sample : r_samples) {
		append_distinct(compact, sample);
	}
	r_samples = std::move(compact);
}

void rebuild_cumulative(RuntimeBase &r_base) {
	r_base.cumulative.assign(r_base.samples.size(), 0.0f);
	for (size_t i = 1; i < r_base.samples.size(); i++) {
		r_base.cumulative[i] = r_base.cumulative[i - 1] +
				float(r_base.samples[i - 1].position.distance_to(r_base.samples[i].position));
	}
	r_base.length = r_base.cumulative.empty() ? 0.0f : r_base.cumulative.back();
}

bool valid_node(const PipelineResult &p_pipeline, int p_node) {
	return p_node >= 0 && p_node < int(p_pipeline.nodes.size());
}

bool is_junction(const PipelineResult &p_pipeline, int p_node) {
	return valid_node(p_pipeline, p_node) && p_pipeline.nodes[p_node].degree >= 3;
}

RuntimeBase prepare_base(const PipelineResult &p_pipeline, const BaseCenterline &p_source, int p_input_index, const Params &p_params) {
	RuntimeBase base;
	base.source = &p_source;
	base.input_index = p_input_index;
	base.samples.reserve(p_source.samples.size() + 1);
	for (const Sample &source_sample : p_source.samples) {
		if (!is_finite(source_sample.position)) {
			continue;
		}
		append_distinct(base.samples, { source_sample.position, sanitize_radius(source_sample.radius, p_params) });
	}
	if (base.samples.size() < 2) {
		return base;
	}

	if (p_source.closed && !same_position(base.samples.front().position, base.samples.back().position)) {
		base.samples.push_back(base.samples.front());
	}
	if (valid_node(p_pipeline, p_source.node_a) && is_finite(p_pipeline.nodes[p_source.node_a].position)) {
		base.samples.front().position = p_pipeline.nodes[p_source.node_a].position;
	}
	if (valid_node(p_pipeline, p_source.node_b) && is_finite(p_pipeline.nodes[p_source.node_b].position)) {
		base.samples.back().position = p_pipeline.nodes[p_source.node_b].position;
	}
	compact_samples(base.samples);
	if (p_source.closed && base.samples.size() >= 2 && !same_position(base.samples.front().position, base.samples.back().position)) {
		base.samples.push_back(base.samples.front());
	}
	rebuild_cumulative(base);
	if (!is_finite(base.length)) {
		base.samples.clear();
		base.cumulative.clear();
		base.length = 0.0f;
	}
	return base;
}

Sample sample_base_at(const RuntimeBase &p_base, float p_s) {
	if (p_base.samples.empty()) {
		return {};
	}
	if (p_s <= 0.0f || p_base.cumulative.size() < 2) {
		return p_base.samples.front();
	}
	if (p_s >= p_base.length) {
		return p_base.samples.back();
	}
	const auto found = std::lower_bound(p_base.cumulative.begin() + 1, p_base.cumulative.end(), p_s);
	const size_t second = size_t(found - p_base.cumulative.begin());
	if (std::abs(double(*found - p_s)) <= POSITION_EPSILON) {
		return p_base.samples[second];
	}
	const size_t first = second - 1;
	const float segment_length = p_base.cumulative[second] - p_base.cumulative[first];
	if (segment_length <= POSITION_EPSILON) {
		return p_base.samples[first];
	}
	return interpolate_sample(p_base.samples[first], p_base.samples[second], (p_s - p_base.cumulative[first]) / segment_length);
}

float global_s_from_end(const RuntimeBase &p_base, BaseEnd p_end, float p_distance) {
	const float distance = clamp_value(p_distance, 0.0f, p_base.length);
	return p_end == BaseEnd::FRONT ? distance : p_base.length - distance;
}

Sample sample_from_end(const RuntimeBase &p_base, BaseEnd p_end, float p_distance) {
	return sample_base_at(p_base, global_s_from_end(p_base, p_end, p_distance));
}

std::tuple<int, int, int> port_key(const Port &p_port, const std::vector<RuntimeBase> &p_bases) {
	const RuntimeBase &base = p_bases[p_port.base_index];
	const int id = base.source != nullptr ? base.source->id : -1;
	return { id, p_port.end == BaseEnd::FRONT ? 0 : 1, base.input_index };
}

void build_port_knots(Port &r_port, const RuntimeBase &p_base) {
	r_port.knots.clear();
	r_port.knots.push_back(0.0f);
	if (r_port.end == BaseEnd::FRONT) {
		for (size_t i = 1; i < p_base.cumulative.size(); i++) {
			if (p_base.cumulative[i] >= r_port.max_arc - POSITION_EPSILON) {
				break;
			}
			r_port.knots.push_back(p_base.cumulative[i]);
		}
	} else {
		for (size_t i = p_base.cumulative.size() - 1; i > 0; i--) {
			const float distance = p_base.length - p_base.cumulative[i - 1];
			if (distance >= r_port.max_arc - POSITION_EPSILON) {
				break;
			}
			r_port.knots.push_back(distance);
		}
	}
	if (r_port.knots.back() < r_port.max_arc - POSITION_EPSILON) {
		r_port.knots.push_back(r_port.max_arc);
	} else {
		r_port.knots.back() = r_port.max_arc;
	}
}

PortState port_at_circle(const Port &p_port, const RuntimeBase &p_base, const Vector2 &p_center, float p_circle_radius) {
	PortState result;
	result.sample = sample_from_end(p_base, p_port.end, 0.0f);
	if (p_port.max_arc <= POSITION_EPSILON || p_circle_radius <= 0.0f) {
		result.capped = p_port.max_arc <= POSITION_EPSILON;
		return result;
	}

	Sample previous = result.sample;
	float previous_distance = 0.0f;
	const double radius_squared = double(p_circle_radius) * double(p_circle_radius);
	if (double(previous.position.distance_squared_to(p_center)) >= radius_squared) {
		return result;
	}
	for (size_t knot_index = 1; knot_index < p_port.knots.size(); knot_index++) {
		const float next_distance = p_port.knots[knot_index];
		const Sample next = sample_from_end(p_base, p_port.end, next_distance);
		if (double(next.position.distance_squared_to(p_center)) + 1e-12 >= radius_squared) {
			const Vector2 delta = next.position - previous.position;
			const Vector2 offset = previous.position - p_center;
			const double a = double(delta.dot(delta));
			const double b = 2.0 * double(offset.dot(delta));
			const double c = double(offset.dot(offset)) - radius_squared;
			double weight = 1.0;
			if (a > 1e-20) {
				const double discriminant = std::max(0.0, b * b - 4.0 * a * c);
				const double root = (-b + std::sqrt(discriminant)) / (2.0 * a);
				weight = clamp_value(root, 0.0, 1.0);
			}
			result.sample = interpolate_sample(previous, next, float(weight));
			result.distance_from_end = previous_distance + (next_distance - previous_distance) * float(weight);
			return result;
		}
		previous = next;
		previous_distance = next_distance;
	}

	result.sample = sample_from_end(p_base, p_port.end, p_port.max_arc);
	result.distance_from_end = p_port.max_arc;
	result.capped = true;
	return result;
}

PortCircleCache build_port_circle_cache(const Port &p_port, const RuntimeBase &p_base, const Vector2 &p_center) {
	PortCircleCache cache;
	cache.samples.reserve(p_port.knots.size());
	cache.prefix_maximum_radius_squared.reserve(p_port.knots.size());
	double prefix_maximum = 0.0;
	for (float distance : p_port.knots) {
		const Sample sample = sample_from_end(p_base, p_port.end, distance);
		cache.samples.push_back(sample);
		const double radius_squared = sample.position.distance_squared_to(p_center);
		prefix_maximum = std::max(prefix_maximum, radius_squared);
		cache.prefix_maximum_radius_squared.push_back(prefix_maximum);
		const float radius = float(sample.position.distance_to(p_center));
		if (is_finite(radius)) {
			cache.radial_extent = std::max(cache.radial_extent, radius);
		}
	}
	return cache;
}

PortState port_at_circle_cached(const Port &p_port, const PortCircleCache &p_cache,
		const Vector2 &p_center, float p_circle_radius) {
	PortState result;
	if (p_cache.samples.empty()) {
		return result;
	}
	result.sample = p_cache.samples.front();
	if (p_port.max_arc <= POSITION_EPSILON || p_circle_radius <= 0.0f) {
		result.capped = p_port.max_arc <= POSITION_EPSILON;
		return result;
	}
	const double radius_squared = double(p_circle_radius) * double(p_circle_radius);
	if (double(result.sample.position.distance_squared_to(p_center)) >= radius_squared) {
		return result;
	}
	const auto found = std::lower_bound(
			p_cache.prefix_maximum_radius_squared.begin() + 1,
			p_cache.prefix_maximum_radius_squared.end(), radius_squared);
	if (found == p_cache.prefix_maximum_radius_squared.end()) {
		result.sample = p_cache.samples.back();
		result.distance_from_end = p_port.max_arc;
		result.capped = true;
		return result;
	}
	const size_t next_index = size_t(found - p_cache.prefix_maximum_radius_squared.begin());
	const Sample &previous = p_cache.samples[next_index - 1];
	const Sample &next = p_cache.samples[next_index];
	const float previous_distance = p_port.knots[next_index - 1];
	const float next_distance = p_port.knots[next_index];
	const Vector2 delta = next.position - previous.position;
	const Vector2 offset = previous.position - p_center;
	const double a = double(delta.dot(delta));
	const double b = 2.0 * double(offset.dot(delta));
	const double c = double(offset.dot(offset)) - radius_squared;
	double weight = 1.0;
	if (a > 1e-20) {
		const double discriminant = std::max(0.0, b * b - 4.0 * a * c);
		const double root = (-b + std::sqrt(discriminant)) / (2.0 * a);
		weight = clamp_value(root, 0.0, 1.0);
	}
	result.sample = interpolate_sample(previous, next, float(weight));
	result.distance_from_end = previous_distance + (next_distance - previous_distance) * float(weight);
	return result;
}

float port_radial_extent(const Port &p_port, const RuntimeBase &p_base, const Vector2 &p_center) {
	float extent = 0.0f;
	for (float distance : p_port.knots) {
		const float radial_distance = float(sample_from_end(p_base, p_port.end, distance).position.distance_to(p_center));
		if (is_finite(radial_distance)) {
			extent = std::max(extent, radial_distance);
		}
	}
	return extent;
}

bool base_has_independent_disk_support(const RuntimeBase &p_base, const Sample &p_junction) {
	for (const Sample &sample : p_base.samples) {
		const double separating_distance = double(p_junction.radius) + double(sample.radius);
		if (sample.position.distance_squared_to(p_junction.position) >
				separating_distance * separating_distance + POSITION_EPSILON) {
			return true;
		}
	}
	return false;
}

bool disks_are_separated(const std::vector<PortState> &p_states) {
	for (const PortState &state : p_states) {
		if (state.capped) {
			return false;
		}
	}
	for (size_t i = 0; i < p_states.size(); i++) {
		for (size_t j = i + 1; j < p_states.size(); j++) {
			const float required = p_states[i].sample.radius + p_states[j].sample.radius;
			if (p_states[i].sample.position.distance_to(p_states[j].sample.position) <= required + POSITION_EPSILON) {
				return false;
			}
		}
	}
	return true;
}

Vector2 base_tangent_at(const RuntimeBase &p_base, float p_s) {
	if (p_base.samples.size() < 2 || p_base.cumulative.size() != p_base.samples.size()) {
		return {};
	}
	const float s = clamp_value(p_s, 0.0f, p_base.length);
	const auto found = std::lower_bound(p_base.cumulative.begin() + 1, p_base.cumulative.end(), s);
	size_t second = size_t(found - p_base.cumulative.begin());
	second = std::min(second, p_base.samples.size() - 1);
	Vector2 tangent = p_base.samples[second].position - p_base.samples[second - 1].position;
	if (std::abs(double(p_base.cumulative[second] - s)) <= POSITION_EPSILON && second + 1 < p_base.samples.size()) {
		const Vector2 outgoing = p_base.samples[second + 1].position - p_base.samples[second].position;
		if (tangent.length_squared() > POSITION_EPSILON * POSITION_EPSILON &&
				outgoing.length_squared() > POSITION_EPSILON * POSITION_EPSILON) {
			const Vector2 averaged = tangent.normalized() + outgoing.normalized();
			if (averaged.length_squared() > POSITION_EPSILON * POSITION_EPSILON) {
				tangent = averaged;
			}
		} else if (outgoing.length_squared() > POSITION_EPSILON * POSITION_EPSILON) {
			tangent = outgoing;
		}
	}
	if (!is_finite(tangent) || tangent.length_squared() <= POSITION_EPSILON * POSITION_EPSILON) {
		return {};
	}
	return tangent.normalized();
}

Vector2 compute_outward_tangent(const Port &p_port, const RuntimeBase &p_base, const Vector2 &p_center) {
	Vector2 tangent = base_tangent_at(p_base, p_port.trim_s);
	if (p_port.end == BaseEnd::BACK) {
		tangent = -tangent;
	}
	if (tangent.length_squared() <= POSITION_EPSILON * POSITION_EPSILON) {
		tangent = p_port.sample.position - p_center;
	}
	if (!is_finite(tangent) || tangent.length_squared() <= POSITION_EPSILON * POSITION_EPSILON) {
		return {};
	}
	return tangent.normalized();
}

void grow_ambiguous_region(JunctionWork &r_junction, std::vector<Port> &r_ports, const std::vector<RuntimeBase> &p_bases,
		const Params &p_params) {
	if (r_junction.ports.size() < 2) {
		return;
	}

	std::vector<PortCircleCache> circle_caches;
	circle_caches.reserve(r_junction.ports.size());
	float maximum_radius = 0.0f;
	float minimum_radius = std::numeric_limits<float>::infinity();
	for (int port_index : r_junction.ports) {
		const Port &port = r_ports[port_index];
		circle_caches.push_back(build_port_circle_cache(
				port, p_bases[port.base_index], r_junction.junction_sample.position));
		maximum_radius = std::max(maximum_radius, circle_caches.back().radial_extent);
		minimum_radius = std::min(minimum_radius, circle_caches.back().radial_extent);
	}
	if (maximum_radius <= POSITION_EPSILON) {
		return;
	}

	const float requested_step = is_finite(p_params.motion_step) ? std::abs(p_params.motion_step) : MINIMUM_AMBIGUITY_STEP;
	float step = std::max(MINIMUM_AMBIGUITY_STEP, requested_step);
	step = std::max(step, maximum_radius / float(MAXIMUM_AMBIGUITY_STEPS));
	auto evaluate_cached = [&](float p_radius, std::vector<PortState> &r_states) {
		r_states.resize(r_junction.ports.size());
		for (size_t port_offset = 0; port_offset < r_junction.ports.size(); port_offset++) {
			const Port &port = r_ports[r_junction.ports[port_offset]];
			r_states[port_offset] = port_at_circle_cached(
					port, circle_caches[port_offset], r_junction.junction_sample.position, p_radius);
		}
	};
	float previous_radius = 0.0f;
	float selected_radius = maximum_radius;
	std::vector<PortState> selected_states;
	std::vector<PortState> states;
	std::vector<PortState> middle_states;
	evaluate_cached(maximum_radius, selected_states);

	for (float radius = std::min(step, maximum_radius);; radius = std::min(maximum_radius, radius + step)) {
		evaluate_cached(radius, states);
		if (disks_are_separated(states)) {
			float low = previous_radius;
			float high = radius;
			for (int iteration = 0; iteration < AMBIGUITY_REFINEMENT_STEPS; iteration++) {
				const float middle = (low + high) * 0.5f;
				evaluate_cached(middle, middle_states);
				if (disks_are_separated(middle_states)) {
					high = middle;
					states = middle_states;
				} else {
					low = middle;
				}
			}
			selected_radius = high;
			evaluate_cached(selected_radius, selected_states);
			r_junction.disks_separated = true;
			break;
		}
		previous_radius = radius;
		if (radius >= maximum_radius - POSITION_EPSILON) {
			break;
		}
	}

	r_junction.consensus_radius = r_junction.disks_separated ? selected_radius : minimum_radius;
	for (size_t i = 0; i < r_junction.ports.size(); i++) {
		Port &port = r_ports[r_junction.ports[i]];
		port.sample = selected_states[i].sample;
		port.distance_from_end = selected_states[i].distance_from_end;
		const RuntimeBase &base = p_bases[port.base_index];
		port.trim_s = global_s_from_end(base, port.end, port.distance_from_end);
		port.outward_tangent = compute_outward_tangent(port, base, r_junction.junction_sample.position);
	}
}

template <typename Sampler>
EndpointJet fit_endpoint_jet_window(const Sample &p_origin, double p_before, double p_after, const Sampler &p_sample) {
	EndpointJet jet;
	jet.origin = p_origin;
	const double maximum_span = MAXIMUM_LATENT_JUNCTION_RADII *
			std::max(double(MINIMUM_RADIUS), double(p_origin.radius));
	const double before = std::min(std::max(0.0, p_before), maximum_span);
	const double after = std::min(std::max(0.0, p_after), maximum_span - before);
	const double span = before + after;
	jet.support = std::max(before, after);
	if (!is_finite(float(jet.support)) || jet.support <= POSITION_EPSILON || span <= POSITION_EPSILON) {
		return jet;
	}

	const int sample_count = int(clamp_value(std::ceil(span), 4.0, 32.0));
	double m11 = 0.0;
	double m12 = 0.0;
	double m22 = 0.0;
	Vector2 rhs1;
	Vector2 rhs2;
	std::vector<std::pair<double, Vector2>> evidence;
	evidence.reserve(size_t(sample_count));
	for (int sample_index = 0; sample_index <= sample_count; sample_index++) {
		const double distance = -before + span * double(sample_index) / double(sample_count);
		if (std::abs(distance) <= POSITION_EPSILON) {
			continue;
		}
		const double u = distance / jet.support;
		const Sample sample = p_sample(distance);
		if (!is_finite(sample.position)) {
			continue;
		}
		const double first_basis = u;
		const double second_basis = 0.5 * u * u;
		const Vector2 offset = sample.position - p_origin.position;
		evidence.push_back({ u, offset });
		m11 += first_basis * first_basis;
		m12 += first_basis * second_basis;
		m22 += second_basis * second_basis;
		rhs1 += offset * float(first_basis);
		rhs2 += offset * float(second_basis);
	}
	const double determinant = m11 * m22 - m12 * m12;
	const Vector2 linear_fit = m11 > 1e-12 ? rhs1 / float(m11) : Vector2();
	Vector2 quadratic_linear;
	Vector2 quadratic_term;
	const bool quadratic_valid = std::abs(determinant) > 1e-12 * std::max(1.0, m11 * m22);
	if (quadratic_valid) {
		quadratic_linear = (rhs1 * float(m22) - rhs2 * float(m12)) / float(determinant);
		quadratic_term = (rhs2 * float(m11) - rhs1 * float(m12)) / float(determinant);
	}
	double linear_error = 0.0;
	double quadratic_error = 0.0;
	for (const auto &item : evidence) {
		const double u = item.first;
		linear_error += (item.second - linear_fit * float(u)).length_squared();
		if (quadratic_valid) {
			quadratic_error += (item.second - quadratic_linear * float(u) -
					quadratic_term * float(0.5 * u * u)).length_squared();
		}
	}
	const double observations = std::max(1.0, 2.0 * double(evidence.size()));
	const double raster_variance = 1.0 / 12.0;
	auto bic = [&](double p_error, double p_parameter_count) {
		return observations * std::log(std::max(p_error, observations * raster_variance) / observations) +
				p_parameter_count * std::log(observations);
	};
	jet.linear = linear_fit;
	double best_bic = bic(linear_error, 2.0);
	if (quadratic_valid && bic(quadratic_error, 4.0) < best_bic) {
		best_bic = bic(quadratic_error, 4.0);
		jet.linear = quadratic_linear;
		jet.quadratic = quadratic_term;
	}
	jet.valid = is_finite(jet.linear) && is_finite(jet.quadratic) &&
			jet.linear.length_squared() > POSITION_EPSILON * POSITION_EPSILON;
	return jet;
}

template <typename Sampler>
EndpointJet fit_endpoint_jet(const Sample &p_origin, double p_available, const Sampler &p_sample) {
	return fit_endpoint_jet_window(p_origin, 0.0, p_available, p_sample);
}

EndpointJet fit_port_jet(const Port &p_port, const RuntimeBase &p_base) {
	const double available = std::max(0.0, double(p_port.max_arc - p_port.distance_from_end));
	auto sample = [&](double p_distance) {
		return sample_from_end(p_base, p_port.end, p_port.distance_from_end + float(p_distance));
	};
	EndpointJet jet = fit_endpoint_jet(p_port.sample, available, sample);
	const double maximum_span = MAXIMUM_LATENT_JUNCTION_RADII *
			std::max(double(MINIMUM_RADIUS), double(p_port.sample.radius));
	const double missing_outward_support = std::max(0.0, maximum_span - available);
	const double evidence_before = std::min(double(p_port.trend_margin), missing_outward_support);
	// Junction-side evidence may refine curvature only when the independent
	// outward support already resolves curvature; otherwise keep a straight tail straight.
	if (evidence_before > POSITION_EPSILON &&
			(!jet.valid || jet.quadratic.length_squared() > POSITION_EPSILON * POSITION_EPSILON)) {
		const EndpointJet window_jet = fit_endpoint_jet_window(p_port.sample, evidence_before, available, sample);
		if (window_jet.valid) {
			jet = window_jet;
		}
	}
	if (!jet.valid && available > POSITION_EPSILON && is_finite(p_port.outward_tangent) &&
			p_port.outward_tangent.length_squared() > POSITION_EPSILON * POSITION_EPSILON) {
		jet.origin = p_port.sample;
		jet.support = available;
		jet.linear = p_port.outward_tangent.normalized() * float(available);
		jet.valid = true;
	}
	return jet;
}

Port stable_carrier_port(const Port &p_port, const RuntimeBase &p_base) {
	Port result = p_port;
	const EndpointJet jet = fit_port_jet(p_port, p_base);
	if (jet.valid) {
		result.outward_tangent = jet.linear.normalized();
	}
	return result;
}

Vector2 evaluate_jet(const EndpointJet &p_jet, double p_distance) {
	if (!p_jet.valid || p_jet.support <= POSITION_EPSILON) {
		return p_jet.origin.position;
	}
	const double u = p_distance / p_jet.support;
	return p_jet.origin.position - p_jet.linear * float(u) + p_jet.quadratic * float(0.5 * u * u);
}

Vector2 jet_tangent(const EndpointJet &p_jet, double p_distance) {
	if (!p_jet.valid || p_jet.support <= POSITION_EPSILON) {
		return {};
	}
	const double u = p_distance / p_jet.support;
	const Vector2 tangent = -p_jet.linear + p_jet.quadratic * float(u);
	return is_finite(tangent) && tangent.length_squared() > POSITION_EPSILON * POSITION_EPSILON ? tangent.normalized() : Vector2();
}

void stabilize_port_tangents(const JunctionWork &p_junction, std::vector<Port> &r_ports,
		const std::vector<RuntimeBase> &p_bases) {
	for (int port_index : p_junction.ports) {
		Port &port = r_ports[port_index];
		const RuntimeBase &base = p_bases[port.base_index];
		port.trend_margin = 0.0f;
		if (p_junction.disks_separated) {
			const float available = std::max(0.0f, port.max_arc - port.distance_from_end);
			const float stability_margin = std::min(
					std::max(MINIMUM_RADIUS, port.sample.radius), available * 0.5f);
			if (stability_margin > POSITION_EPSILON) {
				port.trend_margin = stability_margin;
				port.distance_from_end += stability_margin;
				port.sample = sample_from_end(base, port.end, port.distance_from_end);
				port.trim_s = global_s_from_end(base, port.end, port.distance_from_end);
			}
		}
		const EndpointJet jet = fit_port_jet(port, base);
		if (jet.valid) {
			port.outward_tangent = -jet_tangent(jet, 0.0);
		}
	}
}

Vector2 evaluate_hermite(const Vector2 &p0, const Vector2 &p1, const Vector2 &m0, const Vector2 &m1, float p_t) {
	const float t2 = p_t * p_t;
	const float t3 = t2 * p_t;
	return p0 * (2.0f * t3 - 3.0f * t2 + 1.0f) +
			m0 * (t3 - 2.0f * t2 + p_t) +
			p1 * (-2.0f * t3 + 3.0f * t2) +
			m1 * (t3 - t2);
}

std::vector<float> curve_cumulative(const std::vector<Sample> &p_samples) {
	std::vector<float> cumulative(p_samples.size(), 0.0f);
	for (size_t i = 1; i < p_samples.size(); i++) {
		cumulative[i] = cumulative[i - 1] + float(p_samples[i - 1].position.distance_to(p_samples[i].position));
	}
	return cumulative;
}

Sample sample_curve_at(const std::vector<Sample> &p_samples, const std::vector<float> &p_cumulative, float p_s) {
	if (p_samples.empty()) {
		return {};
	}
	if (p_s <= 0.0f || p_cumulative.size() < 2) {
		return p_samples.front();
	}
	if (p_s >= p_cumulative.back()) {
		return p_samples.back();
	}
	const auto found = std::lower_bound(p_cumulative.begin() + 1, p_cumulative.end(), p_s);
	const size_t second = size_t(found - p_cumulative.begin());
	if (std::abs(double(*found - p_s)) <= POSITION_EPSILON) {
		return p_samples[second];
	}
	const size_t first = second - 1;
	const float length = p_cumulative[second] - p_cumulative[first];
	return length <= POSITION_EPSILON ? p_samples[first] :
			interpolate_sample(p_samples[first], p_samples[second], (p_s - p_cumulative[first]) / length);
}

std::pair<Vector2, Vector2> minimum_bending_derivatives(const Vector2 &p_start, const Vector2 &p_end,
		const Vector2 &p_start_direction, const Vector2 &p_end_direction) {
	const Vector2 chord = p_end - p_start;
	if (!is_finite(chord) || chord.length_squared() <= POSITION_EPSILON * POSITION_EPSILON ||
			p_start_direction.length_squared() <= POSITION_EPSILON * POSITION_EPSILON ||
			p_end_direction.length_squared() <= POSITION_EPSILON * POSITION_EPSILON) {
		return {};
	}
	const Vector2 start_direction = p_start_direction.normalized();
	const Vector2 end_direction = p_end_direction.normalized();
	const double direction_dot = double(start_direction.dot(end_direction));
	const double start_rhs = 12.0 * double(chord.dot(start_direction));
	const double end_rhs = 12.0 * double(chord.dot(end_direction));
	const double determinant = 64.0 - 16.0 * direction_dot * direction_dot;
	double best_start = 0.0;
	double best_end = 0.0;
	double best_energy = std::numeric_limits<double>::infinity();
	auto consider = [&](double p_start_length, double p_end_length) {
		if (!std::isfinite(p_start_length) || !std::isfinite(p_end_length) ||
				p_start_length < 0.0 || p_end_length < 0.0) {
			return;
		}
		const Vector2 start_derivative = start_direction * float(p_start_length);
		const Vector2 end_derivative = end_direction * float(p_end_length);
		const double energy = 12.0 * double(chord.length_squared()) -
				12.0 * double(chord.dot(start_derivative + end_derivative)) +
				4.0 * (double(start_derivative.length_squared()) +
						double(start_derivative.dot(end_derivative)) + double(end_derivative.length_squared()));
		if (energy < best_energy) {
			best_energy = energy;
			best_start = p_start_length;
			best_end = p_end_length;
		}
	};
	if (determinant > 1e-12) {
		consider((8.0 * start_rhs - 4.0 * direction_dot * end_rhs) / determinant,
				(8.0 * end_rhs - 4.0 * direction_dot * start_rhs) / determinant);
	}
	consider(0.0, std::max(0.0, end_rhs / 8.0));
	consider(std::max(0.0, start_rhs / 8.0), 0.0);
	consider(0.0, 0.0);
	return { start_direction * float(best_start), end_direction * float(best_end) };
}

CurveGeometry build_selection_curve(const Port &p_a, const Port &p_b) {
	CurveGeometry curve;
	const Vector2 chord_vector = p_b.sample.position - p_a.sample.position;
	const float chord = float(chord_vector.length());
	if (!is_finite(chord) || chord <= POSITION_EPSILON ||
			p_a.outward_tangent.length_squared() <= POSITION_EPSILON * POSITION_EPSILON ||
			p_b.outward_tangent.length_squared() <= POSITION_EPSILON * POSITION_EPSILON) {
		return curve;
	}
	const auto derivatives = minimum_bending_derivatives(
			p_a.sample.position, p_b.sample.position, -p_a.outward_tangent, p_b.outward_tangent);
	const Vector2 start_derivative = derivatives.first;
	const Vector2 end_derivative = derivatives.second;
	const float target_segment = std::max(0.1f, std::min(p_a.sample.radius, p_b.sample.radius) * 0.25f);
	const int subdivisions = int(clamp_value(
			std::ceil(double(chord) * 2.0 / double(target_segment)), 8.0, double(MAXIMUM_CURVE_SUBDIVISIONS)));
	curve.samples.reserve(size_t(subdivisions) + 1);
	curve.samples.push_back(p_a.sample);
	for (int subdivision = 1; subdivision < subdivisions; subdivision++) {
		const float weight = float(subdivision) / float(subdivisions);
		const Vector2 position = evaluate_hermite(
				p_a.sample.position, p_b.sample.position, start_derivative, end_derivative, weight);
		if (is_finite(position)) {
			append_distinct(curve.samples, { position, p_a.sample.radius + (p_b.sample.radius - p_a.sample.radius) * weight });
		}
	}
	append_distinct(curve.samples, p_b.sample);
	curve.split_points.assign(curve.samples.size(), 0);
	curve.shared_nodes.assign(curve.samples.size(), -1);
	return curve;
}

double circumradius(const Vector2 &p_a, const Vector2 &p_b, const Vector2 &p_c) {
	const double ab_x = double(p_b.x) - double(p_a.x);
	const double ab_y = double(p_b.y) - double(p_a.y);
	const double ac_x = double(p_c.x) - double(p_a.x);
	const double ac_y = double(p_c.y) - double(p_a.y);
	const double twice_area = std::abs(ab_x * ac_y - ab_y * ac_x);
	const double side_ab = p_a.distance_to(p_b);
	const double side_bc = p_b.distance_to(p_c);
	const double side_ac = p_a.distance_to(p_c);
	const double scale = std::max(1.0, side_ab * side_bc + side_bc * side_ac + side_ac * side_ab);
	if (twice_area <= 1e-10 * scale || side_ab <= POSITION_EPSILON || side_bc <= POSITION_EPSILON || side_ac <= POSITION_EPSILON) {
		return std::numeric_limits<double>::infinity();
	}
	return side_ab * side_bc * side_ac / (2.0 * twice_area);
}

double stroke_curvature(const Vector2 &p_before, const Vector2 &p_center, const Vector2 &p_after, float p_radius) {
	const Vector2 incoming = p_center - p_before;
	const Vector2 outgoing = p_after - p_center;
	const double incoming_length = incoming.length();
	const double outgoing_length = outgoing.length();
	if (incoming_length <= POSITION_EPSILON || outgoing_length <= POSITION_EPSILON) {
		return PI;
	}
	const double direction_dot = double(incoming.dot(outgoing)) / (incoming_length * outgoing_length);
	const double direction_cross = std::abs(double(incoming.x) * double(outgoing.y) -
			double(incoming.y) * double(outgoing.x));
	if (direction_cross <= 1e-10 * incoming_length * outgoing_length) {
		return direction_dot < 0.0 ? PI : 0.0;
	}
	const double circle_radius = circumradius(p_before, p_center, p_after);
	if (!std::isfinite(circle_radius)) {
		return PI;
	}
	const double ratio = clamp_value(double(p_radius) / (2.0 * circle_radius), 0.0, 1.0);
	return 2.0 * std::asin(ratio);
}

double maximum_stroke_curvature(const CurveGeometry &p_curve) {
	if (p_curve.samples.size() < 2) {
		return std::numeric_limits<double>::infinity();
	}
	const std::vector<float> cumulative = curve_cumulative(p_curve.samples);
	const float length = cumulative.back();
	if (length <= POSITION_EPSILON) {
		return std::numeric_limits<double>::infinity();
	}

	// Each curvature triple is centered at sample_s and uses the center's local
	// stroke radius as the arc-length distance on both sides.
	double maximum_alpha = 0.0;
	bool sampled = false;
	float sample_s = 0.0f;
	while (sample_s < length - POSITION_EPSILON) {
		const Sample center = sample_curve_at(p_curve.samples, cumulative, sample_s);
		const float radius = center.radius;
		if (!is_finite(radius) || radius <= POSITION_EPSILON) {
			return std::numeric_limits<double>::infinity();
		}
		if (sample_s >= radius && length - sample_s >= radius) {
			const Sample before = sample_curve_at(p_curve.samples, cumulative, sample_s - radius);
			const Sample after = sample_curve_at(p_curve.samples, cumulative, sample_s + radius);
			maximum_alpha = std::max(maximum_alpha,
					stroke_curvature(before.position, center.position, after.position, radius));
			sampled = true;
		}
		sample_s += radius;
	}
	return sampled ? maximum_alpha : std::numeric_limits<double>::infinity();
}

double continuity_angle_degrees(const Port &p_a, const Port &p_b) {
	if (!is_finite(p_a.outward_tangent) || !is_finite(p_b.outward_tangent) ||
			p_a.outward_tangent.length_squared() <= POSITION_EPSILON * POSITION_EPSILON ||
			p_b.outward_tangent.length_squared() <= POSITION_EPSILON * POSITION_EPSILON) {
		return 0.0;
	}
	const double cosine = clamp_value(double(p_a.outward_tangent.normalized().dot(p_b.outward_tangent.normalized())), -1.0, 1.0);
	return std::acos(cosine) * 180.0 / PI;
}

std::pair<std::tuple<int, int, int>, std::tuple<int, int, int>> candidate_key(const HermiteCandidate &p_candidate,
		const JunctionWork &p_junction, const std::vector<Port> &p_ports, const std::vector<RuntimeBase> &p_bases) {
	auto first = port_key(p_ports[p_junction.ports[p_candidate.port_a]], p_bases);
	auto second = port_key(p_ports[p_junction.ports[p_candidate.port_b]], p_bases);
	if (second < first) {
		std::swap(first, second);
	}
	return { first, second };
}

bool all_connected(const std::vector<uint8_t> &p_connected) {
	return std::all_of(p_connected.begin(), p_connected.end(), [](uint8_t p_value) { return p_value != 0; });
}

double cross(double p_ax, double p_ay, double p_bx, double p_by) {
	return p_ax * p_by - p_ay * p_bx;
}

bool intersect_ray_segment(const Vector2 &p_ray_start, const Vector2 &p_ray_direction, const Vector2 &p_curve_start,
		const Vector2 &p_curve_end, double &r_ray_t, double &r_curve_t) {
	const double rx = double(p_ray_direction.x);
	const double ry = double(p_ray_direction.y);
	const double sx = double(p_curve_end.x) - double(p_curve_start.x);
	const double sy = double(p_curve_end.y) - double(p_curve_start.y);
	const double qpx = double(p_curve_start.x) - double(p_ray_start.x);
	const double qpy = double(p_curve_start.y) - double(p_ray_start.y);
	const double denominator = cross(rx, ry, sx, sy);
	const double scale = std::max(1.0, std::sqrt((rx * rx + ry * ry) * (sx * sx + sy * sy)));
	if (std::abs(denominator) > 1e-10 * scale) {
		const double ray_t = cross(qpx, qpy, sx, sy) / denominator;
		const double curve_t = cross(qpx, qpy, rx, ry) / denominator;
		if (ray_t > 1e-7 && curve_t >= -1e-8 && curve_t <= 1.0 + 1e-8) {
			r_ray_t = ray_t;
			r_curve_t = clamp_value(curve_t, 0.0, 1.0);
			return true;
		}
		return false;
	}

	if (std::abs(cross(qpx, qpy, rx, ry)) > 1e-10 * scale) {
		return false;
	}
	const double ray_length_squared = rx * rx + ry * ry;
	const double curve_length_squared = sx * sx + sy * sy;
	if (ray_length_squared <= 1e-20 || curve_length_squared <= 1e-20) {
		return false;
	}
	const double first_t = (qpx * rx + qpy * ry) / ray_length_squared;
	const double second_t = first_t + (sx * rx + sy * ry) / ray_length_squared;
	const double overlap_start = std::max(1e-7, std::min(first_t, second_t));
	const double overlap_end = std::max(first_t, second_t);
	if (overlap_start > overlap_end + 1e-8) {
		return false;
	}
	const double hit_x = double(p_ray_start.x) + rx * overlap_start;
	const double hit_y = double(p_ray_start.y) + ry * overlap_start;
	r_ray_t = overlap_start;
	r_curve_t = clamp_value(((hit_x - double(p_curve_start.x)) * sx + (hit_y - double(p_curve_start.y)) * sy) /
			curve_length_squared,
			0.0, 1.0);
	return true;
}

bool first_nonpositive_quadratic(double p_a, double p_b, double p_c, double p_low, double p_high, double &r_root) {
	const double low = clamp_value(p_low, 0.0, 1.0);
	const double high = clamp_value(p_high, 0.0, 1.0);
	if (low > high + 1e-12) {
		return false;
	}
	auto evaluate = [&](double p_u) { return (p_a * p_u + p_b) * p_u + p_c; };
	const double scale = std::max({ 1.0, std::abs(p_a), std::abs(p_b), std::abs(p_c) });
	const double tolerance = 1e-10 * scale;
	if (evaluate(low) <= tolerance) {
		r_root = low;
		return true;
	}
	if (std::abs(p_a) <= 1e-12 * scale) {
		if (std::abs(p_b) <= 1e-12 * scale) {
			return false;
		}
		const double root = -p_c / p_b;
		if (root >= low - 1e-10 && root <= high + 1e-10) {
			r_root = clamp_value(root, low, high);
			return evaluate(r_root) <= tolerance;
		}
		return false;
	}
	double discriminant = p_b * p_b - 4.0 * p_a * p_c;
	if (discriminant < -tolerance) {
		return false;
	}
	discriminant = std::max(0.0, discriminant);
	const double square_root = std::sqrt(discriminant);
	const double q = -0.5 * (p_b + std::copysign(square_root, p_b));
	double roots[2];
	if (std::abs(q) > 1e-20) {
		roots[0] = q / p_a;
		roots[1] = p_c / q;
	} else {
		roots[0] = roots[1] = -p_b / (2.0 * p_a);
	}
	if (roots[1] < roots[0]) {
		std::swap(roots[0], roots[1]);
	}
	for (double root : roots) {
		if (root >= low - 1e-10 && root <= high + 1e-10) {
			r_root = clamp_value(root, low, high);
			if (evaluate(r_root) <= tolerance) {
				return true;
			}
		}
	}
	return false;
}

ChordContact first_disk_contact(const Vector2 &p_source_start, const Vector2 &p_source_end,
		double p_source_radius_start, double p_source_radius_end, const Sample &p_target_start, const Sample &p_target_end) {
	ChordContact result;
	if (!is_finite(p_source_start) || !is_finite(p_source_end) ||
			!is_finite(p_target_start.position) || !is_finite(p_target_end.position)) {
		return result;
	}
	const double source_radius_start = std::max(0.0, p_source_radius_start);
	const double source_radius_delta = std::max(0.0, p_source_radius_end) - source_radius_start;
	const double target_radius_start = std::max(0.0, double(p_target_start.radius));
	const double target_radius_delta = std::max(0.0, double(p_target_end.radius)) - target_radius_start;
	const Vector2 source_delta = p_source_end - p_source_start;
	const Vector2 target_delta = p_target_end.position - p_target_start.position;
	const Vector2 offset = p_source_start - p_target_start.position;
	const double radius_sum = source_radius_start + target_radius_start;
	const double a = double(target_delta.dot(target_delta)) - target_radius_delta * target_radius_delta;
	const double b0 = -2.0 * (double(offset.dot(target_delta)) + radius_sum * target_radius_delta);
	const double b1 = -2.0 * (double(source_delta.dot(target_delta)) + source_radius_delta * target_radius_delta);
	const double c0 = double(offset.dot(offset)) - radius_sum * radius_sum;
	const double c1 = 2.0 * (double(offset.dot(source_delta)) - radius_sum * source_radius_delta);
	const double c2 = double(source_delta.dot(source_delta)) - source_radius_delta * source_radius_delta;
	auto consider = [&](double p_source_t, double p_target_t) {
		const double source_t = clamp_value(p_source_t, 0.0, 1.0);
		const double target_t = clamp_value(p_target_t, 0.0, 1.0);
		const double source_radius = source_radius_start + source_radius_delta * source_t;
		const double target_radius = target_radius_start + target_radius_delta * target_t;
		const double separation_x = double(offset.x) + double(source_delta.x) * source_t -
				double(target_delta.x) * target_t;
		const double separation_y = double(offset.y) + double(source_delta.y) * source_t -
				double(target_delta.y) * target_t;
		const double separation_squared = separation_x * separation_x + separation_y * separation_y;
		const double allowed = source_radius + target_radius;
		const double tolerance = 1e-8 * std::max(1.0, separation_squared + allowed * allowed);
		if (separation_squared > allowed * allowed + tolerance) {
			return;
		}
		if (!result.found || source_t < result.source_t - 1e-10 ||
				(std::abs(source_t - result.source_t) <= 1e-10 && target_t < result.target_t)) {
			result.found = true;
			result.source_t = source_t;
			result.target_t = target_t;
			result.separation = std::sqrt(std::max(0.0, separation_squared));
		}
	};
	double root = 0.0;
	if (first_nonpositive_quadratic(c2, c1, c0, 0.0, 1.0, root)) {
		consider(root, 0.0);
	}
	if (first_nonpositive_quadratic(c2, c1 + b1, c0 + b0 + a, 0.0, 1.0, root)) {
		consider(root, 1.0);
	}
	const double a_scale = std::max({ 1.0, double(target_delta.length_squared()), target_radius_delta * target_radius_delta });
	if (a > 1e-12 * a_scale) {
		double interval_low = 0.0;
		double interval_high = 1.0;
		const double coefficient_tolerance = 1e-12 * std::max({ 1.0, std::abs(b0), std::abs(b1), 2.0 * a });
		if (std::abs(b1) <= coefficient_tolerance) {
			if (b0 < -2.0 * a - coefficient_tolerance || b0 > coefficient_tolerance) {
				interval_low = 1.0;
				interval_high = 0.0;
			}
		} else {
			const double first_bound = (-2.0 * a - b0) / b1;
			const double second_bound = -b0 / b1;
			interval_low = std::max(0.0, std::min(first_bound, second_bound));
			interval_high = std::min(1.0, std::max(first_bound, second_bound));
		}
		const double interior_a = c2 - b1 * b1 / (4.0 * a);
		const double interior_b = c1 - b0 * b1 / (2.0 * a);
		const double interior_c = c0 - b0 * b0 / (4.0 * a);
		if (first_nonpositive_quadratic(
					interior_a, interior_b, interior_c, interval_low, interval_high, root)) {
			consider(root, -(b0 + b1 * root) / (2.0 * a));
		}
	}
	return result;
}

CurveAttachment find_curve_attachment(const EndpointJet &p_jet, double p_maximum_distance,
		const Port &p_source_port, const RuntimeBase &p_source_base,
		const std::vector<int> &p_curve_indices, const std::vector<CurveGeometry> &p_curves) {
	CurveAttachment best;
	if (!p_jet.valid || p_maximum_distance <= POSITION_EPSILON) {
		return best;
	}
	const double step = std::max(0.25, std::min(1.0, double(p_jet.origin.radius) * 0.25));
	const int subdivisions = int(clamp_value(
			std::ceil(p_maximum_distance / step), 1.0, double(MAXIMUM_CURVE_SUBDIVISIONS)));
	for (int subdivision = 1; subdivision <= subdivisions; subdivision++) {
		const double first_distance = p_maximum_distance * double(subdivision - 1) / double(subdivisions);
		const double second_distance = p_maximum_distance * double(subdivision) / double(subdivisions);
		const Vector2 source_start = evaluate_jet(p_jet, first_distance);
		const Vector2 source_end = evaluate_jet(p_jet, second_distance);
		const Vector2 source_delta = source_end - source_start;
		if (source_delta.length_squared() <= POSITION_EPSILON * POSITION_EPSILON) {
			continue;
		}
		for (int curve_index : p_curve_indices) {
			if (curve_index < 0 || curve_index >= int(p_curves.size())) {
				continue;
			}
			const CurveGeometry &curve = p_curves[curve_index];
			for (size_t segment = 0; segment + 1 < curve.samples.size(); segment++) {
				double source_t = 0.0;
				double curve_t = 0.0;
				if (!intersect_ray_segment(source_start, source_delta, curve.samples[segment].position,
							curve.samples[segment + 1].position, source_t, curve_t) || source_t > 1.0 + 1e-8) {
					continue;
				}
				const double source_distance = first_distance + (second_distance - first_distance) * source_t;
				if (!best.found || source_distance < best.source_distance - 1e-9 ||
						(std::abs(source_distance - best.source_distance) <= 1e-9 &&
								std::tie(curve_index, segment) < std::tie(best.curve, best.segment))) {
					best.found = true;
					best.curve = curve_index;
					best.segment = segment;
					best.source_distance = source_distance;
					best.curve_t = curve_t;
				}
			}
		}
	}
	if (best.found) {
		return best;
	}
	for (int subdivision = 1; subdivision <= subdivisions; subdivision++) {
		CurveAttachment first_supported;
		const double first_distance = p_maximum_distance * double(subdivision - 1) / double(subdivisions);
		const double second_distance = p_maximum_distance * double(subdivision) / double(subdivisions);
		const Vector2 source_start = evaluate_jet(p_jet, first_distance);
		const Vector2 source_end = evaluate_jet(p_jet, second_distance);
		auto source_radius = [&](double p_distance) {
			const float base_distance = clamp_value(
					p_source_port.distance_from_end - float(p_distance), 0.0f, p_source_base.length);
			return double(sample_from_end(p_source_base, p_source_port.end, base_distance).radius);
		};
		const double source_radius_start = source_radius(first_distance);
		const double source_radius_end = source_radius(second_distance);
		for (int curve_index : p_curve_indices) {
			if (curve_index < 0 || curve_index >= int(p_curves.size())) {
				continue;
			}
			const CurveGeometry &curve = p_curves[curve_index];
			for (size_t segment = 0; segment + 1 < curve.samples.size(); segment++) {
				const ChordContact contact = first_disk_contact(source_start, source_end,
						source_radius_start, source_radius_end, curve.samples[segment], curve.samples[segment + 1]);
				if (!contact.found) {
					continue;
				}
				const double source_distance = first_distance + (second_distance - first_distance) * contact.source_t;
				if (!first_supported.found || source_distance < first_supported.source_distance - 1e-9 ||
						(std::abs(source_distance - first_supported.source_distance) <= 1e-9 &&
								std::tie(curve_index, segment, contact.target_t) <
										std::tie(first_supported.curve, first_supported.segment, first_supported.curve_t))) {
					first_supported.found = true;
					first_supported.curve = curve_index;
					first_supported.segment = segment;
					first_supported.source_distance = source_distance;
					first_supported.curve_t = contact.target_t;
					first_supported.separation = contact.separation;
				}
			}
		}
		if (first_supported.found) {
			if (first_supported.source_distance <= POSITION_EPSILON) {
				return {};
			}
			return first_supported;
		}
	}
	return {};
}

CurveGeometry build_supported_extension(const EndpointJet &p_jet, const Sample &p_target, double p_distance) {
	CurveGeometry curve;
	if (!p_jet.valid || !is_finite(p_target.position) || p_distance <= POSITION_EPSILON) {
		return curve;
	}
	const Vector2 prediction = evaluate_jet(p_jet, p_distance);
	const Vector2 correction = p_target.position - prediction;
	const float target_segment = std::max(0.1f, std::min(p_jet.origin.radius, p_target.radius) * 0.25f);
	const int subdivisions = int(clamp_value(
			std::ceil(p_distance / double(target_segment)), 4.0, double(MAXIMUM_CURVE_SUBDIVISIONS)));
	curve.samples.reserve(size_t(subdivisions) + 1);
	curve.samples.push_back(p_jet.origin);
	for (int subdivision = 1; subdivision <= subdivisions; subdivision++) {
		const double weight = double(subdivision) / double(subdivisions);
		const double weight2 = weight * weight;
		const double weight3 = weight2 * weight;
		const double smooth_correction = weight3 * (10.0 + weight * (-15.0 + 6.0 * weight));
		Sample sample;
		sample.position = evaluate_jet(p_jet, p_distance * weight) + correction * float(smooth_correction);
		sample.radius = p_jet.origin.radius + (p_target.radius - p_jet.origin.radius) * float(weight);
		if (is_finite(sample.position)) {
			append_distinct(curve.samples, sample);
		}
	}
	if (curve.samples.size() < 2) {
		curve.samples.clear();
		return curve;
	}
	curve.samples.front() = p_jet.origin;
	curve.samples.back() = p_target;
	curve.split_points.assign(curve.samples.size(), 0);
	curve.split_points.front() = 1;
	curve.split_points.back() = 1;
	curve.shared_nodes.assign(curve.samples.size(), -1);
	return curve;
}

double closest_jet_distance(const EndpointJet &p_jet, const Vector2 &p_target, double p_maximum_distance) {
	if (!p_jet.valid || p_maximum_distance <= POSITION_EPSILON) {
		return 0.0;
	}
	const int subdivisions = int(clamp_value(std::ceil(p_maximum_distance / 0.5), 4.0, 256.0));
	int best_index = 0;
	double best_error = std::numeric_limits<double>::infinity();
	for (int subdivision = 0; subdivision <= subdivisions; subdivision++) {
		const double distance = p_maximum_distance * double(subdivision) / double(subdivisions);
		const double error = evaluate_jet(p_jet, distance).distance_squared_to(p_target);
		if (error < best_error) {
			best_error = error;
			best_index = subdivision;
		}
	}
	double low = p_maximum_distance * double(std::max(0, best_index - 1)) / double(subdivisions);
	double high = p_maximum_distance * double(std::min(subdivisions, best_index + 1)) / double(subdivisions);
	for (int iteration = 0; iteration < 20; iteration++) {
		const double first = (2.0 * low + high) / 3.0;
		const double second = (low + 2.0 * high) / 3.0;
		if (evaluate_jet(p_jet, first).distance_squared_to(p_target) <=
				evaluate_jet(p_jet, second).distance_squared_to(p_target)) {
			high = second;
		} else {
			low = first;
		}
	}
	return 0.5 * (low + high);
}

Vector2 fit_direction_consensus(const Vector2 &p_fallback, const std::vector<Port> &p_ports,
		double p_maximum_displacement) {
	double xx = 0.0;
	double xy = 0.0;
	double yy = 0.0;
	double bx = 0.0;
	double by = 0.0;
	for (const Port &port : p_ports) {
		if (!is_finite(port.sample.position) || !is_finite(port.outward_tangent) ||
				port.outward_tangent.length_squared() <= POSITION_EPSILON * POSITION_EPSILON) {
			continue;
		}
		const Vector2 tangent = port.outward_tangent.normalized();
		const double nx = -double(tangent.y);
		const double ny = double(tangent.x);
		const double projection = nx * double(port.sample.position.x) + ny * double(port.sample.position.y);
		xx += nx * nx;
		xy += nx * ny;
		yy += ny * ny;
		bx += nx * projection;
		by += ny * projection;
	}
	const double determinant = xx * yy - xy * xy;
	if (std::abs(determinant) <= 1e-8 * std::max(1.0, xx + yy)) {
		return p_fallback;
	}
	Vector2 target(
			float((bx * yy - by * xy) / determinant),
			float((by * xx - bx * xy) / determinant));
	if (!is_finite(target)) {
		return p_fallback;
	}
	const Vector2 displacement = target - p_fallback;
	if (p_maximum_displacement > POSITION_EPSILON && displacement.length() > p_maximum_displacement) {
		target = p_fallback + displacement.normalized() * float(p_maximum_displacement);
	}
	return target;
}

CurveGeometry join_supported_extensions(const CurveGeometry &p_first, const CurveGeometry &p_second,
		int p_shared_node) {
	CurveGeometry curve;
	if (p_first.samples.size() < 2 || p_second.samples.size() < 2 ||
			!same_position(p_first.samples.back().position, p_second.samples.back().position)) {
		return curve;
	}
	curve.samples.reserve(p_first.samples.size() + p_second.samples.size() - 1);
	for (const Sample &sample : p_first.samples) {
		append_distinct(curve.samples, sample);
	}
	const size_t shared_sample = curve.samples.size() - 1;
	for (size_t sample = p_second.samples.size() - 1; sample > 0; sample--) {
		append_distinct(curve.samples, p_second.samples[sample - 1]);
	}
	curve.split_points.assign(curve.samples.size(), 0);
	curve.split_points.front() = 1;
	curve.split_points[shared_sample] = 1;
	curve.split_points.back() = 1;
	curve.shared_nodes.assign(curve.samples.size(), -1);
	curve.shared_nodes[shared_sample] = p_shared_node;
	return curve;
}

double coverage_at(const std::vector<float> &p_coverage, int p_width, int p_height, const Vector2 &p_position) {
	const double pixel_x = double(p_position.x) - 0.5;
	const double pixel_y = double(p_position.y) - 0.5;
	const int x0 = int(std::floor(pixel_x));
	const int y0 = int(std::floor(pixel_y));
	if (x0 < -1 || x0 >= p_width || y0 < -1 || y0 >= p_height) {
		return 0.0;
	}
	const int x1 = std::clamp(x0 + 1, 0, p_width - 1);
	const int y1 = std::clamp(y0 + 1, 0, p_height - 1);
	const int clamped_x0 = std::clamp(x0, 0, p_width - 1);
	const int clamped_y0 = std::clamp(y0, 0, p_height - 1);
	const double tx = clamp_value(pixel_x - double(x0), 0.0, 1.0);
	const double ty = clamp_value(pixel_y - double(y0), 0.0, 1.0);
	auto value = [&](int p_x, int p_y) {
		return double(p_coverage[size_t(p_y) * size_t(p_width) + size_t(p_x)]);
	};
	const double top = value(clamped_x0, clamped_y0) * (1.0 - tx) + value(x1, clamped_y0) * tx;
	const double bottom = value(clamped_x0, y1) * (1.0 - tx) + value(x1, y1) * tx;
	return top * (1.0 - ty) + bottom * ty;
}

double stroke_length(const Stroke &p_stroke) {
	double length = 0.0;
	for (size_t sample = 1; sample < p_stroke.samples.size(); sample++) {
		length += p_stroke.samples[sample - 1].position.distance_to(p_stroke.samples[sample].position);
	}
	return length;
}

Sample sample_stroke_from_endpoint(const Stroke &p_stroke, bool p_front, double p_distance) {
	if (p_stroke.samples.empty()) {
		return {};
	}
	const Sample &endpoint = p_front ? p_stroke.samples.front() : p_stroke.samples.back();
	if (p_distance <= 0.0 || p_stroke.samples.size() < 2) {
		return endpoint;
	}
	double traversed = 0.0;
	if (p_front) {
		for (size_t sample = 1; sample < p_stroke.samples.size(); sample++) {
			const Sample &previous = p_stroke.samples[sample - 1];
			const Sample &current = p_stroke.samples[sample];
			const double length = previous.position.distance_to(current.position);
			if (traversed + length >= p_distance && length > POSITION_EPSILON) {
				return interpolate_sample(previous, current, float((p_distance - traversed) / length));
			}
			traversed += length;
		}
		return p_stroke.samples.back();
	}
	for (size_t sample = p_stroke.samples.size() - 1; sample > 0; sample--) {
		const Sample &previous = p_stroke.samples[sample];
		const Sample &current = p_stroke.samples[sample - 1];
		const double length = previous.position.distance_to(current.position);
		if (traversed + length >= p_distance && length > POSITION_EPSILON) {
			return interpolate_sample(previous, current, float((p_distance - traversed) / length));
		}
		traversed += length;
	}
	return p_stroke.samples.front();
}

EndpointJet fit_stroke_endpoint_jet(const Stroke &p_stroke, bool p_front) {
	if (p_stroke.samples.size() < 2) {
		return {};
	}
	const Sample origin = p_front ? p_stroke.samples.front() : p_stroke.samples.back();
	const double available = stroke_length(p_stroke);
	return fit_endpoint_jet(origin, available, [&](double p_distance) {
		return sample_stroke_from_endpoint(p_stroke, p_front, p_distance);
	});
}

Vector2 endpoint_outward_direction(const Stroke &p_stroke, bool p_front) {
	return jet_tangent(fit_stroke_endpoint_jet(p_stroke, p_front), 0.0);
}

void extend_stroke_endpoint(Stroke &r_stroke, bool p_front, const CurveGeometry &p_extension) {
	if (r_stroke.samples.empty() || p_extension.samples.size() < 2) {
		return;
	}
	if (p_front) {
		std::vector<Sample> extended;
		extended.reserve(r_stroke.samples.size() + p_extension.samples.size() - 1);
		for (auto sample = p_extension.samples.rbegin(); sample != p_extension.samples.rend(); sample++) {
			append_distinct(extended, *sample);
		}
		for (size_t sample = 1; sample < r_stroke.samples.size(); sample++) {
			append_distinct(extended, r_stroke.samples[sample]);
		}
		r_stroke.samples = std::move(extended);
		return;
	}
	for (size_t sample = 1; sample < p_extension.samples.size(); sample++) {
		append_distinct(r_stroke.samples, p_extension.samples[sample]);
	}
}

double foreground_jet_extent(const std::vector<float> &p_coverage, int p_width, int p_height,
		const EndpointJet &p_jet, double p_maximum_distance, double p_cutoff) {
	constexpr double SAMPLE_STEP = 0.5;
	double extent = 0.0;
	for (double distance = 0.0; distance <= p_maximum_distance; distance += SAMPLE_STEP) {
		const Vector2 position = evaluate_jet(p_jet, distance);
		if (position.x < 0.0f || position.x >= float(p_width) || position.y < 0.0f || position.y >= float(p_height) ||
				coverage_at(p_coverage, p_width, p_height, position) + 1e-6 < p_cutoff) {
			break;
		}
		extent = distance;
	}
	return extent;
}

void append_mandatory_point(Stroke &r_stroke, const Vector2 &p_position) {
	for (const Vector2 &point : r_stroke.mandatory_points) {
		if (same_position(point, p_position)) {
			return;
		}
	}
	r_stroke.mandatory_points.push_back(p_position);
}

void append_explicit_self_vertex(Stroke &r_stroke, const Vector2 &p_position) {
	for (const Vector2 &point : r_stroke.explicit_self_vertices) {
		if (same_position(point, p_position)) {
			return;
		}
	}
	r_stroke.explicit_self_vertices.push_back(p_position);
}

// Recover latent T junctions by extending a disconnected endpoint along its
// tangent only through foreground support, then make the hit a shared key vertex.
void anchor_supported_endpoint_junctions(std::vector<Stroke> &r_strokes, const Params &p_params,
		const std::vector<float> &p_coverage, int p_width, int p_height) {
	if (p_width <= 0 || p_height <= 0 || p_coverage.size() != size_t(p_width) * size_t(p_height)) {
		return;
	}
	struct Attachment {
		int stroke = -1;
		int target_stroke = -1;
		bool front = false;
		int target_endpoint = -1;
		size_t target_segment = 0;
		double target_parameter = 0.0;
		double continuation_angle_degrees = 0.0;
		Sample target_sample;
		CurveGeometry extension;
	};
	struct ExactSelfAnchor {
		int stroke = -1;
		bool front = false;
		size_t target_segment = 0;
		double target_parameter = 0.0;
		Sample sample;
	};
	struct StrokeSegmentRef {
		int stroke = -1;
		size_t segment = 0;
		Vector2 start;
		Vector2 end;
	};
	constexpr double CELL_SIZE = 8.0;
	auto cell_key = [](int p_x, int p_y) {
		return (uint64_t(uint32_t(p_x)) << 32) | uint32_t(p_y);
	};
	std::vector<StrokeSegmentRef> segments;
	std::unordered_map<uint64_t, std::vector<int>> cells;
	auto visit_segment_cells = [&](const Vector2 &p_start, const Vector2 &p_end, const auto &p_visit) {
		int cell_x = int(std::floor(double(p_start.x) / CELL_SIZE));
		int cell_y = int(std::floor(double(p_start.y) / CELL_SIZE));
		const int end_x = int(std::floor(double(p_end.x) / CELL_SIZE));
		const int end_y = int(std::floor(double(p_end.y) / CELL_SIZE));
		const Vector2 direction = p_end - p_start;
		const int step_x = direction.x > 0.0f ? 1 : (direction.x < 0.0f ? -1 : 0);
		const int step_y = direction.y > 0.0f ? 1 : (direction.y < 0.0f ? -1 : 0);
		const double delta_x = step_x == 0 ? std::numeric_limits<double>::infinity() : CELL_SIZE / std::abs(double(direction.x));
		const double delta_y = step_y == 0 ? std::numeric_limits<double>::infinity() : CELL_SIZE / std::abs(double(direction.y));
		const double boundary_x = step_x > 0 ? double(cell_x + 1) * CELL_SIZE : double(cell_x) * CELL_SIZE;
		const double boundary_y = step_y > 0 ? double(cell_y + 1) * CELL_SIZE : double(cell_y) * CELL_SIZE;
		double next_x = step_x == 0 ? std::numeric_limits<double>::infinity() :
				(boundary_x - double(p_start.x)) / double(direction.x);
		double next_y = step_y == 0 ? std::numeric_limits<double>::infinity() :
				(boundary_y - double(p_start.y)) / double(direction.y);
		const int maximum_steps = std::abs(end_x - cell_x) + std::abs(end_y - cell_y) + 1;
		for (int step = 0; step < maximum_steps; step++) {
			p_visit(cell_x, cell_y);
			if (cell_x == end_x && cell_y == end_y) {
				break;
			}
			if (next_x < next_y) {
				cell_x += step_x;
				next_x += delta_x;
			} else {
				cell_y += step_y;
				next_y += delta_y;
			}
		}
	};
	for (int stroke = 0; stroke < int(r_strokes.size()); stroke++) {
		const std::vector<Sample> &samples = r_strokes[stroke].samples;
		for (size_t segment = 0; segment + 1 < samples.size(); segment++) {
			const StrokeSegmentRef reference = { stroke, segment, samples[segment].position, samples[segment + 1].position };
			if (!is_finite(reference.start) || !is_finite(reference.end) ||
					reference.start.distance_squared_to(reference.end) <= POSITION_EPSILON * POSITION_EPSILON) {
				continue;
			}
			const int segment_index = int(segments.size());
			segments.push_back(reference);
			visit_segment_cells(reference.start, reference.end, [&](int p_x, int p_y) {
				cells[cell_key(p_x, p_y)].push_back(segment_index);
			});
		}
	}
	auto segment_is_endpoint_incident = [&](const StrokeSegmentRef &p_segment, int p_stroke, bool p_front) {
		if (p_segment.stroke != p_stroke) {
			return false;
		}
		const std::vector<Sample> &samples = r_strokes[p_stroke].samples;
		const Vector2 endpoint = p_front ? samples.front().position : samples.back().position;
		return (p_segment.segment == 0 && same_position(endpoint, samples.front().position)) ||
				(p_segment.segment + 2 == samples.size() && same_position(endpoint, samples.back().position));
	};
	std::vector<ExactSelfAnchor> exact_self_anchors;
	auto endpoint_is_anchored = [&](int p_stroke, bool p_front) {
		const Vector2 point = p_front ? r_strokes[p_stroke].samples.front().position :
				r_strokes[p_stroke].samples.back().position;
		const int cell_x = int(std::floor(double(point.x) / CELL_SIZE));
		const int cell_y = int(std::floor(double(point.y) / CELL_SIZE));
		std::unordered_set<int> candidates;
		for (int offset_y = -1; offset_y <= 1; offset_y++) {
			for (int offset_x = -1; offset_x <= 1; offset_x++) {
				const auto found = cells.find(cell_key(cell_x + offset_x, cell_y + offset_y));
				if (found != cells.end()) {
					candidates.insert(found->second.begin(), found->second.end());
				}
			}
		}
		int self_segment = -1;
		double self_parameter = 0.0;
		Sample self_sample;
		bool anchored_to_other_stroke = false;
		for (int segment_index : candidates) {
			const StrokeSegmentRef &target = segments[segment_index];
			if (segment_is_endpoint_incident(target, p_stroke, p_front)) {
				continue;
			}
			const Vector2 delta = target.end - target.start;
			const double length_squared = delta.length_squared();
			if (length_squared <= POSITION_EPSILON * POSITION_EPSILON) {
				continue;
			}
			const double parameter = clamp_value(
					double((point - target.start).dot(delta)) / length_squared, 0.0, 1.0);
			const Vector2 projected = target.start.lerp(target.end, float(parameter));
			if (point.distance_squared_to(projected) > POSITION_EPSILON * POSITION_EPSILON) {
				continue;
			}
			if (target.stroke != p_stroke) {
				anchored_to_other_stroke = true;
				continue;
			}
			if (self_segment < 0 || std::tie(target.segment, parameter) <
					std::tie(segments[self_segment].segment, self_parameter)) {
				self_segment = segment_index;
				self_parameter = parameter;
				self_sample = interpolate_sample(r_strokes[p_stroke].samples[target.segment],
						r_strokes[p_stroke].samples[target.segment + 1], float(parameter));
				self_sample.position = projected;
			}
		}
		if (self_segment >= 0) {
			exact_self_anchors.push_back({ p_stroke, p_front, segments[self_segment].segment,
					self_parameter, self_sample });
		}
		return anchored_to_other_stroke || self_segment >= 0;
	};
	std::vector<Attachment> attachments;
	const double cutoff = std::max(1.0 / 255.0, double(clamp_value(p_params.threshold, 0.0f, 1.0f)));
	for (int stroke = 0; stroke < int(r_strokes.size()); stroke++) {
		if (r_strokes[stroke].samples.size() < 2) {
			continue;
		}
		for (int endpoint = 0; endpoint < 2; endpoint++) {
			const bool front = endpoint == 0;
			const bool anchored = endpoint_is_anchored(stroke, front);
			if (anchored) {
				continue;
			}
			const EndpointJet jet = fit_stroke_endpoint_jet(r_strokes[stroke], front);
			if (!jet.valid) {
				continue;
			}
			const double maximum_extent = MAXIMUM_LATENT_JUNCTION_RADII *
					std::max(double(MINIMUM_RADIUS), double(jet.origin.radius));
			const double extent = foreground_jet_extent(
					p_coverage, p_width, p_height, jet, maximum_extent, cutoff);
			CurveAttachment best;
			const int subdivisions = std::max(1, int(std::ceil((extent + 0.5) / 0.5)));
			std::unordered_set<int> candidates;
			for (int subdivision = 1; subdivision <= subdivisions; subdivision++) {
				const double first_distance = (extent + 0.5) * double(subdivision - 1) / double(subdivisions);
				const double second_distance = (extent + 0.5) * double(subdivision) / double(subdivisions);
				const Vector2 source_start = evaluate_jet(jet, first_distance);
				const Vector2 source_end = evaluate_jet(jet, second_distance);
				const Vector2 source_delta = source_end - source_start;
				if (source_delta.length_squared() <= POSITION_EPSILON * POSITION_EPSILON) {
					continue;
				}
				candidates.clear();
				visit_segment_cells(source_start, source_end, [&](int p_x, int p_y) {
					const auto found = cells.find(cell_key(p_x, p_y));
					if (found != cells.end()) {
						candidates.insert(found->second.begin(), found->second.end());
					}
				});
				for (int segment_index : candidates) {
					const StrokeSegmentRef &target = segments[segment_index];
					if (segment_is_endpoint_incident(target, stroke, front)) {
						continue;
					}
					double source_t = 0.0;
					double curve_t = 0.0;
					if (!intersect_ray_segment(source_start, source_delta, target.start, target.end, source_t, curve_t) ||
							source_t > 1.0 + 1e-8) {
						continue;
					}
					const double source_distance = first_distance + (second_distance - first_distance) * source_t;
					if (!best.found || source_distance < best.source_distance - 1e-9 ||
							(std::abs(source_distance - best.source_distance) <= 1e-9 &&
									std::tie(target.stroke, target.segment) < std::tie(best.curve, best.segment))) {
						best.found = true;
						best.curve = target.stroke;
						best.segment = target.segment;
						best.source_distance = source_distance;
						best.curve_t = curve_t;
					}
				}
				if (best.found) {
					break;
				}
			}
			if (best.found) {
				const std::vector<Sample> &target_samples = r_strokes[best.curve].samples;
				const Sample target_sample = interpolate_sample(
						target_samples[best.segment], target_samples[best.segment + 1], float(best.curve_t));
				int target_endpoint = -1;
				if (best.segment == 0 && best.curve_t <= 1e-7) {
					target_endpoint = 0;
				} else if (best.segment + 2 == target_samples.size() && best.curve_t >= 1.0 - 1e-7) {
					target_endpoint = 1;
				}
				double continuation_angle = 0.0;
				if (target_endpoint >= 0) {
					const Vector2 target_direction = endpoint_outward_direction(r_strokes[best.curve], target_endpoint == 0);
					const Vector2 source_direction = jet_tangent(jet, best.source_distance);
					if (target_direction.length_squared() > POSITION_EPSILON * POSITION_EPSILON &&
							source_direction.length_squared() > POSITION_EPSILON * POSITION_EPSILON) {
						continuation_angle = std::acos(clamp_value(
								double(source_direction.dot(target_direction)), -1.0, 1.0)) * 180.0 / PI;
					}
				}
				CurveGeometry extension = build_supported_extension(jet, target_sample, best.source_distance);
				if (!extension.samples.empty()) {
					attachments.push_back({ stroke, best.curve, front, target_endpoint, best.segment, best.curve_t,
							continuation_angle, target_sample, std::move(extension) });
				}
			}
		}
	}
	struct PendingSelfInsertion {
		int stroke = -1;
		size_t segment = 0;
		double parameter = 0.0;
		Sample sample;
	};
	std::vector<PendingSelfInsertion> self_insertions;
	for (const ExactSelfAnchor &anchor : exact_self_anchors) {
		self_insertions.push_back({ anchor.stroke, anchor.target_segment, anchor.target_parameter, anchor.sample });
	}
	for (int attachment = 0; attachment < int(attachments.size()); attachment++) {
		if (attachments[attachment].stroke == attachments[attachment].target_stroke &&
				attachments[attachment].target_endpoint < 0) {
			self_insertions.push_back({ attachments[attachment].target_stroke, attachments[attachment].target_segment,
					attachments[attachment].target_parameter, attachments[attachment].target_sample });
		}
	}
	std::stable_sort(self_insertions.begin(), self_insertions.end(), [](const PendingSelfInsertion &p_a,
			const PendingSelfInsertion &p_b) {
		return std::tie(p_a.stroke, p_a.segment, p_a.parameter) > std::tie(p_b.stroke, p_b.segment, p_b.parameter);
	});
	for (const PendingSelfInsertion &insertion : self_insertions) {
		std::vector<Sample> &samples = r_strokes[insertion.stroke].samples;
		if (insertion.segment + 1 >= samples.size() || insertion.parameter <= 1e-7 ||
				insertion.parameter >= 1.0 - 1e-7) {
			continue;
		}
		samples.insert(samples.begin() + std::ptrdiff_t(insertion.segment + 1), insertion.sample);
	}
	for (const ExactSelfAnchor &anchor : exact_self_anchors) {
		std::vector<Sample> &samples = r_strokes[anchor.stroke].samples;
		if (anchor.front) {
			samples.front() = anchor.sample;
		} else {
			samples.back() = anchor.sample;
		}
		append_mandatory_point(r_strokes[anchor.stroke], anchor.sample.position);
		append_explicit_self_vertex(r_strokes[anchor.stroke], anchor.sample.position);
	}
	std::vector<int> attachment_order(attachments.size());
	for (int index = 0; index < int(attachment_order.size()); index++) {
		attachment_order[index] = index;
	}
	std::stable_sort(attachment_order.begin(), attachment_order.end(), [&](int p_a, int p_b) {
		if (attachments[p_a].continuation_angle_degrees != attachments[p_b].continuation_angle_degrees) {
			return attachments[p_a].continuation_angle_degrees > attachments[p_b].continuation_angle_degrees;
		}
		return std::tie(attachments[p_a].stroke, attachments[p_a].front, attachments[p_a].target_stroke,
					attachments[p_a].target_endpoint) <
				std::tie(attachments[p_b].stroke, attachments[p_b].front, attachments[p_b].target_stroke,
						attachments[p_b].target_endpoint);
	});
	std::vector<int> continuations(r_strokes.size() * 2, -1);
	std::vector<int> selected_attachments(r_strokes.size() * 2, -1);
	for (int attachment_index : attachment_order) {
		const Attachment &attachment = attachments[attachment_index];
		if (attachment.target_endpoint < 0 || attachment.continuation_angle_degrees < CONTINUATION_ANGLE_THRESHOLD_DEGREES) {
			continue;
		}
		const int source_handle = attachment.stroke * 2 + int(!attachment.front);
		const int target_handle = attachment.target_stroke * 2 + attachment.target_endpoint;
		if (source_handle == target_handle || continuations[source_handle] >= 0 || continuations[target_handle] >= 0) {
			continue;
		}
		continuations[source_handle] = target_handle;
		continuations[target_handle] = source_handle;
		selected_attachments[source_handle] = attachment_index;
	}
	for (int attachment_index = 0; attachment_index < int(attachments.size()); attachment_index++) {
		const Attachment &attachment = attachments[attachment_index];
		const int source_handle = attachment.stroke * 2 + int(!attachment.front);
		if (continuations[source_handle] >= 0 && selected_attachments[source_handle] != attachment_index) {
			continue;
		}
		extend_stroke_endpoint(r_strokes[attachment.stroke], attachment.front, attachment.extension);
		if (continuations[source_handle] >= 0) {
			continue;
		}
		append_mandatory_point(r_strokes[attachment.stroke], attachment.target_sample.position);
		append_mandatory_point(r_strokes[attachment.target_stroke], attachment.target_sample.position);
		if (attachment.stroke == attachment.target_stroke && attachment.target_endpoint < 0) {
			append_explicit_self_vertex(r_strokes[attachment.stroke], attachment.target_sample.position);
		}
	}

	std::vector<uint8_t> used(r_strokes.size(), 0);
	std::vector<Stroke> merged;
	merged.reserve(r_strokes.size());
	auto walk = [&](int p_stroke, bool p_front) {
		Stroke result;
		int stroke = p_stroke;
		bool front = p_front;
		while (stroke >= 0 && stroke < int(r_strokes.size()) && !used[stroke]) {
			used[stroke] = 1;
			const Stroke &source = r_strokes[stroke];
			if (front) {
				for (const Sample &sample : source.samples) {
					append_distinct(result.samples, sample);
				}
			} else {
				for (auto sample = source.samples.rbegin(); sample != source.samples.rend(); sample++) {
					append_distinct(result.samples, *sample);
				}
			}
			for (const Vector2 &point : source.mandatory_points) {
				append_mandatory_point(result, point);
			}
			for (const Vector2 &point : source.explicit_self_vertices) {
				append_explicit_self_vertex(result, point);
			}
			const int exit_handle = stroke * 2 + int(front);
			const int next_handle = continuations[exit_handle];
			stroke = next_handle >= 0 ? next_handle / 2 : -1;
			front = next_handle >= 0 && next_handle % 2 == 0;
		}
		if (result.samples.size() >= 2) {
			merged.push_back(std::move(result));
		}
	};
	for (int stroke = 0; stroke < int(r_strokes.size()); stroke++) {
		if (used[stroke]) {
			continue;
		}
		const int front_handle = stroke * 2;
		const int back_handle = front_handle + 1;
		if (continuations[front_handle] < 0 || continuations[back_handle] < 0) {
			walk(stroke, continuations[front_handle] < 0);
		}
	}
	for (int stroke = 0; stroke < int(r_strokes.size()); stroke++) {
		if (!used[stroke]) {
			walk(stroke, true);
		}
	}
	r_strokes = std::move(merged);
}


InsertedHit insert_hit_sample(CurveGeometry &r_curve, size_t p_segment, double p_curve_t, int p_shared_node) {
	if (r_curve.shared_nodes.size() != r_curve.samples.size()) {
		r_curve.shared_nodes.assign(r_curve.samples.size(), -1);
	}
	if (p_segment + 1 >= r_curve.samples.size()) {
		return { r_curve.samples.back(), r_curve.shared_nodes.back() };
	}
	if (p_curve_t <= 1e-7 || same_position(r_curve.samples[p_segment].position,
				r_curve.samples[p_segment].position.lerp(r_curve.samples[p_segment + 1].position, float(p_curve_t)))) {
		r_curve.split_points[p_segment] = 1;
		if (r_curve.shared_nodes[p_segment] < 0) {
			r_curve.shared_nodes[p_segment] = p_shared_node;
		}
		return { r_curve.samples[p_segment], r_curve.shared_nodes[p_segment] };
	}
	if (p_curve_t >= 1.0 - 1e-7 || same_position(r_curve.samples[p_segment + 1].position,
				r_curve.samples[p_segment].position.lerp(r_curve.samples[p_segment + 1].position, float(p_curve_t)))) {
		r_curve.split_points[p_segment + 1] = 1;
		if (r_curve.shared_nodes[p_segment + 1] < 0) {
			r_curve.shared_nodes[p_segment + 1] = p_shared_node;
		}
		return { r_curve.samples[p_segment + 1], r_curve.shared_nodes[p_segment + 1] };
	}
	const Sample hit = interpolate_sample(r_curve.samples[p_segment], r_curve.samples[p_segment + 1], float(p_curve_t));
	r_curve.samples.insert(r_curve.samples.begin() + std::ptrdiff_t(p_segment + 1), hit);
	r_curve.split_points.insert(r_curve.split_points.begin() + std::ptrdiff_t(p_segment + 1), 1);
	r_curve.shared_nodes.insert(r_curve.shared_nodes.begin() + std::ptrdiff_t(p_segment + 1), p_shared_node);
	return { hit, p_shared_node };
}

CurveGeometry make_line_curve(const Sample &p_start, const Sample &p_end) {
	CurveGeometry curve;
	if (!is_finite(p_start.position) || !is_finite(p_end.position) || same_position(p_start.position, p_end.position)) {
		return curve;
	}
	curve.samples = { p_start, p_end };
	curve.split_points = { 1, 1 };
	curve.shared_nodes = { -1, -1 };
	return curve;
}

std::vector<Sample> trimmed_base_samples(const RuntimeBase &p_base, const std::vector<Port> &p_ports) {
	if (p_base.samples.size() < 2 || p_base.length <= POSITION_EPSILON) {
		return {};
	}
	float start_s = 0.0f;
	float end_s = p_base.length;
	Sample start = p_base.samples.front();
	Sample end = p_base.samples.back();
	if (p_base.front_port >= 0) {
		const Port &port = p_ports[p_base.front_port];
		start_s = port.trim_s;
		start = port.sample;
	}
	if (p_base.back_port >= 0) {
		const Port &port = p_ports[p_base.back_port];
		end_s = port.trim_s;
		end = port.sample;
	}
	if (end_s <= start_s + POSITION_EPSILON) {
		return {};
	}

	std::vector<Sample> result;
	result.reserve(p_base.samples.size() + 2);
	result.push_back(start);
	for (size_t i = 1; i + 1 < p_base.samples.size(); i++) {
		if (p_base.cumulative[i] > start_s + POSITION_EPSILON && p_base.cumulative[i] < end_s - POSITION_EPSILON) {
			append_distinct(result, p_base.samples[i]);
		}
	}
	append_distinct(result, end);
	return result.size() >= 2 ? result : std::vector<Sample>();
}

} // namespace


std::vector<Stroke> reverse_draw(const PipelineResult &p_pipeline, const Params &p_params,
		const std::vector<float> &p_coverage, int p_width, int p_height) {
	std::vector<RuntimeBase> bases;
	bases.reserve(p_pipeline.centerlines.size());
	for (size_t i = 0; i < p_pipeline.centerlines.size(); i++) {
		bases.push_back(prepare_base(p_pipeline, p_pipeline.centerlines[i], int(i), p_params));
	}

	std::vector<Port> ports;
	std::vector<std::vector<int>> ports_by_junction(p_pipeline.nodes.size());
	auto add_port = [&](int p_base_index, BaseEnd p_end, int p_junction) {
		RuntimeBase &base = bases[p_base_index];
		if (base.samples.size() < 2 || base.length <= POSITION_EPSILON || !is_junction(p_pipeline, p_junction)) {
			return;
		}
		Port port;
		port.base_index = p_base_index;
		port.end = p_end;
		port.junction = p_junction;
		port.max_arc = base.length;
		port.sample = sample_from_end(base, p_end, 0.0f);
		port.trim_s = global_s_from_end(base, p_end, 0.0f);
		build_port_knots(port, base);
		const int port_index = int(ports.size());
		ports.push_back(std::move(port));
		ports_by_junction[p_junction].push_back(port_index);
		if (p_end == BaseEnd::FRONT) {
			base.front_port = port_index;
		} else {
			base.back_port = port_index;
		}
	};

	for (int base_index = 0; base_index < int(bases.size()); base_index++) {
		const RuntimeBase &base = bases[base_index];
		if (base.source == nullptr) {
			continue;
		}
		add_port(base_index, BaseEnd::FRONT, base.source->node_a);
		add_port(base_index, BaseEnd::BACK, base.source->node_b);
	}

	std::vector<JunctionWork> junctions;
	for (int node = 0; node < int(ports_by_junction.size()); node++) {
		if (ports_by_junction[node].empty()) {
			continue;
		}
		JunctionWork work;
		work.node = node;
		work.ports = std::move(ports_by_junction[node]);
		std::stable_sort(work.ports.begin(), work.ports.end(), [&](int p_a, int p_b) {
			return port_key(ports[p_a], bases) < port_key(ports[p_b], bases);
		});
		Vector2 center = p_pipeline.nodes[node].position;
		if (!is_finite(center)) {
			center = {};
			for (int port_index : work.ports) {
				center += ports[port_index].sample.position;
			}
			center /= float(work.ports.size());
		}
		work.junction_sample = { center, sanitize_radius(p_pipeline.nodes[node].radius, p_params) };
		junctions.push_back(std::move(work));
	}

	// A medial-axis spur wholly covered by the junction disk never becomes a
	// visually independent stroke. Absorb it into the ambiguity core instead of
	// treating its raster endpoint as another direction to pair.
	for (JunctionWork &junction : junctions) {
		for (int port_index : junction.ports) {
			RuntimeBase &base = bases[ports[port_index].base_index];
			if (!base_has_independent_disk_support(base, junction.junction_sample)) {
				base.core_confined = true;
			}
		}
	}
	for (JunctionWork &junction : junctions) {
		junction.ports.erase(std::remove_if(junction.ports.begin(), junction.ports.end(), [&](int p_port_index) {
			return bases[ports[p_port_index].base_index].core_confined;
		}), junction.ports.end());
	}

	std::vector<CurveGeometry> junction_curves;
	std::vector<JunctionConnection> junction_connections;
	int next_shared_node = 0;
	for (JunctionWork &junction : junctions) {
		grow_ambiguous_region(junction, ports, bases, p_params);
		if (junction.ports.size() < 2) {
			continue;
		}
		if (!junction.disks_separated) {
			for (int port_index : junction.ports) {
				Port &port = ports[port_index];
				const RuntimeBase &base = bases[port.base_index];
				const float radial_support = port_radial_extent(
						port, base, junction.junction_sample.position);
				const PortState stable_state = port_at_circle(
						port, base, junction.junction_sample.position, radial_support * 0.5f);
				port.distance_from_end = stable_state.distance_from_end;
				port.sample = stable_state.sample;
				port.trim_s = global_s_from_end(base, port.end, port.distance_from_end);
			}
		}
		stabilize_port_tangents(junction, ports, bases);
		junction.geometry_reconstructed = true;
		std::vector<Port> carrier_ports;
		carrier_ports.reserve(junction.ports.size());
		for (int port_index : junction.ports) {
			carrier_ports.push_back(stable_carrier_port(ports[port_index], bases[ports[port_index].base_index]));
		}
		std::vector<HermiteCandidate> candidates;
		for (int first = 0; first < int(junction.ports.size()); first++) {
			for (int second = first + 1; second < int(junction.ports.size()); second++) {
				HermiteCandidate candidate;
				candidate.port_a = first;
				candidate.port_b = second;
				const Port &carrier_a = carrier_ports[first];
				const Port &carrier_b = carrier_ports[second];
				candidate.continuity_angle_degrees = continuity_angle_degrees(
						carrier_a, carrier_b);
				candidate.strong_continuation = candidate.continuity_angle_degrees >= CONTINUATION_ANGLE_THRESHOLD_DEGREES;
				candidate.endpoint_a = carrier_a.sample;
				candidate.endpoint_b = carrier_b.sample;
				candidate.outward_tangent_a = carrier_a.outward_tangent;
				candidate.outward_tangent_b = carrier_b.outward_tangent;
				candidate.curve = build_selection_curve(carrier_a, carrier_b);
				if (candidate.curve.samples.size() < 2) {
					continue;
				}
				candidate.max_alpha = maximum_stroke_curvature(candidate.curve);
				if (!std::isfinite(candidate.max_alpha)) {
					// A CC shorter than two local radii has no valid three-point
					// curvature sample. Its endpoint tangent turn is the only
					// resolvable curvature at this scale.
					candidate.max_alpha = std::max(0.0,
							(180.0 - candidate.continuity_angle_degrees) * PI / 180.0);
				}
				if (std::isfinite(candidate.max_alpha)) {
					candidates.push_back(std::move(candidate));
				}
			}
		}
		std::stable_sort(candidates.begin(), candidates.end(), [&](const HermiteCandidate &p_a, const HermiteCandidate &p_b) {
			if (p_a.max_alpha != p_b.max_alpha) {
				return p_a.max_alpha < p_b.max_alpha;
			}
			if (p_a.continuity_angle_degrees != p_b.continuity_angle_degrees) {
				return p_a.continuity_angle_degrees > p_b.continuity_angle_degrees;
			}
			return candidate_key(p_a, junction, ports, bases) < candidate_key(p_b, junction, ports, bases);
		});

		double threshold_degrees = is_finite(p_params.curvature_threshold_degrees) ?
				double(p_params.curvature_threshold_degrees) : 50.0;
		threshold_degrees = clamp_value(threshold_degrees, 0.0, 180.0);
		const double threshold = threshold_degrees * PI / 180.0;
		const int eligible_strong_candidate_count = int(std::count_if(candidates.begin(), candidates.end(), [&](const HermiteCandidate &p_candidate) {
			return p_candidate.strong_continuation && p_candidate.max_alpha <= threshold;
		}));
		const bool has_single_through_pair = eligible_strong_candidate_count == 1;
		std::vector<int> eligible_incidence(junction.ports.size(), 0);
		for (const HermiteCandidate &candidate : candidates) {
			if (candidate.max_alpha <= threshold && (!has_single_through_pair || candidate.strong_continuation)) {
				eligible_incidence[candidate.port_a]++;
				eligible_incidence[candidate.port_b]++;
			}
		}
		const bool matching_is_ambiguous = std::any_of(
				eligible_incidence.begin(), eligible_incidence.end(), [](int p_count) { return p_count > 1; });
		std::vector<uint8_t> connected(junction.ports.size(), 0);
		std::vector<HermiteCandidate> accepted_candidates;
		for (HermiteCandidate &candidate : candidates) {
			if (candidate.port_a < 0 || candidate.port_b < 0 ||
					connected[candidate.port_a] || connected[candidate.port_b]) {
				continue;
			}
			if ((has_single_through_pair && !candidate.strong_continuation) || candidate.max_alpha > threshold) {
				continue;
			}
			connected[candidate.port_a] = 1;
			connected[candidate.port_b] = 1;
			accepted_candidates.push_back(std::move(candidate));
			if (all_connected(connected)) {
				break;
			}
		}
		std::vector<int> local_curve_indices;
		Sample consensus_sample = junction.junction_sample;
		int consensus_shared_node = -1;
		if (matching_is_ambiguous) {
			consensus_sample.position = fit_direction_consensus(
					junction.junction_sample.position, carrier_ports, junction.consensus_radius);
			consensus_shared_node = next_shared_node++;
		}
		for (HermiteCandidate &candidate : accepted_candidates) {
			const int curve_index = int(junction_curves.size());
			const int global_port_a = junction.ports[candidate.port_a];
			const int global_port_b = junction.ports[candidate.port_b];
			CurveGeometry curve;
			if (matching_is_ambiguous) {
				const EndpointJet jet_a = fit_port_jet(ports[global_port_a], bases[ports[global_port_a].base_index]);
				const EndpointJet jet_b = fit_port_jet(ports[global_port_b], bases[ports[global_port_b].base_index]);
				const double maximum_a = MAXIMUM_LATENT_JUNCTION_RADII *
						std::max(double(MINIMUM_RADIUS), double(ports[global_port_a].sample.radius));
				const double maximum_b = MAXIMUM_LATENT_JUNCTION_RADII *
						std::max(double(MINIMUM_RADIUS), double(ports[global_port_b].sample.radius));
				CurveGeometry extension_a = build_supported_extension(jet_a, consensus_sample,
						closest_jet_distance(jet_a, consensus_sample.position, maximum_a));
				CurveGeometry extension_b = build_supported_extension(jet_b, consensus_sample,
						closest_jet_distance(jet_b, consensus_sample.position, maximum_b));
				if (extension_a.samples.empty()) {
					extension_a = make_line_curve(ports[global_port_a].sample, consensus_sample);
				}
				if (extension_b.samples.empty()) {
					extension_b = make_line_curve(ports[global_port_b].sample, consensus_sample);
				}
				curve = join_supported_extensions(extension_a, extension_b, consensus_shared_node);
			} else {
				ports[global_port_a].sample = candidate.endpoint_a;
				ports[global_port_a].outward_tangent = candidate.outward_tangent_a;
				ports[global_port_b].sample = candidate.endpoint_b;
				ports[global_port_b].outward_tangent = candidate.outward_tangent_b;
				curve = std::move(candidate.curve);
			}
			if (!curve.samples.empty()) {
				junction_curves.push_back(std::move(curve));
				junction_connections.push_back({ global_port_a, global_port_b, -1, -1, true });
				local_curve_indices.push_back(curve_index);
			} else {
				connected[candidate.port_a] = 0;
				connected[candidate.port_b] = 0;
			}
		}

		int fallback_shared_node = -1;
		for (int local_port = 0; local_port < int(junction.ports.size()); local_port++) {
			if (connected[local_port]) {
				continue;
			}
			const Port &port = ports[junction.ports[local_port]];
			const RuntimeBase &base = bases[port.base_index];
			const EndpointJet jet = fit_port_jet(port, base);
			const double maximum_distance = std::max(
					double(port.sample.position.distance_to(junction.junction_sample.position)),
					MAXIMUM_LATENT_JUNCTION_RADII * std::max(double(MINIMUM_RADIUS), double(port.sample.radius)));
			CurveAttachment hit;
			if (!matching_is_ambiguous) {
				hit = find_curve_attachment(
						jet, maximum_distance, port, base, local_curve_indices, junction_curves);
			}
			int shared_node = -1;
			int hit_port = -1;
			Sample extension_end;
			double extension_distance = 0.0;
			if (matching_is_ambiguous) {
				extension_distance = closest_jet_distance(
						jet, consensus_sample.position, maximum_distance);
				extension_end = consensus_sample;
				shared_node = consensus_shared_node;
			} else if (hit.found) {
				const CurveGeometry &hit_curve = junction_curves[hit.curve];
				const JunctionConnection &hit_connection = junction_connections[hit.curve];
				const bool at_segment_start = hit.curve_t <= 1e-7;
				const bool at_segment_end = hit.curve_t >= 1.0 - 1e-7;
				const size_t hit_sample = at_segment_end ? hit.segment + 1 : hit.segment;
				if (hit_sample == 0 && at_segment_start && hit_connection.port_a >= 0) {
					hit_port = hit_connection.port_a;
				} else if (hit_sample + 1 == hit_curve.samples.size() && at_segment_end && hit_connection.port_b >= 0) {
					hit_port = hit_connection.port_b;
				}
				if ((at_segment_start || at_segment_end) && hit_port < 0 && hit_sample < hit_curve.shared_nodes.size()) {
					shared_node = hit_curve.shared_nodes[hit_sample];
				}
				if ((!at_segment_start && !at_segment_end) || (hit_port < 0 && shared_node < 0)) {
					shared_node = next_shared_node++;
					InsertedHit inserted = insert_hit_sample(junction_curves[hit.curve], hit.segment, hit.curve_t, shared_node);
					extension_end = inserted.sample;
					shared_node = inserted.shared_node;
				} else {
					extension_end = hit_curve.samples[hit_sample];
				}
				extension_distance = hit.source_distance;
			} else {
				extension_distance = port.sample.position.distance_to(junction.junction_sample.position);
				if (extension_distance <= POSITION_EPSILON) {
					continue;
				}
				extension_end = junction.junction_sample;
				if (fallback_shared_node < 0) {
					fallback_shared_node = next_shared_node++;
				}
				shared_node = fallback_shared_node;
			}
			CurveGeometry extension = build_supported_extension(jet, extension_end, extension_distance);
			if (extension.samples.empty()) {
				extension = make_line_curve(port.sample, extension_end);
			}
			if (!extension.samples.empty()) {
				const int curve_index = int(junction_curves.size());
				const int global_port = junction.ports[local_port];
				junction_curves.push_back(std::move(extension));
				junction_connections.push_back({ global_port, hit_port, -1, shared_node });
			}
			connected[local_port] = 1;
		}
	}
	std::vector<int> base_order(bases.size());
	for (int i = 0; i < int(base_order.size()); i++) {
		base_order[i] = i;
	}
	std::stable_sort(base_order.begin(), base_order.end(), [&](int p_a, int p_b) {
		const int id_a = bases[p_a].source != nullptr ? bases[p_a].source->id : -1;
		const int id_b = bases[p_b].source != nullptr ? bases[p_b].source->id : -1;
		return std::tie(id_a, bases[p_a].input_index) < std::tie(id_b, bases[p_b].input_index);
	});

	// Materialize the reverse-drawing primitives as a graph.  A BC and its
	// selected CC share a port node; walking continuation pairs then produces
	// complete strokes instead of emitting every primitive independently.
	int next_graph_node = 0;
	std::vector<int> port_nodes(ports.size(), -1);
	std::vector<uint8_t> nonseparated_ports(ports.size(), 0);
	for (const JunctionWork &junction : junctions) {
		if (junction.geometry_reconstructed) {
			continue;
		}
		const int junction_node = next_graph_node++;
		for (int port : junction.ports) {
			port_nodes[port] = junction_node;
			nonseparated_ports[port] = 1;
		}
	}
	for (int port = 0; port < int(port_nodes.size()); port++) {
		if (port_nodes[port] < 0) {
			port_nodes[port] = next_graph_node++;
		}
	}
	std::vector<int> regular_nodes(p_pipeline.nodes.size(), -1);
	for (int node = 0; node < int(p_pipeline.nodes.size()); node++) {
		if (!is_junction(p_pipeline, node) && p_pipeline.nodes[node].degree > 0) {
			regular_nodes[node] = next_graph_node++;
		}
	}
	auto endpoint_node = [&](int p_base_index, BaseEnd p_end) {
		const RuntimeBase &base = bases[p_base_index];
		const int port = p_end == BaseEnd::FRONT ? base.front_port : base.back_port;
		if (port >= 0 && port < int(port_nodes.size())) {
			return port_nodes[port];
		}
		const int source_node = p_end == BaseEnd::FRONT ? base.source->node_a : base.source->node_b;
		if (valid_node(p_pipeline, source_node) && regular_nodes[source_node] >= 0) {
			return regular_nodes[source_node];
		}
		return next_graph_node++;
	};

	std::vector<OutputEdge> output_edges;
	std::vector<int> port_base_edges(ports.size(), -1);
	for (int base_index : base_order) {
		if (bases[base_index].core_confined) {
			continue;
		}
		CurveGeometry curve;
		curve.samples = trimmed_base_samples(bases[base_index], ports);
		if (curve.samples.size() < 2) {
			continue;
		}
		curve.split_points.assign(curve.samples.size(), 0);
		curve.split_points.front() = 1;
		curve.split_points.back() = 1;
		curve.shared_nodes.assign(curve.samples.size(), -1);
		OutputEdge edge;
		edge.curve = std::move(curve);
		edge.node_a = endpoint_node(base_index, BaseEnd::FRONT);
		edge.node_b = endpoint_node(base_index, BaseEnd::BACK);
		const int edge_index = int(output_edges.size());
		output_edges.push_back(std::move(edge));
		if (bases[base_index].front_port >= 0) {
			port_base_edges[bases[base_index].front_port] = edge_index;
		}
		if (bases[base_index].back_port >= 0) {
			port_base_edges[bases[base_index].back_port] = edge_index;
		}
	}

	std::vector<int> shared_graph_nodes;
	auto graph_node_for_shared = [&](int p_shared_node) {
		if (p_shared_node < 0) {
			return -1;
		}
		if (p_shared_node >= int(shared_graph_nodes.size())) {
			shared_graph_nodes.resize(size_t(p_shared_node + 1), -1);
		}
		if (shared_graph_nodes[p_shared_node] < 0) {
			shared_graph_nodes[p_shared_node] = next_graph_node++;
		}
		return shared_graph_nodes[p_shared_node];
	};
	auto connection_node = [&](int p_port, int p_shared_node) {
		if (p_port >= 0 && p_port < int(port_nodes.size())) {
			return port_nodes[p_port];
		}
		const int shared_node = graph_node_for_shared(p_shared_node);
		return shared_node >= 0 ? shared_node : next_graph_node++;
	};

	std::vector<PendingContinuation> pending_continuations;
	std::vector<std::vector<int>> candidate_edges_by_port(ports.size());
	for (int curve_index = 0; curve_index < int(junction_curves.size()); curve_index++) {
		const CurveGeometry &source = junction_curves[curve_index];
		if (source.samples.size() < 2 || curve_index >= int(junction_connections.size())) {
			continue;
		}
		const JunctionConnection &connection = junction_connections[curve_index];
		std::vector<size_t> split_indices;
		split_indices.reserve(source.samples.size());
		split_indices.push_back(0);
		for (size_t sample = 1; sample + 1 < source.samples.size(); sample++) {
			if (sample < source.split_points.size() && source.split_points[sample]) {
				split_indices.push_back(sample);
			}
		}
		split_indices.push_back(source.samples.size() - 1);
		std::sort(split_indices.begin(), split_indices.end());
		split_indices.erase(std::unique(split_indices.begin(), split_indices.end()), split_indices.end());

		const int start_node = connection_node(connection.port_a, connection.shared_node_a);
		int previous_edge = -1;
		int previous_end_node = start_node;
		for (size_t segment = 0; segment + 1 < split_indices.size(); segment++) {
			const size_t first_sample = split_indices[segment];
			const size_t last_sample = split_indices[segment + 1];
			if (last_sample <= first_sample) {
				continue;
			}
			CurveGeometry part;
			part.samples.reserve(last_sample - first_sample + 1);
			for (size_t sample = first_sample; sample <= last_sample; sample++) {
				append_distinct(part.samples, source.samples[sample]);
			}
			if (part.samples.size() < 2) {
				continue;
			}
			part.split_points.assign(part.samples.size(), 0);
			part.split_points.front() = 1;
			part.split_points.back() = 1;
			part.shared_nodes.assign(part.samples.size(), -1);
			const bool last_segment = segment + 2 == split_indices.size();
			int end_node = -1;
			if (last_segment) {
				end_node = connection_node(connection.port_b, connection.shared_node_b);
			} else {
				const int shared_node = last_sample < source.shared_nodes.size() ?
						source.shared_nodes[last_sample] : -1;
				end_node = connection_node(-1, shared_node);
			}
			OutputEdge edge;
			edge.curve = std::move(part);
			edge.node_a = previous_end_node;
			edge.node_b = end_node;
			const int edge_index = int(output_edges.size());
			output_edges.push_back(std::move(edge));
			if (connection.is_candidate) {
				if (segment == 0 && connection.port_a >= 0) {
					candidate_edges_by_port[connection.port_a].push_back(edge_index);
				}
				if (last_segment && connection.port_b >= 0) {
					candidate_edges_by_port[connection.port_b].push_back(edge_index);
				}
			}
			if (previous_edge >= 0) {
				pending_continuations.push_back({ previous_end_node, previous_edge, edge_index });
			}
			previous_edge = edge_index;
			previous_end_node = end_node;
		}
	}

	std::vector<std::vector<int>> incident(next_graph_node);
	for (int edge = 0; edge < int(output_edges.size()); edge++) {
		const OutputEdge &path = output_edges[edge];
		if (path.node_a >= 0 && path.node_a < next_graph_node) {
			incident[path.node_a].push_back(edge);
		}
		if (path.node_b >= 0 && path.node_b < next_graph_node) {
			incident[path.node_b].push_back(edge);
		}
	}
	std::vector<std::vector<std::pair<int, int>>> continuations(next_graph_node);
	auto pair_edges = [&](int p_node, int p_edge_a, int p_edge_b) {
		if (p_node < 0 || p_node >= next_graph_node || p_edge_a < 0 || p_edge_b < 0 || p_edge_a == p_edge_b) {
			return;
		}
		for (const auto &pair : continuations[p_node]) {
			if (pair.first == p_edge_a || pair.second == p_edge_a || pair.first == p_edge_b || pair.second == p_edge_b) {
				return;
			}
		}
		continuations[p_node].push_back({ p_edge_a, p_edge_b });
	};
	for (const PendingContinuation &pending : pending_continuations) {
		pair_edges(pending.node, pending.edge_a, pending.edge_b);
	}
	for (int port = 0; port < int(ports.size()); port++) {
		if (port_base_edges[port] < 0) {
			continue;
		}
		if (!candidate_edges_by_port[port].empty()) {
			// Candidate edges are stored in acceptance order. A BC owns one
			// continuation; remaining CCs start separate edge-disjoint trails.
			pair_edges(port_nodes[port], port_base_edges[port], candidate_edges_by_port[port].front());
			continue;
		}
		if (nonseparated_ports[port]) {
			continue;
		}
		std::vector<int> unique_incident = incident[port_nodes[port]];
		std::sort(unique_incident.begin(), unique_incident.end());
		unique_incident.erase(std::unique(unique_incident.begin(), unique_incident.end()), unique_incident.end());
		if (unique_incident.size() == 2 &&
				(unique_incident[0] == port_base_edges[port] || unique_incident[1] == port_base_edges[port])) {
			const int other_edge = unique_incident[0] == port_base_edges[port] ? unique_incident[1] : unique_incident[0];
			pair_edges(port_nodes[port], port_base_edges[port], other_edge);
		}
	}
	struct PortPairCandidate {
		int port_a = -1;
		int port_b = -1;
		double angle_degrees = 0.0;
	};
	for (const JunctionWork &junction : junctions) {
		if (junction.geometry_reconstructed) {
			continue;
		}
		std::vector<PortPairCandidate> candidates;
		for (int first = 0; first < int(junction.ports.size()); first++) {
			const int port_a = junction.ports[first];
			if (port_base_edges[port_a] < 0) {
				continue;
			}
			for (int second = first + 1; second < int(junction.ports.size()); second++) {
				const int port_b = junction.ports[second];
				if (port_base_edges[port_b] < 0 || port_base_edges[port_a] == port_base_edges[port_b]) {
					continue;
				}
				const double angle = continuity_angle_degrees(ports[port_a], ports[port_b]);
				if (angle >= CONTINUATION_ANGLE_THRESHOLD_DEGREES) {
					candidates.push_back({ port_a, port_b, angle });
				}
			}
		}
		std::stable_sort(candidates.begin(), candidates.end(), [](const PortPairCandidate &p_a, const PortPairCandidate &p_b) {
			return p_a.angle_degrees > p_b.angle_degrees;
		});
		for (const PortPairCandidate &candidate : candidates) {
			pair_edges(port_nodes[candidate.port_a], port_base_edges[candidate.port_a], port_base_edges[candidate.port_b]);
		}
	}
	for (int node = 0; node < int(regular_nodes.size()); node++) {
		if (regular_nodes[node] < 0) {
			continue;
		}
		std::vector<int> unique_incident = incident[regular_nodes[node]];
		std::sort(unique_incident.begin(), unique_incident.end());
		unique_incident.erase(std::unique(unique_incident.begin(), unique_incident.end()), unique_incident.end());
		if (unique_incident.size() == 2) {
			pair_edges(regular_nodes[node], unique_incident[0], unique_incident[1]);
		}
	}

	auto continuation_of = [&](int p_node, int p_incoming) {
		if (p_node < 0 || p_node >= next_graph_node) {
			return -1;
		}
		for (const auto &pair : continuations[p_node]) {
			if (pair.first == p_incoming) {
				return pair.second;
			}
			if (pair.second == p_incoming) {
				return pair.first;
			}
		}
		return -1;
	};
	auto append_oriented = [&](Stroke &r_stroke, const OutputEdge &p_edge, int p_start_node) {
		if (p_edge.node_a == p_start_node) {
			for (const Sample &sample : p_edge.curve.samples) {
				append_distinct(r_stroke.samples, sample);
			}
			return p_edge.node_b;
		}
		if (p_edge.node_b == p_start_node) {
			for (auto sample = p_edge.curve.samples.rbegin(); sample != p_edge.curve.samples.rend(); sample++) {
				append_distinct(r_stroke.samples, *sample);
			}
			return p_edge.node_a;
		}
		return -1;
	};

	std::vector<uint8_t> used_edges(output_edges.size(), 0);
	std::vector<Stroke> strokes;
	auto walk = [&](int p_start_node, int p_first_edge) {
		Stroke stroke;
		int current_node = p_start_node;
		int current_edge = p_first_edge;
		while (current_edge >= 0 && current_edge < int(output_edges.size()) && !used_edges[current_edge]) {
			const int reached_node = append_oriented(stroke, output_edges[current_edge], current_node);
			if (reached_node < 0) {
				break;
			}
			used_edges[current_edge] = 1;
			const int next_edge = continuation_of(reached_node, current_edge);
			current_node = reached_node;
			current_edge = next_edge;
		}
		if (stroke.samples.size() >= 2) {
			strokes.push_back(std::move(stroke));
		}
	};
	for (int node = 0; node < next_graph_node; node++) {
		for (int edge : incident[node]) {
			if (!used_edges[edge] && continuation_of(node, edge) < 0) {
				walk(node, edge);
			}
		}
	}
	for (int edge = 0; edge < int(output_edges.size()); edge++) {
		if (!used_edges[edge]) {
			walk(output_edges[edge].node_a, edge);
		}
	}

	anchor_supported_endpoint_junctions(strokes, p_params, p_coverage, p_width, p_height);
	return strokes;
}

} // namespace topology_centerline
