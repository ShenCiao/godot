/**************************************************************************/
/*  layer_2d.cpp                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "layer_2d.h"

#include "scene/resources/material.h"

bool Layer2D::_has_layer_material() const {
	return get_material().is_valid();
}

bool Layer2D::_needs_composite() const {
	if (composite_mode == COMPOSITE_MODE_ALWAYS) {
		return true;
	}

	if (composite_mode != COMPOSITE_MODE_AUTO) {
		return false;
	}

	if (!get_self_modulate().is_equal_approx(Color(1, 1, 1, 1))) {
		return true;
	}
	if (layer_blend_mode != LAYER_BLEND_MODE_DEFAULT || clipping_mask) {
		return true;
	}
	if (_has_layer_material()) {
		return true;
	}
	return false;
}

void Layer2D::_update_render_state() {
	const bool should_composite = _needs_composite();
	if (composite_active == should_composite) {
		if (composite_active) {
			RS::get_singleton()->canvas_item_set_canvas_group_mode(get_canvas_item(), RS::CANVAS_GROUP_MODE_TRANSPARENT, clear_margin, true, fit_margin, false);
			queue_redraw();
		}
		update_configuration_warnings();
		return;
	}

	composite_active = should_composite;
	RS::get_singleton()->canvas_item_set_is_layer(get_canvas_item(), composite_active);
	RS::get_singleton()->canvas_item_set_canvas_group_mode(
			get_canvas_item(),
			composite_active ? RS::CANVAS_GROUP_MODE_TRANSPARENT : RS::CANVAS_GROUP_MODE_DISABLED,
			clear_margin,
			true,
			fit_margin,
			false);
	queue_redraw();
	update_configuration_warnings();
}

void Layer2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_CHILD_ORDER_CHANGED) {
		_update_render_state();
	}
}

void Layer2D::set_fit_margin(real_t p_fit_margin) {
	ERR_FAIL_COND(p_fit_margin < 0.0);
	fit_margin = p_fit_margin;
	if (composite_active) {
		_update_render_state();
	}
}

real_t Layer2D::get_fit_margin() const {
	return fit_margin;
}

void Layer2D::set_clear_margin(real_t p_clear_margin) {
	ERR_FAIL_COND(p_clear_margin < 0.0);
	clear_margin = p_clear_margin;
	if (composite_active) {
		_update_render_state();
	}
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
	RS::get_singleton()->canvas_item_set_layer_blend_mode(get_canvas_item(), RS::CanvasItemLayerBlendMode(layer_blend_mode));
	_update_render_state();
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
	_update_render_state();
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
	_update_render_state();
}

bool Layer2D::is_clipping_mask() const {
	return clipping_mask;
}

void Layer2D::set_self_modulate(const Color &p_self_modulate) {
	Node2D::set_self_modulate(p_self_modulate);
	_update_render_state();
}

void Layer2D::set_material(const Ref<Material> &p_material) {
	Node2D::set_material(p_material);
	_update_render_state();
}

bool Layer2D::is_composite_active() const {
	return composite_active;
}

PackedStringArray Layer2D::get_configuration_warnings() const {
	PackedStringArray warnings = Node2D::get_configuration_warnings();
	if (composite_active && is_inside_tree()) {
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
	RS::get_singleton()->canvas_item_set_layer_blend_mode(get_canvas_item(), RS::CanvasItemLayerBlendMode(layer_blend_mode));
	RS::get_singleton()->canvas_item_set_clipping_mask(get_canvas_item(), clipping_mask);
	_update_render_state();
}

Layer2D::~Layer2D() {
	RS::get_singleton()->canvas_item_set_canvas_group_mode(get_canvas_item(), RS::CANVAS_GROUP_MODE_DISABLED);
	RS::get_singleton()->canvas_item_set_is_layer(get_canvas_item(), false);
}
