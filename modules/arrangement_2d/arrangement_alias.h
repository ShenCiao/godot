#pragma once

#define CGAL_DISABLE_GMP true
#define CGAL_DO_NOT_USE_BOOST_MP

#include "custom_arrangement.h"

#include <CGAL/Arr_batched_point_location.h>
#include <CGAL/Arr_polyline_traits_2.h>
#include <CGAL/Arr_walk_along_line_point_location.h>
#include <CGAL/Arrangement_with_history_2.h>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>

namespace CGAL {
	using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
	using Segment_traits = CGAL::Arr_segment_traits_2<Kernel>;
	using Geom_traits = CGAL::Arr_polyline_traits_2<Segment_traits>;

	using Point = Geom_traits::Point_2;
	using Curve = Geom_traits::Curve_2; // Curve_2 is polyline, its subcurves are straight segments, use source() and target() to get points.
	using X_monotone_curve = Geom_traits::X_monotone_curve_2;

	using Arrangement = CGAL::Arrangement_with_history_2<Geom_traits>;
	using PointLocation = CGAL::Arr_walk_along_line_point_location<Arrangement>;

	using Vertex_const_handle = Arrangement::Vertex_const_handle;
	using Face_const_handle = Arrangement::Face_const_handle;
	using Halfedge_const_handle = Arrangement::Halfedge_const_handle;
	using Curve_handle = Arrangement::Curve_handle;
}
