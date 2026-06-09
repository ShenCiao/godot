#include "arrangement_2d.h"

#include "core/error/error_macros.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <variant>

using CurveConstHandle = CGAL::Arrangement::Curve_const_handle;

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

Dictionary Arrangement2D::make_edge_query_result(int64_t p_source_id, float p_from_t, float p_to_t) {
	Dictionary result{};
	result["source_id"] = p_source_id;
	result["from_t"] = p_from_t;
	result["to_t"] = p_to_t;
	return result;
}

TypedArray<Dictionary> Arrangement2D::polyline_query_edges(PackedVector2Array p_polyline) {
	p_polyline = remove_consecutive_overlapping_points(p_polyline);
	if (p_polyline.size() < 2) {
		return {};
	}

	auto mono_curves = construct_x_monotone_curves(p_polyline);
	std::set<CGAL::Halfedge_const_handle> halfedges{};
	for (auto &curve : mono_curves) {
		for (auto halfedge : zone_query_edges(curve)) {
			halfedges.insert(halfedge);
		}
	}

	TypedArray<Dictionary> result{};
	std::map<const CGAL::Curve *, std::vector<float>> stop_ts_cache;
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
			int64_t source_id = source_id_it->second;

			auto [stop_ts_it, inserted] = stop_ts_cache.try_emplace(source_curve);
			std::vector<float> &stop_ts = stop_ts_it->second;
			if (inserted) {
				stop_ts = curve_stop_ts(arrangement, curve_it);
			}

			for (const PolyTRange &hit_range : halfedge_to_poly_ranges(*curve_it, halfedge)) {
				PolyTRange expanded_range = expand_to_stops(stop_ts, hit_range);
				if (expanded_range.to <= expanded_range.from) {
					continue;
				}
				result.push_back(make_edge_query_result(source_id, expanded_range.from, expanded_range.to));
			}
		}
	}
	return result;
}
