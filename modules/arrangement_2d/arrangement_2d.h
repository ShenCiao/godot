#pragma once

#include "arrangement_alias.h"
#include "arrangement_observer.h"

#include "core/object/object.h"
#include "core/templates/rid_owner.h"
#include "core/typedefs.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"

#include <CGAL/Triangulation_vertex_base_with_info_2.h>

#include <map>
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
	TypedArray<Dictionary> polyline_query_edges(PackedVector2Array p_polyline);
	TypedArray<Dictionary> get_gap_bridge_candidates(double p_max_gap_length);
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

	struct PointLess {
		bool operator()(const CGAL::Point &p_a, const CGAL::Point &p_b) const;
	};

	// Gap bridge support types.
	struct GapBridgeEndpointRef {
		int64_t curve_id = 0;
		float t = 0.0f;
		bool anchor = false;
		std::optional<Vector2> outward_tangent;
	};

	struct GapBridgeEndpointKey {
		int64_t curve_id = 0;
		int64_t quantized_t = 0;

		bool operator<(const GapBridgeEndpointKey &p_other) const;
		bool operator==(const GapBridgeEndpointKey &p_other) const;
	};

	struct GapBridgeCandidate {
		GapBridgeEndpointRef from;
		GapBridgeEndpointRef to;
		double score = 0.0;
	};

	struct GapBridgeSourceCurveSegment {
		CGAL::Point from;
		CGAL::Point to;
		int index = 0;
	};

	struct GapBridgeSourceCurveInfo {
		int64_t curve_id = 0;
		bool is_closed = false;
		float last_t = 0.0f;
		std::vector<GapBridgeSourceCurveSegment> segments;
	};

	using GapBridgeEndpointRefs = std::vector<GapBridgeEndpointRef>;
	using GapBridgeCdtVertexBase = CGAL::Triangulation_vertex_base_with_info_2<GapBridgeEndpointRefs, CGAL::Kernel>;
	using GapBridgeCdtDataStructure = CGAL::Triangulation_data_structure_2<GapBridgeCdtVertexBase, CGAL::CdtFaceBase2>;
	using GapBridgeCdt = CGAL::Constrained_Delaunay_triangulation_2<CGAL::Kernel, GapBridgeCdtDataStructure, CGAL::Exact_intersections_tag>;

	// Face handle cache and arrangement traversal.
	void clear_face_cache();
	void invalidate_face(CGAL::Face_const_handle p_handle);
	RID cache_face_handle(CGAL::Face_const_handle p_handle);
	std::vector<CGAL::Face_const_handle> zone_query(const CGAL::X_monotone_curve &p_mono_curve);
	std::vector<CGAL::Halfedge_const_handle> zone_query_edges(const CGAL::X_monotone_curve &p_mono_curve);

	// Edge query implementation.
	Dictionary make_edge_query_result(int64_t p_source_id, float p_from_t, float p_to_t);
	static std::vector<float> point_to_poly_ts(const CGAL::Curve &p_curve, const CGAL::Point &p_point);
	static std::vector<PolyTRange> subedge_to_poly_ranges(const CGAL::Curve &p_curve, const CGAL::Point &p_from, const CGAL::Point &p_to);
	static std::vector<PolyTRange> halfedge_to_poly_ranges(const CGAL::Curve &p_curve, CGAL::Halfedge_const_handle p_halfedge);
	static void add_point_stop_ts(std::vector<float> &r_stop_ts, const CGAL::Curve &p_curve, const CGAL::Point &p_point);
	static void add_source_self_intersection_stop_ts(std::vector<float> &r_stop_ts, const CGAL::Curve &p_curve);
	static bool has_originating_curve(CGAL::Arrangement &p_arrangement, CGAL::Halfedge_const_handle p_halfedge, CGAL::Arrangement::Curve_const_handle p_curve);
	static std::vector<float> curve_stop_ts(CGAL::Arrangement &p_arrangement, CGAL::Arrangement::Curve_const_handle p_curve);
	static PolyTRange expand_to_stops(const std::vector<float> &p_stop_ts, const PolyTRange &p_hit_range);

	// Gap bridge implementation.
	GapBridgeCdt construct_gap_bridge_cdt(const std::vector<CGAL::Arrangement::Curve_const_handle> &p_curves);
	GapBridgeSourceCurveInfo make_gap_bridge_source_curve_metadata(CGAL::Arrangement::Curve_const_handle p_curve) const;
	void append_gap_bridge_source_constraint_segments(CGAL::Arrangement::Curve_const_handle p_curve, const GapBridgeSourceCurveInfo &p_metadata, std::vector<CGAL::Segment> &r_segments, std::map<CGAL::Point, GapBridgeEndpointRefs, PointLess> &r_point_to_refs) const;
	static GapBridgeEndpointKey gap_bridge_endpoint_key(const GapBridgeEndpointRef &p_ref);
	static bool curve_is_closed(const CGAL::Curve &p_curve);
	static float curve_last_t(const CGAL::Curve &p_curve);
	static std::optional<Vector2> outward_tangent_for_open_endpoint(const GapBridgeSourceCurveSegment &p_segment, bool p_is_start);
	static GapBridgeEndpointRef gap_bridge_endpoint_ref_from_source_segment(const GapBridgeSourceCurveInfo &p_metadata, const GapBridgeSourceCurveSegment &p_segment, const CGAL::Point &p_point, int p_vertex_degree);
	static void append_unique_gap_bridge_endpoint_ref(GapBridgeEndpointRefs &r_refs, const GapBridgeEndpointRef &p_ref);
	static int degree_for_halfedge_point(CGAL::Halfedge_const_handle p_halfedge, const CGAL::Point &p_point);
	static double gap_bridge_tangent_bonus(const GapBridgeEndpointRef &p_ref, const CGAL::Point &p_ref_point, const CGAL::Point &p_other_point);
	static Dictionary make_gap_bridge_candidate_result(const GapBridgeCandidate &p_candidate);

	// Shared geometry predicates and source curve conversion.
	static bool points_equal(const CGAL::Point &p_a, const CGAL::Point &p_b);
	static bool point_on_segment(const CGAL::Point &p_from, const CGAL::Point &p_point, const CGAL::Point &p_to);
	static double segment_fraction(const CGAL::Point &p_from, const CGAL::Point &p_to, const CGAL::Point &p_point);
	static int64_t quantize_t(float p_t);
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
	inline static const CGAL::Geom_traits::Construct_curve_2 curve_constructor =
			CGAL::Geom_traits{}.construct_curve_2_object();
	inline static const CGAL::Geom_traits::Make_x_monotone_2 x_monotone_maker =
			CGAL::Geom_traits{}.make_x_monotone_2_object();
};
