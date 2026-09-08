#include "constrained_triangulation_2d.h"

#include "arrangement_alias.h"

#include "core/error/error_macros.h"
#include "core/object/class_db.h"

#include <CGAL/Constrained_triangulation_face_base_2.h>
#include <CGAL/Constrained_triangulation_plus_2.h>
#include <CGAL/Triangulation_face_base_with_info_2.h>
#include <CGAL/Triangulation_vertex_base_with_info_2.h>

#include <algorithm>
#include <vector>

namespace {
struct VertexInfo {
	int index = -1;
	bool frame = false;
	bool on_constraint = false;
};

using VertexBase = CGAL::Triangulation_vertex_base_with_info_2<VertexInfo, CGAL::Kernel>;
using FaceBase = CGAL::Triangulation_face_base_with_info_2<int, CGAL::Kernel, CGAL::Constrained_triangulation_face_base_2<CGAL::Kernel>>;
using DataStructure = CGAL::Triangulation_data_structure_2<VertexBase, FaceBase>;
using BaseCdt = CGAL::Constrained_Delaunay_triangulation_2<CGAL::Kernel, DataStructure, CGAL::Exact_intersections_tag>;
using Cdt = CGAL::Constrained_triangulation_plus_2<BaseCdt>;
} // namespace

struct ConstrainedTriangulation2D::Impl {
	Cdt cdt;

	int finite_face_index(Cdt::Face_handle p_face) const {
		return cdt.is_infinite(p_face) ? OUTSIDE : p_face->info();
	}
};

void ConstrainedTriangulation2D::_bind_methods() {
	ClassDB::bind_static_method("ConstrainedTriangulation2D", D_METHOD("build", "vertices", "constraints", "frame_vertices"), &ConstrainedTriangulation2D::build, DEFVAL(PackedInt32Array()));
	ClassDB::bind_method(D_METHOD("get_graph"), &ConstrainedTriangulation2D::get_graph);
	ClassDB::bind_method(D_METHOD("get_dimension"), &ConstrainedTriangulation2D::get_dimension);
	ClassDB::bind_method(D_METHOD("locate", "point"), &ConstrainedTriangulation2D::locate);
	BIND_ENUM_CONSTANT(OUTSIDE);
	BIND_ENUM_CONSTANT(ON_CONSTRAINT);
}

ConstrainedTriangulation2D::ConstrainedTriangulation2D() :
		impl(memnew(Impl)) {
}

ConstrainedTriangulation2D::~ConstrainedTriangulation2D() {
	memdelete(impl);
}

Ref<ConstrainedTriangulation2D> ConstrainedTriangulation2D::build(const PackedVector2Array &p_vertices, const PackedInt32Array &p_constraints, const PackedInt32Array &p_frame_vertices) {
	ERR_FAIL_COND_V_MSG(p_constraints.size() % 2 != 0, Ref<ConstrainedTriangulation2D>(), "Constraints must contain pairs of vertex indices.");
	for (int i = 0; i < p_vertices.size(); i++) {
		ERR_FAIL_COND_V_MSG(!p_vertices[i].is_finite(), Ref<ConstrainedTriangulation2D>(), "CDT coordinates must be finite.");
	}
	for (int i = 0; i < p_constraints.size(); i++) {
		ERR_FAIL_INDEX_V(p_constraints[i], p_vertices.size(), Ref<ConstrainedTriangulation2D>());
	}
	for (int i = 0; i < p_frame_vertices.size(); i++) {
		ERR_FAIL_INDEX_V(p_frame_vertices[i], p_vertices.size(), Ref<ConstrainedTriangulation2D>());
	}

	std::vector<CGAL::Point> points;
	points.reserve(p_vertices.size());
	for (int i = 0; i < p_vertices.size(); i++) {
		points.emplace_back(p_vertices[i].x, p_vertices[i].y);
	}
	std::vector<std::pair<int, int>> segments;
	segments.reserve(p_constraints.size() / 2);
	for (int i = 0; i < p_constraints.size(); i += 2) {
		segments.emplace_back(p_constraints[i], p_constraints[i + 1]);
	}

	Ref<ConstrainedTriangulation2D> result;
	result.instantiate();
	Cdt &cdt = result->impl->cdt;
	// CGAL spatially sorts the input points, then inserts constraints and their exact intersections.
	cdt.insert_constraints(points.begin(), points.end(), segments.begin(), segments.end());
	for (int i = 0; i < p_frame_vertices.size(); i++) {
		// Re-inserting an existing point returns its vertex, including merged duplicate inputs.
		cdt.insert(points[p_frame_vertices[i]])->info().frame = true;
	}
	DEV_ASSERT(cdt.is_valid());
	result->export_graph();
	return result;
}

void ConstrainedTriangulation2D::export_graph() {
	Cdt &cdt = impl->cdt;
	const int vertex_count = cdt.number_of_vertices();
	vertices.resize(vertex_count * 2);
	frame_vertices.resize(vertex_count);
	double *vertex_write = vertices.ptrw();
	uint8_t *frame_write = frame_vertices.ptrw();
	int vertex_index = 0;
	for (auto vertex = cdt.finite_vertices_begin(); vertex != cdt.finite_vertices_end(); ++vertex) {
		vertex->info().index = vertex_index;
		vertex_write[vertex_index * 2] = CGAL::to_double(vertex->point().x());
		vertex_write[vertex_index * 2 + 1] = CGAL::to_double(vertex->point().y());
		frame_write[vertex_index] = vertex->info().frame;
		vertex_index++;
	}
	if (cdt.dimension() < 2) {
		return;
	}

	int face_count = 0;
	for (auto face = cdt.finite_faces_begin(); face != cdt.finite_faces_end(); ++face) {
		face->info() = face_count++;
	}
	triangles.resize(face_count * 3);
	opposites.resize(face_count * 3);
	constrained.resize(face_count * 3);
	int32_t *triangle_write = triangles.ptrw();
	int32_t *opposite_write = opposites.ptrw();
	uint8_t *constraint_write = constrained.ptrw();
	for (auto face = cdt.finite_faces_begin(); face != cdt.finite_faces_end(); ++face) {
		for (int side = 0; side < 3; side++) {
			const int halfedge = face->info() * 3 + side;
			triangle_write[halfedge] = face->vertex(side)->info().index;
			// Our side goes vertex(side) -> vertex(side + 1); CGAL indexes the opposite vertex.
			const int edge_index = (side + 2) % 3;
			const auto neighbor = face->neighbor(edge_index);
			opposite_write[halfedge] = cdt.is_infinite(neighbor) ? -1 : neighbor->info() * 3 + (neighbor->index(face) + 1) % 3;
			const bool is_constrained = face->is_constrained(edge_index);
			constraint_write[halfedge] = is_constrained;
			if (is_constrained) {
				face->vertex(side)->info().on_constraint = true;
				face->vertex((side + 1) % 3)->info().on_constraint = true;
			}
		}
	}
}

Dictionary ConstrainedTriangulation2D::get_graph() const {
	Dictionary graph;
	graph["vertices"] = vertices;
	graph["triangles"] = triangles;
	graph["opposites"] = opposites;
	graph["constrained"] = constrained;
	graph["frame_vertices"] = frame_vertices;
	return graph;
}

int ConstrainedTriangulation2D::get_dimension() const {
	return impl->cdt.dimension();
}

int ConstrainedTriangulation2D::locate(const Vector2 &p_point) const {
	ERR_FAIL_COND_V_MSG(!p_point.is_finite(), OUTSIDE, "CDT query coordinates must be finite.");
	const Cdt &cdt = impl->cdt;
	if (cdt.dimension() < 2) {
		return OUTSIDE;
	}
	Cdt::Locate_type type;
	int index;
	const auto face = cdt.locate(CGAL::Point(p_point.x, p_point.y), type, index);
	switch (type) {
		case Cdt::FACE:
			return face->info();
		case Cdt::EDGE: {
			if (face->is_constrained(index)) {
				return ON_CONSTRAINT;
			}
			const int a = impl->finite_face_index(face);
			const int b = impl->finite_face_index(face->neighbor(index));
			return a == OUTSIDE ? b : (b == OUTSIDE ? a : std::min(a, b));
		}
		case Cdt::VERTEX: {
			const auto vertex = face->vertex(index);
			if (vertex->info().on_constraint) {
				return ON_CONSTRAINT;
			}
			int result = OUTSIDE;
			auto incident = cdt.incident_faces(vertex);
			const auto first = incident;
			do {
				const int candidate = impl->finite_face_index(incident);
				if (candidate != OUTSIDE && (result == OUTSIDE || candidate < result)) {
					result = candidate;
				}
			} while (++incident != first);
			return result;
		}
		case Cdt::OUTSIDE_CONVEX_HULL:
		case Cdt::OUTSIDE_AFFINE_HULL:
			return OUTSIDE;
	}
	CRASH_NOW_MSG("Unknown CGAL point-location result.");
	return OUTSIDE;
}
