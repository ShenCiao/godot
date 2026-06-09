#pragma once

#include "arrangement_alias.h"

#include <CGAL/Arr_observer.h>

class Arrangement2D;

class ArrangementObserver : public CGAL::Arr_observer<CGAL::Arrangement> {
public:
	Arrangement2D *arrangement_2d = nullptr;

	explicit ArrangementObserver(CGAL::Arrangement &p_arrangement)
			: CGAL::Arr_observer<CGAL::Arrangement>(p_arrangement) {}

private:
	void before_split_face(Face_handle p_face, Halfedge_handle) override {
		invalidate_face(p_face);
	}

	void before_merge_face(Face_handle p_face_a, Face_handle p_face_b, Halfedge_handle) override {
		invalidate_face(p_face_a);
		invalidate_face(p_face_b);
	}

	void before_add_inner_ccb(Face_handle p_face, Halfedge_handle) override {
		invalidate_face(p_face);
	}

	void before_remove_inner_ccb(Face_handle p_face, Ccb_halfedge_circulator) override {
		invalidate_face(p_face);
	}

	void before_remove_outer_ccb(Face_handle p_face, Ccb_halfedge_circulator) override {
		invalidate_face(p_face);
	}

	void invalidate_face(Face_handle p_face);
};
