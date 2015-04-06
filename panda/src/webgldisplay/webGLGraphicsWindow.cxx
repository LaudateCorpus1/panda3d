// Filename: webGLGraphicsWindow.cxx
// Created by:  rdb (31Mar15)
//
////////////////////////////////////////////////////////////////////
//
// PANDA 3D SOFTWARE
// Copyright (c) Carnegie Mellon University.  All rights reserved.
//
// All use of this software is subject to the terms of the revised BSD
// license.  You should have received a copy of this license along
// with this source code in a file named "LICENSE."
//
////////////////////////////////////////////////////////////////////

#include "webGLGraphicsWindow.h"
#include "webGLGraphicsStateGuardian.h"
#include "config_webgldisplay.h"
#include "mouseButton.h"
#include "keyboardButton.h"

#include <emscripten.h>

TypeHandle WebGLGraphicsWindow::_type_handle;

////////////////////////////////////////////////////////////////////
//     Function: WebGLGraphicsWindow::Constructor
//       Access: Public
//  Description:
////////////////////////////////////////////////////////////////////
WebGLGraphicsWindow::
WebGLGraphicsWindow(GraphicsEngine *engine, GraphicsPipe *pipe,
                    const string &name,
                    const FrameBufferProperties &fb_prop,
                    const WindowProperties &win_prop,
                    int flags,
                    GraphicsStateGuardian *gsg,
                    GraphicsOutput *host) :
  GraphicsWindow(engine, pipe, name, fb_prop, win_prop, flags, gsg, host)
{
  GraphicsWindowInputDevice device =
    GraphicsWindowInputDevice::pointer_and_keyboard(this, "keyboard_mouse");
  add_input_device(device);
}

////////////////////////////////////////////////////////////////////
//     Function: WebGLGraphicsWindow::Destructor
//       Access: Public, Virtual
//  Description:
////////////////////////////////////////////////////////////////////
WebGLGraphicsWindow::
~WebGLGraphicsWindow() {
}

////////////////////////////////////////////////////////////////////
//     Function: WebGLGraphicsWindow::move_pointer
//       Access: Published, Virtual
//  Description: Forces the pointer to the indicated position within
//               the window, if possible.
//
//               Returns true if successful, false on failure.  This
//               may fail if the mouse is not currently within the
//               window, or if the API doesn't support this operation.
////////////////////////////////////////////////////////////////////
bool WebGLGraphicsWindow::
move_pointer(int device, int x, int y) {
  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: WebGLGraphicsWindow::begin_frame
//       Access: Public, Virtual
//  Description: This function will be called within the draw thread
//               before beginning rendering for a given frame.  It
//               should do whatever setup is required, and return true
//               if the frame should be rendered, or false if it
//               should be skipped.
////////////////////////////////////////////////////////////////////
bool WebGLGraphicsWindow::
begin_frame(FrameMode mode, Thread *current_thread) {
  PStatTimer timer(_make_current_pcollector, current_thread);

  begin_frame_spam(mode);
  if (_gsg == (GraphicsStateGuardian *)NULL) {
    return false;
  }

  WebGLGraphicsStateGuardian *webgl_gsg;
  DCAST_INTO_R(webgl_gsg, _gsg, false);

  if (emscripten_webgl_make_context_current(webgl_gsg->_context) != EMSCRIPTEN_RESULT_SUCCESS) {
    webgldisplay_cat.error()
      << "Failed to make context current.\n";
    return false;
  }

  if (mode == FM_render) {
    // begin_render_texture();
    clear_cube_map_selection();
  }

  _gsg->set_current_properties(&get_fb_properties());
  return _gsg->begin_frame(current_thread);
}

////////////////////////////////////////////////////////////////////
//     Function: WebGLGraphicsWindow::end_frame
//       Access: Public, Virtual
//  Description: This function will be called within the draw thread
//               after rendering is completed for a given frame.  It
//               should do whatever finalization is required.
////////////////////////////////////////////////////////////////////
void WebGLGraphicsWindow::
end_frame(FrameMode mode, Thread *current_thread) {
  end_frame_spam(mode);
  nassertv(_gsg != (GraphicsStateGuardian *)NULL);

  if (mode == FM_render) {
    // end_render_texture();
    copy_to_textures();
  }

  _gsg->end_frame(current_thread);

  if (mode == FM_render) {
    trigger_flip();
    clear_cube_map_selection();
  }
}

////////////////////////////////////////////////////////////////////
//     Function: WebGLGraphicsWindow::end_flip
//       Access: Public, Virtual
//  Description: This function will be called within the draw thread
//               after begin_flip() has been called on all windows, to
//               finish the exchange of the front and back buffers.
//
//               This should cause the window to wait for the flip, if
//               necessary.
////////////////////////////////////////////////////////////////////
void WebGLGraphicsWindow::
end_flip() {
  GraphicsWindow::end_flip();
}

////////////////////////////////////////////////////////////////////
//     Function: WebGLGraphicsWindow::process_events
//       Access: Public, Virtual
//  Description: Do whatever processing is necessary to ensure that
//               the window responds to user events.  Also, honor any
//               requests recently made via request_properties()
//
//               This function is called only within the window
//               thread.
////////////////////////////////////////////////////////////////////
void WebGLGraphicsWindow::
process_events() {
  GraphicsWindow::process_events();
}

////////////////////////////////////////////////////////////////////
//     Function: WebGLGraphicsWindow::set_properties_now
//       Access: Public, Virtual
//  Description: Applies the requested set of properties to the
//               window, if possible, for instance to request a change
//               in size or minimization status.
//
//               The window properties are applied immediately, rather
//               than waiting until the next frame.  This implies that
//               this method may *only* be called from within the
//               window thread.
//
//               The return value is true if the properties are set,
//               false if they are ignored.  This is mainly useful for
//               derived classes to implement extensions to this
//               function.
////////////////////////////////////////////////////////////////////
void WebGLGraphicsWindow::
set_properties_now(WindowProperties &properties) {
  GraphicsWindow::set_properties_now(properties);

  if (properties.has_size()) {
    emscripten_set_canvas_size(properties.get_x_size(), properties.get_y_size());
    _properties.set_size(properties.get_size());
    properties.clear_size();
  }

  if (properties.has_fullscreen() &&
      properties.get_fullscreen() != _properties.get_fullscreen()) {

    if (properties.get_fullscreen()) {
      EMSCRIPTEN_RESULT result = emscripten_request_fullscreen(NULL, true);

      if (result == EMSCRIPTEN_RESULT_SUCCESS) {
        _properties.set_fullscreen(true);
        properties.clear_fullscreen();

      } else if (result == EMSCRIPTEN_RESULT_DEFERRED) {
        // We can't switch to fullscreen just yet - this action is deferred
        // until we're in an event handler.  We can't know for sure yet that
        // fullscreen will be supported, but we shouldn't report failure.
        properties.clear_fullscreen();
      }
    } else {
      if (emscripten_exit_fullscreen() == EMSCRIPTEN_RESULT_SUCCESS) {
        _properties.set_fullscreen(false);
        properties.clear_fullscreen();
      }
    }
  }
}

////////////////////////////////////////////////////////////////////
//     Function: WebGLGraphicsWindow::close_window
//       Access: Protected, Virtual
//  Description: Closes the window right now.  Called from the window
//               thread.
////////////////////////////////////////////////////////////////////
void WebGLGraphicsWindow::
close_window() {
  if (_gsg != (GraphicsStateGuardian *)NULL) {
    emscripten_webgl_make_context_current(0);
    _gsg.clear();
  }

  // Clear the assigned callbacks.
  const char *target = NULL;
  emscripten_set_fullscreenchange_callback(target, NULL, false, NULL);

  emscripten_set_keypress_callback(target, NULL, false, NULL);
  emscripten_set_keydown_callback(target, NULL, false, NULL);
  emscripten_set_keyup_callback(target, NULL, false, NULL);

  //emscripten_set_click_callback(target, NULL, false, NULL);
  emscripten_set_mousedown_callback(target, NULL, false, NULL);
  emscripten_set_mouseup_callback(target, NULL, false, NULL);
  emscripten_set_mousemove_callback(target, NULL, false, NULL);
  emscripten_set_mouseenter_callback(target, NULL, false, NULL);
  emscripten_set_mouseleave_callback(target, NULL, false, NULL);

  GraphicsWindow::close_window();
}

////////////////////////////////////////////////////////////////////
//     Function: WebGLGraphicsWindow::open_window
//       Access: Protected, Virtual
//  Description: Opens the window right now.  Called from the window
//               thread.  Returns true if the window is successfully
//               opened, or false if there was a problem.
////////////////////////////////////////////////////////////////////
bool WebGLGraphicsWindow::
open_window() {
  //WebGLGraphicsPipe *webgl_pipe;
  //DCAST_INTO_R(webgl_pipe, _pipe, false);

  const char *target = NULL;

  // GSG Creation/Initialization
  WebGLGraphicsStateGuardian *webgl_gsg;
  if (_gsg == NULL) {
    // There is no old gsg.  Create a new one.
    webgl_gsg = new WebGLGraphicsStateGuardian(_engine, _pipe);
    webgl_gsg->choose_pixel_format(_fb_properties, target);
    _gsg = webgl_gsg;
  } else {
    // If the old gsg has the wrong pixel format, create a new one.
    //DCAST_INTO_R(webgl_gsg, _gsg, false);
    //if (!webgl_gsg->_fb_properties.subsumes(_fb_properties)) {
    //  webgl_gsg = new WebGLGraphicsStateGuardian(_engine, _pipe);
    //  webgl_gsg->choose_pixel_format(_fb_properties, target);
    //  _gsg = webgl_gsg;
    //}
  }

  if (_properties.has_size() && _properties.get_size() != LVecBase2i(1, 1)) {
    emscripten_set_canvas_size(_properties.get_x_size(), _properties.get_y_size());
  } else {
    int width, height, fullscreen;
    emscripten_get_canvas_size(&width, &height, &fullscreen);
    _properties.set_size(width, height);
    _properties.set_fullscreen(fullscreen > 0);
  }

  if (emscripten_webgl_make_context_current(webgl_gsg->_context) != EMSCRIPTEN_RESULT_SUCCESS) {
    webgldisplay_cat.error()
      << "Failed to make context current.\n";
    return false;
  }

  webgl_gsg->reset();

  _fb_properties.clear();
  _fb_properties.set_rgb_color(true);
  _fb_properties.set_force_hardware(true);
  _fb_properties.set_back_buffers(1);

  GLint red_bits, green_bits, blue_bits, alpha_bits, depth_bits, stencil_bits;

  glGetIntegerv(GL_RED_BITS, &red_bits);
  glGetIntegerv(GL_GREEN_BITS, &green_bits);
  glGetIntegerv(GL_BLUE_BITS, &blue_bits);
  glGetIntegerv(GL_ALPHA_BITS, &alpha_bits);
  glGetIntegerv(GL_DEPTH_BITS, &depth_bits);
  glGetIntegerv(GL_STENCIL_BITS, &stencil_bits);

  _fb_properties.set_rgba_bits(red_bits, green_bits, blue_bits, alpha_bits);
  _fb_properties.set_depth_bits(depth_bits);
  _fb_properties.set_stencil_bits(stencil_bits);

  // Set callbacks.
  emscripten_set_fullscreenchange_callback(target, (void *)this, false, &on_fullscreen_event);

  void *user_data = (void *)&_input_devices[0];

  emscripten_set_keypress_callback(target, user_data, false, &on_keyboard_event);
  emscripten_set_keydown_callback(target, user_data, false, &on_keyboard_event);
  emscripten_set_keyup_callback(target, user_data, false, &on_keyboard_event);

  //emscripten_set_click_callback(target, user_data, false, &on_mouse_event);
  emscripten_set_mousedown_callback(target, user_data, false, &on_mouse_event);
  emscripten_set_mouseup_callback(target, user_data, false, &on_mouse_event);
  emscripten_set_mousemove_callback(target, user_data, false, &on_mouse_event);
  emscripten_set_mouseenter_callback(target, user_data, false, &on_mouse_event);
  emscripten_set_mouseleave_callback(target, user_data, false, &on_mouse_event);

  return true;
}

////////////////////////////////////////////////////////////////////
//     Function: WebGLGraphicsWindow::on_fullscreen_event
//       Access: Private, Static
//  Description:
////////////////////////////////////////////////////////////////////
EM_BOOL WebGLGraphicsWindow::
on_fullscreen_event(int type, const EmscriptenFullscreenChangeEvent *event, void *user_data) {
  WebGLGraphicsWindow *window = (WebGLGraphicsWindow *)user_data;
  nassertr(window != NULL, false);

  if (type == EMSCRIPTEN_EVENT_FULLSCREENCHANGE) {
    WindowProperties props;
    props.set_fullscreen(event->isFullscreen);
    window->system_changed_properties(props);
    return true;
  }

  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: WebGLGraphicsWindow::on_keyboard_event
//       Access: Private, Static
//  Description:
////////////////////////////////////////////////////////////////////
EM_BOOL WebGLGraphicsWindow::
on_keyboard_event(int type, const EmscriptenKeyboardEvent *event, void *user_data) {
  GraphicsWindowInputDevice *device;
  device = (GraphicsWindowInputDevice *)user_data;
  nassertr(device != NULL, false);

  if (type == EMSCRIPTEN_EVENT_KEYPRESS) {
    // Chrome doesn't actually send this.  Weird.
    return false;

  } else if (type == EMSCRIPTEN_EVENT_KEYDOWN ||
             type == EMSCRIPTEN_EVENT_KEYUP) {

    ButtonHandle handle;
    switch (event->which) {
    case 16:
      handle = KeyboardButton::shift();
      break;

    case 17:
      handle = KeyboardButton::control();
      break;

    case 18:
      handle = KeyboardButton::alt();
      break;

    case 20:
      handle = KeyboardButton::caps_lock();
      break;

    case 186:
      handle = KeyboardButton::ascii_key(';');
      break;

    case 188:
      handle = KeyboardButton::ascii_key('.');
      break;

    case 189:
      handle = KeyboardButton::ascii_key('-');
      break;

    case 190:
      handle = KeyboardButton::ascii_key(',');
      break;

    case 191:
      handle = KeyboardButton::ascii_key('?');
      break;

    case 192:
      handle = KeyboardButton::ascii_key('`');
      break;

    case 220:
      handle = KeyboardButton::ascii_key('\\');
      break;

    case 222:
      handle = KeyboardButton::ascii_key('\'');
      break;

    default:
      handle = KeyboardButton::ascii_key(tolower(event->which));
    }

    if (handle != ButtonHandle::none()) {
      if (type == EMSCRIPTEN_EVENT_KEYUP) {
        webgldisplay_cat.info() << "button up " << handle << "\n";
        device->button_up(handle);
      } else if (event->repeat) {
        device->button_resume_down(handle);
      } else {
        webgldisplay_cat.info() << "button down " << handle << "\n";
        device->button_down(handle);
      }
    } else {
      webgldisplay_cat.info() << "button event code " << event->which << "\n";
    }
  }

  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: WebGLGraphicsWindow::on_mouse_event
//       Access: Private, Static
//  Description:
////////////////////////////////////////////////////////////////////
EM_BOOL WebGLGraphicsWindow::
on_mouse_event(int type, const EmscriptenMouseEvent *event, void *user_data) {
  GraphicsWindowInputDevice *device;
  device = (GraphicsWindowInputDevice *)user_data;
  nassertr(device != NULL, false);

  double time = event->timestamp * 0.001;

  switch (type) {
  case EMSCRIPTEN_EVENT_MOUSEDOWN:
    // Don't register out-of-bounds mouse downs.
    if (event->canvasX < 0 || event->canvasY < 0) {
      return false;
    }
    device->button_down(MouseButton::button(event->button), time);
    return true;

  case EMSCRIPTEN_EVENT_MOUSEUP:
    device->button_up(MouseButton::button(event->button), time);
    return true;

  case EMSCRIPTEN_EVENT_MOUSEMOVE:
    device->set_pointer_in_window(event->canvasX, event->canvasY, time);
    return true;

  case EMSCRIPTEN_EVENT_MOUSEENTER:
    break;

  case EMSCRIPTEN_EVENT_MOUSELEAVE:
    device->set_pointer_out_of_window(time);
    return true;

  default:
    break;
  }

  return false;
}
