//
// Created by Ciao on 2026/1/16.
//


#include "arrangement_2d.h"

#include "core/error/error_macros.h"
#include "core/object/class_db.h"
#include "core/string/ustring.h"

#include <algorithm>
#include <deque>
#include <iterator>
#include <list>
#include <set>
#include <variant>

void Arrangement2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("create_polyline"), &Arrangement2D::create_polyline);
	ClassDB::bind_method(D_METHOD("remove_polyline", "id"), &Arrangement2D::remove_polyline);
	ClassDB::bind_method(D_METHOD("set_polyline", "id", "data"), &Arrangement2D::set_polyline);
	ClassDB::bind_method(D_METHOD("query", "point"), &Arrangement2D::query);
	ClassDB::bind_method(D_METHOD("polyline_query", "polyline"), &Arrangement2D::polyline_query);
	ClassDB::bind_method(D_METHOD("batch_query", "points"), &Arrangement2D::batch_query);
	ClassDB::bind_method(D_METHOD("get_polygon", "face_id"), &Arrangement2D::get_polygon);
	ClassDB::bind_method(D_METHOD("is_unbounded_face", "id"), &Arrangement2D::is_unbounded_face);
	ClassDB::bind_method(D_METHOD("get_unbounded_face"), &Arrangement2D::get_unbounded_face);
}

void Arrangement2D::_notification(int p_what) {
	// CGAL::Arrangement::Halfedge_handle he;
	// auto x = he->source();
	if (p_what == NOTIFICATION_PREDELETE) {
		LocalVector<RID> rids = curve_handle_owner.get_owned_list();
		for (const RID &id : rids) {
			curve_handle_owner.free(id);
		}

		rids = face_handle_owner.get_owned_list();
		for (const RID &id : rids) {
			face_handle_owner.free(id);
		}
		face_handle_to_rid.clear();
	}
}

Arrangement2D::Arrangement2D() {
}

RID Arrangement2D::create_polyline() {
	return curve_handle_owner.make_rid({});
}

TypedArray<RID> Arrangement2D::set_polyline(RID p_id, PackedVector2Array p_data) {
	CGAL::Curve_handle *ptr = curve_handle_owner.get_or_null(p_id);
	if (ptr == nullptr) {
		ERR_PRINT(vformat("Given RID %d is not a polyline.", p_id.get_id()));
		return {};
	}

	CGAL::Curve_handle curve_handle = *ptr;
	if (curve_handle != nullptr) {
		CGAL::remove_curve(arrangement, curve_handle);
		*ptr = nullptr;
	}

	p_data = remove_consecutive_overlapping_points(p_data);
	if (p_data.size() < 2) {
		return {};
	}
	CGAL::Curve curve = curve_constructor(vector2_to_points(p_data));
	auto handle = CGAL::insert(arrangement, curve);
	*ptr = handle;

	// TypedArray<RID> result = invalid_face_rids;
	// invalid_face_rids = TypedArray<RID>();
	// return result;
	return {};
}

TypedArray<RID> Arrangement2D::remove_polyline(RID p_id) {
	CGAL::Curve_handle *ptr = curve_handle_owner.get_or_null(p_id);
	if (ptr == nullptr) {
		ERR_PRINT(vformat("Given RID %d is not a polyline.", p_id.get_id()));
		return {};
	}
	CGAL::Curve_handle curve_handle = *ptr;
	curve_handle_owner.free(p_id);
	if (curve_handle != nullptr) {
		CGAL::remove_curve(arrangement, curve_handle);
	}

	// auto result = invalid_face_rids;
	// invalid_face_rids = TypedArray<RID>();
	// return result;
	return {};
}

RID Arrangement2D::query(Vector2 p_point) {
	auto obj = point_location.locate(CGAL::Point(p_point.x, p_point.y));
	auto face_handle_ptr = std::get_if<CGAL::Face_const_handle>(&obj);
	if (face_handle_ptr != nullptr) {
		return cache_face_handle(*face_handle_ptr);
	}
	return {};
}

RID Arrangement2D::cache_face_handle(CGAL::Face_const_handle p_handle) {
	if (face_handle_to_rid.find(p_handle) == face_handle_to_rid.end()) {
		RID id = face_handle_owner.make_rid(p_handle);
		face_handle_to_rid[p_handle] = id;
		return id;
	} else {
		return face_handle_to_rid[p_handle];
	}
}

TypedArray<RID> Arrangement2D::batch_query(PackedVector2Array p_points) {
	TypedArray<RID> rids{};
	rids.resize(p_points.size());

	using QueryResult = std::pair<CGAL::Point, CGAL::PointLocation::Result_type>;
	std::list<QueryResult> query_results;

	// CGAL::locate requires a linear container.
	std::vector<CGAL::Point> query_points = vector2_to_points(p_points);
	CGAL::locate(arrangement, query_points.begin(), query_points.end(), std::back_inserter(query_results));

	for (auto &[point, obj] : query_results) {
		size_t index = std::distance(query_points.begin(), std::find(query_points.begin(), query_points.end(), point));
		auto face_handle_ptr = std::get_if<CGAL::Face_const_handle>(&obj);
		if (face_handle_ptr != nullptr) {
			rids[index] = cache_face_handle(*face_handle_ptr);
		}
	}
	return rids;
}

TypedArray<RID> Arrangement2D::polyline_query(PackedVector2Array p_polyline) {
	p_polyline = remove_consecutive_overlapping_points(p_polyline);
	if (p_polyline.size() == 0) {
		return {};
	}
	if (p_polyline.size() == 1) {
		return { query(p_polyline[0]) };
	}

	auto mono_curves = construct_x_monotone_curves(p_polyline);
	std::set<RID> ids{}; // remove duplicate
	for (auto &curve : mono_curves) {
		auto face_handles = zone_query(curve);
		for (auto handle : face_handles) {
			ids.insert(cache_face_handle(handle));
		}
	}

	TypedArray<RID> result{};
	for (RID id : ids)
		result.push_back(id);
	return result;
}

std::vector<CGAL::X_monotone_curve> Arrangement2D::construct_x_monotone_curves(PackedVector2Array p_polyline) {
	auto curve = curve_constructor(vector2_to_points(p_polyline));
	using Make_x_monotone_result = std::variant<CGAL::Point, CGAL::X_monotone_curve>;
	std::vector<Make_x_monotone_result> result_objects;
	x_monotone_maker(curve, std::back_inserter(result_objects));

	std::vector<CGAL::X_monotone_curve> result{};
	for (const auto &x_obj : result_objects) {
		const auto* mono_curve = std::get_if<CGAL::X_monotone_curve>(&x_obj);
		if (mono_curve != nullptr) {
			result.push_back(*mono_curve);
		}
	}
	return result;
}

std::vector<CGAL::Face_const_handle> Arrangement2D::zone_query(const CGAL::X_monotone_curve &p_mono_curve) {
	std::vector<CGAL::Face_const_handle> result{};
	constexpr int MAX_RESULT = 256;
	using Result = std::variant<CGAL::Arrangement::Vertex_handle, CGAL::Arrangement::Halfedge_handle, CGAL::Arrangement::Face_handle>;
	std::vector<Result> output(MAX_RESULT);
	auto begin_it = output.begin();
	auto end_it = CGAL::zone(arrangement, p_mono_curve, begin_it, point_location);

	for (auto it = begin_it; it != end_it; ++it) {
		if (auto face_handle_ptr = std::get_if<CGAL::Arrangement::Face_handle>(&*it)) {
			result.emplace_back(*face_handle_ptr);
		}
	}

	return result;
}

TypedArray<PackedVector2Array> Arrangement2D::get_polygon(RID p_id) {
	if (!p_id.is_valid() || !face_handle_owner.owns(p_id)) {
		return {};
	}
	auto handle = *face_handle_owner.get_or_null(p_id);
	return face_to_polygons(handle);
}

bool Arrangement2D::is_unbounded_face(RID p_id) {
	if (!p_id.is_valid()) {
		return false;
	}
	if (!face_handle_owner.owns(p_id)) {
		ERR_PRINT(vformat("Given RID %d is not a face.", p_id.get_id()));
		return false;
	}
	CGAL::Face_const_handle handle = *face_handle_owner.get_or_null(p_id);
	return handle->is_unbounded();
}

PackedVector2Array Arrangement2D::remove_consecutive_overlapping_points(PackedVector2Array p_polyline) {
	auto end_it = std::unique(p_polyline.begin(), p_polyline.end());
	size_t size = 0;
	auto it = p_polyline.begin();
	while (it != end_it) {
		size++;
		++it;
	}
	p_polyline.resize(size);
	return p_polyline;
}

std::vector<CGAL::Point> Arrangement2D::vector2_to_points(PackedVector2Array p_polyline) {
	std::vector<CGAL::Point> points;
	points.reserve(p_polyline.size());
	for (auto point : p_polyline) {
		points.emplace_back(point.x, point.y);
	}
	return points;
}
/// <remarks>
/// If a line is inserted(pierced) into a face but not across it, CGAL will return vertices associated with this line.
/// Need to eliminate this pattern with palindromic detection.
/// </remarks>
/// <remarks>
/// If the face has holes, they could be bestring-of-beads-shaped, remove their bridge/string lines to create multiple polygons
/// </remarks>
TypedArray<PackedVector2Array> Arrangement2D::face_to_polygons(CGAL::Face_const_handle p_face) {
	std::vector<CGAL::Arrangement::Ccb_halfedge_const_circulator> ccb_circulators{};
	if (!p_face->is_unbounded()) {
		ccb_circulators.push_back(p_face->outer_ccb());
	}
	for (auto hole_it = p_face->holes_begin(); hole_it != p_face->holes_end(); ++hole_it) {
		CGAL::Arrangement::Ccb_halfedge_const_circulator hole_ccb = *hole_it;
		ccb_circulators.push_back(hole_ccb);
	}

	TypedArray<PackedVector2Array> result{};
	for (auto ccb : ccb_circulators) {
		// Remove palindromic halfedges.
		auto curr = ccb;

		std::deque<CGAL::Halfedge_const_handle> halfedges{};
		do {
			if (!halfedges.empty() && halfedges.back() == curr->twin()) {
				halfedges.pop_back();
			} else if (!halfedges.empty() && halfedges.front() == curr->twin()) {
				halfedges.pop_front();
			} else {
				halfedges.push_back(curr);
			}
		} while (++curr != ccb);

		if (halfedges.empty()) {
			continue;
		}

		// Remove bridges in multi-bulbed gourd-like shape
		// twin_set: contains the twins of all halfedges in the CCB.
		// If twin_set contains 'he', it means he->twin() is also in the CCB, so 'he' is a bridge halfedge.
		std::set<CGAL::Halfedge_const_handle> twin_set{};
		for (auto halfedge : halfedges)
		{
			twin_set.insert(halfedge->twin());
		}

		std::vector<CGAL::Halfedge_const_handle> polygon_stack{};
		std::vector<std::vector<CGAL::Halfedge_const_handle>> polygon_groups{};
		std::vector<CGAL::Halfedge_const_handle> bridge_stack{};

		for (auto halfedge : halfedges) {
			if (twin_set.find(halfedge) != twin_set.end()) {
				if (!bridge_stack.empty() && bridge_stack.back() == halfedge->twin()) {
					polygon_groups.emplace_back();
					while (polygon_stack.back() != bridge_stack.back()) {
						polygon_groups.back().push_back(polygon_stack.back());
						polygon_stack.pop_back();
					}
					polygon_stack.pop_back(); // remove the bridge opener
					if (polygon_groups.back().empty()) {
						polygon_groups.pop_back();
					} else {
						std::reverse(polygon_groups.back().begin(), polygon_groups.back().end());
					}

					bridge_stack.pop_back();
				} else {
					polygon_stack.push_back(halfedge);
					bridge_stack.push_back(halfedge);
				}

			} else {
				polygon_stack.push_back(halfedge);
			}
		}
		polygon_groups.push_back(std::move(polygon_stack));

		// Get the points from halfedges.
		for (auto &group : polygon_groups) {
			PackedVector2Array polygon = {};
			for (auto &halfedge : group) {
				// Points in halfedge->curve() is always in x-mono increasing order, may not begin from source and end to target, so we need to reverse some of them.
				// halfedge->source()->point() is the start point of the polyline
				// halfedge->target()->point() is the end point of the polyline
				// halfedge->curve().points_begin() can be both source or target

				// Cannot use `halfedge->curve().points_end() - 1`; use --halfedge->curve().points_end().
				auto begin_it = halfedge->curve().points_begin();

				if (halfedge->source()->point() == *begin_it) {
					for (auto it = begin_it; it != --halfedge->curve().points_end(); ++it) {
						Vector2 vec{
							static_cast<float>(CGAL::to_double(it->x())),
							static_cast<float>(CGAL::to_double(it->y()))
						};
						polygon.push_back(vec);
					}
				} else {
					// reverse iteration
					for (auto it = --halfedge->curve().points_end(); it != begin_it; --it) {
						Vector2 vec{
							static_cast<float>(CGAL::to_double(it->x())),
							static_cast<float>(CGAL::to_double(it->y())) };
						polygon.push_back(vec);
					}
				}
			}

			// Remove consecutive duplicate points caused by float precision loss
			auto deduped = remove_consecutive_overlapping_points(polygon);
			if (!deduped.is_empty()) {
				result.push_back(deduped);
			}
		}
	}
	return result;
}

RID Arrangement2D::get_unbounded_face() {
	return cache_face_handle(arrangement.unbounded_face());
}
