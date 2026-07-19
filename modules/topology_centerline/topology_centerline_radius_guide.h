/**************************************************************************/
/*  topology_centerline_radius_guide.h                                    */
/**************************************************************************/

#pragma once

#include "topology_centerline_types.h"

#include "core/io/image.h"

#include <vector>

namespace topology_centerline {

std::vector<float> build_radius_guide(const Ref<Image> &p_image, const Params &p_params);

} // namespace topology_centerline
