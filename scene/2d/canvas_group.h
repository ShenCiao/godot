/**************************************************************************/
/*  canvas_group.h                                                        */
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

#pragma once

#include "scene/2d/node_2d.h"

class CanvasGroup : public Node2D {
	GDCLASS(CanvasGroup, Node2D)

public:
	enum LayerBlendMode {
		LAYER_BLEND_MODE_DEFAULT = RenderingServer::CANVAS_ITEM_LAYER_BLEND_MODE_DEFAULT,
		LAYER_BLEND_MODE_NORMAL = RenderingServer::CANVAS_ITEM_LAYER_BLEND_MODE_NORMAL,
		LAYER_BLEND_MODE_ADD = RenderingServer::CANVAS_ITEM_LAYER_BLEND_MODE_ADD,
		LAYER_BLEND_MODE_MULTIPLY = RenderingServer::CANVAS_ITEM_LAYER_BLEND_MODE_MULTIPLY,
		LAYER_BLEND_MODE_MAX = RenderingServer::CANVAS_ITEM_LAYER_BLEND_MODE_MAX,
	};

private:
	enum LayerChildConfiguration {
		LAYER_CHILD_CONFIGURATION_COMPOSITE,
		LAYER_CHILD_CONFIGURATION_LEAF,
		LAYER_CHILD_CONFIGURATION_MIXED,
	};
	struct LayerChildAnalysis {
		LayerChildConfiguration configuration = LAYER_CHILD_CONFIGURATION_COMPOSITE;
		bool has_layer_children = false;
		bool has_top_level_layer_child = false;
		bool invalid_layer_order = false;
	};

	real_t fit_margin = 10.0;
	real_t clear_margin = 10.0;
	bool use_mipmaps = false;
	LayerBlendMode layer_blend_mode = LAYER_BLEND_MODE_DEFAULT;
	bool clipping_mask = false;
	bool mixed_layer_children_error_emitted = false;
	bool top_level_layer_child_error_emitted = false;

	LayerChildAnalysis _get_layer_child_analysis() const;
	void _validate_layer_children();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_fit_margin(real_t p_fit_margin);
	real_t get_fit_margin() const;

	void set_clear_margin(real_t p_clear_margin);
	real_t get_clear_margin() const;

	void set_use_mipmaps(bool p_use_mipmaps);
	bool is_using_mipmaps() const;
	void set_layer_blend_mode(LayerBlendMode p_blend_mode);
	LayerBlendMode get_layer_blend_mode() const;
	void set_clipping_mask(bool p_enabled);
	bool is_clipping_mask() const;

	virtual PackedStringArray get_configuration_warnings() const override;

	CanvasGroup();
	~CanvasGroup();
};

VARIANT_ENUM_CAST(CanvasGroup::LayerBlendMode);
