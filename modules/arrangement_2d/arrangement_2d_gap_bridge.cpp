#include "arrangement_2d.h"

#include <CGAL/spatial_sort.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <optional>
#include <tuple>

using CurveConstHandle = CGAL::Arrangement::Curve_const_handle;

bool Arrangement2D::GapBridgeEndpointKey::operator<(const GapBridgeEndpointKey &p_other) const {
	if (curve_id != p_other.curve_id) {
		return curve_id < p_other.curve_id;
	}
	return quantized_t < p_other.quantized_t;
}

bool Arrangement2D::GapBridgeEndpointKey::operator==(const GapBridgeEndpointKey &p_other) const {
	return curve_id == p_other.curve_id && quantized_t == p_other.quantized_t;
}

Arrangement2D::GapBridgeEndpointKey Arrangement2D::gap_bridge_endpoint_key(const GapBridgeEndpointRef &p_ref) {
	return GapBridgeEndpointKey{ p_ref.curve_id, quantize_t(p_ref.t) };
}

bool Arrangement2D::curve_is_closed(const CGAL::Curve &p_curve) {
	auto first_point = p_curve.points_begin();
	auto end_point = p_curve.points_end();
	if (first_point == end_point) {
		return false;
	}
	auto last_point = end_point;
	--last_point;
	return points_equal(*first_point, *last_point);
}

float Arrangement2D::curve_last_t(const CGAL::Curve &p_curve) {
	auto first_point = p_curve.points_begin();
	auto end_point = p_curve.points_end();
	if (first_point == end_point) {
		return 0.0f;
	}
	auto last_point = end_point;
	--last_point;
	return static_cast<float>(std::distance(first_point, last_point));
}

Arrangement2D::GapBridgeSourceCurveInfo Arrangement2D::make_gap_bridge_source_curve_metadata(CurveConstHandle p_curve) const {
	GapBridgeSourceCurveInfo metadata{};
	auto curve_id_it = curve_handle_to_id.find(&(*p_curve));
	if (curve_id_it == curve_handle_to_id.end()) {
		return metadata;
	}

	metadata.curve_id = curve_id_it->second;
	metadata.is_closed = curve_is_closed(*p_curve);
	metadata.last_t = curve_last_t(*p_curve);

	auto prev = p_curve->points_begin();
	auto end_it = p_curve->points_end();
	int segment_index = 0;
	for (auto next = std::next(prev); prev != end_it && next != end_it; ++next, ++prev, ++segment_index) {
		if (!points_equal(*prev, *next)) {
			metadata.segments.push_back({ *prev, *next, segment_index });
		}
	}
	return metadata;
}

std::optional<Vector2> Arrangement2D::outward_tangent_for_open_endpoint(const GapBridgeSourceCurveSegment &p_segment, bool p_is_start) {
	Vector2 tangent(
			static_cast<float>(CGAL::to_double(p_segment.to.x() - p_segment.from.x())),
			static_cast<float>(CGAL::to_double(p_segment.to.y() - p_segment.from.y())));
	if (tangent.is_zero_approx()) {
		return {};
	}
	tangent.normalize();
	return p_is_start ? -tangent : tangent;
}

Arrangement2D::GapBridgeEndpointRef Arrangement2D::gap_bridge_endpoint_ref_from_source_segment(const GapBridgeSourceCurveInfo &p_metadata, const GapBridgeSourceCurveSegment &p_segment, const CGAL::Point &p_point, int p_vertex_degree) {
	float t = static_cast<float>(p_segment.index + segment_fraction(p_segment.from, p_segment.to, p_point));
	bool is_start = quantize_t(t) == 0;
	bool is_end = quantize_t(t) == quantize_t(p_metadata.last_t);
	bool open_endpoint = !p_metadata.is_closed && (is_start || is_end);
	return {
		p_metadata.curve_id,
		t,
		p_vertex_degree == 1 || open_endpoint,
		open_endpoint ? outward_tangent_for_open_endpoint(p_segment, is_start) : std::optional<Vector2>{}
	};
}

void Arrangement2D::append_unique_gap_bridge_endpoint_ref(GapBridgeEndpointRefs &r_refs, const GapBridgeEndpointRef &p_ref) {
	GapBridgeEndpointKey key = gap_bridge_endpoint_key(p_ref);
	auto existing_it = std::find_if(r_refs.begin(), r_refs.end(), [&](const Arrangement2D::GapBridgeEndpointRef &p_existing) {
		return gap_bridge_endpoint_key(p_existing) == key;
	});
	if (existing_it == r_refs.end()) {
		r_refs.push_back(p_ref);
		return;
	}
	existing_it->anchor = existing_it->anchor || p_ref.anchor;
	if (!existing_it->outward_tangent.has_value()) {
		existing_it->outward_tangent = p_ref.outward_tangent;
	}
}

int Arrangement2D::degree_for_halfedge_point(CGAL::Halfedge_const_handle p_halfedge, const CGAL::Point &p_point) {
	if (points_equal(p_point, p_halfedge->source()->point())) {
		return p_halfedge->source()->degree();
	}
	if (points_equal(p_point, p_halfedge->target()->point())) {
		return p_halfedge->target()->degree();
	}
	return 2;
}

void Arrangement2D::append_gap_bridge_source_constraint_segments(CurveConstHandle p_curve, const GapBridgeSourceCurveInfo &p_metadata, std::vector<CGAL::Segment> &r_segments, std::map<CGAL::Point, GapBridgeEndpointRefs, PointLess> &r_point_to_refs) const {
	for (auto edge_it = arrangement.induced_edges_begin(p_curve);
			edge_it != arrangement.induced_edges_end(p_curve);
			++edge_it) {
		CGAL::Halfedge_const_handle halfedge = *edge_it;
		const CGAL::Curve &edge_curve = halfedge->curve();
		auto prev = edge_curve.points_begin();
		auto end_it = edge_curve.points_end();
		if (prev == end_it) {
			continue;
		}

		for (auto next = std::next(prev); next != end_it; ++next, ++prev) {
			if (points_equal(*prev, *next)) {
				continue;
			}

			r_segments.push_back(CGAL::Segment(*prev, *next));

			int prev_degree = degree_for_halfedge_point(halfedge, *prev);
			int next_degree = degree_for_halfedge_point(halfedge, *next);
			for (const GapBridgeSourceCurveSegment &source_segment : p_metadata.segments) {
				if (!point_on_segment(source_segment.from, *prev, source_segment.to) ||
						!point_on_segment(source_segment.from, *next, source_segment.to)) {
					continue;
				}

				append_unique_gap_bridge_endpoint_ref(r_point_to_refs[*prev], gap_bridge_endpoint_ref_from_source_segment(p_metadata, source_segment, *prev, prev_degree));
				append_unique_gap_bridge_endpoint_ref(r_point_to_refs[*next], gap_bridge_endpoint_ref_from_source_segment(p_metadata, source_segment, *next, next_degree));
			}
		}
	}
}

double Arrangement2D::gap_bridge_tangent_bonus(const GapBridgeEndpointRef &p_ref, const CGAL::Point &p_ref_point, const CGAL::Point &p_other_point) {
	if (!p_ref.outward_tangent.has_value()) {
		return 0.0;
	}
	Vector2 bridge(
			static_cast<float>(CGAL::to_double(p_other_point.x() - p_ref_point.x())),
			static_cast<float>(CGAL::to_double(p_other_point.y() - p_ref_point.y())));
	if (bridge.is_zero_approx()) {
		return 0.0;
	}
	bridge.normalize();
	return 0.125 * std::max(0.0f, p_ref.outward_tangent->dot(bridge));
}

Dictionary Arrangement2D::make_gap_bridge_candidate_result(const GapBridgeCandidate &p_candidate) {
	Dictionary result{};
	result["from_curve_id"] = p_candidate.from.curve_id;
	result["from_t"] = p_candidate.from.t;
	result["to_curve_id"] = p_candidate.to.curve_id;
	result["to_t"] = p_candidate.to.t;
	result["score"] = p_candidate.score;
	return result;
}

Arrangement2D::GapBridgeCdt Arrangement2D::construct_gap_bridge_cdt(const std::vector<CurveConstHandle> &p_curves) {
	GapBridgeCdt triangulation{};
	std::vector<CGAL::Segment> segments;
	std::map<CGAL::Point, GapBridgeEndpointRefs, PointLess> point_to_refs;
	for (CurveConstHandle curve : p_curves) {
		GapBridgeSourceCurveInfo source_metadata = make_gap_bridge_source_curve_metadata(curve);
		if (source_metadata.segments.empty()) {
			continue;
		}
		append_gap_bridge_source_constraint_segments(curve, source_metadata, segments, point_to_refs);
	}
	if (segments.empty()) {
		return triangulation;
	}

	std::map<CGAL::Point, GapBridgeCdt::Vertex_handle, PointLess> point_to_vertex;
	for (const CGAL::Segment &segment : segments) {
		point_to_vertex.emplace(segment.source(), GapBridgeCdt::Vertex_handle{});
		point_to_vertex.emplace(segment.target(), GapBridgeCdt::Vertex_handle{});
	}

	std::vector<CGAL::Point> points;
	points.reserve(point_to_vertex.size());
	for (const auto &[point, vertex] : point_to_vertex) {
		points.push_back(point);
	}
	CGAL::spatial_sort(points.begin(), points.end());

	GapBridgeCdt::Face_handle hint;
	for (const CGAL::Point &point : points) {
		auto vertex = triangulation.insert(point, hint);
		auto refs_it = point_to_refs.find(point);
		if (refs_it != point_to_refs.end()) {
			vertex->info() = refs_it->second;
		}
		point_to_vertex[point] = vertex;
		hint = vertex->face();
	}

	for (const CGAL::Segment &segment : segments) {
		auto from_it = point_to_vertex.find(segment.source());
		auto to_it = point_to_vertex.find(segment.target());
		if (from_it != point_to_vertex.end() && to_it != point_to_vertex.end()) {
			triangulation.insert_constraint(from_it->second, to_it->second);
		}
	}
	return triangulation;
}

TypedArray<Dictionary> Arrangement2D::get_gap_bridge_candidates(double p_max_gap_length) {
	TypedArray<Dictionary> result{};
	if (p_max_gap_length <= 0.0 || curve_handles.empty()) {
		return result;
	}

	// This temporary CDT is a drawing-app visual heuristic, not CAD-grade topology proof.
	std::vector<CurveConstHandle> live_curve_handles;
	live_curve_handles.reserve(curve_handles.size());
	for (const auto &[id, curve_handle] : curve_handles) {
		if (curve_handle != nullptr) {
			live_curve_handles.push_back(curve_handle);
		}
	}
	GapBridgeCdt triangulation = construct_gap_bridge_cdt(live_curve_handles);

	if (triangulation.dimension() < 1) {
		return result;
	}

	std::map<std::pair<GapBridgeEndpointKey, GapBridgeEndpointKey>, GapBridgeCandidate> candidates;
	const double max_gap_length_squared = p_max_gap_length * p_max_gap_length;
	for (auto edge_it = triangulation.finite_edges_begin(); edge_it != triangulation.finite_edges_end(); ++edge_it) {
		if (triangulation.is_constrained(*edge_it)) {
			continue;
		}

		auto segment = triangulation.segment(edge_it);
		CGAL::Point from_point = segment.source();
		CGAL::Point to_point = segment.target();
		const auto squared_length = segment.squared_length();
		if (CGAL::to_double(squared_length) > max_gap_length_squared) {
			continue;
		}

		const double distance = std::sqrt(CGAL::to_double(squared_length));
		auto face = edge_it->first;
		int edge_index = edge_it->second;
		const GapBridgeEndpointRefs &from_refs = face->vertex(CGAL::Triangulation_cw_ccw_2::ccw(edge_index))->info();
		const GapBridgeEndpointRefs &to_refs = face->vertex(CGAL::Triangulation_cw_ccw_2::cw(edge_index))->info();
		for (GapBridgeEndpointRef from_ref : from_refs) {
			for (GapBridgeEndpointRef to_ref : to_refs) {
				if (!from_ref.anchor && !to_ref.anchor) {
					continue;
				}
				if (from_ref.curve_id == to_ref.curve_id && std::abs(from_ref.t - to_ref.t) <= 1.001f) {
					continue;
				}

				GapBridgeEndpointKey from_key = gap_bridge_endpoint_key(from_ref);
				GapBridgeEndpointKey to_key = gap_bridge_endpoint_key(to_ref);
				if (from_key == to_key) {
					continue;
				}
				bool swapped = false;
				if (to_key < from_key) {
					std::swap(from_key, to_key);
					std::swap(from_ref, to_ref);
					swapped = true;
				}

				const CGAL::Point &ordered_from_point = swapped ? to_point : from_point;
				const CGAL::Point &ordered_to_point = swapped ? from_point : to_point;
				GapBridgeCandidate candidate{
					from_ref,
					to_ref,
					1.0 - distance / p_max_gap_length +
							(from_ref.anchor && to_ref.anchor ? 0.25 : 0.0) +
							gap_bridge_tangent_bonus(from_ref, ordered_from_point, ordered_to_point) +
							gap_bridge_tangent_bonus(to_ref, ordered_to_point, ordered_from_point)
				};
				auto key = std::make_pair(from_key, to_key);
				auto existing_it = candidates.find(key);
				if (existing_it == candidates.end() ||
						candidate.score > existing_it->second.score ||
						(candidate.score == existing_it->second.score &&
								std::tie(candidate.from.t, candidate.to.t) < std::tie(existing_it->second.from.t, existing_it->second.to.t))) {
					candidates[key] = candidate;
				}
			}
		}
	}

	for (const auto &[key, candidate] : candidates) {
		result.push_back(make_gap_bridge_candidate_result(candidate));
	}
	return result;
}
