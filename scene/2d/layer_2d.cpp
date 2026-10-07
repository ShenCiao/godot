/**************************************************************************/
/*  layer_2d.cpp                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "layer_2d.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

void Layer2D::_update_layer_group() {
	RS::get_singleton()->canvas_item_set_layer_group(get_canvas_item(), true, composite_mode == COMPOSITE_MODE_ALWAYS, fit_margin, clear_margin);
}

void Layer2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_CHILD_ORDER_CHANGED) {
		update_configuration_warnings();
	}
}

void Layer2D::set_fit_margin(real_t p_fit_margin) {
	ERR_FAIL_COND(p_fit_margin < 0.0);
	fit_margin = p_fit_margin;
	_update_layer_group();
}

real_t Layer2D::get_fit_margin() const {
	return fit_margin;
}

void Layer2D::set_clear_margin(real_t p_clear_margin) {
	ERR_FAIL_COND(p_clear_margin < 0.0);
	clear_margin = p_clear_margin;
	_update_layer_group();
}

real_t Layer2D::get_clear_margin() const {
	return clear_margin;
}

void Layer2D::set_layer_blend_mode(LayerBlendMode p_blend_mode) {
	ERR_FAIL_INDEX(p_blend_mode, LAYER_BLEND_MODE_MAX);
	if (layer_blend_mode == p_blend_mode) {
		return;
	}
	layer_blend_mode = p_blend_mode;
	RS::get_singleton()->canvas_item_set_layer_blend_mode(get_canvas_item(), RSE::CanvasItemLayerBlendMode(layer_blend_mode));
}

Layer2D::LayerBlendMode Layer2D::get_layer_blend_mode() const {
	return layer_blend_mode;
}

void Layer2D::set_composite_mode(CompositeMode p_mode) {
	ERR_FAIL_INDEX(p_mode, COMPOSITE_MODE_MAX);
	if (composite_mode == p_mode) {
		return;
	}
	composite_mode = p_mode;
	_update_layer_group();
}

Layer2D::CompositeMode Layer2D::get_composite_mode() const {
	return composite_mode;
}

void Layer2D::set_clipping_mask(bool p_enabled) {
	if (clipping_mask == p_enabled) {
		return;
	}
	clipping_mask = p_enabled;
	RS::get_singleton()->canvas_item_set_clipping_mask(get_canvas_item(), clipping_mask);
}

bool Layer2D::is_clipping_mask() const {
	return clipping_mask;
}

bool Layer2D::is_composite_active() const {
	return RS::get_singleton()->canvas_item_is_layer_composite_active(get_canvas_item());
}

PackedStringArray Layer2D::get_configuration_warnings() const {
	PackedStringArray warnings = Node2D::get_configuration_warnings();
	if (is_inside_tree()) {
		Node *n = get_parent();
		while (n) {
			CanvasItem *as_canvas_item = Object::cast_to<CanvasItem>(n);
			if (as_canvas_item && as_canvas_item->get_clip_children_mode() != CLIP_CHILDREN_DISABLED) {
				warnings.push_back(vformat(RTR("Ancestor \"%s\" clips its children, so this composited Layer2D will not function properly."), as_canvas_item->get_name()));
				break;
			}
			n = n->get_parent();
		}
	}
	return warnings;
}

void Layer2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_fit_margin", "fit_margin"), &Layer2D::set_fit_margin);
	ClassDB::bind_method(D_METHOD("get_fit_margin"), &Layer2D::get_fit_margin);
	ClassDB::bind_method(D_METHOD("set_clear_margin", "clear_margin"), &Layer2D::set_clear_margin);
	ClassDB::bind_method(D_METHOD("get_clear_margin"), &Layer2D::get_clear_margin);
	ClassDB::bind_method(D_METHOD("set_layer_blend_mode", "blend_mode"), &Layer2D::set_layer_blend_mode);
	ClassDB::bind_method(D_METHOD("get_layer_blend_mode"), &Layer2D::get_layer_blend_mode);
	ClassDB::bind_method(D_METHOD("set_composite_mode", "mode"), &Layer2D::set_composite_mode);
	ClassDB::bind_method(D_METHOD("get_composite_mode"), &Layer2D::get_composite_mode);
	ClassDB::bind_method(D_METHOD("set_clipping_mask", "enabled"), &Layer2D::set_clipping_mask);
	ClassDB::bind_method(D_METHOD("is_clipping_mask"), &Layer2D::is_clipping_mask);
	ClassDB::bind_method(D_METHOD("is_composite_active"), &Layer2D::is_composite_active);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "layer_blend_mode", PROPERTY_HINT_ENUM, "Default,Normal,Add,Multiply"), "set_layer_blend_mode", "get_layer_blend_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "composite_mode", PROPERTY_HINT_ENUM, "Auto,Always"), "set_composite_mode", "get_composite_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "clipping_mask"), "set_clipping_mask", "is_clipping_mask");
	ADD_GROUP("Tweaks", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fit_margin", PROPERTY_HINT_RANGE, "0,1024,1.0,or_greater,suffix:px"), "set_fit_margin", "get_fit_margin");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "clear_margin", PROPERTY_HINT_RANGE, "0,1024,1.0,or_greater,suffix:px"), "set_clear_margin", "get_clear_margin");

	BIND_ENUM_CONSTANT(LAYER_BLEND_MODE_DEFAULT);
	BIND_ENUM_CONSTANT(LAYER_BLEND_MODE_NORMAL);
	BIND_ENUM_CONSTANT(LAYER_BLEND_MODE_ADD);
	BIND_ENUM_CONSTANT(LAYER_BLEND_MODE_MULTIPLY);
	BIND_ENUM_CONSTANT(LAYER_BLEND_MODE_MAX);
	BIND_ENUM_CONSTANT(COMPOSITE_MODE_AUTO);
	BIND_ENUM_CONSTANT(COMPOSITE_MODE_ALWAYS);
	BIND_ENUM_CONSTANT(COMPOSITE_MODE_MAX);
}

Layer2D::Layer2D() {
	RS::get_singleton()->canvas_item_set_is_layer(get_canvas_item(), true);
	RS::get_singleton()->canvas_item_set_layer_blend_mode(get_canvas_item(), RSE::CanvasItemLayerBlendMode(layer_blend_mode));
	RS::get_singleton()->canvas_item_set_clipping_mask(get_canvas_item(), clipping_mask);
	_update_layer_group();
}

Layer2D::~Layer2D() {
	RS::get_singleton()->canvas_item_set_layer_group(get_canvas_item(), false, false, fit_margin, clear_margin);
	RS::get_singleton()->canvas_item_set_is_layer(get_canvas_item(), false);
}
