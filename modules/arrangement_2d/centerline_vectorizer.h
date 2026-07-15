/**************************************************************************/
/*  centerline_vectorizer.h                                               */
/**************************************************************************/

#pragma once

#include "core/io/image.h"
#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"
#include "scene/resources/texture.h"

/**
 * Raster / texture ink regions to centerline strokes with per-vertex radii.
 *
 * Skeleton: topology-preserving Zhang-Suen thinning of the thresholded raster.
 * Radius: Euclidean distance transform sampled along the skeleton.
 * Geometry: arc-length local quadratic smoothing followed by topology-aware
 * CGAL polyline simplification. Endpoints, junctions, and stable cusps remain
 * exact shared anchors.
 *
 * Coordinates: Image pixel space, origin top-left, +Y down. Skeleton vertices
 * lie at pixel centers.
 *
 * Output stroke schema (maps 1:1 to SampledPolyline):
 *   { "positions": PackedVector2Array, "radii": PackedFloat32Array }
 * Smooth opposite branches continue through skeleton junctions, so crossings
 * become complete strokes. Branches without a sufficiently straight partner
 * remain endpoints at the junction.
 *
 * Params (Dictionary):
 *   threshold      float  default 0.5  - ink if coverage >= threshold
 *   max_thickness  float  default 0    - clamp radii; 0 = unlimited
 *   despeckling    float  default 0    - drop 4-connected components smaller than this pixel area
 */
class CenterlineVectorizer : public Object {
	GDCLASS(CenterlineVectorizer, Object)

protected:
	static void _bind_methods();

public:
	/**
	 * @param p_source Image or Texture2D.
	 * @param p_params Optional threshold / max_thickness / despeckling.
	 * @return TypedArray<Dictionary> of { positions, radii }.
	 */
	static TypedArray<Dictionary> vectorize(const Variant &p_source, const Dictionary &p_params = Dictionary());

	/** Explicit Image entry (same as vectorize with Image). */
	static TypedArray<Dictionary> vectorize_image(const Ref<Image> &p_image, const Dictionary &p_params = Dictionary());

	/** Explicit Texture2D entry (calls get_image(), then vectorize_image). */
	static TypedArray<Dictionary> vectorize_texture(const Ref<Texture2D> &p_texture, const Dictionary &p_params = Dictionary());
};
