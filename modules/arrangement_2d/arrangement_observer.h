// #pragma once

// #include "arrangement_alias.h"

// #include <CGAL/Arr_observer.h>

// class Arrangement2D;

// class ArrangementObserver : public CGAL::Arr_observer<CGAL::Arrangement> {
// public:
// 	Arrangement2D *arrangement_2d = nullptr;

// 	explicit ArrangementObserver(CGAL::Arrangement &p_arrangement)
// 			: CGAL::Arr_observer<CGAL::Arrangement>(p_arrangement) {}

// private:
// 	void before_split_face(Face_handle p_face, Halfedge_handle) override {
// 		invalidate_face(p_face);
// 	};

// 	void before_merge_face(Face_handle p_face_a, Face_handle p_face_b, Halfedge_handle) override {
// 		invalidate_face(p_face_a);
// 		invalidate_face(p_face_b);
// 	};

// 	void before_add_inner_ccb(Face_handle p_face, Halfedge_handle) override {
// 		invalidate_face(p_face);
// 	};

// 	void before_remove_inner_ccb(Face_handle p_face, Ccb_halfedge_circulator) override {
// 		invalidate_face(p_face);
// 	};

// 	void before_remove_outer_ccb(Face_handle p_face, Ccb_halfedge_circulator) override {
// 		invalidate_face(p_face);
// 	};

// 	void invalidate_face(Face_handle p_face);

//     // Following events contains face change but not override
//     // - before_(split, merge)_(outer, inner)_ccb
//     // - before_(add, remove, move)_isolated_vertex
//     // - before_remove_inner_ccb
//     // because they don't change query point visual result
//     // - before_add_outer_ccb: newly created face have any query point associate with it

//     // Grok explanation about before_merge_outer_ccb:
//     /*
//     Imagine you have an arrangement that looks like a "dumbbell" shape: two separate closed loops (like two circles or polygons) connected by a single straight line segment (this segment is the edge e). The entire structure is one connected component.
//     In this setup, the unbounded face (the infinite outer region) surrounds the whole dumbbell with a single outer CCB (connected boundary component). If you trace this outer CCB, it goes around one loop, travels along one side of the connecting segment to the other loop, goes around that loop, and then travels back along the other side of the connecting segment to close the path. It's like one big, figure-eight-ish boundary.
//     Now, if you remove that connecting edge e (the "bridge" between the two loops), the two loops become completely disconnected. The unbounded face now surrounds two separate components, each with its own outer CCB. What was one continuous boundary has now been split into two independent boundaries—one around each loop.
//     This is when before_split_outer_ccb (and its after counterpart) gets triggered: right before (and after) that single outer CCB of the unbounded face is divided into two due to the edge removal. It's a way for the observer to catch and react to the arrangement breaking into multiple disconnected pieces.
//      */
// };
