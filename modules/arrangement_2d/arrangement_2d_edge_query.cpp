#include "arrangement_2d.h"

#include "core/error/error_macros.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <set>
#include <variant>

using CurveConstHandle = CGAL::Arrangement::Curve_const_handle;

double Arrangement2D::point_distance(const CGAL::Point &p_a, const CGAL::Point &p_b) {
	return std::sqrt(CGAL::to_double(CGAL::Segment(p_a, p_b).squared_length()));
}

double Arrangement2D::x_monotone_curve_length(const CGAL::X_monotone_curve &p_curve) {
	double length = 0.0;
	auto prev = p_curve.points_begin();
	auto end_it = p_curve.points_end();
	if (prev == end_it) {
		return length;
	}

	for (auto next = std::next(prev); next != end_it; ++next, ++prev) {
		length += point_distance(*prev, *next);
	}
	return length;
}

bool Arrangement2D::x_monotone_curve_leaves_endpoint_toward(const CGAL::X_monotone_curve &p_curve, const CGAL::Point &p_endpoint, const CGAL::Point &p_next_source_point) {
	auto first = p_curve.points_begin();
	auto end = p_curve.points_end();
	if (first == end) {
		return false;
	}
	auto second = std::next(first);
	if (second == end) {
		return false;
	}

	auto last = end;
	--last;
	if (points_equal(*first, p_endpoint)) {
		return point_on_segment(p_endpoint, *second, p_next_source_point);
	}
	if (points_equal(*last, p_endpoint)) {
		auto before_last = last;
		--before_last;
		return point_on_segment(p_endpoint, *before_last, p_next_source_point);
	}
	return false;
}

std::vector<float> Arrangement2D::point_to_poly_ts(const CGAL::Curve &p_curve, const CGAL::Point &p_point) {
	std::vector<float> result;
	auto prev = p_curve.points_begin();
	auto end_it = p_curve.points_end();
	if (prev == end_it) {
		return result;
	}

	int segment_index = 0;
	for (auto next = std::next(prev); next != end_it; ++next, ++prev, ++segment_index) {
		if (point_on_segment(*prev, p_point, *next)) {
			result.push_back(static_cast<float>(segment_index + segment_fraction(*prev, *next, p_point)));
		}
	}
	return result;
}

std::vector<Arrangement2D::PolyTRange> Arrangement2D::subedge_to_poly_ranges(const CGAL::Curve &p_curve, const CGAL::Point &p_from, const CGAL::Point &p_to) {
	std::vector<PolyTRange> ranges;
	CGAL::Segment_traits traits;
	CGAL::Segment_traits::Collinear_2 collinear = traits.collinear_2_object();

	auto prev = p_curve.points_begin();
	auto end_it = p_curve.points_end();
	if (prev == end_it) {
		return ranges;
	}

	int segment_index = 0;
	for (auto next = std::next(prev); next != end_it; ++next, ++prev, ++segment_index) {
		if (!point_on_segment(*prev, p_from, *next) ||
				!point_on_segment(*prev, p_to, *next) ||
				!collinear(*prev, p_from, p_to)) {
			continue;
		}

		float from_t = static_cast<float>(segment_index + segment_fraction(*prev, *next, p_from));
		float to_t = static_cast<float>(segment_index + segment_fraction(*prev, *next, p_to));
		ranges.push_back({ std::min(from_t, to_t), std::max(from_t, to_t) });
	}

	return ranges;
}

std::vector<Arrangement2D::PolyTRange> Arrangement2D::halfedge_to_poly_ranges(const CGAL::Curve &p_curve, CGAL::Halfedge_const_handle p_halfedge) {
	std::vector<PolyTRange> ranges;
	auto prev = p_halfedge->curve().points_begin();
	auto end_it = p_halfedge->curve().points_end();
	if (prev == end_it) {
		return ranges;
	}

	for (auto next = std::next(prev); next != end_it; ++next, ++prev) {
		std::vector<PolyTRange> subedge_ranges = subedge_to_poly_ranges(p_curve, *prev, *next);
		if (subedge_ranges.empty()) {
			WARN_PRINT_ONCE("Arrangement halfedge subedge could not be mapped back to its source polyline segment.");
			continue;
		}
		for (const PolyTRange &range : subedge_ranges) {
			if (range.to > range.from) {
				ranges.push_back(range);
			}
		}
	}
	return ranges;
}

void Arrangement2D::add_point_stop_ts(std::vector<float> &r_stop_ts, const CGAL::Curve &p_curve, const CGAL::Point &p_point) {
	for (float t : point_to_poly_ts(p_curve, p_point)) {
		r_stop_ts.push_back(t);
	}
}

void Arrangement2D::add_source_self_intersection_stop_ts(std::vector<float> &r_stop_ts, const CGAL::Curve &p_curve) {
	struct SourceSegment {
		CGAL::Segment_traits::X_monotone_curve_2 curve;
		int index = 0;
	};

	std::vector<SourceSegment> segments;
	auto prev = p_curve.points_begin();
	auto end_it = p_curve.points_end();
	if (prev == end_it) {
		return;
	}

	int segment_index = 0;
	for (auto next = std::next(prev); next != end_it; ++next, ++prev, ++segment_index) {
		if (!points_equal(*prev, *next)) {
			segments.push_back({ CGAL::Segment_traits::X_monotone_curve_2(*prev, *next), segment_index });
		}
	}

	using IntersectionPoint = std::pair<CGAL::Point, CGAL::Segment_traits::Multiplicity>;
	using IntersectionResult = std::variant<IntersectionPoint, CGAL::Segment_traits::X_monotone_curve_2>;

	CGAL::Segment_traits traits;
	CGAL::Segment_traits::Intersect_2 intersect = traits.intersect_2_object();
	for (size_t i = 0; i < segments.size(); ++i) {
		for (size_t j = i + 1; j < segments.size(); ++j) {
			std::vector<IntersectionResult> intersections;
			intersect(segments[i].curve, segments[j].curve, std::back_inserter(intersections));

			const bool adjacent = segments[i].index + 1 == segments[j].index;
			for (const IntersectionResult &intersection : intersections) {
				if (const IntersectionPoint *point = std::get_if<IntersectionPoint>(&intersection)) {
					if (!adjacent) {
						add_point_stop_ts(r_stop_ts, p_curve, point->first);
					}
					continue;
				}

				const CGAL::Segment_traits::X_monotone_curve_2 *overlap = std::get_if<CGAL::Segment_traits::X_monotone_curve_2>(&intersection);
				if (overlap != nullptr) {
					add_point_stop_ts(r_stop_ts, p_curve, overlap->source());
					add_point_stop_ts(r_stop_ts, p_curve, overlap->target());
				}
			}
		}
	}
}

bool Arrangement2D::has_originating_curve(CGAL::Arrangement &p_arrangement, CGAL::Halfedge_const_handle p_halfedge, CurveConstHandle p_curve) {
	for (auto curve_it = p_arrangement.originating_curves_begin(p_halfedge);
			curve_it != p_arrangement.originating_curves_end(p_halfedge);
			++curve_it) {
		if (&(*curve_it) == &(*p_curve)) {
			return true;
		}
	}
	return false;
}

std::vector<float> Arrangement2D::curve_stop_ts(CGAL::Arrangement &p_arrangement, CurveConstHandle p_curve) {
	auto first_point = p_curve->points_begin();
	auto end_point = p_curve->points_end();
	if (first_point == end_point) {
		WARN_PRINT_ONCE("Cannot compute trim stops for an empty source curve.");
		return {};
	}
	auto last_point = end_point;
	--last_point;

	std::vector<float> stop_ts = {
		0.0f,
		static_cast<float>(std::distance(first_point, last_point))
	};
	add_source_self_intersection_stop_ts(stop_ts, *p_curve);

	for (auto edge_it = p_arrangement.induced_edges_begin(p_curve);
			edge_it != p_arrangement.induced_edges_end(p_curve);
			++edge_it) {
		CGAL::Halfedge_const_handle halfedge = *edge_it;
		if (!has_originating_curve(p_arrangement, halfedge, p_curve)) {
			halfedge = halfedge->twin();
			if (!has_originating_curve(p_arrangement, halfedge, p_curve)) {
				WARN_PRINT_ONCE("Induced edge is missing its originating curve.");
				continue;
			}
		}

		if (halfedge->source()->degree() != 2) {
			add_point_stop_ts(stop_ts, *p_curve, halfedge->source()->point());
		}
		if (halfedge->target()->degree() != 2) {
			add_point_stop_ts(stop_ts, *p_curve, halfedge->target()->point());
		}
	}

	std::sort(stop_ts.begin(), stop_ts.end());
	stop_ts.erase(std::unique(stop_ts.begin(), stop_ts.end()), stop_ts.end());
	return stop_ts;
}

Arrangement2D::PolyTRange Arrangement2D::expand_to_stops(const std::vector<float> &p_stop_ts, const PolyTRange &p_hit_range) {
	if (p_stop_ts.empty()) {
		return p_hit_range;
	}

	float expanded_from = p_stop_ts.front();
	float expanded_to = p_stop_ts.back();
	for (float stop_t : p_stop_ts) {
		if (stop_t <= p_hit_range.from) {
			expanded_from = stop_t;
		}
		if (stop_t >= p_hit_range.to) {
			expanded_to = stop_t;
			break;
		}
	}

	return PolyTRange{ expanded_from, expanded_to };
}

std::vector<Arrangement2D::SourceCurveHit> Arrangement2D::collect_polyline_query_source_hits(PackedVector2Array p_polyline) {
	std::vector<SourceCurveHit> result{};
	p_polyline = remove_consecutive_overlapping_points(p_polyline);
	if (p_polyline.size() < 2) {
		return result;
	}

	auto mono_curves = construct_x_monotone_curves(p_polyline);
	std::set<CGAL::Halfedge_const_handle> halfedges{};
	for (auto &curve : mono_curves) {
		for (auto halfedge : zone_query_edges(curve)) {
			halfedges.insert(halfedge);
		}
	}

	for (auto halfedge : halfedges) {
		for (auto curve_it = arrangement.originating_curves_begin(halfedge);
				curve_it != arrangement.originating_curves_end(halfedge);
				++curve_it) {
			const CGAL::Curve *source_curve = &(*curve_it);
			auto source_id_it = curve_handle_to_id.find(source_curve);
			if (source_id_it == curve_handle_to_id.end()) {
				WARN_PRINT_ONCE("Arrangement edge query found an unknown source curve.");
				continue;
			}
			result.push_back({ halfedge, curve_it, source_id_it->second });
		}
	}
	return result;
}

Dictionary Arrangement2D::make_edge_query_result(int64_t p_source_id, float p_from_t, float p_to_t) {
	Dictionary result{};
	result["source_id"] = p_source_id;
	result["from_t"] = p_from_t;
	result["to_t"] = p_to_t;
	return result;
}

PackedInt64Array Arrangement2D::polyline_query_curves(PackedVector2Array p_polyline) {
	std::set<int64_t> curve_ids{};
	for (const SourceCurveHit &hit : collect_polyline_query_source_hits(p_polyline)) {
		curve_ids.insert(hit.source_id);
	}

	PackedInt64Array result{};
	for (int64_t id : curve_ids) {
		result.push_back(id);
	}
	return result;
}

Vector2 Arrangement2D::get_curve_endpoint_junction_lengths(int64_t p_curve_id) {
	auto curve_handle_it = curve_handles.find(p_curve_id);
	ERR_FAIL_COND_V_MSG(curve_handle_it == curve_handles.end() || curve_handle_it->second == nullptr, Vector2(-1, -1), vformat("Curve id %d does not exist.", p_curve_id));
	return curve_endpoint_junction_lengths(curve_handle_it->second);
}

Dictionary Arrangement2D::get_curve_endpoint_info(int64_t p_curve_id) {
	auto curve_handle_it = curve_handles.find(p_curve_id);
	ERR_FAIL_COND_V_MSG(curve_handle_it == curve_handles.end() || curve_handle_it->second == nullptr, Dictionary{}, vformat("Curve id %d does not exist.", p_curve_id));
	return make_curve_endpoint_info(curve_handle_it->second);
}

std::optional<CGAL::Halfedge_const_handle> Arrangement2D::source_halfedge_from_endpoint(CurveConstHandle p_curve, const CGAL::Point &p_endpoint, const CGAL::Point &p_next_source_point) {
	auto located = point_location.locate(p_endpoint);
	auto vertex_ptr = std::get_if<CGAL::Vertex_const_handle>(&located);
	if (vertex_ptr == nullptr) {
		WARN_PRINT_ONCE("Source curve endpoint could not be located as an arrangement vertex.");
		return std::nullopt;
	}

	CGAL::Arrangement::Halfedge_around_vertex_const_circulator current = (*vertex_ptr)->incident_halfedges();
	CGAL::Arrangement::Halfedge_around_vertex_const_circulator end = current;
	do {
		CGAL::Halfedge_const_handle halfedge = current;
		if (!has_originating_curve(arrangement, halfedge, p_curve)) {
			halfedge = halfedge->twin();
			if (!has_originating_curve(arrangement, halfedge, p_curve)) {
				++current;
				continue;
			}
		}

		if (halfedge->source() == *vertex_ptr &&
				x_monotone_curve_leaves_endpoint_toward(halfedge->curve(), p_endpoint, p_next_source_point)) {
			return halfedge;
		}
		if (halfedge->target() == *vertex_ptr &&
				x_monotone_curve_leaves_endpoint_toward(halfedge->curve(), p_endpoint, p_next_source_point)) {
			return halfedge->twin();
		}
		++current;
	} while (current != end);

	return std::nullopt;
}

std::optional<CGAL::Halfedge_const_handle> Arrangement2D::next_source_halfedge(CurveConstHandle p_curve, CGAL::Vertex_const_handle p_vertex, CGAL::Halfedge_const_handle p_previous) {
	CGAL::Arrangement::Halfedge_around_vertex_const_circulator current = p_vertex->incident_halfedges();
	CGAL::Arrangement::Halfedge_around_vertex_const_circulator end = current;
	do {
		CGAL::Halfedge_const_handle candidate = current;
		if (candidate == p_previous || candidate == p_previous->twin()) {
			++current;
			continue;
		}

		if (!has_originating_curve(arrangement, candidate, p_curve)) {
			candidate = candidate->twin();
			if (!has_originating_curve(arrangement, candidate, p_curve)) {
				++current;
				continue;
			}
		}

		if (candidate->source() == p_vertex) {
			return candidate;
		}
		if (candidate->target() == p_vertex) {
			return candidate->twin();
		}
		++current;
	} while (current != end);

	return std::nullopt;
}

double Arrangement2D::curve_endpoint_junction_length(CurveConstHandle p_curve, const CGAL::Point &p_endpoint, const CGAL::Point &p_next_source_point) {
	std::optional<CGAL::Halfedge_const_handle> current = source_halfedge_from_endpoint(p_curve, p_endpoint, p_next_source_point);
	if (!current.has_value()) {
		return 0.0;
	}
	if ((*current)->source()->degree() > 2) {
		return 0.0;
	}

	double length = 0.0;
	std::set<CGAL::Halfedge_const_handle> visited;
	while (current.has_value() && visited.insert(*current).second) {
		length += x_monotone_curve_length((*current)->curve());

		CGAL::Vertex_const_handle target = (*current)->target();
		if (target->degree() > 2) {
			return length;
		}
		current = next_source_halfedge(p_curve, target, *current);
	}

	return length;
}

bool Arrangement2D::curve_endpoint_is_dangling(CurveConstHandle p_curve, const CGAL::Point &p_endpoint, const CGAL::Point &p_next_source_point) {
	std::optional<CGAL::Halfedge_const_handle> halfedge = source_halfedge_from_endpoint(p_curve, p_endpoint, p_next_source_point);
	return halfedge.has_value() && (*halfedge)->source()->degree() == 1;
}

Vector2 Arrangement2D::curve_endpoint_junction_lengths(CurveConstHandle p_curve) {
	auto first_point = p_curve->points_begin();
	auto end_point = p_curve->points_end();
	if (first_point == end_point) {
		return Vector2(0, 0);
	}

	auto second_point = std::next(first_point);
	if (second_point == end_point) {
		return Vector2(0, 0);
	}

	auto last_point = end_point;
	--last_point;
	auto before_last_point = last_point;
	--before_last_point;

	double start_length = curve_endpoint_junction_length(p_curve, *first_point, *second_point);
	double end_length = curve_endpoint_junction_length(p_curve, *last_point, *before_last_point);
	return Vector2(static_cast<float>(start_length), static_cast<float>(end_length));
}

Dictionary Arrangement2D::make_curve_endpoint_info(CurveConstHandle p_curve) {
	Dictionary result{};
	result["start_junction_length"] = 0.0f;
	result["start_dangling"] = false;
	result["end_junction_length"] = 0.0f;
	result["end_dangling"] = false;

	auto first_point = p_curve->points_begin();
	auto end_point = p_curve->points_end();
	if (first_point == end_point) {
		return result;
	}

	auto second_point = std::next(first_point);
	if (second_point == end_point) {
		return result;
	}

	auto last_point = end_point;
	--last_point;
	auto before_last_point = last_point;
	--before_last_point;

	result["start_junction_length"] = static_cast<float>(curve_endpoint_junction_length(p_curve, *first_point, *second_point));
	result["start_dangling"] = curve_endpoint_is_dangling(p_curve, *first_point, *second_point);
	result["end_junction_length"] = static_cast<float>(curve_endpoint_junction_length(p_curve, *last_point, *before_last_point));
	result["end_dangling"] = curve_endpoint_is_dangling(p_curve, *last_point, *before_last_point);
	return result;
}

TypedArray<Dictionary> Arrangement2D::polyline_query_edges(PackedVector2Array p_polyline) {
	TypedArray<Dictionary> result{};
	std::map<const CGAL::Curve *, std::vector<float>> stop_ts_cache;
	for (const SourceCurveHit &hit : collect_polyline_query_source_hits(p_polyline)) {
		const CGAL::Curve *source_curve = &(*hit.source_curve);
		auto [stop_ts_it, inserted] = stop_ts_cache.try_emplace(source_curve);
		std::vector<float> &stop_ts = stop_ts_it->second;
		if (inserted) {
			stop_ts = curve_stop_ts(arrangement, hit.source_curve);
		}

		for (const PolyTRange &hit_range : halfedge_to_poly_ranges(*hit.source_curve, hit.halfedge)) {
			PolyTRange expanded_range = expand_to_stops(stop_ts, hit_range);
			if (expanded_range.to <= expanded_range.from) {
				continue;
			}
			result.push_back(make_edge_query_result(hit.source_id, expanded_range.from, expanded_range.to));
		}
	}
	return result;
}
