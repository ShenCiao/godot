//
// Created by Ciao on 2026/1/16.
//


#include "arrangement_2d.h"

#include "core/error/error_macros.h"
#include "core/object/class_db.h"
#include "core/string/ustring.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <variant>

namespace {

using CurveConstHandle = CGAL::Arrangement::Curve_const_handle;

struct PolyTRange {
	float from = 0.0f;
	float to = 0.0f;
};

double segment_fraction(const CGAL::Point &p_from, const CGAL::Point &p_to, const CGAL::Point &p_point) {
	double dx = CGAL::to_double(p_to.x() - p_from.x());
	double dy = CGAL::to_double(p_to.y() - p_from.y());
	double numerator = std::abs(dx) >= std::abs(dy)
			? CGAL::to_double(p_point.x() - p_from.x())
			: CGAL::to_double(p_point.y() - p_from.y());
	double denominator = std::abs(dx) >= std::abs(dy) ? dx : dy;
	if (denominator == 0.0) {
		return 0.0;
	}
	return numerator / denominator;
}

bool point_on_segment(const CGAL::Point &p_from, const CGAL::Point &p_point, const CGAL::Point &p_to) {
	CGAL::Segment_traits traits;
	CGAL::Segment_traits::Collinear_2 collinear = traits.collinear_2_object();
	if (!collinear(p_from, p_point, p_to)) {
		return false;
	}
	CGAL::Segment_traits::Collinear_are_ordered_along_line_2 ordered = traits.collinear_are_ordered_along_line_2_object();
	return ordered(p_from, p_point, p_to);
}

std::vector<float> point_to_poly_ts(const CGAL::Curve &p_curve, const CGAL::Point &p_point) {
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

std::vector<PolyTRange> subedge_to_poly_ranges(const CGAL::Curve &p_curve, const CGAL::Point &p_from, const CGAL::Point &p_to) {
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

std::vector<PolyTRange> halfedge_to_poly_ranges(const CGAL::Curve &p_curve, CGAL::Halfedge_const_handle p_halfedge) {
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

void add_point_stop_ts(std::vector<float> &r_stop_ts, const CGAL::Curve &p_curve, const CGAL::Point &p_point) {
	for (float t : point_to_poly_ts(p_curve, p_point)) {
		r_stop_ts.push_back(t);
	}
}

bool points_equal(const CGAL::Point &p_a, const CGAL::Point &p_b) {
	CGAL::Segment_traits traits;
	CGAL::Segment_traits::Compare_xy_2 compare_xy = traits.compare_xy_2_object();
	return compare_xy(p_a, p_b) == CGAL::EQUAL;
}

void add_source_self_intersection_stop_ts(std::vector<float> &r_stop_ts, const CGAL::Curve &p_curve) {
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

bool has_originating_curve(CGAL::Arrangement &p_arrangement, CGAL::Halfedge_const_handle p_halfedge, CurveConstHandle p_curve) {
	for (auto curve_it = p_arrangement.originating_curves_begin(p_halfedge);
			curve_it != p_arrangement.originating_curves_end(p_halfedge);
			++curve_it) {
		if (&(*curve_it) == &(*p_curve)) {
			return true;
		}
	}
	return false;
}

std::vector<float> curve_stop_ts(CGAL::Arrangement &p_arrangement, CurveConstHandle p_curve) {
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

PolyTRange expand_to_stops(const std::vector<float> &p_stop_ts, const PolyTRange &p_hit_range) {
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

} // namespace

void Arrangement2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("clear"), &Arrangement2D::clear);
	ClassDB::bind_method(D_METHOD("create_polyline", "id"), &Arrangement2D::create_polyline);
	ClassDB::bind_method(D_METHOD("remove_polyline", "id"), &Arrangement2D::remove_polyline);
	ClassDB::bind_method(D_METHOD("set_polyline", "id", "data"), &Arrangement2D::set_polyline);
	ClassDB::bind_method(D_METHOD("point_query_face", "point"), &Arrangement2D::point_query_face);
	ClassDB::bind_method(D_METHOD("polyline_query_faces", "polyline"), &Arrangement2D::polyline_query_faces);
	ClassDB::bind_method(D_METHOD("polyline_query_edges", "polyline"), &Arrangement2D::polyline_query_edges);
	ClassDB::bind_method(D_METHOD("points_query_faces", "points"), &Arrangement2D::points_query_faces);
	ClassDB::bind_method(D_METHOD("get_all_faces"), &Arrangement2D::get_all_faces);
	ClassDB::bind_method(D_METHOD("get_polygon_from_face", "face_id"), &Arrangement2D::get_polygon_from_face);
	ClassDB::bind_method(D_METHOD("get_triangles_from_face", "face_id"), &Arrangement2D::get_triangles_from_face);
	ClassDB::bind_method(D_METHOD("is_unbounded_face", "id"), &Arrangement2D::is_unbounded_face);
	ClassDB::bind_method(D_METHOD("get_unbounded_face"), &Arrangement2D::get_unbounded_face);
	ClassDB::bind_static_method("Arrangement2D", D_METHOD("repair_and_triangulate", "polygons"), &Arrangement2D::repair_and_triangulate);
}

void Arrangement2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_PREDELETE) {
		clear();
	}
}

Arrangement2D::Arrangement2D() {
	observer.arrangement_2d = this;
}

void Arrangement2D::clear() {
	clear_face_cache();
	curve_handles.clear();
	curve_handle_to_id.clear();
	arrangement.clear();
}

void Arrangement2D::create_polyline(int64_t p_id) {
	CGAL::Curve_handle curve_handle = curve_handles[p_id];
	if (curve_handle != nullptr) {
		curve_handle_to_id.erase(&(*curve_handle));
		CGAL::remove_curve(arrangement, curve_handle);
	}
	curve_handles[p_id] = nullptr;
}

void Arrangement2D::set_polyline(int64_t p_id, PackedVector2Array p_data) {
	CGAL::Curve_handle curve_handle = curve_handles[p_id];
	if (curve_handle != nullptr) {
		curve_handle_to_id.erase(&(*curve_handle));
		CGAL::remove_curve(arrangement, curve_handle);
		curve_handles[p_id] = nullptr;
	}
	p_data = remove_consecutive_overlapping_points(p_data);
	if (p_data.size() < 2) {
		return;
	}
	CGAL::Curve curve = curve_constructor(vector2_to_points(p_data));
	auto handle = CGAL::insert(arrangement, curve);
	curve_handles[p_id] = handle;
	curve_handle_to_id[&(*handle)] = p_id;
}

void Arrangement2D::remove_polyline(int64_t p_id) {
	auto curve_handle_it = curve_handles.find(p_id);
	if (curve_handle_it == curve_handles.end()) {
		return;
	}

	CGAL::Curve_handle curve_handle = curve_handle_it->second;
	if (curve_handle != nullptr) {
		curve_handle_to_id.erase(&(*curve_handle));
		CGAL::remove_curve(arrangement, curve_handle);
	}
	curve_handles.erase(curve_handle_it);
}

RID Arrangement2D::point_query_face(Vector2 p_point) {
	auto obj = point_location.locate(CGAL::Point(p_point.x, p_point.y));
	auto face_handle_ptr = std::get_if<CGAL::Face_const_handle>(&obj);
	if (face_handle_ptr != nullptr) {
		return cache_face_handle(*face_handle_ptr);
	}
	return {};
}

void Arrangement2D::clear_face_cache() {
	LocalVector<RID> rids = face_handle_owner.get_owned_list();
	for (const RID &id : rids) {
		face_handle_owner.free(id);
	}
	face_handle_to_rid.clear();
}

void Arrangement2D::invalidate_face(CGAL::Face_const_handle p_handle) {
	const auto rid_it = face_handle_to_rid.find(p_handle);
	if (rid_it == face_handle_to_rid.end()) {
		return;
	}
	face_handle_owner.free(rid_it->second);
	face_handle_to_rid.erase(rid_it);
}

RID Arrangement2D::cache_face_handle(CGAL::Face_const_handle p_handle) {
	if (face_handle_to_rid.find(p_handle) == face_handle_to_rid.end()) {
		RID id = face_handle_owner.make_rid(p_handle);
		face_handle_to_rid[p_handle] = id;
		return id;
	} else {
		return face_handle_to_rid[p_handle];
	}
}

TypedArray<RID> Arrangement2D::points_query_faces(PackedVector2Array p_points) {
	TypedArray<RID> rids{};
	rids.resize(p_points.size());

	using QueryResult = std::pair<CGAL::Point, CGAL::PointLocation::Result_type>;
	std::list<QueryResult> query_results;

	// CGAL::locate requires a linear container.
	std::vector<CGAL::Point> query_points = vector2_to_points(p_points);
	CGAL::locate(arrangement, query_points.begin(), query_points.end(), std::back_inserter(query_results));

	for (auto &[point, obj] : query_results) {
		size_t index = std::distance(query_points.begin(), std::find(query_points.begin(), query_points.end(), point));
		auto face_handle_ptr = std::get_if<CGAL::Face_const_handle>(&obj);
		if (face_handle_ptr != nullptr) {
			rids[index] = cache_face_handle(*face_handle_ptr);
		}
	}
	return rids;
}

TypedArray<RID> Arrangement2D::polyline_query_faces(PackedVector2Array p_polyline) {
	p_polyline = remove_consecutive_overlapping_points(p_polyline);
	if (p_polyline.size() == 0) {
		return {};
	}
	if (p_polyline.size() == 1) {
		return { point_query_face(p_polyline[0]) };
	}

	auto mono_curves = construct_x_monotone_curves(p_polyline);
	std::set<RID> ids{}; // remove duplicate
	for (auto &curve : mono_curves) {
		auto face_handles = zone_query(curve);
		for (auto handle : face_handles) {
			ids.insert(cache_face_handle(handle));
		}
	}

	TypedArray<RID> result{};
	for (RID id : ids)
		result.push_back(id);
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
			if (inserted) {
				stop_ts_it->second = curve_stop_ts(arrangement, curve_it);
			}

			for (const PolyTRange &hit_range : halfedge_to_poly_ranges(*curve_it, halfedge)) {
				PolyTRange expanded_range = expand_to_stops(stop_ts_it->second, hit_range);
				if (expanded_range.to <= expanded_range.from) {
					continue;
				}
				result.push_back(make_edge_query_result(source_id, expanded_range.from, expanded_range.to));
			}
		}
	}
	return result;
}

std::vector<CGAL::X_monotone_curve> Arrangement2D::construct_x_monotone_curves(PackedVector2Array p_polyline) {
	auto curve = curve_constructor(vector2_to_points(p_polyline));
	using Make_x_monotone_result = std::variant<CGAL::Point, CGAL::X_monotone_curve>;
	std::vector<Make_x_monotone_result> result_objects;
	x_monotone_maker(curve, std::back_inserter(result_objects));

	std::vector<CGAL::X_monotone_curve> result{};
	for (const auto &x_obj : result_objects) {
		const auto* mono_curve = std::get_if<CGAL::X_monotone_curve>(&x_obj);
		if (mono_curve != nullptr) {
			result.push_back(*mono_curve);
		}
	}
	return result;
}

std::vector<CGAL::Face_const_handle> Arrangement2D::zone_query(const CGAL::X_monotone_curve &p_mono_curve) {
	std::vector<CGAL::Face_const_handle> result{};
	using Result = std::variant<CGAL::Arrangement::Vertex_handle, CGAL::Arrangement::Halfedge_handle, CGAL::Arrangement::Face_handle>;
	std::vector<Result> output;
	output.reserve(256);
	CGAL::zone(arrangement, p_mono_curve, std::back_inserter(output), point_location);

	for (auto &object : output) {
		if (auto face_handle_ptr = std::get_if<CGAL::Arrangement::Face_handle>(&object)) {
			result.emplace_back(*face_handle_ptr);
		}
	}

	return result;
}

std::vector<CGAL::Halfedge_const_handle> Arrangement2D::zone_query_edges(const CGAL::X_monotone_curve &p_mono_curve) {
	std::vector<CGAL::Halfedge_const_handle> result{};
	using Result = std::variant<CGAL::Arrangement::Vertex_handle, CGAL::Arrangement::Halfedge_handle, CGAL::Arrangement::Face_handle>;
	std::vector<Result> output;
	output.reserve(1024);
	CGAL::zone(arrangement, p_mono_curve, std::back_inserter(output), point_location);

	for (auto &object : output) {
		if (auto halfedge_handle_ptr = std::get_if<CGAL::Arrangement::Halfedge_handle>(&object)) {
			result.emplace_back(*halfedge_handle_ptr);
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

TypedArray<RID> Arrangement2D::get_all_faces() {
	TypedArray<RID> result{};
	for (auto face_it = arrangement.faces_begin(); face_it != arrangement.faces_end(); ++face_it) {
		result.push_back(cache_face_handle(face_it));
	}
	return result;
}

TypedArray<PackedVector2Array> Arrangement2D::get_polygon_from_face(RID p_id) {
	if (!p_id.is_valid() || !face_handle_owner.owns(p_id)) {
		return {};
	}
	auto handle = *face_handle_owner.get_or_null(p_id);
	return face_to_polygons(handle);
}

Dictionary Arrangement2D::get_triangles_from_face(RID p_id) {
	if (!p_id.is_valid() || !face_handle_owner.owns(p_id)) {
		return make_triangle_result();
	}

	CGAL::Face_const_handle handle = *face_handle_owner.get_or_null(p_id);
	// Face rings can include hole CCBs and zero-width artifacts, so triangulation must run after repair.
	return triangulate(repair_polygons(face_to_raw_polygons(handle)));
}

bool Arrangement2D::is_unbounded_face(RID p_id) {
	if (!p_id.is_valid()) {
		return false;
	}
	if (!face_handle_owner.owns(p_id)) {
		ERR_PRINT(vformat("Given RID %d is not a face.", p_id.get_id()));
		return false;
	}
	CGAL::Face_const_handle handle = *face_handle_owner.get_or_null(p_id);
	return handle->is_unbounded();
}

Dictionary Arrangement2D::repair_and_triangulate(TypedArray<PackedVector2Array> p_polygons) {
	return triangulate(repair_polygons(packed_to_polygons(p_polygons)));
}

PackedVector2Array Arrangement2D::remove_consecutive_overlapping_points(PackedVector2Array p_polyline) {
	auto end_it = std::unique(p_polyline.begin(), p_polyline.end());
	size_t size = 0;
	auto it = p_polyline.begin();
	while (it != end_it) {
		size++;
		++it;
	}
	p_polyline.resize(size);
	return p_polyline;
}

std::vector<CGAL::Point> Arrangement2D::vector2_to_points(PackedVector2Array p_polyline) {
	std::vector<CGAL::Point> points;
	points.reserve(p_polyline.size());
	for (auto point : p_polyline) {
		points.emplace_back(point.x, point.y);
	}
	return points;
}

Vector2 Arrangement2D::point_to_vector2(const CGAL::Point &p_point) {
	return {
		static_cast<float>(CGAL::to_double(p_point.x())),
		static_cast<float>(CGAL::to_double(p_point.y()))
	};
}

CGAL::Polygon2 Arrangement2D::packed_to_polygon(const PackedVector2Array &p_polygon) {
	CGAL::Polygon2 polygon{};
	polygon.reserve(p_polygon.size());

	for (Vector2 point : p_polygon) {
		CGAL::Point cgal_point(point.x, point.y);
		if (polygon.size() > 0 && polygon.container().back() == cgal_point) {
			continue;
		}
		polygon.push_back(cgal_point);
	}
	if (polygon.size() > 1 && polygon.container().front() == polygon.container().back()) {
		polygon.container().pop_back();
	}
	return polygon;
}

std::vector<CGAL::Polygon2> Arrangement2D::packed_to_polygons(TypedArray<PackedVector2Array> p_polygons) {
	std::vector<CGAL::Polygon2> polygons{};
	polygons.reserve(p_polygons.size());

	for (int i = 0; i < p_polygons.size(); i++) {
		CGAL::Polygon2 polygon = packed_to_polygon(p_polygons[i]);
		if (polygon.size() >= 3) {
			polygons.push_back(std::move(polygon));
		}
	}
	return polygons;
}

PackedVector2Array Arrangement2D::polygon_to_packed(const CGAL::Polygon2 &p_polygon) {
	PackedVector2Array result{};
	result.resize(p_polygon.size());

	Vector2 *write = result.ptrw();
	int index = 0;
	for (const CGAL::Point &point : p_polygon.vertices()) {
		write[index++] = {
			static_cast<float>(CGAL::to_double(point.x())),
			static_cast<float>(CGAL::to_double(point.y()))
		};
	}
	return result;
}

std::vector<CGAL::Polygon2> Arrangement2D::face_to_raw_polygons(CGAL::Face_const_handle p_face) {
	std::vector<CGAL::Arrangement::Ccb_halfedge_const_circulator> ccb_circulators{};
	if (!p_face->is_unbounded()) {
		ccb_circulators.push_back(p_face->outer_ccb());
	}
	// For unbounded faces, hole CCBs are the finite regions enclosed by the infinite exterior.
	// Keep all CCBs in one ring set; even-odd repair reconstructs the filled areas and holes.
	for (auto hole_it = p_face->holes_begin(); hole_it != p_face->holes_end(); ++hole_it) {
		CGAL::Arrangement::Ccb_halfedge_const_circulator hole_ccb = *hole_it;
		ccb_circulators.push_back(hole_ccb);
	}

	std::vector<CGAL::Polygon2> result{};
	result.reserve(ccb_circulators.size());

	for (auto ccb : ccb_circulators) {
		auto curr = ccb;
		CGAL::Polygon2 polygon{};

		do {
			// Points in halfedge->curve() are always in x-mono increasing order, which may not match source-to-target order.
			auto begin_it = curr->curve().points_begin();
			auto end_it = curr->curve().points_end();
			auto last_it = end_it;
			--last_it;

			if (curr->source()->point() == *begin_it) {
				for (auto it = begin_it; it != last_it; ++it) {
					if (polygon.size() == 0 || polygon.container().back() != *it) {
						polygon.push_back(*it);
					}
				}
			} else {
				for (auto it = last_it; it != begin_it; --it) {
					if (polygon.size() == 0 || polygon.container().back() != *it) {
						polygon.push_back(*it);
					}
				}
			}
		} while (++curr != ccb);

		if (polygon.size() > 1 && polygon.container().front() == polygon.container().back()) {
			polygon.container().pop_back();
		}
		if (polygon.size() >= 3) {
			result.push_back(std::move(polygon));
		}
	}
	return result;
}

CGAL::MultipolygonWithHoles2 Arrangement2D::repair_polygons(std::vector<CGAL::Polygon2> p_polygons) {
	CGAL::MultipolygonWithHoles2 multipolygon{};
	for (CGAL::Polygon2 &polygon : p_polygons) {
		multipolygon.add_polygon(std::move(polygon));
	}
	// The default repair rule is even-odd: nested rings become holes, while spike/bridge
	// artifacts collapse before we export polygons or triangulate.
	return CGAL::Polygon_repair::repair(multipolygon);
}

TypedArray<PackedVector2Array> Arrangement2D::face_to_polygons(CGAL::Face_const_handle p_face) {
	return multipolygon_to_packed_polygons(repair_polygons(face_to_raw_polygons(p_face)));
}

TypedArray<PackedVector2Array> Arrangement2D::multipolygon_to_packed_polygons(const CGAL::MultipolygonWithHoles2 &p_multipolygon) {
	TypedArray<PackedVector2Array> result{};
	for (const CGAL::PolygonWithHoles2 &polygon_with_holes : p_multipolygon.polygons_with_holes()) {
		// Repair has made hole topology explicit again; the script API still exposes flat rings.
		PackedVector2Array outer_boundary = polygon_to_packed(polygon_with_holes.outer_boundary());
		if (!outer_boundary.is_empty()) {
			result.push_back(outer_boundary);
		}
		for (auto hole_it = polygon_with_holes.holes_begin(); hole_it != polygon_with_holes.holes_end(); ++hole_it) {
			PackedVector2Array hole = polygon_to_packed(*hole_it);
			if (!hole.is_empty()) {
				result.push_back(hole);
			}
		}
	}
	return result;
}

Dictionary Arrangement2D::triangulate(const CGAL::MultipolygonWithHoles2 &p_multipolygon) {
	CGAL::ConstrainedDelaunayTriangulation2 triangulation{};

	for (const CGAL::PolygonWithHoles2 &polygon_with_holes : p_multipolygon.polygons_with_holes()) {
		const CGAL::Polygon2 &outer_boundary = polygon_with_holes.outer_boundary();
		triangulation.insert_constraint(outer_boundary.vertices_begin(), outer_boundary.vertices_end(), true);

		// Holes are inserted as constraints too; domain marking later excludes their interior.
		for (auto hole_it = polygon_with_holes.holes_begin(); hole_it != polygon_with_holes.holes_end(); ++hole_it) {
			triangulation.insert_constraint(hole_it->vertices_begin(), hole_it->vertices_end(), true);
		}
	}

	if (triangulation.dimension() < 2 || triangulation.number_of_faces() == 0) {
		return make_triangle_result();
	}

	// mark_domain_in_triangulation treats odd nesting levels as inside the domain.
	CGAL::mark_domain_in_triangulation(triangulation);

	std::map<CGAL::ConstrainedDelaunayTriangulation2::Vertex_handle, int> vertex_indices{};
	PackedVector2Array vertices{};
	PackedInt32Array indices{};

	auto add_vertex = [&](CGAL::ConstrainedDelaunayTriangulation2::Vertex_handle p_vertex) {
		auto it = vertex_indices.find(p_vertex);
		if (it != vertex_indices.end()) {
			return it->second;
		}

		const CGAL::Point &point = p_vertex->point();
		int index = vertices.size();
		vertices.push_back(point_to_vector2(point));
		vertex_indices[p_vertex] = index;
		return index;
	};

	for (auto face_it = triangulation.finite_faces_begin(); face_it != triangulation.finite_faces_end(); ++face_it) {
		if (!face_it->is_in_domain()) {
			continue;
		}

		indices.push_back(add_vertex(face_it->vertex(0)));
		indices.push_back(add_vertex(face_it->vertex(1)));
		indices.push_back(add_vertex(face_it->vertex(2)));
	}

	return make_triangle_result(vertices, indices);
}

Dictionary Arrangement2D::make_triangle_result(PackedVector2Array p_vertices, PackedInt32Array p_indices) {
	Dictionary result{};
	result["vertices"] = p_vertices;
	result["indices"] = p_indices;
	return result;
}

RID Arrangement2D::get_unbounded_face() {
	return cache_face_handle(arrangement.unbounded_face());
}
