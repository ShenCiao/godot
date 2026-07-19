/**************************************************************************/
/*  topology_centerline_types.h                                           */
/**************************************************************************/

#pragma once

#include "core/math/vector2.h"

#include <vector>

namespace topology_centerline {

struct Params {
	float threshold = 0.5f;
	float max_thickness = 0.0f;
	float despeckling = 0.0f;
	float gradient_threshold_ratio = 0.1f;
	float motion_step = 0.1f;
	int smoothing_iterations = 5;
	float curvature_threshold_degrees = 50.0f;
	float polyline_max_error = 0.35f;
	float polyline_max_segment_length = 8.0f;
};

struct Sample {
	Vector2 position;
	float radius = 0.5f;
};

struct TopologyNode {
	Vector2 position;
	float radius = 0.5f;
	int degree = 0;
};

struct BaseCenterline {
	int id = -1;
	int node_a = -1;
	int node_b = -1;
	bool closed = false;
	std::vector<Sample> samples;
};

struct PipelineResult {
	std::vector<TopologyNode> nodes;
	std::vector<BaseCenterline> centerlines;
	int clustered_pixel_count = 0;
	int cluster_edge_count = 0;
};

struct Stroke {
	std::vector<Sample> samples;
	std::vector<Vector2> mandatory_points;
	std::vector<Vector2> explicit_self_vertices;
};

} // namespace topology_centerline
