#include "arrangement_observer.h"

#include "arrangement_2d.h"

void ArrangementObserver::invalidate_face(Face_handle p_face) {
	arrangement_2d->invalidate_face(p_face);
}
