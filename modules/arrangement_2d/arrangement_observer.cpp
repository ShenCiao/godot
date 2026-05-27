//
// Created by Ciao on 2026/1/21.
//

#include "arrangement_observer.h"

#include "arrangement_2d.h"

void ArrangementObserver::invalidate_face(Face_handle p_face) {
	if (arrangement_2d->face_handle_to_rid.find(p_face) != arrangement_2d->face_handle_to_rid.end()) {
		RID id = arrangement_2d->face_handle_to_rid[p_face];
		arrangement_2d->invalid_face_rids.append(id);

		arrangement_2d->face_handle_to_rid.erase(p_face);
		arrangement_2d->face_handle_owner.free(id);
	}
}
