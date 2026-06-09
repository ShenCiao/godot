#pragma once

#define CGAL_DISABLE_GMP true
#define CGAL_DO_NOT_USE_BOOST_MP

#include "custom_arrangement.h" // Custom arrangement with curve history don't fking remove.

#include <CGAL/Arr_batched_point_location.h>
#include <CGAL/Arr_polyline_traits_2.h>
#include <CGAL/Arr_walk_along_line_point_location.h>
#include <CGAL/Arrangement_with_history_2.h>
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Delaunay_mesh_face_base_2.h>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Multipolygon_with_holes_2.h>
#include <CGAL/Polygon_2.h>
#include <CGAL/Polygon_repair/repair.h>
#include <CGAL/Triangulation_data_structure_2.h>
#include <CGAL/Triangulation_vertex_base_2.h>
#include <CGAL/mark_domain_in_triangulation.h>

namespace CGAL {
	using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
	using Segment_traits = CGAL::Arr_segment_traits_2<Kernel>;
	using Geom_traits = CGAL::Arr_polyline_traits_2<Segment_traits>;

	using Point = Geom_traits::Point_2;
	using Segment = Kernel::Segment_2;
	using Curve = Geom_traits::Curve_2; // Curve_2 is polyline, its subcurves are straight segments, use source() and target() to get points.
	using X_monotone_curve = Geom_traits::X_monotone_curve_2;

	using Arrangement = CGAL::Arrangement_with_history_2<Geom_traits>;
	using PointLocation = CGAL::Arr_walk_along_line_point_location<Arrangement>;
	using Polygon2 = CGAL::Polygon_2<Kernel>;
	using PolygonWithHoles2 = CGAL::Polygon_with_holes_2<Kernel>;
	using MultipolygonWithHoles2 = CGAL::Multipolygon_with_holes_2<Kernel>;

	using CdtVertexBase2 = CGAL::Triangulation_vertex_base_2<Kernel>;
	using CdtFaceBase2 = CGAL::Delaunay_mesh_face_base_2<Kernel>;
	using CdtDataStructure2 = CGAL::Triangulation_data_structure_2<CdtVertexBase2, CdtFaceBase2>;
	using ConstrainedDelaunayTriangulation2 = CGAL::Constrained_Delaunay_triangulation_2<Kernel, CdtDataStructure2, CGAL::Exact_intersections_tag>;

	using Vertex_const_handle = Arrangement::Vertex_const_handle;
	using Face_const_handle = Arrangement::Face_const_handle;
	using Halfedge_const_handle = Arrangement::Halfedge_const_handle;
	using Curve_handle = Arrangement::Curve_handle;
}
