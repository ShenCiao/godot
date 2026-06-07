#pragma once

#include "arrangement_alias.h"
#include "arrangement_observer.h"

#include "core/object/object.h"
#include "core/templates/rid_owner.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"

#include <unordered_map>
#include <vector>

class Arrangement2D : public Object {
	GDCLASS(Arrangement2D, Object)

protected:
	static void _bind_methods();

public:
	CGAL::Arrangement arrangement;
	CGAL::PointLocation point_location = { arrangement };

	RID_Owner<CGAL::Curve_handle> curve_handle_owner;
	RID_Owner<CGAL::Face_const_handle> face_handle_owner;
	std::unordered_map<CGAL::Face_const_handle, RID> face_handle_to_rid;
	std::unordered_map<const CGAL::Curve *, RID> curve_handle_to_rid;

	void _notification(int p_what);

	Arrangement2D();

	RID create_polyline();
	void set_polyline(RID p_id, PackedVector2Array p_data); // Returns invalid face RIDs.
	void remove_polyline(RID p_id);

	RID point_query_face(Vector2 p_point);
	TypedArray<RID> points_query_faces(PackedVector2Array p_points);
	TypedArray<RID> polyline_query_faces(PackedVector2Array p_polyline); // Returns face RIDs.
	TypedArray<Dictionary> polyline_query_edges(PackedVector2Array p_polyline);
	TypedArray<PackedVector2Array> get_polygon_from_face(RID p_id);
	Dictionary get_triangles_from_face(RID p_id);
	bool is_unbounded_face(RID p_id);
	RID get_unbounded_face();

	static Dictionary repair_and_triangulate(TypedArray<PackedVector2Array> p_polygons);

	RID cache_face_handle(CGAL::Face_const_handle p_handle);
	std::vector<CGAL::Face_const_handle> zone_query(const CGAL::X_monotone_curve &p_mono_curve);
	std::vector<CGAL::Halfedge_const_handle> zone_query_edges(const CGAL::X_monotone_curve &p_mono_curve);
	Dictionary make_edge_query_result(RID p_source_id, float p_from_t, float p_to_t, Vector2 p_from_point, Vector2 p_to_point);
	static float point_to_poly_t(const CGAL::Curve &p_curve, const CGAL::Point &p_point);
	static std::vector<CGAL::X_monotone_curve> construct_x_monotone_curves(PackedVector2Array p_polyline);
	static PackedVector2Array remove_consecutive_overlapping_points(PackedVector2Array p_polyline);
	static std::vector<CGAL::Point> vector2_to_points(PackedVector2Array p_polyline);
	static Vector2 point_to_vector2(const CGAL::Point &p_point);
	static CGAL::Polygon2 packed_to_polygon(const PackedVector2Array &p_polygon);
	static std::vector<CGAL::Polygon2> packed_to_polygons(TypedArray<PackedVector2Array> p_polygons);
	static PackedVector2Array polygon_to_packed(const CGAL::Polygon2 &p_polygon);
	static std::vector<CGAL::Polygon2> face_to_raw_polygons(CGAL::Face_const_handle p_face);
	// Takes ownership of the polygon list so each ring can be moved into CGAL's repair input.
	static CGAL::MultipolygonWithHoles2 repair_polygons(std::vector<CGAL::Polygon2> p_polygons);
	static TypedArray<PackedVector2Array> face_to_polygons(CGAL::Face_const_handle p_face);
	static TypedArray<PackedVector2Array> multipolygon_to_packed_polygons(const CGAL::MultipolygonWithHoles2 &p_multipolygon);
	static Dictionary triangulate(const CGAL::MultipolygonWithHoles2 &p_multipolygon);
	static Dictionary make_triangle_result(PackedVector2Array p_vertices = {}, PackedInt32Array p_indices = {});
	inline static const CGAL::Geom_traits::Construct_curve_2 curve_constructor =
			CGAL::Geom_traits{}.construct_curve_2_object();
	inline static const CGAL::Geom_traits::Make_x_monotone_2 x_monotone_maker =
			CGAL::Geom_traits{}.make_x_monotone_2_object();
};
