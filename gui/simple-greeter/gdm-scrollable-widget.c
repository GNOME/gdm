/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
 * USA.
 *
 * Written by: Ray Strode <rstrode@redhat.com>
 *
 * Parts taken from gtkscrolledwindow.c in the GTK+ toolkit.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "gdm-scrollable-widget.h"
#include "gdm-timer.h"

#define GDM_SCROLLABLE_WIDGET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SCROLLABLE_WIDGET, GdmScrollableWidgetPrivate))

enum
{
        SCROLL_CHILD,
        MOVE_FOCUS_OUT,
        NUMBER_OF_SIGNALS
};

typedef struct GdmScrollableWidgetAnimation GdmScrollableWidgetAnimation;

struct GdmScrollableWidgetPrivate
{
        GtkWidget *scrollbar;

        GdmScrollableWidgetAnimation *animation;
        GtkWidget *invisible_event_sink;
        guint      key_press_signal_id;
        guint      key_release_signal_id;

        int        forced_height;

        GQueue    *key_event_queue;
};

struct GdmScrollableWidgetAnimation
{
        GtkWidget *widget;
        GdmTimer  *timer;
        int        start_height;
        int        desired_height;
        GdmScrollableWidgetSlideStepFunc step_func;
        gpointer   step_func_user_data;
        GdmScrollableWidgetSlideDoneFunc done_func;
        gpointer   done_func_user_data;
};

static void     gdm_scrollable_widget_class_init  (GdmScrollableWidgetClass *klass);
static void     gdm_scrollable_widget_init        (GdmScrollableWidget      *clock_widget);
static void     gdm_scrollable_widget_finalize    (GObject             *object);

static guint signals[NUMBER_OF_SIGNALS] = { 0 };

G_DEFINE_TYPE (GdmScrollableWidget, gdm_scrollable_widget, GTK_TYPE_BIN)

static GdmScrollableWidgetAnimation *
gdm_scrollable_widget_animation_new (GtkWidget *widget,
                                     int        start_height,
                                     int        desired_height,
                                     GdmScrollableWidgetSlideStepFunc step_func,
                                     gpointer   step_func_user_data,
                                     GdmScrollableWidgetSlideDoneFunc done_func,
                                     gpointer   done_func_user_data)
{
        GdmScrollableWidgetAnimation *animation;

        animation = g_slice_new (GdmScrollableWidgetAnimation);

        animation->widget = widget;
        animation->timer = gdm_timer_new ();
        animation->start_height = start_height;
        animation->desired_height = desired_height;
        animation->step_func = step_func;
        animation->step_func_user_data = step_func_user_data;
        animation->done_func = done_func;
        animation->done_func_user_data = done_func_user_data;

        return animation;
}

static void
gdm_scrollable_widget_animation_free (GdmScrollableWidgetAnimation *animation)
{
        g_object_unref (animation->timer);
        animation->timer = NULL;
        g_slice_free (GdmScrollableWidgetAnimation, animation);
}

static void
on_animation_tick (GdmScrollableWidgetAnimation *animation,
                   double                        progress)
{
        GdmScrollableWidget *scrollable_widget;
        int progress_in_pixels;
        int height;

        scrollable_widget = GDM_SCROLLABLE_WIDGET (animation->widget);

        progress_in_pixels = progress * (animation->start_height - animation->desired_height);

        height = animation->start_height - progress_in_pixels;
        scrollable_widget->priv->forced_height = height;

        gtk_widget_queue_resize (animation->widget);

        if (animation->step_func != NULL) {
                GdmTimer *timer;
                GtkStyleContext *context;
                GtkStateFlags state;
                GtkBorder padding, border;

                height = animation->desired_height;

                context = gtk_widget_get_style_context (animation->widget);
                state = gtk_widget_get_state_flags (animation->widget);

                gtk_style_context_get_padding (context, state, &padding);
                gtk_style_context_get_border (context, state, &border);

                height -= padding.top + padding.bottom;
                height -= border.top + border.bottom;

                timer = g_object_ref (animation->timer);
                animation->step_func (GDM_SCROLLABLE_WIDGET (animation->widget),
                                      progress,
                                      &height,
                                      animation->step_func_user_data);

                if (gdm_timer_is_started (timer)) {
                        height += padding.top + padding.bottom;
                        height += border.top + border.bottom;

                        animation->desired_height = height;
                }
                g_object_unref (timer);
        }
}

static gboolean
on_key_event (GdmScrollableWidget *scrollable_widget,
              GdkEventKey         *key_event)
{
        g_queue_push_tail (scrollable_widget->priv->key_event_queue,
                           gdk_event_copy ((GdkEvent *)key_event));
        return FALSE;
}

static gboolean
gdm_scrollable_redirect_input_to_event_sink (GdmScrollableWidget *scrollable_widget)
{
        GdkGrabStatus status;

        status = gdk_pointer_grab (gtk_widget_get_window (scrollable_widget->priv->invisible_event_sink),
                          FALSE, 0, NULL, NULL, GDK_CURRENT_TIME);
        if (status != GDK_GRAB_SUCCESS) {
                return FALSE;
        }

        status = gdk_keyboard_grab (gtk_widget_get_window (scrollable_widget->priv->invisible_event_sink),
                           FALSE, GDK_CURRENT_TIME);
        if (status != GDK_GRAB_SUCCESS) {
                gdk_pointer_ungrab (GDK_CURRENT_TIME);
                return FALSE;
        }

        scrollable_widget->priv->key_press_signal_id =
            g_signal_connect_swapped (scrollable_widget->priv->invisible_event_sink,
                                      "key-press-event", G_CALLBACK (on_key_event),
                                      scrollable_widget);

        scrollable_widget->priv->key_release_signal_id =
            g_signal_connect_swapped (scrollable_widget->priv->invisible_event_sink,
                                      "key-release-event", G_CALLBACK (on_key_event),
                                      scrollable_widget);

        return TRUE;
}

static void
gdm_scrollable_unredirect_input (GdmScrollableWidget *scrollable_widget)
{
        g_signal_handler_disconnect (scrollable_widget->priv->invisible_event_sink,
                                     scrollable_widget->priv->key_press_signal_id);
        scrollable_widget->priv->key_press_signal_id = 0;

        g_signal_handler_disconnect (scrollable_widget->priv->invisible_event_sink,
                                     scrollable_widget->priv->key_release_signal_id);
        scrollable_widget->priv->key_release_signal_id = 0;
        gdk_keyboard_ungrab (GDK_CURRENT_TIME);
        gdk_pointer_ungrab (GDK_CURRENT_TIME);
}

static void
on_animation_stop (GdmScrollableWidgetAnimation *animation)
{
        GdmScrollableWidget *widget;

        widget = GDM_SCROLLABLE_WIDGET (animation->widget);

        if (animation->done_func != NULL) {
                animation->done_func (widget, animation->done_func_user_data);
        }

        gdm_scrollable_widget_animation_free (widget->priv->animation);
        widget->priv->animation = NULL;

        gdm_scrollable_unredirect_input (widget);
}

static void
gdm_scrollable_widget_animation_start (GdmScrollableWidgetAnimation *animation)
{
        g_signal_connect_swapped (G_OBJECT (animation->timer), "tick",
                                  G_CALLBACK (on_animation_tick),
                                  animation);
        g_signal_connect_swapped (G_OBJECT (animation->timer), "stop",
                                  G_CALLBACK (on_animation_stop),
                                  animation);
        gdm_timer_start (animation->timer, .10);
}

static void
gdm_scrollable_widget_animation_stop (GdmScrollableWidgetAnimation *animation)
{
        gdm_timer_stop (animation->timer);
}

static gboolean
gdm_scrollable_widget_needs_scrollbar (GdmScrollableWidget *widget)
{
        GtkWidget *child;
        gboolean needs_scrollbar;

        if (widget->priv->scrollbar == NULL) {
                return FALSE;
        }

        if (widget->priv->animation != NULL) {
                return FALSE;
        }

        child = gtk_bin_get_child (GTK_BIN (widget));
        if (child != NULL && GTK_IS_SCROLLABLE (child)) {
                GtkStyleContext *context;
                GtkStateFlags state;
                GtkBorder padding, border;
                int available_height;
                int child_scrolled_height;

                context = gtk_widget_get_style_context (GTK_WIDGET (widget));
                state = gtk_widget_get_state_flags (GTK_WIDGET (widget));

                gtk_style_context_get_padding (context, state, &padding);
                gtk_style_context_get_border (context, state, &border);

                available_height = gtk_widget_get_allocated_height (GTK_WIDGET (widget));
                available_height -= padding.top + padding.bottom;
                available_height -= border.top + border.bottom;

                gtk_widget_get_preferred_height (child, NULL, &child_scrolled_height);
                needs_scrollbar = child_scrolled_height > available_height;
        } else {
                needs_scrollbar = FALSE;
        }

        return needs_scrollbar;
}

static void
gdm_scrollable_widget_get_preferred_size (GtkWidget      *widget,
                                          GtkOrientation  orientation,
                                          gint           *minimum_size,
                                          gint           *natural_size)
{
        GdmScrollableWidget *scrollable_widget;
        GtkStyleContext *context;
        GtkStateFlags state;
        GtkBorder padding, border;
        GtkRequisition scrollbar_requisition;
        GtkRequisition minimum_req, natural_req;
        GtkWidget *child;
        int min_child_size, nat_child_size;

        context = gtk_widget_get_style_context (widget);
        state = gtk_widget_get_state_flags (widget);

        gtk_style_context_get_padding (context, state, &padding);
        gtk_style_context_get_border (context, state, &border);

        scrollable_widget = GDM_SCROLLABLE_WIDGET (widget);

        minimum_req.width = padding.left + padding.right;
        minimum_req.width += border.left + border.right;
        minimum_req.height = padding.top + padding.bottom;
        minimum_req.height += border.top + border.bottom;

        natural_req.width = padding.left + padding.right;
        natural_req.width += border.left + border.right;
        natural_req.height = padding.top + padding.bottom;
        natural_req.height += border.top + border.bottom;

        if (orientation == GTK_ORIENTATION_VERTICAL
            && scrollable_widget->priv->forced_height >= 0) {
                minimum_req.height += scrollable_widget->priv->forced_height;
                natural_req.height += scrollable_widget->priv->forced_height;
        } else {
                child = gtk_bin_get_child (GTK_BIN (widget));

                gtk_widget_get_preferred_size (scrollable_widget->priv->scrollbar,
                                               &scrollbar_requisition,
                                               NULL);

                if (child && gtk_widget_get_visible (child)) {
                        if (orientation == GTK_ORIENTATION_HORIZONTAL) {
                                gtk_widget_get_preferred_width (child,
                                                                &min_child_size,
                                                                &nat_child_size);
                                minimum_req.width += min_child_size;
                                natural_req.width += nat_child_size;
                        } else {
                                gtk_widget_get_preferred_height (child,
                                                                 &min_child_size,
                                                                 &nat_child_size);

                                natural_req.height += nat_child_size;
                        }
                }

                if (gdm_scrollable_widget_needs_scrollbar (scrollable_widget)) {
                        minimum_req.height = MAX (minimum_req.height,
                                                  scrollbar_requisition.height);
                        minimum_req.width += scrollbar_requisition.width;
                        natural_req.height = MAX (natural_req.height,
                                                  scrollbar_requisition.height);
                        natural_req.width += scrollbar_requisition.width;
                }
        }

        if (orientation == GTK_ORIENTATION_HORIZONTAL) {
                if (minimum_size)
                        *minimum_size = minimum_req.width;
                if (natural_size)
                        *natural_size = natural_req.width;
        } else {
                if (minimum_size)
                        *minimum_size = minimum_req.height;
                if (natural_size)
                        *natural_size = natural_req.height;
        }
}

static void
gdm_scrollable_widget_get_preferred_width (GtkWidget *widget,
                                           gint      *minimum_size,
                                           gint      *natural_size)
{
        gdm_scrollable_widget_get_preferred_size (widget, GTK_ORIENTATION_HORIZONTAL, minimum_size, natural_size);
}

static void
gdm_scrollable_widget_get_preferred_height (GtkWidget *widget,
                                            gint      *minimum_size,
                                            gint      *natural_size)
{
        gdm_scrollable_widget_get_preferred_size (widget, GTK_ORIENTATION_VERTICAL, minimum_size, natural_size);
}

static void
gdm_scrollable_widget_size_allocate (GtkWidget     *widget,
                                     GtkAllocation *allocation)
{
        GdmScrollableWidget *scrollable_widget;
        GtkAllocation        scrollbar_allocation;
        GtkAllocation        child_allocation;
        gboolean             has_child;
        gboolean             needs_scrollbar;
        gboolean             is_flipped;
        GtkWidget           *child;
        GtkStyleContext     *context;
        GtkStateFlags        state;
        GtkBorder            padding, border;

        scrollable_widget = GDM_SCROLLABLE_WIDGET (widget);
        context = gtk_widget_get_style_context (widget);
        state = gtk_widget_get_state_flags (widget);

        gtk_style_context_get_padding (context, state, &padding);
        gtk_style_context_get_border (context, state, &border);

        gtk_widget_set_allocation (widget, allocation);

        child = gtk_bin_get_child (GTK_BIN (widget));
        has_child = child && gtk_widget_get_visible (child);
        needs_scrollbar = gdm_scrollable_widget_needs_scrollbar (scrollable_widget);
        is_flipped = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;

        if (needs_scrollbar) {
                gtk_widget_show (scrollable_widget->priv->scrollbar);

                gtk_widget_get_preferred_width (scrollable_widget->priv->scrollbar, NULL, &scrollbar_allocation.width);

                if (!is_flipped) {
                        scrollbar_allocation.x = allocation->x + allocation->width;
                        scrollbar_allocation.x -= scrollbar_allocation.width - padding.right;
                } else {
                        scrollbar_allocation.x = allocation->x + padding.right + border.right;
                }

                scrollbar_allocation.height = allocation->height;

                scrollbar_allocation.y = allocation->y + padding.top;

                gtk_widget_size_allocate (scrollable_widget->priv->scrollbar,
                                          &scrollbar_allocation);
        } else {
                gtk_widget_hide (scrollable_widget->priv->scrollbar);
        }

        if (has_child) {
                child_allocation.width = allocation->width;
                child_allocation.width = MAX (child_allocation.width - padding.left, 1);
                child_allocation.width = MAX (child_allocation.width - padding.right, 1);
                child_allocation.width = MAX (child_allocation.width - border.left, 1);
                child_allocation.width = MAX (child_allocation.width - border.right, 1);

                if (needs_scrollbar) {
                        child_allocation.width = MAX (child_allocation.width - scrollbar_allocation.width, 1);
                }

                if (!is_flipped) {
                        child_allocation.x = allocation->x;
                        child_allocation.x += padding.left;
                        child_allocation.x += border.left;
                } else {
                        child_allocation.x = allocation->x + allocation->width;
                        child_allocation.x -= child_allocation.width;
                        child_allocation.x -= padding.left;
                        child_allocation.x -= border.left;
                }

                child_allocation.height = allocation->height;
                child_allocation.height = MAX (child_allocation.height - padding.top, 1);
                child_allocation.height = MAX (child_allocation.height - border.top, 1);
                child_allocation.height = MAX (child_allocation.height - padding.bottom, 1);
                child_allocation.height = MAX (child_allocation.height - border.bottom, 1);

                child_allocation.y = allocation->y;
                child_allocation.y += padding.top;
                child_allocation.y += border.top;

                gtk_widget_size_allocate (child,
                                          &child_allocation);
        }
}

static void
gdm_scrollable_widget_add (GtkContainer *container,
                           GtkWidget    *child)
{
        GtkAdjustment *adjustment;

        GTK_CONTAINER_CLASS (gdm_scrollable_widget_parent_class)->add (container, child);

        adjustment = gtk_range_get_adjustment (GTK_RANGE (GDM_SCROLLABLE_WIDGET (container)->priv->scrollbar));

        g_signal_connect_swapped (adjustment, "changed",
                                  G_CALLBACK (gtk_widget_queue_resize),
                                  container);

        if (GTK_IS_SCROLLABLE (child))
                g_object_set (child, "hadjustment", NULL, "vadjustment", adjustment, NULL);
        else
                g_warning ("gdm_scrollable_widget_add(): cannot add non scrollable widget");
}

static void
gdm_scrollable_widget_remove (GtkContainer *container,
                              GtkWidget    *child)
{
        g_object_set (child, "hadjustment", NULL, "vadjustment", NULL, NULL);

        GTK_CONTAINER_CLASS (gdm_scrollable_widget_parent_class)->remove (container, child);
}

static void
gdm_scrollable_widget_forall (GtkContainer *container,
                              gboolean      include_internals,
                              GtkCallback   callback,
                              gpointer      callback_data)
{

        GdmScrollableWidget *scrollable_widget;

        scrollable_widget = GDM_SCROLLABLE_WIDGET (container);

        GTK_CONTAINER_CLASS (gdm_scrollable_widget_parent_class)->forall (container,
                                                                          include_internals,
                                                                          callback,
                                                                          callback_data);

        if (!include_internals) {
                return;
        }

        if (scrollable_widget->priv->scrollbar != NULL) {
                callback (scrollable_widget->priv->scrollbar, callback_data);
        }
}

static void
gdm_scrollable_widget_destroy (GtkWidget *object)
{
        GdmScrollableWidget *scrollable_widget;

        scrollable_widget = GDM_SCROLLABLE_WIDGET (object);

        gtk_widget_unparent (scrollable_widget->priv->scrollbar);
        gtk_widget_destroy (scrollable_widget->priv->scrollbar);

        GTK_WIDGET_CLASS (gdm_scrollable_widget_parent_class)->destroy (object);
}

static void
gdm_scrollable_widget_finalize (GObject *object)
{
        GdmScrollableWidget *scrollable_widget;

        scrollable_widget = GDM_SCROLLABLE_WIDGET (object);

        g_queue_free (scrollable_widget->priv->key_event_queue);

        G_OBJECT_CLASS (gdm_scrollable_widget_parent_class)->finalize (object);
}

static gboolean
gdm_scrollable_widget_draw (GtkWidget *widget,
                            cairo_t   *cr)
{
        GdmScrollableWidget *scrollable_widget;
        int                  x;
        int                  y;
        int                  width;
        int                  height;
        gboolean             is_flipped;
        GtkStyleContext     *context;
        GtkStateFlags        state;
        GtkBorder            padding, border;
        GtkAllocation widget_allocation;

        context = gtk_widget_get_style_context (widget);
        state = gtk_widget_get_state_flags (widget);
        is_flipped = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;

        gtk_style_context_get_padding (context, state, &padding);
        gtk_style_context_get_border (context, state, &border);

        gtk_widget_get_allocation (widget, &widget_allocation);

        scrollable_widget = GDM_SCROLLABLE_WIDGET (widget);

        if (!gtk_widget_is_drawable (widget)) {
                return FALSE;
        }

        x = 0;
        x += padding.left;

        width = widget_allocation.width;
        width -= padding.left + padding.right;

        if (gdm_scrollable_widget_needs_scrollbar (scrollable_widget)) {
                GtkAllocation scrollbar_allocation;
                gtk_widget_get_allocation (scrollable_widget->priv->scrollbar, &scrollbar_allocation);
                width -= scrollbar_allocation.width;

                if (is_flipped) {
                        x += scrollbar_allocation.width;
                }
        }

        y = 0;
        y += padding.top;

        height = widget_allocation.height;
        height -= padding.top + padding.bottom;

        if (width > 0 && height > 0) {
                gtk_render_frame (context, cr,
                                  x, y, width, height);
        }

        return GTK_WIDGET_CLASS (gdm_scrollable_widget_parent_class)->draw (widget, cr);
}

static gboolean
gdm_scrollable_widget_scroll_event (GtkWidget      *widget,
                                    GdkEventScroll *event)
{
        if (event->direction != GDK_SCROLL_UP && event->direction != GDK_SCROLL_DOWN) {
                return FALSE;
        }

        if (!gtk_widget_get_visible (GTK_WIDGET (widget))) {
                return FALSE;
        }

        return gtk_widget_event (GDM_SCROLLABLE_WIDGET (widget)->priv->scrollbar,
                                 (GdkEvent *) event);
}

static void
add_scroll_binding (GtkBindingSet  *binding_set,
                    guint           keyval,
                    GdkModifierType mask,
                    GtkScrollType   scroll)
{
        guint keypad_keyval = keyval - GDK_KEY_Left + GDK_KEY_KP_Left;

        gtk_binding_entry_add_signal (binding_set, keyval, mask,
                                      "scroll-child", 1,
                                      GTK_TYPE_SCROLL_TYPE, scroll);
        gtk_binding_entry_add_signal (binding_set, keypad_keyval, mask,
                                      "scroll-child", 1,
                                      GTK_TYPE_SCROLL_TYPE, scroll);
}

static void
add_tab_bindings (GtkBindingSet    *binding_set,
                  GdkModifierType   modifiers,
                  GtkDirectionType  direction)
{
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_Tab, modifiers,
                                      "move-focus-out", 1,
                                      GTK_TYPE_DIRECTION_TYPE, direction);
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Tab, modifiers,
                                      "move-focus-out", 1,
                                      GTK_TYPE_DIRECTION_TYPE, direction);
}

static void
gdm_scrollable_widget_class_install_bindings (GdmScrollableWidgetClass *klass)
{
        GtkBindingSet *binding_set;

        binding_set = gtk_binding_set_by_class (klass);

        add_scroll_binding (binding_set, GDK_KEY_Up, GDK_CONTROL_MASK, GTK_SCROLL_STEP_BACKWARD);
        add_scroll_binding (binding_set, GDK_KEY_Down, GDK_CONTROL_MASK, GTK_SCROLL_STEP_FORWARD);

        add_scroll_binding (binding_set, GDK_KEY_Page_Up, 0, GTK_SCROLL_PAGE_BACKWARD);
        add_scroll_binding (binding_set, GDK_KEY_Page_Down, 0, GTK_SCROLL_PAGE_FORWARD);

        add_scroll_binding (binding_set, GDK_KEY_Home, 0, GTK_SCROLL_START);
        add_scroll_binding (binding_set, GDK_KEY_End, 0, GTK_SCROLL_END);

        add_tab_bindings (binding_set, GDK_CONTROL_MASK, GTK_DIR_TAB_FORWARD);
        add_tab_bindings (binding_set, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_DIR_TAB_BACKWARD);
}

static void
gdm_scrollable_widget_class_init (GdmScrollableWidgetClass *klass)
{
        GObjectClass             *object_class;
        GtkWidgetClass           *widget_class;
        GtkContainerClass        *container_class;
        GdmScrollableWidgetClass *scrollable_widget_class;

        object_class = G_OBJECT_CLASS (klass);
        widget_class = GTK_WIDGET_CLASS (klass);
        container_class = GTK_CONTAINER_CLASS (klass);
        scrollable_widget_class = GDM_SCROLLABLE_WIDGET_CLASS (klass);

        object_class->finalize = gdm_scrollable_widget_finalize;

        widget_class->destroy = gdm_scrollable_widget_destroy;

        widget_class->get_preferred_width = gdm_scrollable_widget_get_preferred_width;
        widget_class->get_preferred_height = gdm_scrollable_widget_get_preferred_height;
        widget_class->size_allocate = gdm_scrollable_widget_size_allocate;
        widget_class->draw = gdm_scrollable_widget_draw;
        widget_class->scroll_event = gdm_scrollable_widget_scroll_event;

        container_class->add = gdm_scrollable_widget_add;
        container_class->remove = gdm_scrollable_widget_remove;
        container_class->forall = gdm_scrollable_widget_forall;
        gtk_container_class_handle_border_width (container_class);

        signals[SCROLL_CHILD] =
          g_signal_new ("scroll-child",
                        G_TYPE_FROM_CLASS (object_class),
                        G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                        G_STRUCT_OFFSET (GtkScrolledWindowClass, scroll_child),
                        NULL, NULL,
                        g_cclosure_marshal_VOID__ENUM,
                        G_TYPE_BOOLEAN, 1,
                        GTK_TYPE_SCROLL_TYPE);
        signals[MOVE_FOCUS_OUT] =
          g_signal_new ("move-focus-out",
                        G_TYPE_FROM_CLASS (object_class),
                        G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                        G_STRUCT_OFFSET (GtkScrolledWindowClass, move_focus_out),
                        NULL, NULL,
                        g_cclosure_marshal_VOID__ENUM,
                        G_TYPE_NONE, 1,
                        GTK_TYPE_DIRECTION_TYPE);
        gdm_scrollable_widget_class_install_bindings (klass);

        g_type_class_add_private (klass, sizeof (GdmScrollableWidgetPrivate));
}

static void
gdm_scrollable_widget_add_scrollbar (GdmScrollableWidget *widget)
{
        gtk_widget_push_composite_child ();
        widget->priv->scrollbar = gtk_vscrollbar_new (NULL);
        g_object_set (widget->priv->scrollbar, "expand", TRUE, NULL);
        gtk_widget_set_composite_name (widget->priv->scrollbar, "scrollbar");
        gtk_widget_pop_composite_child ();
        gtk_widget_set_parent (widget->priv->scrollbar, GTK_WIDGET (widget));
        g_object_ref (widget->priv->scrollbar);
}

static void
gdm_scrollable_widget_add_invisible_event_sink (GdmScrollableWidget *widget)
{
        widget->priv->invisible_event_sink =
            gtk_invisible_new_for_screen (gtk_widget_get_screen (GTK_WIDGET (widget)));
        gtk_widget_show (widget->priv->invisible_event_sink);

        widget->priv->key_event_queue = g_queue_new ();
}

static void
gdm_scrollable_widget_adopt_style_from_scrolled_window_class (GdmScrollableWidget *widget)
{
        GtkStyleContext *context;
        GtkWidgetPath   *path;

        context = gtk_widget_get_style_context (GTK_WIDGET (widget));

        gtk_style_context_add_class (context, GTK_STYLE_CLASS_FRAME);

        path = gtk_widget_path_new ();
        gtk_widget_path_append_type (path, GTK_TYPE_SCROLLED_WINDOW);
        gtk_style_context_set_path (context, path);
        gtk_widget_path_free (path);
}

static void
gdm_scrollable_widget_init (GdmScrollableWidget *widget)
{
        widget->priv = GDM_SCROLLABLE_WIDGET_GET_PRIVATE (widget);

        widget->priv->forced_height = -1;

        gdm_scrollable_widget_add_scrollbar (widget);
        gdm_scrollable_widget_add_invisible_event_sink (widget);

        gdm_scrollable_widget_adopt_style_from_scrolled_window_class (widget);
        g_signal_connect (G_OBJECT (widget),
                          "style-updated",
                          G_CALLBACK (gdm_scrollable_widget_adopt_style_from_scrolled_window_class),
                          NULL);
}

GtkWidget *
gdm_scrollable_widget_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_SCROLLABLE_WIDGET, NULL);

        return GTK_WIDGET (object);
}

static gboolean
gdm_scrollable_widget_animations_are_disabled (GdmScrollableWidget *scrollable_widget)
{
        GtkSettings *settings;
        gboolean     animations_are_enabled;

        settings = gtk_settings_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (scrollable_widget)));
        g_object_get (settings, "gtk-enable-animations", &animations_are_enabled, NULL);

        return animations_are_enabled == FALSE;
}

void
gdm_scrollable_widget_stop_sliding (GdmScrollableWidget *scrollable_widget)
{
        g_return_if_fail (GDM_IS_SCROLLABLE_WIDGET (scrollable_widget));

        if (scrollable_widget->priv->animation != NULL) {
                gdm_scrollable_widget_animation_stop (scrollable_widget->priv->animation);
        }

        g_assert (scrollable_widget->priv->animation == NULL);
}

void
gdm_scrollable_widget_slide_to_height (GdmScrollableWidget *scrollable_widget,
                                       int                  height,
                                       GdmScrollableWidgetSlideStepFunc step_func,
                                       gpointer             step_user_data,
                                       GdmScrollableWidgetSlideDoneFunc done_func,
                                       gpointer             done_user_data)
{
        GtkWidget *widget;
        gboolean   input_redirected;
        GtkAllocation widget_allocation;
        GtkStyleContext *context;
        GtkStateFlags state;
        GtkBorder padding, border;

        g_return_if_fail (GDM_IS_SCROLLABLE_WIDGET (scrollable_widget));
        widget = GTK_WIDGET (scrollable_widget);

        gdm_scrollable_widget_stop_sliding (scrollable_widget);

        input_redirected = gdm_scrollable_redirect_input_to_event_sink (scrollable_widget);

        if (!input_redirected || gdm_scrollable_widget_animations_are_disabled (scrollable_widget)) {
                scrollable_widget->priv->forced_height = height;

                if (step_func != NULL) {
                        step_func (scrollable_widget, 0.0, &height, step_user_data);
                }

                if (done_func != NULL) {
                        done_func (scrollable_widget, done_user_data);
                }

                if (input_redirected) {
                        gdm_scrollable_unredirect_input (scrollable_widget);
                }

                return;
        }

        context = gtk_widget_get_style_context (widget);
        state = gtk_widget_get_state_flags (widget);

        gtk_style_context_get_padding (context, state, &padding);
        gtk_style_context_get_border (context, state, &border);

        height += padding.top + padding.bottom;
        height += border.top + border.bottom;

        gtk_widget_get_allocation (widget, &widget_allocation);

        scrollable_widget->priv->animation =
            gdm_scrollable_widget_animation_new (widget,
                                                 widget_allocation.height,
                                                 height, step_func, step_user_data,
                                                 done_func, done_user_data);

        gdm_scrollable_widget_animation_start (scrollable_widget->priv->animation);
}

gboolean
gdm_scrollable_widget_has_queued_key_events (GdmScrollableWidget *widget)
{
        g_return_val_if_fail (GDM_IS_SCROLLABLE_WIDGET (widget), FALSE);

        return !g_queue_is_empty (widget->priv->key_event_queue);
}

void
gdm_scrollable_widget_replay_queued_key_events (GdmScrollableWidget *widget)
{
        GtkWidget *toplevel;
        GdkEvent  *event;

        toplevel = gtk_widget_get_toplevel (GTK_WIDGET (widget));

        while ((event = g_queue_pop_head (widget->priv->key_event_queue)) != NULL) {
                gtk_propagate_event (toplevel, event);
        }
}
