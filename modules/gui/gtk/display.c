/*****************************************************************************
 * display.c: Gtk+ tools for main interface
 *****************************************************************************
 * Copyright (C) 1999, 2000 VideoLAN
 * $Id: display.c,v 1.2 2002/08/08 22:28:22 sam Exp $
 *
 * Authors: Samuel Hocevar <sam@zoy.org>
 *          St�phane Borel <stef@via.ecp.fr>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <errno.h>                                                 /* ENOMEM */
#include <stdlib.h>                                                /* free() */
#include <string.h>                                            /* strerror() */
#include <stdio.h>

#include <vlc/vlc.h>
#include <vlc/intf.h>

#ifdef MODULE_NAME_IS_gnome
#   include <gnome.h>
#else
#   include <gtk/gtk.h>
#endif

#include "gtk_callbacks.h"
#include "gtk_interface.h"
#include "gtk_support.h"

#include "menu.h"
#include "display.h"
#include "common.h"

/*****************************************************************************
 * GtkDisplayDate: display stream date
 *****************************************************************************
 * This function displays the current date related to the position in
 * the stream. It is called whenever the slider changes its value.
 * The lock has to be taken before you call the function.
 *****************************************************************************/
void GtkDisplayDate( GtkAdjustment *p_adj )
{
    intf_thread_t *p_intf;

    p_intf = gtk_object_get_data( GTK_OBJECT( p_adj ), "p_intf" );

    if( p_intf->p_sys->p_input )
    {
#define p_area p_intf->p_sys->p_input->stream.p_selected_area
        char psz_time[ OFFSETTOTIME_MAX_SIZE ];

        gtk_frame_set_label( GTK_FRAME( p_intf->p_sys->p_slider_frame ),
                        input_OffsetToTime( p_intf->p_sys->p_input, psz_time,
                                   ( p_area->i_size * p_adj->value ) / 100 ) );
#undef p_area
     }
}


/*****************************************************************************
 * GtkModeManage: actualise the aspect of the interface whenever the input
 *                changes.
 *****************************************************************************
 * The lock has to be taken before you call the function.
 *****************************************************************************/
gint GtkModeManage( intf_thread_t * p_intf )
{
    GtkWidget *     p_dvd_box;
    GtkWidget *     p_file_box;
    GtkWidget *     p_network_box;
    GtkWidget *     p_slider;
    GtkWidget *     p_label;
    GtkWidget *     p_channel;
    vlc_bool_t      b_control;

#define GETWIDGET( ptr, name ) GTK_WIDGET( gtk_object_get_data( GTK_OBJECT( \
                           p_intf->p_sys->ptr ) , ( name ) ) )
    /* hide all boxes except default file box */
    p_file_box = GTK_WIDGET( gtk_object_get_data( GTK_OBJECT(
                 p_intf->p_sys->p_window ), "file_box" ) );
    gtk_widget_hide( GTK_WIDGET( p_file_box ) );

    p_network_box = GTK_WIDGET( gtk_object_get_data( GTK_OBJECT(
                 p_intf->p_sys->p_window ), "network_box" ) );
    gtk_widget_hide( GTK_WIDGET( p_network_box ) );

    p_dvd_box = GTK_WIDGET( gtk_object_get_data( GTK_OBJECT(
                 p_intf->p_sys->p_window ), "dvd_box" ) );
    gtk_widget_hide( GTK_WIDGET( p_dvd_box ) );

    /* hide slider */
    p_slider = GTK_WIDGET( gtk_object_get_data( GTK_OBJECT(
                           p_intf->p_sys->p_window ), "slider_frame" ) );
    gtk_widget_hide( GTK_WIDGET( p_slider ) );

    /* controls unavailable */
    b_control = 0;

    /* show the box related to current input mode */
    if( p_intf->p_sys->p_input )
    {
        switch( p_intf->p_sys->p_input->stream.i_method & 0xf0 )
        {
            case INPUT_METHOD_FILE:
                gtk_widget_show( GTK_WIDGET( p_file_box ) );
                p_label = gtk_object_get_data( GTK_OBJECT(
                            p_intf->p_sys->p_window ),
                            "label_status" );
                gtk_label_set_text( GTK_LABEL( p_label ),
                                    p_intf->p_sys->p_input->psz_source );
                break;
            case INPUT_METHOD_DISC:
                gtk_widget_show( GTK_WIDGET( p_dvd_box ) );
                break;
            case INPUT_METHOD_NETWORK:
                gtk_widget_show( GTK_WIDGET( p_network_box ) );
                p_label = gtk_object_get_data( GTK_OBJECT(
                            p_intf->p_sys->p_window ),
                            "network_address_label" );
                gtk_label_set_text( GTK_LABEL( p_label ),
                                    p_intf->p_sys->p_input->psz_source );
                p_channel = GTK_WIDGET( gtk_object_get_data( GTK_OBJECT(
                           p_intf->p_sys->p_window ), "network_channel_box" ) );
                if( config_GetInt( p_intf, "network-channel" ) )
                {
                    gtk_widget_show( GTK_WIDGET( p_channel ) );
                }
                else
                {
                    gtk_widget_hide( GTK_WIDGET( p_channel ) );
                }

                break;
            default:
                msg_Warn( p_intf, "cannot determine input method" );
                gtk_widget_show( GTK_WIDGET( p_file_box ) );
                p_label = gtk_object_get_data( GTK_OBJECT(
                            p_intf->p_sys->p_window ),
                            "label_status" );
                gtk_label_set_text( GTK_LABEL( p_label ),
                                    p_intf->p_sys->p_input->psz_source );
                break;
        }

        /* initialize and show slider for seekable streams */
        if( p_intf->p_sys->p_input->stream.b_seekable )
        {
            p_intf->p_sys->p_adj->value = p_intf->p_sys->f_adj_oldvalue = 0;
            gtk_signal_emit_by_name( GTK_OBJECT( p_intf->p_sys->p_adj ),
                                     "value_changed" );
            gtk_widget_show( GTK_WIDGET( p_slider ) );
        }

        /* control buttons for free pace streams */
        b_control = p_intf->p_sys->p_input->stream.b_pace_control;

        /* get ready for menu regeneration */
        p_intf->p_sys->b_program_update = 1;
        p_intf->p_sys->b_title_update = 1;
        p_intf->p_sys->b_chapter_update = 1;
        p_intf->p_sys->b_audio_update = 1;
        p_intf->p_sys->b_spu_update = 1;
        p_intf->p_sys->i_part = 0;
    
        p_intf->p_sys->p_input->stream.b_changed = 0;
        msg_Dbg( p_intf, "stream has changed, refreshing interface" );
    }
    else
    {
        if( config_GetInt( p_intf, "network-channel" ) )
        {
            gtk_widget_show( GTK_WIDGET( p_network_box ) );

            p_channel = GTK_WIDGET( gtk_object_get_data( GTK_OBJECT(
                       p_intf->p_sys->p_window ), "network_channel_box" ) );
            gtk_widget_show( GTK_WIDGET( p_channel ) );
        }
        else
        {
            /* default mode */
            p_label = gtk_object_get_data(
                    GTK_OBJECT( p_intf->p_sys->p_window ), "label_status" );
            gtk_label_set_text( GTK_LABEL( p_label ), "" );
            gtk_widget_show( GTK_WIDGET( p_file_box ) );
        }

        /* unsensitize menus */
        gtk_widget_set_sensitive( GETWIDGET(p_window,"menubar_program"),
                                  FALSE );
        gtk_widget_set_sensitive( GETWIDGET(p_window,"menubar_title"), FALSE );
        gtk_widget_set_sensitive( GETWIDGET(p_window,"menubar_chapter"),
                                  FALSE );
        gtk_widget_set_sensitive( GETWIDGET(p_window,"menubar_audio"), FALSE );
        gtk_widget_set_sensitive( GETWIDGET(p_window,"menubar_subpictures"),
                                  FALSE );
        gtk_widget_set_sensitive( GETWIDGET(p_popup,"popup_navigation"),
                                  FALSE );
        gtk_widget_set_sensitive( GETWIDGET(p_popup,"popup_audio"), FALSE );
        gtk_widget_set_sensitive( GETWIDGET(p_popup,"popup_subpictures"),
                                  FALSE );
    }

    /* set control items */
    gtk_widget_set_sensitive( GETWIDGET(p_window, "toolbar_back"), FALSE );
    gtk_widget_set_sensitive( GETWIDGET(p_window, "toolbar_eject"), !b_control);
    gtk_widget_set_sensitive( GETWIDGET(p_window, "toolbar_pause"), b_control );
    gtk_widget_set_sensitive( GETWIDGET(p_window, "toolbar_slow"), b_control );
    gtk_widget_set_sensitive( GETWIDGET(p_window, "toolbar_fast"), b_control );
    gtk_widget_set_sensitive( GETWIDGET(p_popup, "popup_back"), FALSE );
    gtk_widget_set_sensitive( GETWIDGET(p_popup, "popup_pause"), b_control );
    gtk_widget_set_sensitive( GETWIDGET(p_popup, "popup_slow"), b_control );
    gtk_widget_set_sensitive( GETWIDGET(p_popup, "popup_fast"), b_control );

#undef GETWIDGET
    return TRUE;
}

/*****************************************************************************
 * GtkHideTooltips: show or hide the tooltips depending on the configuration
 *                  option gnome-tooltips
 *****************************************************************************/
void GtkHideTooltips( vlc_object_t *p_this )
{
    intf_thread_t *p_intf = vlc_object_find( p_this, VLC_OBJECT_INTF,
                                                     FIND_ANYWHERE );

    if( !p_intf )
    {
        return;
    }

    if( config_GetInt( p_this, "gnome-tooltips" ) )
    {
        gtk_tooltips_enable( p_intf->p_sys->p_tooltips );
    }
    else
    {
        gtk_tooltips_disable( p_intf->p_sys->p_tooltips );
    }

    vlc_object_release( p_intf );
}

#ifdef MODULE_NAME_IS_gnome
/*****************************************************************************
 * GtkHideToolbartext: show or hide the tooltips depending on the
 *                     configuration option gnome-toolbartext
 *****************************************************************************
 * FIXME: GNOME only because of missing icons in gtk interface
 *****************************************************************************/
void GtkHideToolbarText( vlc_object_t *p_this )
{
    GtkToolbarStyle style;
    GtkToolbar * p_toolbar;

    intf_thread_t *p_intf = vlc_object_find( p_this, VLC_OBJECT_INTF,
                                                     FIND_ANYWHERE );

    if( !p_intf )
    {
        return;
    }

    style = config_GetInt( p_this, "gnome-toolbartext" )
            ? GTK_TOOLBAR_BOTH
            : GTK_TOOLBAR_ICONS;

    p_toolbar = GTK_TOOLBAR(lookup_widget( p_intf->p_sys->p_window,
                                           "toolbar" ));
    gtk_toolbar_set_style( p_toolbar, style );

    vlc_object_release( p_intf );
}
#endif
