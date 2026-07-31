/**************************************************************************/
/*  canvas_group.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "canvas_group.h"

#include "scene/2d/sprite_2d.h"

CanvasGroup::LayerChildAnalysis CanvasGroup::_get_layer_child_analysis() const {
	LayerChildAnalysis analysis;
	bool has_content_children = false;
	bool invalid_layer_order = is_y_sort_enabled();

	for (int i = 0; i < get_child_count(); i++) {
		Node2D *child = Object::cast_to<Node2D>(get_child(i));
		if (child == nullptr) {
			continue;
		}

		if (Object::cast_to<Sprite2D>(child) != nullptr || Object::cast_to<CanvasGroup>(child) != nullptr) {
			analysis.has_layer_children = true;
			analysis.has_top_level_layer_child |= child->is_set_as_top_level();
			invalid_layer_order |= child->get_z_index() != 0 || !child->is_z_relative() || child->is_draw_behind_parent_enabled() || child->is_set_as_top_level();
		} else {
			has_content_children = true;
		}
	}

	if (analysis.has_layer_children && has_content_children) {
		analysis.configuration = LAYER_CHILD_CONFIGURATION_MIXED;
	} else if (has_content_children) {
		analysis.configuration = LAYER_CHILD_CONFIGURATION_LEAF;
	}
	analysis.invalid_layer_order = analysis.has_layer_children && invalid_layer_order;
	return analysis;
}

void CanvasGroup::_validate_layer_children() {
	const LayerChildAnalysis analysis = _get_layer_child_analysis();
	const bool mixed = analysis.configuration == LAYER_CHILD_CONFIGURATION_MIXED;
	if (mixed && !mixed_layer_children_error_emitted) {
		ERR_PRINT(vformat("CanvasGroup '%s' mixes Sprite2D or CanvasGroup Layer children with ordinary Node2D Layer Content. Layer rendering for this subtree is undefined.", get_name()));
	}
	mixed_layer_children_error_emitted = mixed;

	const bool has_top_level_layer_child = analysis.configuration == LAYER_CHILD_CONFIGURATION_COMPOSITE && analysis.has_top_level_layer_child;

	if (has_top_level_layer_child && !top_level_layer_child_error_emitted) {
		ERR_PRINT(vformat("CanvasGroup '%s' has a child Layer with top_level enabled. Layer rendering for this subtree is undefined.", get_name()));
	}
	top_level_layer_child_error_emitted = has_top_level_layer_child;
}

void CanvasGroup::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_CHILD_ORDER_CHANGED) {
		_validate_layer_children();
		update_configuration_warnings();
	}
}

void CanvasGroup::set_fit_margin(real_t p_fit_margin) {
	ERR_FAIL_COND(p_fit_margin < 0.0);

	fit_margin = p_fit_margin;
	RS::get_singleton()->canvas_item_set_canvas_group_mode(get_canvas_item(), RS::CANVAS_GROUP_MODE_TRANSPARENT, clear_margin, true, fit_margin, use_mipmaps);

	queue_redraw();
}

real_t CanvasGroup::get_fit_margin() const {
	return fit_margin;
}

void CanvasGroup::set_clear_margin(real_t p_clear_margin) {
	ERR_FAIL_COND(p_clear_margin < 0.0);

	clear_margin = p_clear_margin;
	RS::get_singleton()->canvas_item_set_canvas_group_mode(get_canvas_item(), RS::CANVAS_GROUP_MODE_TRANSPARENT, clear_margin, true, fit_margin, use_mipmaps);

	queue_redraw();
}

real_t CanvasGroup::get_clear_margin() const {
	return clear_margin;
}

void CanvasGroup::set_use_mipmaps(bool p_use_mipmaps) {
	use_mipmaps = p_use_mipmaps;
	RS::get_singleton()->canvas_item_set_canvas_group_mode(get_canvas_item(), RS::CANVAS_GROUP_MODE_TRANSPARENT, clear_margin, true, fit_margin, use_mipmaps);
}
bool CanvasGroup::is_using_mipmaps() const {
	return use_mipmaps;
}

void CanvasGroup::set_layer_blend_mode(LayerBlendMode p_blend_mode) {
	ERR_FAIL_INDEX(p_blend_mode, LAYER_BLEND_MODE_MAX);
	if (layer_blend_mode == p_blend_mode) {
		return;
	}
	layer_blend_mode = p_blend_mode;
	RS::get_singleton()->canvas_item_set_layer_blend_mode(get_canvas_item(), RS::CanvasItemLayerBlendMode(layer_blend_mode));
}

CanvasGroup::LayerBlendMode CanvasGroup::get_layer_blend_mode() const {
	return layer_blend_mode;
}

void CanvasGroup::set_clipping_mask(bool p_enabled) {
	if (clipping_mask == p_enabled) {
		return;
	}
	clipping_mask = p_enabled;
	RS::get_singleton()->canvas_item_set_clipping_mask(get_canvas_item(), clipping_mask);
}

bool CanvasGroup::is_clipping_mask() const {
	return clipping_mask;
}

PackedStringArray CanvasGroup::get_configuration_warnings() const {
	PackedStringArray warnings = Node2D::get_configuration_warnings();
	const LayerChildAnalysis analysis = _get_layer_child_analysis();

	if (analysis.configuration == LAYER_CHILD_CONFIGURATION_MIXED) {
		warnings.push_back(RTR("This CanvasGroup mixes Sprite2D or CanvasGroup Layer children with ordinary Node2D Layer Content. Layer rendering for this subtree is undefined."));
	} else if (analysis.configuration == LAYER_CHILD_CONFIGURATION_COMPOSITE && analysis.invalid_layer_order) {
		warnings.push_back(RTR("Child Layers require z_index = 0, z_as_relative = true, show_behind_parent = false, and top_level = false. A Composite CanvasGroup must also have y_sort_enabled = false."));
	}

	if (is_inside_tree()) {
		Node *n = get_parent();
		while (n) {
			CanvasItem *as_canvas_item = Object::cast_to<CanvasItem>(n);
			if (as_canvas_item && as_canvas_item->get_clip_children_mode() != CLIP_CHILDREN_DISABLED) {
				warnings.push_back(vformat(RTR("Ancestor \"%s\" clips its children, so this CanvasGroup will not function properly."), as_canvas_item->get_name()));
				break;
			}
			n = n->get_parent();
		}
	}

	return warnings;
}

void CanvasGroup::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_fit_margin", "fit_margin"), &CanvasGroup::set_fit_margin);
	ClassDB::bind_method(D_METHOD("get_fit_margin"), &CanvasGroup::get_fit_margin);

	ClassDB::bind_method(D_METHOD("set_clear_margin", "clear_margin"), &CanvasGroup::set_clear_margin);
	ClassDB::bind_method(D_METHOD("get_clear_margin"), &CanvasGroup::get_clear_margin);

	ClassDB::bind_method(D_METHOD("set_use_mipmaps", "use_mipmaps"), &CanvasGroup::set_use_mipmaps);
	ClassDB::bind_method(D_METHOD("is_using_mipmaps"), &CanvasGroup::is_using_mipmaps);
	ClassDB::bind_method(D_METHOD("set_layer_blend_mode", "blend_mode"), &CanvasGroup::set_layer_blend_mode);
	ClassDB::bind_method(D_METHOD("get_layer_blend_mode"), &CanvasGroup::get_layer_blend_mode);
	ClassDB::bind_method(D_METHOD("set_clipping_mask", "enabled"), &CanvasGroup::set_clipping_mask);
	ClassDB::bind_method(D_METHOD("is_clipping_mask"), &CanvasGroup::is_clipping_mask);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "layer_blend_mode", PROPERTY_HINT_ENUM, "Default,Normal,Add,Multiply"), "set_layer_blend_mode", "get_layer_blend_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "clipping_mask"), "set_clipping_mask", "is_clipping_mask");

	ADD_GROUP("Tweaks", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fit_margin", PROPERTY_HINT_RANGE, "0,1024,1.0,or_greater,suffix:px"), "set_fit_margin", "get_fit_margin");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "clear_margin", PROPERTY_HINT_RANGE, "0,1024,1.0,or_greater,suffix:px"), "set_clear_margin", "get_clear_margin");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_mipmaps"), "set_use_mipmaps", "is_using_mipmaps");

	BIND_ENUM_CONSTANT(LAYER_BLEND_MODE_DEFAULT);
	BIND_ENUM_CONSTANT(LAYER_BLEND_MODE_NORMAL);
	BIND_ENUM_CONSTANT(LAYER_BLEND_MODE_ADD);
	BIND_ENUM_CONSTANT(LAYER_BLEND_MODE_MULTIPLY);
	BIND_ENUM_CONSTANT(LAYER_BLEND_MODE_MAX);
}

CanvasGroup::CanvasGroup() {
	RS::get_singleton()->canvas_item_set_is_layer(get_canvas_item(), true);
	RS::get_singleton()->canvas_item_set_layer_blend_mode(get_canvas_item(), RS::CanvasItemLayerBlendMode(layer_blend_mode));
	RS::get_singleton()->canvas_item_set_clipping_mask(get_canvas_item(), clipping_mask);
	set_fit_margin(10.0); //sets things
}
CanvasGroup::~CanvasGroup() {
	RS::get_singleton()->canvas_item_set_canvas_group_mode(get_canvas_item(), RS::CANVAS_GROUP_MODE_DISABLED);
}
