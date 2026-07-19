/**************************************************************************/
/*  topology_centerline_pipeline.h                                        */
/**************************************************************************/

#pragma once

#include "topology_centerline_types.h"

namespace topology_centerline {

PipelineResult extract_base_centerlines(const std::vector<float> &p_coverage, int p_width, int p_height, const Params &p_params,
		const std::vector<float> *p_radius_guide = nullptr);

} // namespace topology_centerline
