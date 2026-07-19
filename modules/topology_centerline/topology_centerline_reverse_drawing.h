/**************************************************************************/
/*  topology_centerline_reverse_drawing.h                                 */
/**************************************************************************/

#pragma once

#include "topology_centerline_types.h"

namespace topology_centerline {

std::vector<Stroke> reverse_draw(const PipelineResult &p_pipeline, const Params &p_params,
		const std::vector<float> &p_coverage, int p_width, int p_height);

} // namespace topology_centerline
