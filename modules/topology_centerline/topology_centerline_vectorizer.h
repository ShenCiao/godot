/**************************************************************************/
/*  topology_centerline_vectorizer.h                                      */
/**************************************************************************/

#pragma once

#include "core/io/image.h"
#include "core/object/object.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"
#include "scene/resources/texture.h"

/**
 * Topology-driven raster centerline vectorization with per-sample radii.
 *
 * The implementation follows the gradient clustering, global/local MST,
 * base-centerline smoothing, and reverse-drawing stages from Noris et al.
 * Output dictionaries contain equal-length `positions` and `radii` arrays in
 * image pixel coordinates. Every stroke intersection is represented by an
 * identical position sample on each participating polyline.
 *
 * Params retain the CenterlineVectorizer API:
 *   threshold      float  default 0.5 - compatibility coverage cutoff before clustering;
 *                                      use 0 to preserve the full grayscale gradient field
 *   max_thickness  float  default 0   - output radius clamp; 0 = unlimited
 *   despeckling    float  default 0   - minimum 4-connected component area
 *   polyline_max_error          float  default 0.35 - maximum geometric fitting error in pixels
 *   polyline_max_segment_length float  default 8    - maximum output segment length; 0 = unlimited
 */
class TopologyCenterlineVectorizer : public Object {
	GDCLASS(TopologyCenterlineVectorizer, Object)

protected:
	static void _bind_methods();

public:
	static TypedArray<Dictionary> vectorize(const Variant &p_source, const Dictionary &p_params = Dictionary());
	static TypedArray<Dictionary> vectorize_image(const Ref<Image> &p_image, const Dictionary &p_params = Dictionary());
	static TypedArray<Dictionary> vectorize_texture(const Ref<Texture2D> &p_texture, const Dictionary &p_params = Dictionary());
};
