/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/

#include "register_types.h"

#include "arrangement_2d.h"
#include "centerline_vectorizer.h"

#include "core/object/class_db.h"

void initialize_arrangement_2d_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(Arrangement2D);
	GDREGISTER_CLASS(CenterlineVectorizer);
}

void uninitialize_arrangement_2d_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}
