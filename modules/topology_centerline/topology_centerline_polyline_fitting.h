/**************************************************************************/
/*  topology_centerline_polyline_fitting.h                                */
/**************************************************************************/

#pragma once

#include "topology_centerline_types.h"

#include <vector>

namespace topology_centerline {

void fit_stroke_polylines(std::vector<Stroke> &r_strokes, const Params &p_params);

} // namespace topology_centerline
