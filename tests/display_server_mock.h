/**************************************************************************/
/*  display_server_mock.h                                                 */
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

#include "servers/display/display_server_headless.h"

// Specialized DisplayServer for unittests based on DisplayServerHeadless, that
// additionally supports things like mouse enter/exit events and clipboard.
class DisplayServerMock : public DisplayServerHeadless {
	GDSOFTCLASS(DisplayServerMock, DisplayServerHeadless);

private:
	friend class DisplayServer;

	Point2i mouse_position = Point2i(-1, -1); // Outside of Window.
	DisplayServerEnums::CursorShape cursor_shape = DisplayServerEnums::CursorShape::CURSOR_ARROW;
	bool window_over = false;
	Callable event_callback;
	Callable sub_window_event_callback;
	Callable rect_changed_callback;

	bool window_geometry_enabled = false;
	Vector<Rect2i> screen_rects;
	int main_window_screen = 0;
	Rect2i sub_window_rect;
	Rect2i last_created_sub_window_rect;

	String clipboard_text;
	String primary_clipboard_text;

	static Vector<String> get_rendering_drivers_func();
	static DisplayServer *create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error);

	void _set_mouse_position(const Point2i &p_position);
	void _set_window_over(bool p_over);
	void _send_window_event(DisplayServerEnums::WindowEvent p_event, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID);

public:
	void configure_window_geometry(const Vector<Rect2i> &p_screen_rects, int p_main_window_screen = 0) {
		screen_rects = p_screen_rects;
		main_window_screen = p_main_window_screen;
		window_geometry_enabled = true;
	}

	void reset_window_geometry() {
		window_geometry_enabled = false;
		screen_rects.clear();
		main_window_screen = 0;
		sub_window_rect = Rect2i();
		last_created_sub_window_rect = Rect2i();
		sub_window_event_callback = Callable();
		rect_changed_callback = Callable();
	}

	Rect2i get_last_created_sub_window_rect() const {
		return last_created_sub_window_rect;
	}

	void simulate_window_rect_changed(const Rect2i &p_rect) {
		sub_window_rect = p_rect;
		Variant rect = p_rect;
		rect_changed_callback.call(rect);
	}

	bool has_feature(DisplayServerEnums::Feature p_feature) const override;

	String get_name() const override { return "mock"; }

	// You can simulate DisplayServer-events by calling this function.
	// The events will be delivered to Godot's Input-system.
	// Mouse-events (Button & Motion) will additionally update the DisplayServer's mouse position.
	// For Mouse motion events, the `relative`-property is set based on the distance to the previous mouse position.
	void simulate_event(Ref<InputEvent> p_event);

	// Returns the current cursor shape.
	DisplayServerEnums::CursorShape get_cursor_shape() {
		return cursor_shape;
	}

	virtual Point2i mouse_get_position() const override { return mouse_position; }

	virtual int get_screen_count() const override {
		return window_geometry_enabled ? screen_rects.size() : DisplayServerHeadless::get_screen_count();
	}

	virtual Point2i screen_get_position(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override {
		if (!window_geometry_enabled) {
			return DisplayServerHeadless::screen_get_position(p_screen);
		}
		return screen_rects[_get_screen_index(p_screen)].position;
	}

	virtual Size2i screen_get_size(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override {
		if (!window_geometry_enabled) {
			return DisplayServerHeadless::screen_get_size(p_screen);
		}
		return screen_rects[_get_screen_index(p_screen)].size;
	}

	virtual Rect2i screen_get_usable_rect(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override {
		if (!window_geometry_enabled) {
			return DisplayServerHeadless::screen_get_usable_rect(p_screen);
		}
		return screen_rects[_get_screen_index(p_screen)];
	}

	virtual void clipboard_set(const String &p_text) override { clipboard_text = p_text; }
	virtual String clipboard_get() const override { return clipboard_text; }
	virtual void clipboard_set_primary(const String &p_text) override { primary_clipboard_text = p_text; }
	virtual String clipboard_get_primary() const override { return primary_clipboard_text; }

	virtual Size2i window_get_size(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override {
		return Size2i(1920, 1080);
	}

	virtual DisplayServerEnums::WindowID create_sub_window(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Rect2i &p_rect = Rect2i(), bool p_exclusive = false, DisplayServerEnums::WindowID p_transient_parent = DisplayServerEnums::INVALID_WINDOW_ID) override {
		if (!window_geometry_enabled) {
			return DisplayServerHeadless::create_sub_window(p_mode, p_vsync_mode, p_flags, p_rect, p_exclusive, p_transient_parent);
		}
		sub_window_rect = p_rect;
		last_created_sub_window_rect = p_rect;
		return DisplayServerEnums::MAIN_WINDOW_ID + 1;
	}

	virtual void delete_sub_window(DisplayServerEnums::WindowID p_id) override {
		if (!window_geometry_enabled) {
			DisplayServerHeadless::delete_sub_window(p_id);
			return;
		}
		sub_window_event_callback = Callable();
		rect_changed_callback = Callable();
	}

	virtual int window_get_current_screen(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override {
		if (!window_geometry_enabled) {
			return DisplayServerHeadless::window_get_current_screen(p_window);
		}
		return p_window == DisplayServerEnums::MAIN_WINDOW_ID ? main_window_screen : get_screen_from_rect(sub_window_rect);
	}

	virtual void window_set_rect_changed_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {
		if (!window_geometry_enabled) {
			DisplayServerHeadless::window_set_rect_changed_callback(p_callable, p_window);
			return;
		}
		rect_changed_callback = p_callable;
	}

	virtual void cursor_set_shape(DisplayServerEnums::CursorShape p_shape) override {
		cursor_shape = p_shape;
	}

	virtual void window_set_window_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {
		if (p_window == DisplayServerEnums::MAIN_WINDOW_ID) {
			event_callback = p_callable;
		} else {
			sub_window_event_callback = p_callable;
		}
	}

	static void register_mock_driver() {
		register_create_function("mock", create_func, get_rendering_drivers_func);
	}
};

// Utility macros to send an event actions to a given object
// Requires Message Queue and InputMap to be setup.
// SEND_GUI_ACTION    - takes an input map key. e.g SEND_GUI_ACTION("ui_text_newline").
// SEND_GUI_KEY_EVENT - takes a keycode set.   e.g SEND_GUI_KEY_EVENT(Key::A | KeyModifierMask::META).
// SEND_GUI_KEY_UP_EVENT - takes a keycode set.   e.g SEND_GUI_KEY_UP_EVENT(Key::A | KeyModifierMask::META).
// SEND_GUI_MOUSE_BUTTON_EVENT - takes a position, mouse button, mouse mask and modifiers e.g SEND_GUI_MOUSE_BUTTON_EVENT(Vector2(50, 50), MOUSE_BUTTON_NONE, MOUSE_BUTTON_NONE, Key::None);
// SEND_GUI_MOUSE_BUTTON_RELEASED_EVENT - takes a position, mouse button, mouse mask and modifiers e.g SEND_GUI_MOUSE_BUTTON_RELEASED_EVENT(Vector2(50, 50), MOUSE_BUTTON_NONE, MOUSE_BUTTON_NONE, Key::None);
// SEND_GUI_MOUSE_MOTION_EVENT - takes a position, mouse mask and modifiers e.g SEND_GUI_MOUSE_MOTION_EVENT(Vector2(50, 50), MouseButtonMask::LEFT, KeyModifierMask::META);
// SEND_GUI_DOUBLE_CLICK - takes a position and modifiers. e.g SEND_GUI_DOUBLE_CLICK(Vector2(50, 50), KeyModifierMask::META);

#define _SEND_DISPLAYSERVER_EVENT(m_event) ((DisplayServerMock *)(DisplayServer::get_singleton()))->simulate_event(m_event);

#define SEND_GUI_ACTION(m_action) \
	{ \
		const List<Ref<InputEvent>> *events = InputMap::get_singleton()->action_get_events(m_action); \
		const List<Ref<InputEvent>>::Element *first_event = events->front(); \
		Ref<InputEventKey> event = first_event->get()->duplicate(); \
		event->set_pressed(true); \
		_SEND_DISPLAYSERVER_EVENT(event); \
		MessageQueue::get_singleton()->flush(); \
	}

#define SEND_GUI_KEY_EVENT(m_input) \
	{ \
		Ref<InputEventKey> event = InputEventKey::create_reference(m_input); \
		event->set_pressed(true); \
		_SEND_DISPLAYSERVER_EVENT(event); \
		MessageQueue::get_singleton()->flush(); \
	}

#define SEND_GUI_KEY_UP_EVENT(m_input) \
	{ \
		Ref<InputEventKey> event = InputEventKey::create_reference(m_input); \
		event->set_pressed(false); \
		_SEND_DISPLAYSERVER_EVENT(event); \
		MessageQueue::get_singleton()->flush(); \
	}

#define _UPDATE_EVENT_MODIFIERS(m_event, m_modifiers) \
	m_event->set_shift_pressed(((m_modifiers) & KeyModifierMask::SHIFT) != Key::NONE); \
	m_event->set_alt_pressed(((m_modifiers) & KeyModifierMask::ALT) != Key::NONE); \
	m_event->set_ctrl_pressed(((m_modifiers) & KeyModifierMask::CTRL) != Key::NONE); \
	m_event->set_meta_pressed(((m_modifiers) & KeyModifierMask::META) != Key::NONE);

#define _CREATE_GUI_MOUSE_EVENT(m_screen_pos, m_input, m_mask, m_modifiers) \
	Ref<InputEventMouseButton> event; \
	event.instantiate(); \
	event->set_position(m_screen_pos); \
	event->set_button_index(m_input); \
	event->set_button_mask(m_mask); \
	event->set_factor(1); \
	_UPDATE_EVENT_MODIFIERS(event, m_modifiers); \
	event->set_pressed(true);

#define _CREATE_GUI_TOUCH_EVENT(m_screen_pos, m_pressed, m_double) \
	Ref<InputEventScreenTouch> event; \
	event.instantiate(); \
	event->set_position(m_screen_pos); \
	event->set_pressed(m_pressed); \
	event->set_double_tap(m_double);

#define SEND_GUI_MOUSE_BUTTON_EVENT(m_screen_pos, m_input, m_mask, m_modifiers) \
	{ \
		_CREATE_GUI_MOUSE_EVENT(m_screen_pos, m_input, m_mask, m_modifiers); \
		_SEND_DISPLAYSERVER_EVENT(event); \
		MessageQueue::get_singleton()->flush(); \
	}

#define SEND_GUI_MOUSE_BUTTON_RELEASED_EVENT(m_screen_pos, m_input, m_mask, m_modifiers) \
	{ \
		_CREATE_GUI_MOUSE_EVENT(m_screen_pos, m_input, m_mask, m_modifiers); \
		event->set_pressed(false); \
		_SEND_DISPLAYSERVER_EVENT(event); \
		MessageQueue::get_singleton()->flush(); \
	}

#define SEND_GUI_DOUBLE_CLICK(m_screen_pos, m_modifiers) \
	{ \
		_CREATE_GUI_MOUSE_EVENT(m_screen_pos, MouseButton::LEFT, MouseButtonMask::NONE, m_modifiers); \
		event->set_double_click(true); \
		_SEND_DISPLAYSERVER_EVENT(event); \
		MessageQueue::get_singleton()->flush(); \
	}

// We toggle _print_error_enabled to prevent display server not supported warnings.
#define SEND_GUI_MOUSE_MOTION_EVENT(m_screen_pos, m_mask, m_modifiers) \
	{ \
		bool errors_enabled = CoreGlobals::print_error_enabled; \
		CoreGlobals::print_error_enabled = false; \
		Ref<InputEventMouseMotion> event; \
		event.instantiate(); \
		event->set_position(m_screen_pos); \
		event->set_button_mask(m_mask); \
		_UPDATE_EVENT_MODIFIERS(event, m_modifiers); \
		_SEND_DISPLAYSERVER_EVENT(event); \
		MessageQueue::get_singleton()->flush(); \
		CoreGlobals::print_error_enabled = errors_enabled; \
	}

#define SEND_GUI_TOUCH_EVENT(m_screen_pos, m_pressed, m_double) \
	{ \
		_CREATE_GUI_TOUCH_EVENT(m_screen_pos, m_pressed, m_double) \
		_SEND_DISPLAYSERVER_EVENT(event); \
		MessageQueue::get_singleton()->flush(); \
	}
