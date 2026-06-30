#pragma once

#include "arrangement_alias.h"
#include "arrangement_observer.h"

#include "core/object/object.h"
#include "core/templates/rid_owner.h"
#include "core/typedefs.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"

#include <optional>
#include <unordered_map>
#include <vector>

class Arrangement2D : public Object {
	GDCLASS(Arrangement2D, Object)
	friend class ArrangementObserver;

protected:
	static void _bind_methods();

public:
	// Arrangement state.
	CGAL::Arrangement arrangement;
	CGAL::PointLocation point_location = { arrangement };
	ArrangementObserver observer{ arrangement };

	RID_Owner<CGAL::Face_const_handle> face_handle_owner;
	std::unordered_map<CGAL::Face_const_handle, RID> face_handle_to_rid;
	std::unordered_map<int64_t, CGAL::Curve_handle> curve_handles;
	std::unordered_map<const CGAL::Curve *, int64_t> curve_handle_to_id;

	// Object lifecycle.
	void _notification(int p_what);

	Arrangement2D();

	// Source curve mutation.
	void clear();
	void create_polyline(int64_t p_id);
	void set_polyline(int64_t p_id, PackedVector2Array p_data); // Returns invalid face RIDs.
	void remove_polyline(int64_t p_id);

	// Script-facing query API.
	RID point_query_face(Vector2 p_point);
	TypedArray<RID> points_query_faces(PackedVector2Array p_points);
	TypedArray<RID> polyline_query_faces(PackedVector2Array p_polyline); // Returns face RIDs.
	PackedInt64Array polyline_query_curves(PackedVector2Array p_polyline);
	TypedArray<Dictionary> polyline_query_edges(PackedVector2Array p_polyline);
	TypedArray<Dictionary> polyline_query_curve_intersections(PackedVector2Array p_polyline);
	Vector2 get_curve_endpoint_junction_lengths(int64_t p_curve_id);
	Dictionary get_curve_endpoint_info(int64_t p_curve_id);
	TypedArray<RID> get_all_faces();
	TypedArray<PackedVector2Array> get_polygon_from_face(RID p_id);
	Dictionary get_triangles_from_face(RID p_id);
	bool is_unbounded_face(RID p_id);
	RID get_unbounded_face();

	// Script-facing polygon repair API.
	static Dictionary repair_and_triangulate(TypedArray<PackedVector2Array> p_polygons);

private:
	// Shared query support types.
	struct PolyTRange {
		float from = 0.0f;
		float to = 0.0f;
	};

	struct SourceCurveHit {
		CGAL::Halfedge_const_handle halfedge;
		CGAL::Arrangement::Curve_const_handle source_curve;
		int64_t source_id = 0;
	};

	// Face handle cache and arrangement traversal.
	void clear_face_cache();
	void invalidate_face(CGAL::Face_const_handle p_handle);
	RID cache_face_handle(CGAL::Face_const_handle p_handle);
	std::vector<CGAL::Face_const_handle> zone_query(const CGAL::X_monotone_curve &p_mono_curve);
	std::vector<CGAL::Halfedge_const_handle> zone_query_edges(const CGAL::X_monotone_curve &p_mono_curve);

	// Edge query implementation.
	std::vector<SourceCurveHit> collect_polyline_query_source_hits(PackedVector2Array p_polyline);
	Dictionary make_edge_query_result(int64_t p_source_id, float p_from_t, float p_to_t);
	Dictionary make_curve_intersection_result(int64_t p_source_id, float p_query_t, float p_source_t, Vector2 p_position);
	Vector2 curve_endpoint_junction_lengths(CGAL::Arrangement::Curve_const_handle p_curve);
	Dictionary make_curve_endpoint_info(CGAL::Arrangement::Curve_const_handle p_curve);
	double curve_endpoint_junction_length(CGAL::Arrangement::Curve_const_handle p_curve, const CGAL::Point &p_endpoint, const CGAL::Point &p_next_source_point);
	bool curve_endpoint_is_dangling(CGAL::Arrangement::Curve_const_handle p_curve, const CGAL::Point &p_endpoint, const CGAL::Point &p_next_source_point);
	std::optional<CGAL::Halfedge_const_handle> source_halfedge_from_endpoint(CGAL::Arrangement::Curve_const_handle p_curve, const CGAL::Point &p_endpoint, const CGAL::Point &p_next_source_point);
	std::optional<CGAL::Halfedge_const_handle> next_source_halfedge(CGAL::Arrangement::Curve_const_handle p_curve, CGAL::Vertex_const_handle p_vertex, CGAL::Halfedge_const_handle p_previous);
	static bool x_monotone_curve_leaves_endpoint_toward(const CGAL::X_monotone_curve &p_curve, const CGAL::Point &p_endpoint, const CGAL::Point &p_next_source_point);
	static double point_distance(const CGAL::Point &p_a, const CGAL::Point &p_b);
	static double x_monotone_curve_length(const CGAL::X_monotone_curve &p_curve);
	static std::vector<float> point_to_poly_ts(const CGAL::Curve &p_curve, const CGAL::Point &p_point);
	static std::vector<PolyTRange> subedge_to_poly_ranges(const CGAL::Curve &p_curve, const CGAL::Point &p_from, const CGAL::Point &p_to);
	static std::vector<PolyTRange> halfedge_to_poly_ranges(const CGAL::Curve &p_curve, CGAL::Halfedge_const_handle p_halfedge);
	static void add_point_stop_ts(std::vector<float> &r_stop_ts, const CGAL::Curve &p_curve, const CGAL::Point &p_point);
	static void add_source_self_intersection_stop_ts(std::vector<float> &r_stop_ts, const CGAL::Curve &p_curve);
	static bool has_originating_curve(CGAL::Arrangement &p_arrangement, CGAL::Halfedge_const_handle p_halfedge, CGAL::Arrangement::Curve_const_handle p_curve);
	static std::vector<float> curve_stop_ts(CGAL::Arrangement &p_arrangement, CGAL::Arrangement::Curve_const_handle p_curve);
	static PolyTRange expand_to_stops(const std::vector<float> &p_stop_ts, const PolyTRange &p_hit_range);

	// Shared geometry predicates and source curve conversion.
	static bool points_equal(const CGAL::Point &p_a, const CGAL::Point &p_b);
	static bool point_on_segment(const CGAL::Point &p_from, const CGAL::Point &p_point, const CGAL::Point &p_to);
	static double segment_fraction(const CGAL::Point &p_from, const CGAL::Point &p_to, const CGAL::Point &p_point);
	static std::vector<CGAL::X_monotone_curve> construct_x_monotone_curves(PackedVector2Array p_polyline);
	static PackedVector2Array remove_consecutive_overlapping_points(PackedVector2Array p_polyline);
	static std::vector<CGAL::Point> vector2_to_points(PackedVector2Array p_polyline);
	static Vector2 point_to_vector2(const CGAL::Point &p_point);
	static CGAL::Polygon2 packed_to_polygon(const PackedVector2Array &p_polygon);
	static std::vector<CGAL::Polygon2> packed_to_polygons(TypedArray<PackedVector2Array> p_polygons);
	static PackedVector2Array polygon_to_packed(const CGAL::Polygon2 &p_polygon);

	// Face polygon extraction and triangulation.
	static std::vector<CGAL::Polygon2> face_to_raw_polygons(CGAL::Face_const_handle p_face);
	// Takes ownership of the polygon list so each ring can be moved into CGAL's repair input.
	static CGAL::MultipolygonWithHoles2 repair_polygons(std::vector<CGAL::Polygon2> p_polygons);
	static TypedArray<PackedVector2Array> face_to_polygons(CGAL::Face_const_handle p_face);
	static TypedArray<PackedVector2Array> multipolygon_to_packed_polygons(const CGAL::MultipolygonWithHoles2 &p_multipolygon);
	static Dictionary triangulate(const CGAL::MultipolygonWithHoles2 &p_multipolygon);
	static Dictionary make_triangle_result(PackedVector2Array p_vertices = {}, PackedInt32Array p_indices = {});

	// CGAL construction functors.
	// Point-range construction requires at least two consecutive distinct points.
	inline static const CGAL::Geom_traits::Construct_curve_2 curve_constructor =
			CGAL::Geom_traits{}.construct_curve_2_object();
	inline static const CGAL::Geom_traits::Make_x_monotone_2 x_monotone_maker =
			CGAL::Geom_traits{}.make_x_monotone_2_object();
};
