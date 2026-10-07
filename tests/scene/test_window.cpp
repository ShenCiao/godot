/**************************************************************************/
/*  test_window.cpp                                                       */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_window)

#include "core/input/input_map.h" // IWYU pragma: keep // Used by `SEND_GUI_MOUSE_MOTION_EVENT` macro.
#include "scene/gui/control.h"
#include "scene/gui/popup.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/display_server_mock.h"

namespace TestWindow {

class NotificationControlWindow : public Control {
	GDCLASS(NotificationControlWindow, Control);

protected:
	void _notification(int p_what) {
		switch (p_what) {
			case NOTIFICATION_MOUSE_ENTER: {
				mouse_over = true;
			} break;

			case NOTIFICATION_MOUSE_EXIT: {
				mouse_over = false;
			} break;
		}
	}

public:
	bool mouse_over = false;
};

TEST_CASE("[SceneTree][Window]") {
	Window *root = SceneTree::get_singleton()->get_root();

	SUBCASE("Control-mouse-over within Window-black bars should not happen") {
		Window *w = memnew(Window);
		root->add_child(w);
		w->set_size(Size2i(400, 200));
		w->set_position(Size2i(0, 0));
		w->set_content_scale_size(Size2i(200, 200));
		w->set_content_scale_mode(Window::CONTENT_SCALE_MODE_CANVAS_ITEMS);
		w->set_content_scale_aspect(Window::CONTENT_SCALE_ASPECT_KEEP);
		NotificationControlWindow *c = memnew(NotificationControlWindow);
		w->add_child(c);
		c->set_size(Size2i(100, 100));
		c->set_position(Size2i(-50, -50));

		CHECK_FALSE(c->mouse_over);
		SEND_GUI_MOUSE_MOTION_EVENT(Point2i(110, 10), MouseButtonMask::NONE, Key::NONE);
		CHECK(c->mouse_over);
		SEND_GUI_MOUSE_MOTION_EVENT(Point2i(90, 10), MouseButtonMask::NONE, Key::NONE);
		CHECK_FALSE(c->mouse_over); // GH-80011

		/* TODO:
		SEND_GUI_MOUSE_BUTTON_EVENT(Point2i(90, 10), MouseButton::LEFT, MouseButtonMask::LEFT, Key::NONE);
		SEND_GUI_MOUSE_BUTTON_RELEASED_EVENT(Point2i(90, 10), MouseButton::LEFT, MouseButtonMask::NONE, Key::NONE);
		CHECK(Control was not pressed);
		*/

		memdelete(c);
		memdelete(w);
	}
}

TEST_CASE("[SceneTree][Window] Initial position is only applied to the first popup") {
	Window *root = SceneTree::get_singleton()->get_root();
	DisplayServerMock *display_server = static_cast<DisplayServerMock *>(DisplayServer::get_singleton());
	const bool was_embedding_subwindows = root->is_embedding_subwindows();
	const Point2i previous_mouse_position = display_server->mouse_get_position();

	Vector<Rect2i> screen_rects;
	screen_rects.push_back(Rect2i(0, 0, 1920, 1080));
	screen_rects.push_back(Rect2i(1920, -120, 2560, 1440));
	display_server->configure_window_geometry(screen_rects);
	root->set_embedding_subwindows(false);

	Ref<InputEventMouseMotion> mouse_motion;
	mouse_motion.instantiate();
	mouse_motion->set_position(Point2i(2000, 0));
	display_server->simulate_event(mouse_motion);

	Window *window = memnew(Window);
	window->set_visible(false);
	window->set_size(Size2i(400, 300));
	window->set_initial_position(Window::WINDOW_INITIAL_POSITION_CENTER_SCREEN_WITH_MOUSE_FOCUS);
	root->add_child(window);

	window->popup();
	CHECK_EQ(display_server->get_last_created_sub_window_rect(), Rect2i(3000, 450, 400, 300));

	const Rect2i user_rect(2500, 200, 700, 500);
	display_server->simulate_window_rect_changed(user_rect);
	CHECK_EQ(window->get_position(), user_rect.position);
	CHECK_EQ(window->get_size(), user_rect.size);

	window->hide();
	window->popup();
	CHECK_EQ(display_server->get_last_created_sub_window_rect(), user_rect);

	memdelete(window);

	Popup *popup = memnew(Popup);
	popup->set_visible(false);
	popup->set_size(Size2i(400, 300));
	popup->set_initial_position(Window::WINDOW_INITIAL_POSITION_CENTER_SCREEN_WITH_MOUSE_FOCUS);
	root->add_child(popup);

	popup->popup();
	CHECK_EQ(display_server->get_last_created_sub_window_rect(), Rect2i(3000, 450, 400, 300));

	memdelete(popup);

	window = memnew(Window);
	window->set_visible(false);
	window->set_size(Size2i(400, 300));
	window->set_initial_position(Window::WINDOW_INITIAL_POSITION_CENTER_SCREEN_WITH_MOUSE_FOCUS);
	root->add_child(window);

	const Rect2i first_popup_rect(2300, 100, 640, 480);
	window->popup(first_popup_rect);
	CHECK_EQ(display_server->get_last_created_sub_window_rect(), first_popup_rect);

	const Rect2i second_user_rect(2600, 200, 720, 540);
	display_server->simulate_window_rect_changed(second_user_rect);
	window->hide();
	window->popup();
	CHECK_EQ(display_server->get_last_created_sub_window_rect(), second_user_rect);

	const Rect2i second_popup_rect(200, 100, 800, 600);
	window->hide();
	window->popup(second_popup_rect);
	CHECK_EQ(display_server->get_last_created_sub_window_rect(), second_popup_rect);
	window->hide();
	window->popup();
	CHECK_EQ(display_server->get_last_created_sub_window_rect(), second_popup_rect);

	memdelete(window);

	display_server->configure_window_geometry(screen_rects, 1);
	window = memnew(Window);
	window->set_visible(false);
	window->set_position(Point2i(-2000, -2000));
	window->set_size(Size2i(400, 300));
	root->add_child(window);

	ERR_PRINT_OFF;
	window->popup();
	ERR_PRINT_ON;
	CHECK_EQ(window->get_position(), Point2i(3000, 450));

	memdelete(window);

	root->set_embedding_subwindows(true);
	window = memnew(Window);
	window->set_visible(false);
	window->set_size(Size2i(400, 300));
	window->set_initial_position(Window::WINDOW_INITIAL_POSITION_CENTER_MAIN_WINDOW_SCREEN);
	root->add_child(window);

	window->popup();
	const Point2i embedded_user_position(37, 53);
	const Size2i embedded_user_size(600, 420);
	CHECK_NE(window->get_position(), embedded_user_position);
	window->set_position(embedded_user_position);
	window->set_size(embedded_user_size);
	window->hide();
	window->popup();
	CHECK_EQ(window->get_position(), embedded_user_position);
	CHECK_EQ(window->get_size(), embedded_user_size);

	memdelete(window);
	Ref<InputEventMouseMotion> restore_mouse_motion;
	restore_mouse_motion.instantiate();
	restore_mouse_motion->set_position(previous_mouse_position);
	display_server->simulate_event(restore_mouse_motion);
	display_server->reset_window_geometry();
	root->set_embedding_subwindows(was_embedding_subwindows);
}

} // namespace TestWindow
