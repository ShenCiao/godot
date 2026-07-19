/**************************************************************************/
/*  topology_centerline_pipeline.cpp                                      */
/**************************************************************************/

#include "topology_centerline_pipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace topology_centerline {

namespace {

constexpr double COLLISION_RADIUS = 1.0;
constexpr double GRAPH_NEIGHBOR_RADIUS = 1.0;
constexpr double POSITION_EPSILON = 1e-8;
constexpr double DISTANCE_EPSILON = 1e-10;

struct PointD {
	double x = 0.0;
	double y = 0.0;
};

PointD operator+(const PointD &p_a, const PointD &p_b) {
	return { p_a.x + p_b.x, p_a.y + p_b.y };
}

PointD operator-(const PointD &p_a, const PointD &p_b) {
	return { p_a.x - p_b.x, p_a.y - p_b.y };
}

PointD operator*(const PointD &p_point, double p_scale) {
	return { p_point.x * p_scale, p_point.y * p_scale };
}

PointD operator/(const PointD &p_point, double p_scale) {
	return { p_point.x / p_scale, p_point.y / p_scale };
}

double dot(const PointD &p_a, const PointD &p_b) {
	return p_a.x * p_b.x + p_a.y * p_b.y;
}

double length_squared(const PointD &p_point) {
	return dot(p_point, p_point);
}

double length(const PointD &p_point) {
	return std::sqrt(length_squared(p_point));
}

double distance_squared(const PointD &p_a, const PointD &p_b) {
	return length_squared(p_a - p_b);
}

double distance(const PointD &p_a, const PointD &p_b) {
	return std::sqrt(distance_squared(p_a, p_b));
}

PointD normalized_or_zero(const PointD &p_point) {
	const double point_length = length(p_point);
	if (point_length <= DISTANCE_EPSILON) {
		return {};
	}
	return p_point / point_length;
}

Vector2 to_vector2(const PointD &p_point) {
	return Vector2(static_cast<float>(p_point.x), static_cast<float>(p_point.y));
}

bool in_bounds(const PointD &p_point, int p_width, int p_height) {
	return p_point.x >= 0.0 && p_point.y >= 0.0 && p_point.x < double(p_width) && p_point.y < double(p_height);
}

int foreground_component_at(const std::vector<int> &p_components, int p_width, int p_height, const PointD &p_point) {
	if (!in_bounds(p_point, p_width, p_height)) {
		return -1;
	}
	return p_components[int(std::floor(p_point.y)) * p_width + int(std::floor(p_point.x))];
}

bool segment_supported_by_component(const std::vector<int> &p_components, int p_width, int p_height,
		const PointD &p_start, const PointD &p_end, int p_component) {
	if (p_component < 0 || foreground_component_at(p_components, p_width, p_height, p_start) != p_component ||
			foreground_component_at(p_components, p_width, p_height, p_end) != p_component) {
		return false;
	}

	int x = int(std::floor(p_start.x));
	int y = int(std::floor(p_start.y));
	const int end_x = int(std::floor(p_end.x));
	const int end_y = int(std::floor(p_end.y));
	const double dx = p_end.x - p_start.x;
	const double dy = p_end.y - p_start.y;
	const int step_x = dx > 0.0 ? 1 : (dx < 0.0 ? -1 : 0);
	const int step_y = dy > 0.0 ? 1 : (dy < 0.0 ? -1 : 0);
	const double infinity = std::numeric_limits<double>::infinity();
	const double delta_x = step_x == 0 ? infinity : 1.0 / std::abs(dx);
	const double delta_y = step_y == 0 ? infinity : 1.0 / std::abs(dy);
	double crossing_x = step_x == 0 ? infinity :
			((step_x > 0 ? double(x + 1) : double(x)) - p_start.x) / dx;
	double crossing_y = step_y == 0 ? infinity :
			((step_y > 0 ? double(y + 1) : double(y)) - p_start.y) / dy;

	while (x != end_x || y != end_y) {
		if (crossing_x < crossing_y - DISTANCE_EPSILON) {
			x += step_x;
			crossing_x += delta_x;
		} else if (crossing_y < crossing_x - DISTANCE_EPSILON) {
			y += step_y;
			crossing_y += delta_y;
		} else {
			x += step_x;
			y += step_y;
			crossing_x += delta_x;
			crossing_y += delta_y;
		}
		if (x < 0 || y < 0 || x >= p_width || y >= p_height || p_components[y * p_width + x] != p_component) {
			return false;
		}
	}
	return true;
}

bool positions_share_pixel_neighborhood(const PointD &p_a, const PointD &p_b) {
	return std::abs(int(std::floor(p_a.x)) - int(std::floor(p_b.x))) <= 1 &&
			std::abs(int(std::floor(p_a.y)) - int(std::floor(p_b.y))) <= 1;
}

uint64_t cell_key(int p_x, int p_y) {
	return (uint64_t(uint32_t(p_x)) << 32) | uint32_t(p_y);
}

class SpatialBuckets {
public:
	explicit SpatialBuckets(double p_cell_size = 1.0) : cell_size(p_cell_size) {}

	void clear(size_t p_expected_size = 0) {
		buckets.clear();
		if (p_expected_size > 0) {
			buckets.reserve(p_expected_size * 2);
		}
	}

	void insert(const PointD &p_point, int p_index) {
		buckets[cell_key(cell_coordinate(p_point.x), cell_coordinate(p_point.y))].push_back(p_index);
	}

	void query(const PointD &p_point, double p_radius, std::vector<int> &r_indices) const {
		r_indices.clear();
		const int min_x = cell_coordinate(p_point.x - p_radius);
		const int max_x = cell_coordinate(p_point.x + p_radius);
		const int min_y = cell_coordinate(p_point.y - p_radius);
		const int max_y = cell_coordinate(p_point.y + p_radius);
		for (int y = min_y; y <= max_y; y++) {
			for (int x = min_x; x <= max_x; x++) {
				auto bucket = buckets.find(cell_key(x, y));
				if (bucket != buckets.end()) {
					r_indices.insert(r_indices.end(), bucket->second.begin(), bucket->second.end());
				}
			}
		}
	}

private:
	int cell_coordinate(double p_value) const {
		return int(std::floor(p_value / cell_size));
	}

	double cell_size = 1.0;
	std::unordered_map<uint64_t, std::vector<int>> buckets;
};

class DisjointSet {
public:
	explicit DisjointSet(int p_count) : parent(p_count), rank(p_count, 0) {
		for (int i = 0; i < p_count; i++) {
			parent[i] = i;
		}
	}

	int find(int p_node) {
		int root = p_node;
		while (parent[root] != root) {
			root = parent[root];
		}
		while (parent[p_node] != p_node) {
			const int next = parent[p_node];
			parent[p_node] = root;
			p_node = next;
		}
		return root;
	}

	bool unite(int p_a, int p_b) {
		int root_a = find(p_a);
		int root_b = find(p_b);
		if (root_a == root_b) {
			return false;
		}
		if (rank[root_a] < rank[root_b] || (rank[root_a] == rank[root_b] && root_b < root_a)) {
			std::swap(root_a, root_b);
		}
		parent[root_b] = root_a;
		if (rank[root_a] == rank[root_b]) {
			rank[root_a]++;
		}
		return true;
	}

private:
	std::vector<int> parent;
	std::vector<uint8_t> rank;
};

struct Particle {
	PointD position;
	PointD motion;
	double traveled = 0.0;
	bool active = true;
	bool collided = false;
	int foreground_component = -1;
};

struct ClusterNodeInternal {
	PointD position;
	double radius = 0.0;
	int foreground_component = -1;
};

struct ClusterEdge {
	int id = -1;
	int a = -1;
	int b = -1;
	double weight = 0.0;
};

struct AbstractRoute {
	int start = -1;
	int end = -1;
	std::vector<int> nodes;
	std::vector<int> edges;
};

double clamp_coverage(float p_value) {
	if (!std::isfinite(p_value)) {
		return 0.0;
	}
	return std::clamp(double(p_value), 0.0, 1.0);
}

std::vector<double> filter_coverage(const std::vector<float> &p_coverage, int p_width, int p_height, const Params &p_params) {
	const int pixel_count = p_width * p_height;
	std::vector<double> coverage(pixel_count, 0.0);
	const double threshold = std::clamp(double(p_params.threshold), 0.0, 1.0);
	const double requested_component_size = std::ceil(std::max(0.0, double(p_params.despeckling)));
	const int minimum_component_size = requested_component_size >= double(std::numeric_limits<int>::max()) ?
			std::numeric_limits<int>::max() : int(requested_component_size);
	if (minimum_component_size <= 1) {
		for (int i = 0; i < pixel_count; i++) {
			const double value = clamp_coverage(p_coverage[i]);
			if (value > 0.0 && value >= threshold) {
				coverage[i] = value;
			}
		}
		return coverage;
	}

	std::vector<uint8_t> ink(pixel_count, 0);
	for (int i = 0; i < pixel_count; i++) {
		const double value = clamp_coverage(p_coverage[i]);
		if (value > 0.0 && value >= threshold) {
			coverage[i] = value;
			ink[i] = 1;
		}
	}

	std::vector<uint8_t> visited(pixel_count, 0);
	std::vector<int> stack;
	std::vector<int> component;
	stack.reserve(256);
	component.reserve(256);
	constexpr int offsets[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
	for (int seed = 0; seed < pixel_count; seed++) {
		if (!ink[seed] || visited[seed]) {
			continue;
		}
		stack.clear();
		component.clear();
		stack.push_back(seed);
		visited[seed] = 1;
		while (!stack.empty()) {
			const int pixel = stack.back();
			stack.pop_back();
			component.push_back(pixel);
			const int x = pixel % p_width;
			const int y = pixel / p_width;
			for (const auto &offset : offsets) {
				const int nx = x + offset[0];
				const int ny = y + offset[1];
				if (nx < 0 || ny < 0 || nx >= p_width || ny >= p_height) {
					continue;
				}
				const int neighbor = ny * p_width + nx;
				if (ink[neighbor] && !visited[neighbor]) {
					visited[neighbor] = 1;
					stack.push_back(neighbor);
				}
			}
		}
		if (int(component.size()) < minimum_component_size) {
			for (int pixel : component) {
				coverage[pixel] = 0.0;
			}
		}
	}
	return coverage;
}

std::vector<int> label_foreground_components(const std::vector<double> &p_coverage, int p_width, int p_height) {
	const int pixel_count = p_width * p_height;
	std::vector<int> components(pixel_count, -1);
	std::vector<int> frontier;
	frontier.reserve(4096);
	constexpr int offsets[8][2] = {
		{ -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 },
		{ 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 },
	};
	int next_component = 0;
	for (int seed = 0; seed < pixel_count; seed++) {
		if (p_coverage[seed] <= 0.0 || components[seed] >= 0) {
			continue;
		}
		components[seed] = next_component++;
		frontier.push_back(seed);
		for (size_t head = 0; head < frontier.size(); head++) {
			const int pixel = frontier[head];
			const int x = pixel % p_width;
			const int y = pixel / p_width;
			for (const auto &offset : offsets) {
				const int nx = x + offset[0];
				const int ny = y + offset[1];
				if (nx < 0 || ny < 0 || nx >= p_width || ny >= p_height) {
					continue;
				}
				const int neighbor = ny * p_width + nx;
				if (p_coverage[neighbor] > 0.0 && components[neighbor] < 0) {
					components[neighbor] = components[seed];
					frontier.push_back(neighbor);
				}
			}
		}
		frontier.clear();
	}
	return components;
}

double coverage_at(const std::vector<double> &p_coverage, int p_width, int p_height, int p_x, int p_y) {
	if (p_x < 0 || p_y < 0 || p_x >= p_width || p_y >= p_height) {
		return 0.0;
	}
	return p_coverage[p_y * p_width + p_x];
}

std::vector<Particle> create_particles(const std::vector<double> &p_coverage, const std::vector<int> &p_foreground_components,
		int p_width, int p_height, const Params &p_params) {
	const int pixel_count = p_width * p_height;
	std::vector<PointD> gradients(pixel_count);
	double maximum_magnitude_squared = 0.0;
	for (int y = 0; y < p_height; y++) {
		for (int x = 0; x < p_width; x++) {
			const double top_left = coverage_at(p_coverage, p_width, p_height, x - 1, y - 1);
			const double top = coverage_at(p_coverage, p_width, p_height, x, y - 1);
			const double top_right = coverage_at(p_coverage, p_width, p_height, x + 1, y - 1);
			const double left = coverage_at(p_coverage, p_width, p_height, x - 1, y);
			const double right = coverage_at(p_coverage, p_width, p_height, x + 1, y);
			const double bottom_left = coverage_at(p_coverage, p_width, p_height, x - 1, y + 1);
			const double bottom = coverage_at(p_coverage, p_width, p_height, x, y + 1);
			const double bottom_right = coverage_at(p_coverage, p_width, p_height, x + 1, y + 1);
			const PointD gradient = {
				-top_left + top_right - 2.0 * left + 2.0 * right - bottom_left + bottom_right,
				-top_left - 2.0 * top - top_right + bottom_left + 2.0 * bottom + bottom_right
			};
			const int pixel = y * p_width + x;
			gradients[pixel] = gradient;
			maximum_magnitude_squared = std::max(maximum_magnitude_squared, length_squared(gradient));
		}
	}
	if (maximum_magnitude_squared <= DISTANCE_EPSILON * DISTANCE_EPSILON) {
		return {};
	}

	const double ratio = std::max(0.0, double(p_params.gradient_threshold_ratio));
	const double gradient_threshold_squared = maximum_magnitude_squared * ratio * ratio;
	const double speed_factor = p_params.motion_step > 0.0f ? double(p_params.motion_step) : 0.1;
	std::vector<Particle> particles;
	particles.reserve(pixel_count / 4);
	for (int y = 0; y < p_height; y++) {
		for (int x = 0; x < p_width; x++) {
			const int pixel = y * p_width + x;
			if (length_squared(gradients[pixel]) < gradient_threshold_squared) {
				continue;
			}
			const PointD motion = gradients[pixel] * speed_factor;
			int foreground_component = p_foreground_components[pixel];
			if (foreground_component < 0) {
				double best_alignment = -std::numeric_limits<double>::infinity();
				for (int dy = -1; dy <= 1; dy++) {
					for (int dx = -1; dx <= 1; dx++) {
						const int nx = x + dx;
						const int ny = y + dy;
						if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= p_width || ny >= p_height) {
							continue;
						}
						const int candidate = p_foreground_components[ny * p_width + nx];
						if (candidate < 0) {
							continue;
						}
						const double alignment = dot(gradients[pixel], { double(dx), double(dy) });
						if (alignment > best_alignment) {
							best_alignment = alignment;
							foreground_component = candidate;
						}
					}
				}
			}
			if (foreground_component < 0) {
				continue;
			}
			particles.push_back({ { double(x) + 0.5, double(y) + 0.5 }, motion, 0.0, true,
					false, foreground_component });
		}
	}
	return particles;
}

void move_particles(std::vector<Particle> &r_particles, const std::vector<int> &p_foreground_components,
		int p_width, int p_height) {
	if (r_particles.empty()) {
		return;
	}
	const size_t active_limit = (r_particles.size() - 1) / 100;
	size_t active_count = r_particles.size();
	double minimum_motion = std::numeric_limits<double>::infinity();
	for (const Particle &particle : r_particles) {
		minimum_motion = std::min(minimum_motion, length(particle.motion));
	}
	if (!std::isfinite(minimum_motion) || minimum_motion <= DISTANCE_EPSILON) {
		return;
	}
	const double diagonal = std::hypot(double(p_width), double(p_height));
	const size_t maximum_iterations = size_t(std::ceil((diagonal + 2.0) / minimum_motion)) + 2;
	SpatialBuckets buckets;
	std::vector<PointD> old_positions(r_particles.size());
	std::vector<PointD> proposed_positions(r_particles.size());
	std::vector<uint8_t> proposed_supported(r_particles.size(), 0);
	std::vector<uint8_t> collision_eligible(r_particles.size(), 0);
	std::vector<uint8_t> collision_stop(r_particles.size(), 0);
	std::vector<uint8_t> stop(r_particles.size(), 0);
	std::vector<int> nearby;
	for (size_t iteration = 0; active_count > active_limit && iteration < maximum_iterations; iteration++) {
		for (int i = 0; i < int(r_particles.size()); i++) {
			old_positions[i] = r_particles[i].position;
			stop[i] = 0;
			collision_stop[i] = 0;
			if (r_particles[i].active) {
				proposed_positions[i] = old_positions[i] + r_particles[i].motion;
				proposed_supported[i] = foreground_component_at(p_foreground_components, p_width, p_height,
						proposed_positions[i]) == r_particles[i].foreground_component;
				collision_eligible[i] = proposed_supported[i];
			} else {
				proposed_positions[i] = old_positions[i];
				proposed_supported[i] = r_particles[i].collided;
				collision_eligible[i] = r_particles[i].collided;
			}
		}

		buckets.clear(r_particles.size());
		for (int i = 0; i < int(r_particles.size()); i++) {
			if (in_bounds(proposed_positions[i], p_width, p_height)) {
				buckets.insert(proposed_positions[i], i);
			}
		}

		for (int i = 0; i < int(r_particles.size()); i++) {
			Particle &particle = r_particles[i];
			if (!particle.active) {
				continue;
			}
			const PointD &proposed = proposed_positions[i];
			if (!in_bounds(proposed, p_width, p_height)) {
				stop[i] = 1;
				continue;
			}
			if (!collision_eligible[i]) {
				continue;
			}
			buckets.query(proposed, COLLISION_RADIUS, nearby);
			for (int neighbor : nearby) {
				if (neighbor == i || !collision_eligible[neighbor] ||
						r_particles[neighbor].foreground_component != particle.foreground_component ||
						distance_squared(proposed, proposed_positions[neighbor]) > COLLISION_RADIUS * COLLISION_RADIUS + DISTANCE_EPSILON ||
						!positions_share_pixel_neighborhood(proposed, proposed_positions[neighbor])) {
					continue;
				}
				if (dot(particle.motion, r_particles[neighbor].motion) < 0.0 &&
						dot(particle.motion, proposed_positions[neighbor] - proposed) < 0.0) {
					stop[i] = 1;
					collision_stop[i] = 1;
					break;
				}
			}
		}

		active_count = 0;
		for (int i = 0; i < int(r_particles.size()); i++) {
			if (!r_particles[i].active) {
				continue;
			}
			r_particles[i].traveled += distance(old_positions[i], proposed_positions[i]);
			r_particles[i].position = proposed_positions[i];
			r_particles[i].collided = r_particles[i].collided || collision_stop[i] != 0;
			if (stop[i]) {
				r_particles[i].active = false;
			} else {
				active_count++;
			}
		}
	}
}

double clamped_radius(double p_radius, const Params &p_params) {
	double radius = std::max(0.0, p_radius);
	if (p_params.max_thickness > 0.0f) {
		radius = std::min(radius, double(p_params.max_thickness));
	}
	return radius;
}

float radius_guide_at(const std::vector<float> *p_radius_guide, const PointD &p_position, int p_width, int p_height) {
	if (p_radius_guide == nullptr || p_radius_guide->size() != size_t(p_width) * size_t(p_height)) {
		return -1.0f;
	}
	const int x = std::clamp(int(std::floor(p_position.x)), 0, p_width - 1);
	const int y = std::clamp(int(std::floor(p_position.y)), 0, p_height - 1);
	const float radius = (*p_radius_guide)[size_t(y) * size_t(p_width) + size_t(x)];
	return std::isfinite(double(radius)) && radius > 0.0f ? radius : -1.0f;
}

std::vector<ClusterNodeInternal> create_cluster_nodes(const std::vector<Particle> &p_particles,
		const std::vector<int> &p_foreground_components, int p_width, int p_height,
		const Params &p_params, const std::vector<float> *p_radius_guide) {
	SpatialBuckets particle_buckets;
	particle_buckets.clear(p_particles.size());
	for (int i = 0; i < int(p_particles.size()); i++) {
		if (p_particles[i].collided && in_bounds(p_particles[i].position, p_width, p_height)) {
			particle_buckets.insert(p_particles[i].position, i);
		}
	}

	std::vector<ClusterNodeInternal> nodes;
	nodes.reserve(p_particles.size());
	SpatialBuckets node_buckets;
	std::vector<int> nearby_particles;
	std::vector<int> nearby_nodes;
	for (int i = 0; i < int(p_particles.size()); i++) {
		const Particle &particle = p_particles[i];
		if (!particle.collided || foreground_component_at(p_foreground_components, p_width, p_height, particle.position) !=
				particle.foreground_component) {
			continue;
		}
		particle_buckets.query(particle.position, GRAPH_NEIGHBOR_RADIUS, nearby_particles);
		int neighbor_count = 0;
		double radius = particle.traveled;
		for (int neighbor : nearby_particles) {
			if (p_particles[neighbor].foreground_component == particle.foreground_component &&
					distance_squared(particle.position, p_particles[neighbor].position) <=
					GRAPH_NEIGHBOR_RADIUS * GRAPH_NEIGHBOR_RADIUS + DISTANCE_EPSILON) {
				neighbor_count++;
				radius = std::max(radius, p_particles[neighbor].traveled);
			}
		}
		const float guided_radius = radius_guide_at(p_radius_guide, particle.position, p_width, p_height);
		if (guided_radius > 0.0f) {
			radius = guided_radius;
		}
		if (neighbor_count < 3) {
			continue;
		}

		node_buckets.query(particle.position, POSITION_EPSILON, nearby_nodes);
		int duplicate = -1;
		for (int node : nearby_nodes) {
			if (nodes[node].foreground_component == particle.foreground_component &&
					distance_squared(nodes[node].position, particle.position) <= POSITION_EPSILON * POSITION_EPSILON) {
				duplicate = node;
				break;
			}
		}
		if (duplicate >= 0) {
			nodes[duplicate].radius = std::max(nodes[duplicate].radius, clamped_radius(radius, p_params));
			continue;
		}
		const int node_index = int(nodes.size());
		nodes.push_back({ particle.position, clamped_radius(radius, p_params), particle.foreground_component });
		node_buckets.insert(particle.position, node_index);
	}
	return nodes;
}

int direction_sector(const PointD &p_delta) {
	constexpr double DIAGONAL_BOUNDARY = 2.4142135623730951;
	const double absolute_x = std::abs(p_delta.x);
	const double absolute_y = std::abs(p_delta.y);
	if (absolute_x > DIAGONAL_BOUNDARY * absolute_y) {
		return p_delta.x >= 0.0 ? 0 : 4;
	}
	if (absolute_y > DIAGONAL_BOUNDARY * absolute_x) {
		return p_delta.y >= 0.0 ? 2 : 6;
	}
	if (p_delta.x >= 0.0) {
		return p_delta.y >= 0.0 ? 1 : 7;
	}
	return p_delta.y >= 0.0 ? 3 : 5;
}

std::vector<ClusterEdge> create_cluster_graph(const std::vector<ClusterNodeInternal> &p_nodes,
		const std::vector<int> &p_foreground_components, int p_width, int p_height) {
	SpatialBuckets buckets;
	buckets.clear(p_nodes.size());
	for (int node = 0; node < int(p_nodes.size()); node++) {
		buckets.insert(p_nodes[node].position, node);
	}

	std::vector<std::pair<int, int>> endpoint_pairs;
	endpoint_pairs.reserve(p_nodes.size() * 8);
	std::vector<int> nearby;
	std::array<std::vector<std::pair<double, int>>, 8> directional_candidates;
	for (int i = 0; i < int(p_nodes.size()); i++) {
		const double neighbor_radius = std::max(GRAPH_NEIGHBOR_RADIUS, p_nodes[i].radius);
		buckets.query(p_nodes[i].position, neighbor_radius, nearby);
		for (auto &sector : directional_candidates) {
			sector.clear();
		}
		for (int neighbor : nearby) {
			if (neighbor == i || p_nodes[neighbor].foreground_component != p_nodes[i].foreground_component) {
				continue;
			}
			const PointD delta = p_nodes[neighbor].position - p_nodes[i].position;
			const double edge_length_squared = length_squared(delta);
			if (edge_length_squared <= POSITION_EPSILON * POSITION_EPSILON ||
					edge_length_squared > neighbor_radius * neighbor_radius + DISTANCE_EPSILON) {
				continue;
			}
			directional_candidates[direction_sector(delta)].push_back({ edge_length_squared, neighbor });
		}
		for (auto &sector : directional_candidates) {
			std::sort(sector.begin(), sector.end(), [](const auto &p_a, const auto &p_b) {
				return p_a < p_b;
			});
			for (const auto &candidate : sector) {
				const int neighbor = candidate.second;
				if (positions_share_pixel_neighborhood(p_nodes[i].position, p_nodes[neighbor].position) ||
						segment_supported_by_component(p_foreground_components, p_width, p_height,
								p_nodes[i].position, p_nodes[neighbor].position, p_nodes[i].foreground_component)) {
					endpoint_pairs.push_back(std::minmax(i, neighbor));
					break;
				}
			}
		}
	}
	std::sort(endpoint_pairs.begin(), endpoint_pairs.end());
	endpoint_pairs.erase(std::unique(endpoint_pairs.begin(), endpoint_pairs.end()), endpoint_pairs.end());

	std::vector<ClusterEdge> edges;
	edges.reserve(endpoint_pairs.size());
	for (const std::pair<int, int> &endpoints : endpoint_pairs) {
		edges.push_back({ int(edges.size()), endpoints.first, endpoints.second,
				distance(p_nodes[endpoints.first].position, p_nodes[endpoints.second].position) });
	}
	return edges;
}

struct RasterBridgeCandidate {
	int source_a = -1;
	int source_b = -1;
	int meeting_a = -1;
	int meeting_b = -1;
	int distance = std::numeric_limits<int>::max();
};

// Zhang-Suen thinning removes only simple boundary pixels, so the resulting
// one-pixel scaffold preserves 8-connected foreground components and holes.
std::vector<uint8_t> thin_foreground(const std::vector<double> &p_coverage, int p_width, int p_height) {
	const int pixel_count = p_width * p_height;
	std::vector<uint8_t> skeleton(pixel_count, 0);
	for (int pixel = 0; pixel < pixel_count; pixel++) {
		skeleton[pixel] = p_coverage[pixel] > 0.0;
	}
	if (p_width < 3 || p_height < 3) {
		return skeleton;
	}

	std::vector<int> removable;
	bool changed = false;
	do {
		changed = false;
		for (int pass = 0; pass < 2; pass++) {
			removable.clear();
			for (int y = 1; y + 1 < p_height; y++) {
				for (int x = 1; x + 1 < p_width; x++) {
					const int pixel = y * p_width + x;
					if (!skeleton[pixel]) {
						continue;
					}
					const uint8_t neighbors[8] = {
						skeleton[pixel - p_width],
						skeleton[pixel - p_width + 1],
						skeleton[pixel + 1],
						skeleton[pixel + p_width + 1],
						skeleton[pixel + p_width],
						skeleton[pixel + p_width - 1],
						skeleton[pixel - 1],
						skeleton[pixel - p_width - 1],
					};
					int neighbor_count = 0;
					int transitions = 0;
					for (int neighbor = 0; neighbor < 8; neighbor++) {
						neighbor_count += neighbors[neighbor];
						transitions += !neighbors[neighbor] && neighbors[(neighbor + 1) % 8];
					}
					if (neighbor_count < 2 || neighbor_count > 6 || transitions != 1) {
						continue;
					}
					const bool first_constraint = pass == 0 ?
							!(neighbors[0] && neighbors[2] && neighbors[4]) :
							!(neighbors[0] && neighbors[2] && neighbors[6]);
					const bool second_constraint = pass == 0 ?
							!(neighbors[2] && neighbors[4] && neighbors[6]) :
							!(neighbors[0] && neighbors[4] && neighbors[6]);
					if (first_constraint && second_constraint) {
						removable.push_back(pixel);
					}
				}
			}
			for (int pixel : removable) {
				skeleton[pixel] = 0;
			}
			changed |= !removable.empty();
		}
	} while (changed);
	return skeleton;
}

bool skeleton_step_allowed(const std::vector<uint8_t> &p_skeleton, int p_width,
		int p_x, int p_y, int p_dx, int p_dy) {
	if (p_dx == 0 || p_dy == 0) {
		return true;
	}
	return !p_skeleton[p_y * p_width + p_x + p_dx] &&
			!p_skeleton[(p_y + p_dy) * p_width + p_x];
}

void seed_unrepresented_foreground_components(const std::vector<uint8_t> &p_skeleton,
		const std::vector<int> &p_foreground_components, int p_width, int p_height,
		const Params &p_params, const std::vector<float> *p_radius_guide,
		std::vector<ClusterNodeInternal> &r_nodes, std::vector<ClusterEdge> &r_edges) {
	std::unordered_set<int> represented_components;
	for (const ClusterEdge &edge : r_edges) {
		represented_components.insert(r_nodes[edge.a].foreground_component);
	}

	bool has_unrepresented_foreground = false;
	for (int component : p_foreground_components) {
		if (component >= 0 && represented_components.find(component) == represented_components.end()) {
			has_unrepresented_foreground = true;
			break;
		}
	}
	if (!has_unrepresented_foreground) {
		return;
	}

	std::unordered_set<int> branching_components;
	constexpr int neighbor_offsets[8][2] = {
		{ -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 },
		{ 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 },
	};
	for (int pixel = 0; pixel < int(p_skeleton.size()); pixel++) {
		if (!p_skeleton[pixel]) {
			continue;
		}
		const int x = pixel % p_width;
		const int y = pixel / p_width;
		int degree = 0;
		for (const auto &offset : neighbor_offsets) {
			const int nx = x + offset[0];
			const int ny = y + offset[1];
			if (nx >= 0 && ny >= 0 && nx < p_width && ny < p_height &&
					p_skeleton[ny * p_width + nx] &&
					skeleton_step_allowed(p_skeleton, p_width, x, y, offset[0], offset[1])) {
				degree++;
			}
		}
		if (degree > 2) {
			branching_components.insert(p_foreground_components[pixel]);
		}
	}
	std::vector<int> pixel_nodes(p_skeleton.size(), -1);
	for (int pixel = 0; pixel < int(p_skeleton.size()); pixel++) {
		const int component = p_foreground_components[pixel];
		if (!p_skeleton[pixel] || component < 0 || represented_components.find(component) != represented_components.end() ||
				branching_components.find(component) != branching_components.end()) {
			continue;
		}
		const PointD position = { double(pixel % p_width) + 0.5, double(pixel / p_width) + 0.5 };
		const float guided_radius = radius_guide_at(p_radius_guide, position, p_width, p_height);
		const double radius = guided_radius > 0.0f ? double(guided_radius) : 0.5;
		pixel_nodes[pixel] = int(r_nodes.size());
		r_nodes.push_back({ position, clamped_radius(radius, p_params), component });
	}

	constexpr int edge_offsets[4][2] = {
		{ 1, 0 }, { 0, 1 }, { 1, 1 }, { -1, 1 },
	};
	for (int pixel = 0; pixel < int(p_skeleton.size()); pixel++) {
		if (pixel_nodes[pixel] < 0) {
			continue;
		}
		const int x = pixel % p_width;
		const int y = pixel / p_width;
		for (const auto &offset : edge_offsets) {
			const int nx = x + offset[0];
			const int ny = y + offset[1];
			if (nx < 0 || ny < 0 || nx >= p_width || ny >= p_height) {
				continue;
			}
			const int neighbor = ny * p_width + nx;
			if (pixel_nodes[neighbor] < 0 || !skeleton_step_allowed(p_skeleton, p_width, x, y, offset[0], offset[1])) {
				continue;
			}
			const int a = pixel_nodes[pixel];
			const int b = pixel_nodes[neighbor];
			r_edges.push_back({ int(r_edges.size()), a, b, distance(r_nodes[a].position, r_nodes[b].position) });
		}
	}
}

void restore_foreground_connectivity(const std::vector<double> &p_coverage,
		const std::vector<uint8_t> &p_skeleton, const std::vector<int> &p_foreground_components, int p_width, int p_height,
		const Params &p_params, const std::vector<float> *p_radius_guide,
		std::vector<ClusterNodeInternal> &r_nodes, std::vector<ClusterEdge> &r_edges,
		std::vector<uint8_t> &r_selected) {
	const int original_node_count = int(r_nodes.size());
	if (original_node_count < 2 || r_selected.size() != r_edges.size()) {
		return;
	}

	DisjointSet components(original_node_count);
	for (size_t edge = 0; edge < r_edges.size(); edge++) {
		if (r_selected[edge]) {
			components.unite(r_edges[edge].a, r_edges[edge].b);
		}
	}
	std::vector<int> degrees(original_node_count, 0);
	for (size_t edge = 0; edge < r_edges.size(); edge++) {
		if (r_selected[edge]) {
			degrees[r_edges[edge].a]++;
			degrees[r_edges[edge].b]++;
		}
	}
	std::unordered_set<int> active_components;
	for (int node = 0; node < original_node_count; node++) {
		if (degrees[node] > 0) {
			active_components.insert(components.find(node));
		}
	}
	if (active_components.empty()) {
		return;
	}
	const bool has_disconnected_selected_graph = active_components.size() >= 2;

	const int pixel_count = p_width * p_height;
	constexpr int neighbor_offsets[8][2] = {
		{ -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 },
		{ 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 },
	};
	std::vector<uint8_t> skeleton_degrees(pixel_count, 0);
	for (int pixel = 0; pixel < pixel_count; pixel++) {
		if (!p_skeleton[pixel]) {
			continue;
		}
		const int x = pixel % p_width;
		const int y = pixel / p_width;
		for (const auto &offset : neighbor_offsets) {
			const int nx = x + offset[0];
			const int ny = y + offset[1];
			if (nx >= 0 && ny >= 0 && nx < p_width && ny < p_height &&
					p_skeleton[ny * p_width + nx] &&
					skeleton_step_allowed(p_skeleton, p_width, x, y, offset[0], offset[1])) {
				skeleton_degrees[pixel]++;
			}
		}
	}
	std::vector<uint8_t> near_skeleton_junctions(pixel_count, 0);
	for (int pixel = 0; pixel < pixel_count; pixel++) {
		if (!p_skeleton[pixel]) {
			continue;
		}
		if (skeleton_degrees[pixel] > 2) {
			near_skeleton_junctions[pixel] = 1;
			continue;
		}
		const int x = pixel % p_width;
		const int y = pixel / p_width;
		for (const auto &offset : neighbor_offsets) {
			const int nx = x + offset[0];
			const int ny = y + offset[1];
			if (nx >= 0 && ny >= 0 && nx < p_width && ny < p_height &&
					p_skeleton[ny * p_width + nx] && skeleton_degrees[ny * p_width + nx] > 2) {
				near_skeleton_junctions[pixel] = 1;
				break;
			}
		}
	}
	auto skeleton_degree = [&](int p_pixel) {
		return int(skeleton_degrees[p_pixel]);
	};
	auto near_skeleton_junction = [&](int p_pixel) {
		return near_skeleton_junctions[p_pixel] != 0;
	};
	std::unordered_set<int> branching_foreground_components;
	for (int pixel = 0; pixel < pixel_count; pixel++) {
		if (p_skeleton[pixel] && skeleton_degree(pixel) > 2) {
			branching_foreground_components.insert(p_foreground_components[pixel]);
		}
	}
	std::vector<int> frontier;
	frontier.reserve(4096);
	size_t frontier_head = 0;
	std::vector<int> nearest_skeleton(pixel_count, -1);
	std::vector<int> skeleton_predecessor(pixel_count, -1);
	for (int pixel = 0; pixel < pixel_count; pixel++) {
		if (p_skeleton[pixel]) {
			nearest_skeleton[pixel] = pixel;
			frontier.push_back(pixel);
		}
	}
	while (frontier_head < frontier.size()) {
		const int pixel = frontier[frontier_head++];
		const int x = pixel % p_width;
		const int y = pixel / p_width;
		for (const auto &offset : neighbor_offsets) {
			const int nx = x + offset[0];
			const int ny = y + offset[1];
			if (nx < 0 || ny < 0 || nx >= p_width || ny >= p_height) {
				continue;
			}
			const int neighbor = ny * p_width + nx;
			if (p_coverage[neighbor] <= 0.0 || nearest_skeleton[neighbor] >= 0) {
				continue;
			}
			nearest_skeleton[neighbor] = nearest_skeleton[pixel];
			skeleton_predecessor[neighbor] = pixel;
			frontier.push_back(neighbor);
		}
	}
	frontier.clear();
	frontier_head = 0;

	std::vector<int> projected_pixel(original_node_count, -1);
	std::vector<int> projection_anchor(original_node_count, -1);
	auto project_node = [&](int p_node) {
		const PointD &position = r_nodes[p_node].position;
		const int x = std::clamp(int(std::floor(position.x)), 0, p_width - 1);
		const int y = std::clamp(int(std::floor(position.y)), 0, p_height - 1);
		const int pixel = y * p_width + x;
		const int foreground_component = r_nodes[p_node].foreground_component;
		if (foreground_component < 0) {
			return -1;
		}
		if (p_foreground_components[pixel] == foreground_component && nearest_skeleton[pixel] >= 0) {
			projection_anchor[p_node] = pixel;
			return nearest_skeleton[pixel];
		}
		const double support = std::max(GRAPH_NEIGHBOR_RADIUS, r_nodes[p_node].radius) + std::sqrt(0.5);
		const int reach = std::max(1, int(std::ceil(support)));
		int nearest_anchor = -1;
		double nearest_distance = std::numeric_limits<double>::infinity();
		for (int ny = std::max(0, y - reach); ny <= std::min(p_height - 1, y + reach); ny++) {
			for (int nx = std::max(0, x - reach); nx <= std::min(p_width - 1, x + reach); nx++) {
				const int candidate = ny * p_width + nx;
				if (p_foreground_components[candidate] != foreground_component || nearest_skeleton[candidate] < 0) {
					continue;
				}
				const PointD center = { double(nx) + 0.5, double(ny) + 0.5 };
				const double candidate_distance = distance_squared(position, center);
				if (candidate_distance <= support * support + DISTANCE_EPSILON && candidate_distance < nearest_distance) {
					nearest_distance = candidate_distance;
					nearest_anchor = candidate;
				}
			}
		}
		if (nearest_anchor < 0) {
			return -1;
		}
		projection_anchor[p_node] = nearest_anchor;
		return nearest_skeleton[nearest_anchor];
	};
	for (int node = 0; node < original_node_count; node++) {
		if (degrees[node] == 1) {
			projected_pixel[node] = project_node(node);
		}
	}
	// A round cap can create several medial leaves at one raster frontier. Keep
	// one representative before extending that frontier, otherwise the siblings
	// become short spurs as soon as the continuation is restored.
	std::unordered_map<uint64_t, std::vector<int>> coincident_frontiers;
	coincident_frontiers.reserve(original_node_count);
	for (int node = 0; node < original_node_count; node++) {
		const int pixel = projected_pixel[node];
		if (degrees[node] != 1 || pixel < 0 || near_skeleton_junction(pixel)) {
			continue;
		}
		const uint64_t key = (uint64_t(uint32_t(components.find(node))) << 32) | uint32_t(pixel);
		coincident_frontiers[key].push_back(node);
	}
	std::vector<std::vector<int>> selected_incident(original_node_count);
	for (int node = 0; node < original_node_count; node++) {
		selected_incident[node].reserve(degrees[node]);
	}
	for (int edge = 0; edge < int(r_edges.size()); edge++) {
		if (r_selected[edge]) {
			selected_incident[r_edges[edge].a].push_back(edge);
			selected_incident[r_edges[edge].b].push_back(edge);
		}
	}
	for (const auto &entry : coincident_frontiers) {
		const std::vector<int> &leaves = entry.second;
		if (!has_disconnected_selected_graph || leaves.size() < 2) {
			continue;
		}
		std::vector<std::vector<int>> leaf_paths(leaves.size());
		int common_hub = -1;
		bool collapsible = true;
		for (size_t leaf_index = 0; leaf_index < leaves.size() && collapsible; leaf_index++) {
			int current = leaves[leaf_index];
			int previous_edge = -1;
			for (int guard = 0; guard <= original_node_count; guard++) {
				if (current != leaves[leaf_index] && degrees[current] != 2) {
					if (degrees[current] < 3 || (common_hub >= 0 && common_hub != current)) {
						collapsible = false;
					} else {
						common_hub = current;
					}
					break;
				}
				int next_edge = -1;
				for (int edge : selected_incident[current]) {
					if (r_selected[edge] && edge != previous_edge) {
						if (next_edge >= 0) {
							collapsible = false;
							break;
						}
						next_edge = edge;
					}
				}
				if (!collapsible || next_edge < 0) {
					collapsible = false;
					break;
				}
				leaf_paths[leaf_index].push_back(next_edge);
				const ClusterEdge &edge = r_edges[next_edge];
				const int next = edge.a == current ? edge.b : edge.a;
				previous_edge = next_edge;
				current = next;
			}
		}
		if (!collapsible || common_hub < 0 || degrees[common_hub] != int(leaves.size()) + 1) {
			continue;
		}
		const int pixel = int(uint32_t(entry.first));
		const PointD center = { double(pixel % p_width) + 0.5, double(pixel / p_width) + 0.5 };
		int representative = leaves.front();
		for (int leaf : leaves) {
			const double error = distance_squared(r_nodes[leaf].position, center);
			const double best_error = distance_squared(r_nodes[representative].position, center);
			if (error < best_error - DISTANCE_EPSILON ||
					(std::abs(error - best_error) <= DISTANCE_EPSILON && leaf < representative)) {
				representative = leaf;
			}
		}
		for (size_t leaf_index = 0; leaf_index < leaves.size(); leaf_index++) {
			const int leaf = leaves[leaf_index];
			if (leaf == representative) {
				continue;
			}
			for (int selected_edge : leaf_paths[leaf_index]) {
				if (!r_selected[selected_edge]) {
					continue;
				}
				r_selected[selected_edge] = 0;
				degrees[r_edges[selected_edge].a]--;
				degrees[r_edges[selected_edge].b]--;
			}
		}
	}
	components = DisjointSet(original_node_count);
	std::fill(degrees.begin(), degrees.end(), 0);
	active_components.clear();
	for (size_t edge = 0; edge < r_edges.size(); edge++) {
		if (r_selected[edge]) {
			components.unite(r_edges[edge].a, r_edges[edge].b);
			degrees[r_edges[edge].a]++;
			degrees[r_edges[edge].b]++;
		}
	}
	for (int node = 0; node < original_node_count; node++) {
		if (degrees[node] > 0) {
			active_components.insert(components.find(node));
		}
	}
	if (active_components.empty()) {
		return;
	}

	std::vector<int> owner(pixel_count, -1);
	std::vector<int> source(pixel_count, -1);
	std::vector<int> predecessor(pixel_count, -1);
	std::vector<int> raster_distance(pixel_count, -1);
	std::vector<RasterBridgeCandidate> candidates;
	for (int node = 0; node < original_node_count; node++) {
		if (degrees[node] != 1) {
			continue;
		}
		const int pixel = projected_pixel[node];
		if (pixel < 0) {
			continue;
		}
		const int root = components.find(node);
		if (owner[pixel] < 0) {
			owner[pixel] = root;
			source[pixel] = node;
			raster_distance[pixel] = 0;
			frontier.push_back(pixel);
		} else if (owner[pixel] != root) {
			candidates.push_back({ source[pixel], node, pixel, pixel, 0 });
		}
	}
	while (frontier_head < frontier.size()) {
		const int pixel = frontier[frontier_head++];
		const int x = pixel % p_width;
		const int y = pixel / p_width;
		for (const auto &offset : neighbor_offsets) {
			const int nx = x + offset[0];
			const int ny = y + offset[1];
			if (nx < 0 || ny < 0 || nx >= p_width || ny >= p_height ||
					!p_skeleton[ny * p_width + nx] ||
					!skeleton_step_allowed(p_skeleton, p_width, x, y, offset[0], offset[1])) {
				continue;
			}
			const int neighbor = ny * p_width + nx;
			if (owner[neighbor] >= 0) {
				continue;
			}
			owner[neighbor] = owner[pixel];
			source[neighbor] = source[pixel];
			predecessor[neighbor] = pixel;
			raster_distance[neighbor] = raster_distance[pixel] + 1;
			frontier.push_back(neighbor);
		}
	}
	constexpr int boundary_offsets[4][2] = {
		{ 1, 0 }, { 0, 1 }, { 1, 1 }, { -1, 1 },
	};
	for (int pixel = 0; pixel < pixel_count; pixel++) {
		if (owner[pixel] < 0) {
			continue;
		}
		const int x = pixel % p_width;
		const int y = pixel / p_width;
		for (const auto &offset : boundary_offsets) {
			const int nx = x + offset[0];
			const int ny = y + offset[1];
			if (nx < 0 || ny < 0 || nx >= p_width || ny >= p_height ||
					!p_skeleton[ny * p_width + nx] ||
					!skeleton_step_allowed(p_skeleton, p_width, x, y, offset[0], offset[1])) {
				continue;
			}
			const int neighbor = ny * p_width + nx;
			if (owner[neighbor] < 0 || owner[neighbor] == owner[pixel]) {
				continue;
			}
			if (near_skeleton_junction(pixel) || near_skeleton_junction(neighbor)) {
				continue;
			}
			candidates.push_back({ source[pixel], source[neighbor], pixel, neighbor,
					raster_distance[pixel] + raster_distance[neighbor] + 1 });
		}
	}
	auto candidate_less = [](const RasterBridgeCandidate &p_a, const RasterBridgeCandidate &p_b) {
		return std::tie(p_a.distance, p_a.source_a, p_a.source_b, p_a.meeting_a, p_a.meeting_b) <
				std::tie(p_b.distance, p_b.source_a, p_b.source_b, p_b.meeting_a, p_b.meeting_b);
	};
	auto component_pair_key = [&](const RasterBridgeCandidate &p_candidate) {
		int first = components.find(p_candidate.source_a);
		int second = components.find(p_candidate.source_b);
		if (second < first) {
			std::swap(first, second);
		}
		return (uint64_t(uint32_t(first)) << 32) | uint32_t(second);
	};
	std::unordered_map<uint64_t, std::vector<int>> candidates_by_pair;
	candidates_by_pair.reserve(candidates.size());
	for (int candidate = 0; candidate < int(candidates.size()); candidate++) {
		candidates_by_pair[component_pair_key(candidates[candidate])].push_back(candidate);
	}
	std::vector<RasterBridgeCandidate> distinct_candidates;
	for (const auto &entry : candidates_by_pair) {
		const std::vector<int> &indices = entry.second;
		DisjointSet interfaces(int(indices.size()));
		auto adjacent_pixels = [&](int p_a, int p_b) {
			return std::abs(p_a % p_width - p_b % p_width) <= 1 &&
					std::abs(p_a / p_width - p_b / p_width) <= 1;
		};
		for (int first = 0; first < int(indices.size()); first++) {
			const RasterBridgeCandidate &a = candidates[indices[first]];
			for (int second = first + 1; second < int(indices.size()); second++) {
				const RasterBridgeCandidate &b = candidates[indices[second]];
				if (adjacent_pixels(a.meeting_a, b.meeting_a) || adjacent_pixels(a.meeting_a, b.meeting_b) ||
						adjacent_pixels(a.meeting_b, b.meeting_a) || adjacent_pixels(a.meeting_b, b.meeting_b)) {
					interfaces.unite(first, second);
				}
			}
		}
		std::unordered_map<int, int> best_by_interface;
		best_by_interface.reserve(indices.size());
		for (int candidate = 0; candidate < int(indices.size()); candidate++) {
			const int root = interfaces.find(candidate);
			auto found = best_by_interface.find(root);
			if (found == best_by_interface.end() ||
					candidate_less(candidates[indices[candidate]], candidates[found->second])) {
				best_by_interface[root] = indices[candidate];
			}
		}
		for (const auto &best : best_by_interface) {
			distinct_candidates.push_back(candidates[best.second]);
		}
	}
	candidates = std::move(distinct_candidates);
	std::stable_sort(candidates.begin(), candidates.end(), candidate_less);

	auto node_pair_key = [](int p_a, int p_b) {
		if (p_b < p_a) {
			std::swap(p_a, p_b);
		}
		return (uint64_t(uint32_t(p_a)) << 32) | uint32_t(p_b);
	};
	std::unordered_map<uint64_t, int> edges_by_pair;
	edges_by_pair.reserve(r_edges.size() * 2 + candidates.size() * 2);
	for (int edge = 0; edge < int(r_edges.size()); edge++) {
		edges_by_pair[node_pair_key(r_edges[edge].a, r_edges[edge].b)] = edge;
	}
	auto append_edge = [&](int p_a, int p_b) {
		if (p_a == p_b) {
			return;
		}
		const uint64_t key = node_pair_key(p_a, p_b);
		auto found = edges_by_pair.find(key);
		if (found != edges_by_pair.end()) {
			r_selected[found->second] = 1;
			return;
		}
		edges_by_pair[key] = int(r_edges.size());
		r_edges.push_back({ int(r_edges.size()), p_a, p_b, distance(r_nodes[p_a].position, r_nodes[p_b].position) });
		r_selected.push_back(1);
	};
	auto pixel_chain = [&](int p_pixel) {
		std::vector<int> chain;
		for (int pixel = p_pixel; pixel >= 0; pixel = predecessor[pixel]) {
			chain.push_back(pixel);
		}
		return chain;
	};
	std::vector<int> pixel_nodes(pixel_count, -1);
	for (int node = 0; node < original_node_count; node++) {
		const int pixel = projected_pixel[node];
		if (degrees[node] == 1 && pixel >= 0 && projection_anchor[node] == pixel && pixel_nodes[pixel] < 0) {
			pixel_nodes[pixel] = node;
		}
	}
	auto node_for_pixel = [&](int p_pixel) {
		if (pixel_nodes[p_pixel] >= 0) {
			return pixel_nodes[p_pixel];
		}
		const PointD position = { double(p_pixel % p_width) + 0.5, double(p_pixel / p_width) + 0.5 };
		const float guided_radius = radius_guide_at(p_radius_guide, position, p_width, p_height);
		const double radius = guided_radius > 0.0f ? double(guided_radius) : 0.5;
		const int node = int(r_nodes.size());
		r_nodes.push_back({ position, clamped_radius(radius, p_params), p_foreground_components[p_pixel] });
		pixel_nodes[p_pixel] = node;
		return node;
	};
	auto projection_chain = [&](int p_node) {
		std::vector<int> chain;
		const int target = projected_pixel[p_node];
		for (int pixel = projection_anchor[p_node]; pixel >= 0; pixel = skeleton_predecessor[pixel]) {
			chain.push_back(pixel);
			if (pixel == target) {
				break;
			}
		}
		if (chain.empty() || chain.back() != target) {
			chain.clear();
		}
		return chain;
	};
	auto append_pixel_path = [](std::vector<int> &r_path, const std::vector<int> &p_suffix) {
		size_t begin = !r_path.empty() && !p_suffix.empty() && r_path.back() == p_suffix.front() ? 1 : 0;
		r_path.insert(r_path.end(), p_suffix.begin() + std::ptrdiff_t(begin), p_suffix.end());
	};
	for (const RasterBridgeCandidate &candidate : candidates) {
		if (candidate.source_a < 0 || candidate.source_b < 0) {
			continue;
		}
		if (near_skeleton_junction(candidate.meeting_a) || near_skeleton_junction(candidate.meeting_b)) {
			continue;
		}
		std::vector<int> first = pixel_chain(candidate.meeting_a);
		std::vector<int> second = pixel_chain(candidate.meeting_b);
		std::reverse(first.begin(), first.end());
		if (!first.empty() && !second.empty() && first.back() == second.front()) {
			second.erase(second.begin());
		}
		first.insert(first.end(), second.begin(), second.end());
		if (std::any_of(first.begin(), first.end(), near_skeleton_junction)) {
			continue;
		}
		std::vector<int> path = projection_chain(candidate.source_a);
		std::vector<int> tail = projection_chain(candidate.source_b);
		if (path.empty() || tail.empty()) {
			continue;
		}
		append_pixel_path(path, first);
		std::reverse(tail.begin(), tail.end());
		append_pixel_path(path, tail);

		int previous = candidate.source_a;
		for (int pixel : path) {
			const int pixel_node = node_for_pixel(pixel);
			append_edge(previous, pixel_node);
			previous = pixel_node;
		}
		append_edge(previous, candidate.source_b);
	}

	const int completed_node_count = int(r_nodes.size());
	std::vector<int> completed_degrees(completed_node_count, 0);
	for (int edge = 0; edge < int(r_edges.size()); edge++) {
		if (r_selected[edge]) {
			completed_degrees[r_edges[edge].a]++;
			completed_degrees[r_edges[edge].b]++;
		}
	}
	std::unordered_set<int> branching_selected_components;
	for (int node = 0; node < completed_node_count; node++) {
		if (completed_degrees[node] > 2) {
			branching_selected_components.insert(r_nodes[node].foreground_component);
		}
	}
	std::vector<int> completion_projection(completed_node_count, -1);
	std::vector<int> first_occupied_node(pixel_count, -1);
	std::vector<int> next_occupied_node(completed_node_count, -1);
	for (int node = 0; node < completed_node_count; node++) {
		if (completed_degrees[node] <= 0) {
			continue;
		}
		int pixel = -1;
		if (node < original_node_count) {
			pixel = project_node(node);
		} else {
			const int x = std::clamp(int(std::floor(r_nodes[node].position.x)), 0, p_width - 1);
			const int y = std::clamp(int(std::floor(r_nodes[node].position.y)), 0, p_height - 1);
			const int anchor = y * p_width + x;
			if (p_foreground_components[anchor] == r_nodes[node].foreground_component) {
				pixel = nearest_skeleton[anchor];
			}
		}
		if (pixel < 0) {
			continue;
		}
		completion_projection[node] = pixel;
		next_occupied_node[node] = first_occupied_node[pixel];
		first_occupied_node[pixel] = node;
	}

	struct EndpointExtension {
		int source_node = -1;
		std::vector<int> pixels;
	};
	std::vector<EndpointExtension> endpoint_extensions;
	std::vector<uint8_t> claimed_source(completed_node_count, 0);
	std::vector<uint8_t> claimed_pixel(pixel_count, 0);
	for (int terminal = 0; terminal < pixel_count; terminal++) {
		if (!p_skeleton[terminal] || skeleton_degree(terminal) != 1 || near_skeleton_junction(terminal)) {
			continue;
		}
		if (branching_foreground_components.find(p_foreground_components[terminal]) != branching_foreground_components.end()) {
			continue;
		}
		if (branching_selected_components.find(p_foreground_components[terminal]) != branching_selected_components.end()) {
			continue;
		}
		std::vector<int> reverse_path;
		int previous = -1;
		int current = terminal;
		int source_node = -1;
		bool valid = true;
		for (int guard = 0; guard < pixel_count; guard++) {
			if (near_skeleton_junction(current)) {
				valid = false;
				break;
			}
			reverse_path.push_back(current);
			if (first_occupied_node[current] >= 0) {
				for (int node = first_occupied_node[current]; node >= 0; node = next_occupied_node[node]) {
					if (completed_degrees[node] == 1 && !claimed_source[node]) {
						source_node = node;
						break;
					}
				}
				valid = source_node >= 0;
				break;
			}
			const int x = current % p_width;
			const int y = current / p_width;
			int next = -1;
			for (const auto &offset : neighbor_offsets) {
				const int nx = x + offset[0];
				const int ny = y + offset[1];
				if (nx < 0 || ny < 0 || nx >= p_width || ny >= p_height) {
					continue;
				}
				const int neighbor = ny * p_width + nx;
				if (neighbor == previous || !p_skeleton[neighbor] || near_skeleton_junction(neighbor) ||
						!skeleton_step_allowed(p_skeleton, p_width, x, y, offset[0], offset[1])) {
					continue;
				}
				if (next >= 0) {
					valid = false;
					break;
				}
				next = neighbor;
			}
			if (!valid || next < 0) {
				valid = false;
				break;
			}
			previous = current;
			current = next;
		}
		if (!valid || source_node < 0 || reverse_path.size() < 2 || claimed_source[source_node] ||
				completion_projection[source_node] != reverse_path.back()) {
			continue;
		}
		const PointD terminal_center = {
			double(terminal % p_width) + 0.5,
			double(terminal / p_width) + 0.5,
		};
		const double support_radius = std::max(0.0, r_nodes[source_node].radius) + std::sqrt(0.5);
		if (distance(r_nodes[source_node].position, terminal_center) <= support_radius + DISTANCE_EPSILON) {
			continue;
		}
		const int foreground_component = r_nodes[source_node].foreground_component;
		for (int pixel : reverse_path) {
			if (p_foreground_components[pixel] != foreground_component || claimed_pixel[pixel]) {
				valid = false;
				break;
			}
		}
		if (!valid) {
			continue;
		}
		std::reverse(reverse_path.begin(), reverse_path.end());
		claimed_source[source_node] = 1;
		for (size_t pixel_index = 1; pixel_index < reverse_path.size(); pixel_index++) {
			claimed_pixel[reverse_path[pixel_index]] = 1;
		}
		endpoint_extensions.push_back({ source_node, std::move(reverse_path) });
	}
	for (const EndpointExtension &extension : endpoint_extensions) {
		std::vector<int> path = projection_chain(extension.source_node);
		if (path.empty()) {
			continue;
		}
		append_pixel_path(path, extension.pixels);
		int previous = extension.source_node;
		for (int pixel : path) {
			const int pixel_node = node_for_pixel(pixel);
			append_edge(previous, pixel_node);
			previous = pixel_node;
		}
	}
}

std::vector<std::vector<int>> build_adjacency(int p_node_count, const std::vector<ClusterEdge> &p_edges) {
	std::vector<std::vector<int>> adjacency(p_node_count);
	std::vector<int> incident_counts(p_node_count, 0);
	for (const ClusterEdge &edge : p_edges) {
		incident_counts[edge.a]++;
		incident_counts[edge.b]++;
	}
	for (int node = 0; node < p_node_count; node++) {
		adjacency[node].reserve(incident_counts[node]);
	}
	for (const ClusterEdge &edge : p_edges) {
		adjacency[edge.a].push_back(edge.id);
		adjacency[edge.b].push_back(edge.id);
	}
	for (std::vector<int> &incident : adjacency) {
		std::sort(incident.begin(), incident.end());
	}
	return adjacency;
}

int other_endpoint(const ClusterEdge &p_edge, int p_node) {
	return p_edge.a == p_node ? p_edge.b : p_edge.a;
}

std::vector<uint8_t> kruskal_forest(int p_node_count, const std::vector<ClusterEdge> &p_edges,
		const std::vector<int> &p_candidates, const std::vector<uint8_t> *p_zero_weight_edges = nullptr) {
	std::vector<int> candidates = p_candidates;
	std::sort(candidates.begin(), candidates.end(), [&](int p_a, int p_b) {
		const double weight_a = p_zero_weight_edges && (*p_zero_weight_edges)[p_a] ? 0.0 : p_edges[p_a].weight;
		const double weight_b = p_zero_weight_edges && (*p_zero_weight_edges)[p_b] ? 0.0 : p_edges[p_b].weight;
		if (weight_a != weight_b) {
			return weight_a < weight_b;
		}
		return p_a < p_b;
	});
	DisjointSet sets(p_node_count);
	std::vector<uint8_t> selected(p_edges.size(), 0);
	for (int edge_id : candidates) {
		const ClusterEdge &edge = p_edges[edge_id];
		if (sets.unite(edge.a, edge.b)) {
			selected[edge_id] = 1;
		}
	}
	return selected;
}

std::vector<int> selected_degrees(int p_node_count, const std::vector<ClusterEdge> &p_edges, const std::vector<uint8_t> &p_selected) {
	std::vector<int> degrees(p_node_count, 0);
	for (const ClusterEdge &edge : p_edges) {
		if (p_selected[edge.id]) {
			degrees[edge.a]++;
			degrees[edge.b]++;
		}
	}
	return degrees;
}

void prune_leaves(const std::vector<ClusterNodeInternal> &p_nodes, const std::vector<ClusterEdge> &p_edges,
		const std::vector<std::vector<int>> &p_adjacency, std::vector<uint8_t> &r_selected,
		const std::vector<uint8_t> *p_protected_leaves = nullptr) {
	std::vector<int> degrees = selected_degrees(int(p_nodes.size()), p_edges, r_selected);
	std::unordered_map<int, int> selected_edges_by_foreground_component;
	for (const ClusterEdge &edge : p_edges) {
		if (r_selected[edge.id]) {
			selected_edges_by_foreground_component[p_nodes[edge.a].foreground_component]++;
		}
	}
	std::vector<double> branch_lengths(p_nodes.size(), 0.0);
	std::priority_queue<int, std::vector<int>, std::greater<int>> leaves;
	for (int node = 0; node < int(p_nodes.size()); node++) {
		if (degrees[node] == 1 && (!p_protected_leaves || !(*p_protected_leaves)[node])) {
			leaves.push(node);
		}
	}
	while (!leaves.empty()) {
		const int leaf = leaves.top();
		leaves.pop();
		if (degrees[leaf] != 1 || (p_protected_leaves && (*p_protected_leaves)[leaf])) {
			continue;
		}
		int leaf_edge = -1;
		for (int edge_id : p_adjacency[leaf]) {
			if (r_selected[edge_id]) {
				leaf_edge = edge_id;
				break;
			}
		}
		if (leaf_edge < 0) {
			continue;
		}
		int &component_edge_count = selected_edges_by_foreground_component[p_nodes[leaf].foreground_component];
		if (component_edge_count <= 1) {
			continue;
		}
		const double next_branch_length = branch_lengths[leaf] + p_edges[leaf_edge].weight;
		if (next_branch_length > p_nodes[leaf].radius + DISTANCE_EPSILON) {
			continue;
		}
		const int neighbor = other_endpoint(p_edges[leaf_edge], leaf);
		r_selected[leaf_edge] = 0;
		component_edge_count--;
		degrees[leaf]--;
		degrees[neighbor]--;
		branch_lengths[neighbor] = std::max(branch_lengths[neighbor], next_branch_length);
		if (degrees[neighbor] == 1 && (!p_protected_leaves || !(*p_protected_leaves)[neighbor])) {
			leaves.push(neighbor);
		}
	}
}

void repair_loops(const std::vector<ClusterNodeInternal> &p_nodes, const std::vector<ClusterEdge> &p_edges,
		const std::vector<std::vector<int>> &p_adjacency, const std::vector<uint8_t> &p_global_mst,
		const std::vector<int> &p_surviving_leaves, std::vector<uint8_t> &r_union) {
	SpatialBuckets node_buckets;
	node_buckets.clear(p_nodes.size());
	for (int node = 0; node < int(p_nodes.size()); node++) {
		node_buckets.insert(p_nodes[node].position, node);
	}
	std::vector<int> nearby;
	std::vector<int> component;
	std::vector<int> candidates;
	std::vector<uint8_t> inside(p_nodes.size(), 0);
	std::vector<uint8_t> reached(p_nodes.size(), 0);
	std::vector<int> stack;
	std::vector<int> current_degrees = selected_degrees(int(p_nodes.size()), p_edges, r_union);
	for (int leaf : p_surviving_leaves) {
		if (current_degrees[leaf] != 1) {
			continue;
		}
		const double neighborhood_radius = 4.0 * p_nodes[leaf].radius;
		if (neighborhood_radius <= DISTANCE_EPSILON) {
			continue;
		}
		std::fill(inside.begin(), inside.end(), 0);
		std::fill(reached.begin(), reached.end(), 0);
		node_buckets.query(p_nodes[leaf].position, neighborhood_radius, nearby);
		for (int node : nearby) {
			if (distance_squared(p_nodes[leaf].position, p_nodes[node].position) <=
					neighborhood_radius * neighborhood_radius + DISTANCE_EPSILON) {
				inside[node] = 1;
			}
		}
		inside[leaf] = 1;
		component.clear();
		stack.clear();
		stack.push_back(leaf);
		reached[leaf] = 1;
		while (!stack.empty()) {
			const int node = stack.back();
			stack.pop_back();
			component.push_back(node);
			for (int edge_id : p_adjacency[node]) {
				const int neighbor = other_endpoint(p_edges[edge_id], node);
				if (inside[neighbor] && !reached[neighbor]) {
					reached[neighbor] = 1;
					stack.push_back(neighbor);
				}
			}
		}
		if (component.size() < 2) {
			continue;
		}
		candidates.clear();
		for (int node : component) {
			for (int edge_id : p_adjacency[node]) {
				const ClusterEdge &edge = p_edges[edge_id];
				if (edge.a == node && reached[edge.b]) {
					candidates.push_back(edge_id);
				}
			}
		}
		std::sort(candidates.begin(), candidates.end());
		candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
		std::vector<uint8_t> local_mst = kruskal_forest(int(p_nodes.size()), p_edges, candidates, &p_global_mst);
		prune_leaves(p_nodes, p_edges, p_adjacency, local_mst);
		for (int edge_id : candidates) {
			if (local_mst[edge_id] && !r_union[edge_id]) {
				r_union[edge_id] = 1;
				current_degrees[p_edges[edge_id].a]++;
				current_degrees[p_edges[edge_id].b]++;
			}
		}
	}
}

std::vector<AbstractRoute> compress_topology(int p_node_count, const std::vector<ClusterEdge> &p_edges,
		const std::vector<std::vector<int>> &p_adjacency, const std::vector<uint8_t> &p_selected,
		std::vector<uint8_t> &r_is_topology_node) {
	const std::vector<int> degrees = selected_degrees(p_node_count, p_edges, p_selected);
	r_is_topology_node.assign(p_node_count, 0);
	for (int node = 0; node < p_node_count; node++) {
		if (degrees[node] > 0 && degrees[node] != 2) {
			r_is_topology_node[node] = 1;
		}
	}

	std::vector<uint8_t> component_seen(p_node_count, 0);
	std::vector<int> stack;
	std::vector<int> component;
	for (int seed = 0; seed < p_node_count; seed++) {
		if (degrees[seed] == 0 || component_seen[seed]) {
			continue;
		}
		stack.clear();
		component.clear();
		stack.push_back(seed);
		component_seen[seed] = 1;
		bool has_topology_node = false;
		int minimum_node = seed;
		while (!stack.empty()) {
			const int node = stack.back();
			stack.pop_back();
			component.push_back(node);
			minimum_node = std::min(minimum_node, node);
			has_topology_node |= r_is_topology_node[node] != 0;
			for (int edge_id : p_adjacency[node]) {
				if (!p_selected[edge_id]) {
					continue;
				}
				const int neighbor = other_endpoint(p_edges[edge_id], node);
				if (!component_seen[neighbor]) {
					component_seen[neighbor] = 1;
					stack.push_back(neighbor);
				}
			}
		}
		if (!has_topology_node) {
			r_is_topology_node[minimum_node] = 1;
		}
	}

	std::vector<uint8_t> edge_seen(p_edges.size(), 0);
	std::vector<AbstractRoute> routes;
	for (int start = 0; start < p_node_count; start++) {
		if (!r_is_topology_node[start]) {
			continue;
		}
		for (int first_edge : p_adjacency[start]) {
			if (!p_selected[first_edge] || edge_seen[first_edge]) {
				continue;
			}
			AbstractRoute route;
			route.start = start;
			route.nodes.push_back(start);
			int current = start;
			int edge_id = first_edge;
			for (int guard = 0; guard <= int(p_edges.size()); guard++) {
				edge_seen[edge_id] = 1;
				route.edges.push_back(edge_id);
				const int next = other_endpoint(p_edges[edge_id], current);
				route.nodes.push_back(next);
				if (r_is_topology_node[next]) {
					route.end = next;
					break;
				}
				int next_edge = -1;
				for (int candidate : p_adjacency[next]) {
					if (p_selected[candidate] && candidate != edge_id) {
						next_edge = candidate;
						break;
					}
				}
				if (next_edge < 0 || edge_seen[next_edge]) {
					route.end = next;
					break;
				}
				current = next;
				edge_id = next_edge;
			}
			if (route.end >= 0 && !route.edges.empty()) {
				routes.push_back(std::move(route));
			}
		}
	}
	return routes;
}

int route_waypoint(const AbstractRoute &p_route, const std::vector<ClusterEdge> &p_edges, double p_fraction) {
	if (p_route.nodes.size() <= 2 || p_route.edges.empty()) {
		return p_route.nodes[p_route.nodes.size() / 2];
	}
	double total_length = 0.0;
	for (int edge_id : p_route.edges) {
		total_length += p_edges[edge_id].weight;
	}
	const double target = total_length * p_fraction;
	double accumulated = 0.0;
	for (int i = 0; i < int(p_route.edges.size()); i++) {
		accumulated += p_edges[p_route.edges[i]].weight;
		if (accumulated >= target) {
			const int node_index = std::clamp(i + 1, 1, int(p_route.nodes.size()) - 2);
			return p_route.nodes[node_index];
		}
	}
	return p_route.nodes[p_route.nodes.size() - 2];
}

uint64_t endpoint_pair_key(int p_a, int p_b) {
	if (p_b < p_a) {
		std::swap(p_a, p_b);
	}
	return (uint64_t(uint32_t(p_a)) << 32) | uint32_t(p_b);
}

void smooth_centerline(BaseCenterline &r_centerline, int p_iterations, bool p_anchor_closure) {
	if (p_iterations <= 0 || r_centerline.samples.size() < 3) {
		return;
	}
	const bool duplicated_closure = r_centerline.closed && r_centerline.samples.front().position == r_centerline.samples.back().position;
	const int unique_count = int(r_centerline.samples.size()) - int(duplicated_closure);
	if (unique_count < 3) {
		return;
	}
	std::vector<PointD> positions(unique_count);
	std::vector<PointD> evidence(unique_count);
	std::vector<PointD> next_positions(unique_count);
	std::vector<double> cumulative(unique_count, 0.0);
	for (int i = 0; i < unique_count; i++) {
		positions[i] = { double(r_centerline.samples[i].position.x), double(r_centerline.samples[i].position.y) };
		evidence[i] = positions[i];
		if (i > 0) {
			cumulative[i] = cumulative[i - 1] + distance(evidence[i - 1], evidence[i]);
		}
	}
	const double total_length = cumulative.back() + (duplicated_closure ? distance(evidence.back(), evidence.front()) : 0.0);
	for (int iteration = 0; iteration < p_iterations; iteration++) {
		next_positions = positions;
		const int begin = duplicated_closure && !p_anchor_closure ? 0 : 1;
		const int end = duplicated_closure ? unique_count : unique_count - 1;
		for (int i = begin; i < end; i++) {
			const PointD &previous = positions[(i + unique_count - 1) % unique_count];
			const PointD &current = positions[i];
			const PointD &next = positions[(i + 1) % unique_count];
			const double sigma = std::max(double(r_centerline.samples[i].radius), 1e-6);
			const double denominator = 2.0 * sigma * sigma;
			const double neighborhood_radius = std::max(GRAPH_NEIGHBOR_RADIUS, 3.0 * sigma);
			PointD weighted_sum;
			double weight_sum = 0.0;
			auto accumulate_evidence = [&](int p_sample, double p_arc_distance) {
				const double weight = std::exp(-(p_arc_distance * p_arc_distance) / denominator);
				weighted_sum = weighted_sum + evidence[p_sample] * weight;
				weight_sum += weight;
			};
			if (duplicated_closure) {
				for (int sample = 0; sample < unique_count; sample++) {
					double arc_distance = std::abs(cumulative[sample] - cumulative[i]);
					arc_distance = std::min(arc_distance, total_length - arc_distance);
					if (arc_distance <= neighborhood_radius + DISTANCE_EPSILON) {
						accumulate_evidence(sample, arc_distance);
					}
				}
			} else {
				const auto first = std::lower_bound(cumulative.begin(), cumulative.end(), cumulative[i] - neighborhood_radius);
				const auto last = std::upper_bound(cumulative.begin(), cumulative.end(), cumulative[i] + neighborhood_radius);
				for (auto sample = first; sample != last; sample++) {
					const int sample_index = int(sample - cumulative.begin());
					accumulate_evidence(sample_index, std::abs(*sample - cumulative[i]));
				}
			}
			if (weight_sum <= DISTANCE_EPSILON) {
				continue;
			}
			const PointD centroid = weighted_sum / weight_sum;
			PointD tangent = normalized_or_zero(next - current) + normalized_or_zero(current - previous);
			if (length_squared(tangent) <= DISTANCE_EPSILON) {
				tangent = normalized_or_zero(next - previous);
			} else {
				tangent = normalized_or_zero(tangent);
			}
			if (length_squared(tangent) <= DISTANCE_EPSILON) {
				continue;
			}
			const PointD normal = { -tangent.y, tangent.x };
			next_positions[i] = current + normal * dot(centroid - current, normal);
		}
		positions.swap(next_positions);
	}
	for (int i = 0; i < unique_count; i++) {
		r_centerline.samples[i].position = to_vector2(positions[i]);
	}
	if (duplicated_closure) {
		r_centerline.samples.back().position = r_centerline.samples.front().position;
		r_centerline.samples.back().radius = r_centerline.samples.front().radius;
	}
}

void contract_junction_cores(PipelineResult &r_result) {
	const int node_count = int(r_result.nodes.size());
	if (node_count < 2 || r_result.centerlines.empty()) {
		return;
	}

	std::unordered_map<uint64_t, int> pair_counts;
	std::vector<std::vector<int>> incident_centerlines(node_count);
	for (int centerline_index = 0; centerline_index < int(r_result.centerlines.size()); centerline_index++) {
		const BaseCenterline &centerline = r_result.centerlines[centerline_index];
		if (centerline.closed || centerline.node_a < 0 || centerline.node_b < 0 ||
				centerline.node_a >= node_count || centerline.node_b >= node_count || centerline.node_a == centerline.node_b) {
			continue;
		}
		pair_counts[endpoint_pair_key(centerline.node_a, centerline.node_b)]++;
		incident_centerlines[centerline.node_a].push_back(centerline_index);
		incident_centerlines[centerline.node_b].push_back(centerline_index);
	}

	std::vector<int> parent(node_count);
	std::vector<uint8_t> rank(node_count, 0);
	for (int node = 0; node < node_count; node++) {
		parent[node] = node;
	}
	auto find_root = [&](int p_node) {
		int root = p_node;
		while (parent[root] != root) {
			root = parent[root];
		}
		while (parent[p_node] != p_node) {
			const int next = parent[p_node];
			parent[p_node] = root;
			p_node = next;
		}
		return root;
	};
	auto unite = [&](int p_a, int p_b) {
		int root_a = find_root(p_a);
		int root_b = find_root(p_b);
		if (root_a == root_b) {
			return;
		}
		if (rank[root_a] < rank[root_b] || (rank[root_a] == rank[root_b] && root_b < root_a)) {
			std::swap(root_a, root_b);
		}
		parent[root_b] = root_a;
		if (rank[root_a] == rank[root_b]) {
			rank[root_a]++;
		}
	};

	for (const BaseCenterline &centerline : r_result.centerlines) {
		if (centerline.closed || centerline.samples.size() < 2 || centerline.node_a < 0 || centerline.node_b < 0 ||
				centerline.node_a >= node_count || centerline.node_b >= node_count || centerline.node_a == centerline.node_b ||
				r_result.nodes[centerline.node_a].degree < 3 || r_result.nodes[centerline.node_b].degree < 3 ||
				pair_counts[endpoint_pair_key(centerline.node_a, centerline.node_b)] != 1) {
			continue;
		}

		const float radius_a = std::max(0.0f, r_result.nodes[centerline.node_a].radius);
		const float radius_b = std::max(0.0f, r_result.nodes[centerline.node_b].radius);
		if (radius_a + radius_b <= float(DISTANCE_EPSILON)) {
			continue;
		}
		double path_length = 0.0;
		for (size_t sample = 1; sample < centerline.samples.size(); sample++) {
			path_length += double(centerline.samples[sample - 1].position.distance_to(centerline.samples[sample].position));
		}
		if (path_length <= double(radius_a + radius_b) + DISTANCE_EPSILON) {
			unite(centerline.node_a, centerline.node_b);
		}
	}

	// A round stroke cap can leave several sub-pixel terminal routes around a
	// single hub after topology compression. They describe one endpoint, not a
	// junction. Contract only topology-adjacent leaves wholly contained by the
	// hub disk, and only when one route actually exits that disk.
	for (int hub = 0; hub < node_count; hub++) {
		if (incident_centerlines[hub].size() < 3 ||
				r_result.nodes[hub].degree != int(incident_centerlines[hub].size())) {
			continue;
		}
		const float radius = std::max(0.0f, r_result.nodes[hub].radius);
		if (radius <= float(DISTANCE_EPSILON)) {
			continue;
		}
		const double radius_squared = double(radius) * double(radius);
		std::vector<int> cap_leaves;
		int exiting_routes = 0;
		for (int centerline_index : incident_centerlines[hub]) {
			const BaseCenterline &centerline = r_result.centerlines[centerline_index];
			const int other = centerline.node_a == hub ? centerline.node_b : centerline.node_a;
			bool inside_hub_disk = r_result.nodes[other].degree == 1;
			for (const Sample &sample : centerline.samples) {
				if (sample.position.distance_squared_to(r_result.nodes[hub].position) > radius_squared + DISTANCE_EPSILON) {
					inside_hub_disk = false;
					break;
				}
			}
			if (inside_hub_disk) {
				cap_leaves.push_back(other);
			} else {
				exiting_routes++;
			}
		}
		if (cap_leaves.size() < 2 || exiting_routes != 1) {
			continue;
		}
		for (int leaf : cap_leaves) {
			unite(hub, leaf);
		}
	}

	std::vector<int> group_indices(node_count, -1);
	std::vector<std::vector<int>> groups;
	for (int node = 0; node < node_count; node++) {
		const int root = find_root(node);
		if (group_indices[root] < 0) {
			group_indices[root] = int(groups.size());
			groups.push_back({});
		}
		groups[group_indices[root]].push_back(node);
	}
	if (groups.size() == size_t(node_count)) {
		return;
	}

	std::vector<TopologyNode> contracted_nodes;
	contracted_nodes.reserve(groups.size());
	std::vector<int> node_to_group(node_count, -1);
	for (size_t group = 0; group < groups.size(); group++) {
		TopologyNode node;
		node.radius = 0.0f;
		for (int member : groups[group]) {
			node.position += r_result.nodes[member].position;
			node.radius = std::max(node.radius, r_result.nodes[member].radius);
			node_to_group[member] = int(group);
		}
		node.position /= float(groups[group].size());
		contracted_nodes.push_back(node);
	}

	std::vector<BaseCenterline> contracted_centerlines;
	contracted_centerlines.reserve(r_result.centerlines.size());
	for (const BaseCenterline &source : r_result.centerlines) {
		if (source.node_a < 0 || source.node_b < 0 || source.node_a >= node_count || source.node_b >= node_count ||
				source.samples.size() < 2) {
			continue;
		}
		BaseCenterline centerline = source;
		centerline.id = int(contracted_centerlines.size());
		centerline.node_a = node_to_group[source.node_a];
		centerline.node_b = node_to_group[source.node_b];
		if (!centerline.closed && centerline.node_a == centerline.node_b) {
			continue;
		}
		centerline.samples.front().position = contracted_nodes[centerline.node_a].position;
		centerline.samples.front().radius = contracted_nodes[centerline.node_a].radius;
		centerline.samples.back().position = contracted_nodes[centerline.node_b].position;
		centerline.samples.back().radius = contracted_nodes[centerline.node_b].radius;
		contracted_centerlines.push_back(std::move(centerline));
	}

	for (TopologyNode &node : contracted_nodes) {
		node.degree = 0;
	}
	for (const BaseCenterline &centerline : contracted_centerlines) {
		if (centerline.node_a == centerline.node_b) {
			contracted_nodes[centerline.node_a].degree += 2;
		} else {
			contracted_nodes[centerline.node_a].degree++;
			contracted_nodes[centerline.node_b].degree++;
		}
	}

	r_result.nodes = std::move(contracted_nodes);
	r_result.centerlines = std::move(contracted_centerlines);
}

} // namespace

PipelineResult extract_base_centerlines(const std::vector<float> &p_coverage, int p_width, int p_height, const Params &p_params,
		const std::vector<float> *p_radius_guide) {
	PipelineResult result;
	if (p_width <= 0 || p_height <= 0 || size_t(p_width) > std::numeric_limits<size_t>::max() / size_t(p_height) ||
			p_coverage.size() != size_t(p_width) * size_t(p_height) ||
			size_t(p_width) * size_t(p_height) > size_t(std::numeric_limits<int>::max())) {
		return result;
	}

	const std::vector<double> coverage = filter_coverage(p_coverage, p_width, p_height, p_params);
	const std::vector<int> foreground_components = label_foreground_components(coverage, p_width, p_height);
	const std::vector<uint8_t> foreground_skeleton = thin_foreground(coverage, p_width, p_height);
	std::vector<Particle> particles = create_particles(coverage, foreground_components, p_width, p_height, p_params);
	move_particles(particles, foreground_components, p_width, p_height);
	std::vector<ClusterNodeInternal> cluster_nodes = create_cluster_nodes(particles, foreground_components,
			p_width, p_height, p_params, p_radius_guide);
	std::vector<ClusterEdge> cluster_edges = create_cluster_graph(
			cluster_nodes, foreground_components, p_width, p_height);
	seed_unrepresented_foreground_components(foreground_skeleton, foreground_components, p_width, p_height, p_params,
			p_radius_guide, cluster_nodes, cluster_edges);
	if (cluster_edges.empty()) {
		return result;
	}
	std::vector<std::vector<int>> adjacency = build_adjacency(int(cluster_nodes.size()), cluster_edges);
	std::vector<int> all_edges(cluster_edges.size());
	for (int edge_id = 0; edge_id < int(cluster_edges.size()); edge_id++) {
		all_edges[edge_id] = edge_id;
	}
	std::vector<uint8_t> global_mst = kruskal_forest(int(cluster_nodes.size()), cluster_edges, all_edges);
	std::vector<uint8_t> pruned_skeleton = global_mst;
	prune_leaves(cluster_nodes, cluster_edges, adjacency, pruned_skeleton);

	const std::vector<int> first_degrees = selected_degrees(int(cluster_nodes.size()), cluster_edges, pruned_skeleton);
	std::vector<int> surviving_leaves;
	std::vector<uint8_t> protected_leaves(cluster_nodes.size(), 0);
	for (int node = 0; node < int(cluster_nodes.size()); node++) {
		if (first_degrees[node] == 1) {
			surviving_leaves.push_back(node);
			protected_leaves[node] = 1;
		}
	}

	std::vector<uint8_t> repaired_skeleton = pruned_skeleton;
	repair_loops(cluster_nodes, cluster_edges, adjacency, global_mst, surviving_leaves, repaired_skeleton);
	prune_leaves(cluster_nodes, cluster_edges, adjacency, repaired_skeleton, &protected_leaves);
	restore_foreground_connectivity(coverage, foreground_skeleton, foreground_components, p_width, p_height, p_params, p_radius_guide,
			cluster_nodes, cluster_edges, repaired_skeleton);
	adjacency = build_adjacency(int(cluster_nodes.size()), cluster_edges);
	result.clustered_pixel_count = int(cluster_nodes.size());
	result.cluster_edge_count = int(cluster_edges.size());

	std::vector<uint8_t> is_topology_node;
	const std::vector<AbstractRoute> routes = compress_topology(int(cluster_nodes.size()), cluster_edges, adjacency, repaired_skeleton, is_topology_node);
	if (routes.empty()) {
		return result;
	}

	const std::vector<int> final_degrees = selected_degrees(int(cluster_nodes.size()), cluster_edges, repaired_skeleton);
	std::vector<int> topology_index(cluster_nodes.size(), -1);
	for (int node = 0; node < int(cluster_nodes.size()); node++) {
		if (!is_topology_node[node]) {
			continue;
		}
		topology_index[node] = int(result.nodes.size());
		result.nodes.push_back({ to_vector2(cluster_nodes[node].position), float(cluster_nodes[node].radius), final_degrees[node] });
	}

	std::unordered_map<uint64_t, int> route_pair_counts;
	for (const AbstractRoute &route : routes) {
		route_pair_counts[endpoint_pair_key(route.start, route.end)]++;
	}
	result.centerlines.reserve(routes.size() * 2);
	auto append_centerline = [&](const std::vector<int> &p_path, int p_node_a, int p_node_b, bool p_closed) {
		if (p_path.size() < 2) {
			return;
		}
		BaseCenterline centerline;
		centerline.id = int(result.centerlines.size());
		centerline.node_a = p_node_a;
		centerline.node_b = p_node_b;
		centerline.closed = p_closed;
		centerline.samples.reserve(p_path.size() + int(p_closed));
		for (int cluster_node : p_path) {
			centerline.samples.push_back({ to_vector2(cluster_nodes[cluster_node].position), float(cluster_nodes[cluster_node].radius) });
		}
		if (centerline.closed && centerline.samples.front().position != centerline.samples.back().position) {
			centerline.samples.push_back(centerline.samples.front());
		}
		result.centerlines.push_back(std::move(centerline));
	};

	for (const AbstractRoute &route : routes) {
		const bool parallel = route_pair_counts[endpoint_pair_key(route.start, route.end)] > 1;
		const bool needs_dummy = route.start == route.end || parallel;
		if (!needs_dummy) {
			append_centerline(route.nodes, topology_index[route.start], topology_index[route.end], false);
			continue;
		}

		// Split loops and parallel routes without leaving the selected route.
		const int middle = route_waypoint(route, cluster_edges, 0.5);
		if (middle == route.start || middle == route.end) {
			append_centerline(route.nodes, topology_index[route.start], topology_index[route.end], route.start == route.end);
			continue;
		}

		const auto middle_it = std::find(route.nodes.begin() + 1, route.nodes.end() - 1, middle);
		if (middle_it == route.nodes.end() - 1) {
			append_centerline(route.nodes, topology_index[route.start], topology_index[route.end], route.start == route.end);
			continue;
		}
		const std::vector<int> first_path(route.nodes.begin(), middle_it + 1);
		const std::vector<int> second_path(middle_it, route.nodes.end());

		const int dummy_node = int(result.nodes.size());
		result.nodes.push_back({ to_vector2(cluster_nodes[middle].position), float(cluster_nodes[middle].radius), 2 });
		append_centerline(first_path, topology_index[route.start], dummy_node, false);
		append_centerline(second_path, dummy_node, topology_index[route.end], false);
	}
	contract_junction_cores(result);
	for (BaseCenterline &centerline : result.centerlines) {
		const bool anchor_closure = centerline.closed && centerline.node_a >= 0 &&
				centerline.node_a < int(result.nodes.size());
		smooth_centerline(centerline, std::max(0, p_params.smoothing_iterations), anchor_closure);
	}
	return result;
}

} // namespace topology_centerline
