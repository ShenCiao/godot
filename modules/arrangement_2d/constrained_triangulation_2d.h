#pragma once

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/type_info.h"
#include "core/variant/variant.h"

// Immutable CDT snapshot. Build and query on an owning thread; retain a reference
// until all operations finish. Packed arrays returned by get_graph() are copy-on-write.
class ConstrainedTriangulation2D : public RefCounted {
	GDCLASS(ConstrainedTriangulation2D, RefCounted)

	struct Impl;
	Impl *impl;

	PackedFloat64Array vertices;
	PackedInt32Array triangles;
	PackedInt32Array opposites;
	PackedByteArray constrained;
	PackedByteArray frame_vertices;

	void export_graph();

protected:
	static void _bind_methods();

public:
	enum LocateResult {
		OUTSIDE = -1,
		ON_CONSTRAINT = -2,
	};

	// Constraint indices are pairs of input vertex indices. Optional frame indices
	// only mark input vertices; the caller supplies the frame's geometry.
	static Ref<ConstrainedTriangulation2D> build(const PackedVector2Array &p_vertices, const PackedInt32Array &p_constraints, const PackedInt32Array &p_frame_vertices = PackedInt32Array());
	Dictionary get_graph() const;
	int get_dimension() const;
	// Returns a triangle index, OUTSIDE, or ON_CONSTRAINT. Dimensions below 2 return OUTSIDE.
	int locate(const Vector2 &p_point) const;

	ConstrainedTriangulation2D();
	~ConstrainedTriangulation2D();
};

VARIANT_ENUM_CAST(ConstrainedTriangulation2D::LocateResult);
