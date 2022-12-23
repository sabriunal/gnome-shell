/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#include "config.h"

#include "shell-global.h"
#include "shell-tray-icon-private.h"
#include "shell-util.h"
#include "tray/na-tray-child.h"
#include "st.h"

#include <X11/Xatom.h>

enum {
   PROP_0,

   PROP_PID,
   PROP_TITLE,
   PROP_WM_CLASS
};

typedef struct _ShellTrayIconPrivate ShellTrayIconPrivate;

struct _ShellTrayIcon
{
  ClutterClone parent;
  NaTrayChild *tray_child;
  ClutterActor *window_actor;

  gulong window_actor_destroyed_handler;
  gulong window_created_handler;
  pid_t pid;
  char *title;
  char *wm_class;
};

G_DEFINE_TYPE (ShellTrayIcon, shell_tray_icon, CLUTTER_TYPE_CLONE);

static void
shell_tray_icon_finalize (GObject *object)
{
  ShellTrayIcon *icon = SHELL_TRAY_ICON (object);

  g_free (icon->title);
  g_free (icon->wm_class);

  G_OBJECT_CLASS (shell_tray_icon_parent_class)->finalize (object);
}

static void
shell_tray_icon_remove_window_actor (ShellTrayIcon *tray_icon)
{
  if (tray_icon->window_actor)
    {
      g_clear_signal_handler (&tray_icon->window_actor_destroyed_handler,
                              tray_icon->window_actor);
      g_clear_object (&tray_icon->window_actor);
    }

  clutter_clone_set_source (CLUTTER_CLONE (tray_icon), NULL);
}

static void
shell_tray_icon_window_created_cb (MetaDisplay   *display,
                                   MetaWindow    *window,
                                   ShellTrayIcon *tray_icon)
{
  Window xwindow = meta_window_get_xwindow (window);

  if (tray_icon->tray_child &&
      xwindow == na_xembed_get_socket_window (NA_XEMBED (tray_icon->tray_child)))
    {
      ClutterActor *window_actor = tray_icon->window_actor;

      clutter_clone_set_source (CLUTTER_CLONE (tray_icon), window_actor);

      /* We want to explicitly clear the clone source when the window
         actor is destroyed because otherwise we might end up keeping
         it alive after it has been disposed. Otherwise this can cause
         a crash if there is a paint after mutter notices that the top
         level window has been destroyed, which causes it to dispose
         the window, and before the tray manager notices that the
         window is gone which would otherwise reset the window and
         unref the clone */
      tray_icon->window_actor = g_object_ref (window_actor);
      tray_icon->window_actor_destroyed_handler =
        g_signal_connect_swapped (window_actor,
                                  "destroy",
                                  G_CALLBACK (shell_tray_icon_remove_window_actor),
                                  tray_icon);

      /* Hide the original actor otherwise it will appear in the scene
         as a normal window */
      clutter_actor_set_opacity (window_actor, 0);

      /* Also make sure it (or any of its children) doesn't block
         events on wayland */
      shell_util_set_hidden_from_pick (window_actor, TRUE);

#if 0
      /* Set an empty input shape on the window so that it can't get
         any input. This probably isn't the ideal way to achieve this.
         It would probably be better to force the window to go behind
         Mutter's guard window, but this is quite difficult to do as
         Mutter doesn't manage the stacking for override redirect
         windows and the guard window is repeatedly lowered to the
         bottom of the stack. */
      empty_region = cairo_region_create ();
      gdk_window_input_shape_combine_region (gdk_window,
                                             empty_region,
                                             0, 0 /* offset x/y */);
      cairo_region_destroy (empty_region);

      gdk_window_lower (gdk_window);
#endif

      /* Now that we've found the window we don't need to listen for
         new windows anymore */
      g_clear_signal_handler (&tray_icon->window_created_handler,
                              display);
    }
}

static void
shell_tray_icon_get_property (GObject         *object,
                              guint            prop_id,
                              GValue          *value,
                              GParamSpec      *pspec)
{
  ShellTrayIcon *icon = SHELL_TRAY_ICON (object);

  switch (prop_id)
    {
    case PROP_PID:
      g_value_set_uint (value, icon->pid);
      break;

    case PROP_TITLE:
      g_value_set_string (value, icon->title);
      break;

    case PROP_WM_CLASS:
      g_value_set_string (value, icon->wm_class);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
shell_tray_icon_dispose (GObject *object)
{
  ShellTrayIcon *tray_icon = SHELL_TRAY_ICON (object);

  shell_tray_icon_remove_window_actor (tray_icon);

  G_OBJECT_CLASS (shell_tray_icon_parent_class)->dispose (object);
}

static void
shell_tray_icon_get_preferred_width (ClutterActor *actor,
                                     float         for_height,
                                     float        *min_width_p,
                                     float        *natural_width_p)
{
  ShellTrayIcon *tray_icon = SHELL_TRAY_ICON (actor);
  int width;

  na_xembed_get_size (NA_XEMBED (tray_icon->tray_child), &width, NULL);

  *min_width_p = width;
  *natural_width_p = width;
}

static void
shell_tray_icon_get_preferred_height (ClutterActor *actor,
                                      float         for_width,
                                      float        *min_height_p,
                                      float        *natural_height_p)
{
  ShellTrayIcon *tray_icon = SHELL_TRAY_ICON (actor);
  int height;

  na_xembed_get_size (NA_XEMBED (tray_icon->tray_child), NULL, &height);

  *min_height_p = height;
  *natural_height_p = height;
}

static void
shell_tray_icon_allocate (ClutterActor          *actor,
                          const ClutterActorBox *box)
{
  ShellTrayIcon *tray_icon = SHELL_TRAY_ICON (actor);
  float wx, wy;

  CLUTTER_ACTOR_CLASS (shell_tray_icon_parent_class)->allocate (actor, box);

  /* Find the actor's new coordinates in terms of the stage.
   */
  clutter_actor_get_transformed_position (actor, &wx, &wy);
  na_xembed_set_root_position (NA_XEMBED (tray_icon->tray_child),
                               (int)(0.5 + wx), (int)(0.5 + wy));
}

static void
shell_tray_icon_class_init (ShellTrayIconClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  object_class->get_property = shell_tray_icon_get_property;
  object_class->finalize = shell_tray_icon_finalize;
  object_class->dispose = shell_tray_icon_dispose;

  actor_class->get_preferred_width = shell_tray_icon_get_preferred_width;
  actor_class->get_preferred_height = shell_tray_icon_get_preferred_height;
  actor_class->allocate = shell_tray_icon_allocate;

  g_object_class_install_property (object_class,
                                   PROP_PID,
                                   g_param_spec_uint ("pid",
                                                      "PID",
                                                      "The PID of the icon's application",
                                                      0, G_MAXUINT, 0,
                                                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
                                   PROP_TITLE,
                                   g_param_spec_string ("title",
                                                        "Title",
                                                        "The icon's window title",
                                                        NULL,
                                                        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class,
                                   PROP_WM_CLASS,
                                   g_param_spec_string ("wm-class",
                                                        "WM Class",
                                                        "The icon's window WM_CLASS",
                                                        NULL,
                                                        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
shell_tray_icon_init (ShellTrayIcon *icon)
{
}

static void
shell_tray_icon_set_child (ShellTrayIcon *tray_icon,
                           NaTrayChild   *tray_child)
{
  MetaDisplay *display = shell_global_get_display (shell_global_get ());

  g_return_if_fail (tray_icon != NULL);
  g_return_if_fail (tray_child != NULL);

  /* We do all this now rather than computing it on the fly later,
   * because the shell may want to see their values from a
   * tray-icon-removed signal handler, at which point the plug has
   * already been removed from the socket.
   */

  tray_icon->tray_child = tray_child;

  tray_icon->title = na_tray_child_get_title (tray_icon->tray_child);
  na_tray_child_get_wm_class (tray_icon->tray_child,
                              NULL, &tray_icon->wm_class);
  tray_icon->pid = na_tray_child_get_pid (tray_icon->tray_child);

  tray_icon->window_created_handler =
    g_signal_connect (display,
                      "window-created",
                      G_CALLBACK (shell_tray_icon_window_created_cb),
                      tray_icon);
}

/*
 * Public API
 */
ClutterActor *
shell_tray_icon_new (NaTrayChild *tray_child)
{
  ShellTrayIcon *tray_icon;

  g_return_val_if_fail (NA_IS_TRAY_CHILD (tray_child), NULL);

  tray_icon = g_object_new (SHELL_TYPE_TRAY_ICON, NULL);
  shell_tray_icon_set_child (tray_icon, tray_child);

  return CLUTTER_ACTOR (tray_icon);
}

/**
 * shell_tray_icon_click:
 * @icon: a #ShellTrayIcon
 * @event: the #ClutterEvent triggering the fake click
 *
 * Fakes a press and release on @icon. @event must be a
 * %CLUTTER_BUTTON_RELEASE, %CLUTTER_KEY_PRESS or %CLUTTER_KEY_RELEASE event.
 * Its relevant details will be passed on to the icon, but its
 * coordinates will be ignored; the click is
 * always made on the center of @icon.
 */
void
shell_tray_icon_click (ShellTrayIcon *icon,
                       ClutterEvent  *event)
{
  MetaDisplay *display = shell_global_get_display (shell_global_get ());
  MetaX11Display *x11_display;
  XKeyEvent xkevent;
  XButtonEvent xbevent;
  XCrossingEvent xcevent;
  Display *xdisplay;
  Window xwindow, xrootwindow;
  ClutterEventType event_type = clutter_event_type (event);
  int width, height;

  g_return_if_fail (event_type == CLUTTER_BUTTON_RELEASE ||
                    event_type == CLUTTER_KEY_PRESS ||
                    event_type == CLUTTER_KEY_RELEASE);

  x11_display = meta_display_get_x11_display (display);
  if (!x11_display)
    return;

  xwindow = na_xembed_get_plug_window (NA_XEMBED (icon->tray_child));
  if (xwindow == None)
    {
      g_warning ("shell tray: plug window is gone");
      return;
    }

  na_xembed_get_size (NA_XEMBED (icon->tray_child), &width, &height);

  meta_x11_error_trap_push (x11_display);

  xdisplay = meta_x11_display_get_xdisplay (x11_display);
  xrootwindow = XDefaultRootWindow (xdisplay);

  /* First make the icon believe the pointer is inside it */
  xcevent.type = EnterNotify;
  xcevent.window = xwindow;
  xcevent.root = xrootwindow;
  xcevent.subwindow = None;
  xcevent.time = clutter_event_get_time (event);
  xcevent.x = width / 2;
  xcevent.y = height / 2;
  xcevent.x_root = xcevent.x;
  xcevent.y_root = xcevent.y;
  xcevent.mode = NotifyNormal;
  xcevent.detail = NotifyNonlinear;
  xcevent.same_screen = True;
  XSendEvent (xdisplay, xwindow, False, 0, (XEvent *)&xcevent);

  /* Now do the click */
  if (event_type == CLUTTER_BUTTON_RELEASE)
    {
      xbevent.window = xwindow;
      xbevent.root = xrootwindow;
      xbevent.subwindow = None;
      xbevent.time = xcevent.time;
      xbevent.x = xcevent.x;
      xbevent.y = xcevent.y;
      xbevent.x_root = xcevent.x_root;
      xbevent.y_root = xcevent.y_root;
      xbevent.state = clutter_event_get_state (event);
      xbevent.same_screen = True;
      xbevent.type = ButtonPress;
      xbevent.button = clutter_event_get_button (event);
      XSendEvent (xdisplay, xwindow, False, 0, (XEvent *)&xbevent);

      xbevent.type = ButtonRelease;
      XSendEvent (xdisplay, xwindow, False, 0, (XEvent *)&xbevent);
    }
  else
    {
      xkevent.window = xwindow;
      xkevent.root = xrootwindow;
      xkevent.subwindow = None;
      xkevent.time = xcevent.time;
      xkevent.x = xcevent.x;
      xkevent.y = xcevent.y;
      xkevent.x_root = xcevent.x_root;
      xkevent.y_root = xcevent.y_root;
      xkevent.state = clutter_event_get_state (event);
      xkevent.same_screen = True;
      xkevent.keycode = clutter_event_get_key_code (event);

      xkevent.type = KeyPress;
      XSendEvent (xdisplay, xwindow, False, 0, (XEvent *)&xkevent);

      if (event_type == CLUTTER_KEY_RELEASE)
        {
          /* If the application takes a grab on KeyPress, we don't
           * want to send it a KeyRelease. There's no good way of
           * knowing whether a tray icon will take a grab, so just
           * assume it does, and don't send the KeyRelease. That might
           * make the tracking for key events messed up if it doesn't take
           * a grab, but the tray icon won't get key focus in normal cases,
           * so let's hope this isn't too damaging...
           */
          xkevent.type = KeyRelease;
          XSendEvent (xdisplay, xwindow, False, 0, (XEvent *)&xkevent);
        }
    }

  /* And move the pointer back out */
  xcevent.type = LeaveNotify;
  XSendEvent (xdisplay, xwindow, False, 0, (XEvent *)&xcevent);

  meta_x11_error_trap_pop (x11_display);
}
