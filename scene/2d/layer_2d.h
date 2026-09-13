/**************************************************************************/
/*  layer_2d.h                                                            */
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
/* permit persons to whom the Software is furnished to do so, subject to   */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY    */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,    */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "scene/2d/node_2d.h"

class Layer2D : public Node2D {
	GDCLASS(Layer2D, Node2D)

public:
	enum LayerBlendMode {
		LAYER_BLEND_MODE_DEFAULT = RenderingServer::CANVAS_ITEM_LAYER_BLEND_MODE_DEFAULT,
		LAYER_BLEND_MODE_NORMAL = RenderingServer::CANVAS_ITEM_LAYER_BLEND_MODE_NORMAL,
		LAYER_BLEND_MODE_ADD = RenderingServer::CANVAS_ITEM_LAYER_BLEND_MODE_ADD,
		LAYER_BLEND_MODE_MULTIPLY = RenderingServer::CANVAS_ITEM_LAYER_BLEND_MODE_MULTIPLY,
		LAYER_BLEND_MODE_MAX = RenderingServer::CANVAS_ITEM_LAYER_BLEND_MODE_MAX,
	};

	enum CompositeMode {
		COMPOSITE_MODE_AUTO,
		COMPOSITE_MODE_ALWAYS,
		COMPOSITE_MODE_MAX,
	};

private:
	real_t fit_margin = 10.0;
	real_t clear_margin = 10.0;
	LayerBlendMode layer_blend_mode = LAYER_BLEND_MODE_DEFAULT;
	CompositeMode composite_mode = COMPOSITE_MODE_AUTO;
	bool clipping_mask = false;
	void _update_layer_group();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_fit_margin(real_t p_fit_margin);
	real_t get_fit_margin() const;

	void set_clear_margin(real_t p_clear_margin);
	real_t get_clear_margin() const;

	void set_layer_blend_mode(LayerBlendMode p_blend_mode);
	LayerBlendMode get_layer_blend_mode() const;

	void set_composite_mode(CompositeMode p_mode);
	CompositeMode get_composite_mode() const;

	void set_clipping_mask(bool p_enabled);
	bool is_clipping_mask() const;

	bool is_composite_active() const;

	virtual PackedStringArray get_configuration_warnings() const override;

	Layer2D();
	~Layer2D();
};

VARIANT_ENUM_CAST(Layer2D::LayerBlendMode);
VARIANT_ENUM_CAST(Layer2D::CompositeMode);
