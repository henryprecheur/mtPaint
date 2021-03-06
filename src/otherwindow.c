/*	otherwindow.c
	Copyright (C) 2004-2010 Mark Tyler and Dmitry Groshev

	This file is part of mtPaint.

	mtPaint is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	mtPaint is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with mtPaint in the file COPYING.
*/

#include "global.h"

#include "mygtk.h"
#include "memory.h"
#include "otherwindow.h"
#include "png.h"
#include "mainwindow.h"
#include "viewer.h"
#include "inifile.h"
#include "canvas.h"
#include "layer.h"
#include "wu.h"
#include "ani.h"
#include "channels.h"
#include "toolbar.h"
#include "csel.h"
#include "font.h"
#include "cpick.h"
#include "icons.h"

///	NEW IMAGE WINDOW

int im_type, new_window_type = 0;
GtkWidget *new_window;
GtkWidget *spinbutton_height, *spinbutton_width, *spinbutton_cols, *tog_undo;


gint delete_new( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	gtk_widget_destroy(new_window);

	return FALSE;
}

void reset_tools()
{
	if (!mem_img[mem_channel]) mem_channel = CHN_IMAGE; // Safety first
	pressed_select(FALSE); // To prevent automatic paste
	change_to_tool(DEFAULT_TOOL_ICON);

	init_istate(&mem_state, &mem_image);
	memset(channel_col_A, 255, NUM_CHANNELS);
	memset(channel_col_B, 0, NUM_CHANNELS);
	tool_opacity = 255;		// Set opacity to 100% to start with

	if (inifile_get_gboolean("zoomToggle", FALSE))
		can_zoom = 1;		// Always start at 100%

	update_stuff(UPD_RESET | UPD_ALL);
}

void do_new_chores(int undo)
{
	set_new_filename(layer_selected, NULL);
	if (layers_total) layers_notify_changed();

	// No reason to reset tools in undoable mode
	if (!undo) reset_tools();
	else update_stuff(UPD_ALL);
}

int do_new_one(int nw, int nh, int nc, png_color *pal, int bpp, int undo)
{
	int res = 0;

	nw = nw < MIN_WIDTH ? MIN_WIDTH : nw > MAX_WIDTH ? MAX_WIDTH : nw;
	nh = nh < MIN_HEIGHT ? MIN_HEIGHT : nh > MAX_HEIGHT ? MAX_HEIGHT : nh;
	mem_cols = nc < 2 ? 2 : nc > 256 ? 256 : nc;

	/* Check memory for undo */
	if (undo) undo = !undo_next_core(UC_CREATE | UC_GETMEM, nw, nh, bpp, CMASK_IMAGE);
	/* Create undo frame if requested */
	if (undo)
	{
		undo_next_core(UC_DELETE, nw, nh, bpp, CMASK_ALL);
		undo = !!(mem_img[CHN_IMAGE] = calloc(1, nw * nh * bpp));
	}
	/* Create image anew if all else fails */
	if (!undo)
	{
		res = mem_new( nw, nh, bpp, CMASK_IMAGE );
		if (res) memory_errors(1);	// Not enough memory!
	}
	/* *Now* prepare and update palette */
	if (pal) mem_pal_copy(mem_pal, pal);
	else mem_bw_pal(mem_pal, 0, nc - 1);
	update_undo(&mem_image);

	do_new_chores(undo);

	return (res);
}

static int clip_to_layer(int layer)
{
	image_info *img;
	image_state *state;
	int cmask, undo = undo_load;

	cmask = cmask_from(mem_clip.img);
	if (layer == layer_selected)
	{
		if (undo) undo = !undo_next_core(UC_CREATE | UC_GETMEM,
			mem_clip_w, mem_clip_h, mem_clip_bpp, cmask);
		if (undo) undo_next_core(UC_DELETE, mem_clip_w, mem_clip_h,
			mem_clip_bpp, CMASK_ALL);
		else mem_free_image(&mem_image, FREE_IMAGE);
		img = &mem_image;
		state = &mem_state;
	}
	else
	{
		img = &layer_table[layer].image->image_;
		state = &layer_table[layer].image->state_;
		*state = mem_state;
		mem_free_image(img, FREE_IMAGE);
		mem_pal_copy(img->pal, mem_pal);
		img->cols = mem_cols;
	}
	if (!mem_alloc_image(AI_COPY, img, 0, 0, 0, 0, &mem_clip)) return (0);
	update_undo(img);
	state->channel = CHN_IMAGE;
	return (1);
}

static void create_new(GtkWidget *widget)
{
	png_color *pal = im_type == 1 ? NULL : mem_pal_def;
	int nw, nh, nc, err = 1, bpp;

	nw = read_spin(spinbutton_width);
	nh = read_spin(spinbutton_height);
	nc = read_spin(spinbutton_cols);
	if (!new_window_type) undo_load = gtk_toggle_button_get_active(
		GTK_TOGGLE_BUTTON(tog_undo));

	if (im_type == 4) /* Screenshot */
	{
#if GTK_MAJOR_VERSION == 1
		gdk_window_lower( main_window->window );
		gdk_window_lower( new_window->window );

		gdk_flush();
		handle_events();	// Wait for minimize

		sleep(1);		// Wait a second for screen to redraw
#else /* #if GTK_MAJOR_VERSION == 2 */
		gtk_window_set_transient_for( GTK_WINDOW(new_window), NULL );
		gdk_window_iconify( new_window->window );
		gdk_window_iconify( main_window->window );

		gdk_flush();
		handle_events(); 	// Wait for minimize

		g_usleep(400000);	// Wait 0.4 of a second for screen to redraw
#endif

		// Use current layer
		if (!new_window_type)
		{
			err = load_image(NULL, FS_PNG_LOAD, undo_load ?
				FT_PIXMAP | FTM_UNDO : FT_PIXMAP);
			if (err == 1)
			{
				do_new_chores(undo_load);
				notify_changed();
			}
		}
		// Add new layer
		else if (layer_add(0, 0, 1, 0, mem_pal_def, 0))
		{
			err = load_image(NULL, FS_LAYER_LOAD, FT_PIXMAP);
			if (err == 1) layer_show_new();
			else layer_delete(layers_total);
		}

#if GTK_MAJOR_VERSION == 2
		gdk_window_deiconify( main_window->window );
#endif
		gdk_window_raise( main_window->window );
	}

	if (im_type == 3) /* Clipboard */
	{
		// Use current layer
		if (!new_window_type)
		{
			err = import_clipboard(FS_PNG_LOAD);
			if ((err != 1) && mem_clipboard)
				err = clip_to_layer(layer_selected);
			if (err == 1)
			{
				do_new_chores(undo_load);
				notify_changed();
			}
		}
		// Add new layer
		else if (layer_add(0, 0, 1, 0, mem_pal_def, 0))
		{
			err = import_clipboard(FS_LAYER_LOAD);
			if ((err != 1) && mem_clipboard)
				err = clip_to_layer(layers_total);
			if (err == 1) layer_show_new();
			else layer_delete(layers_total);
		}
	}

	/* Fallthrough if error */
	if (err != 1) im_type = 0;

	/* RGB / Greyscale / Indexed */
	bpp = im_type == 0 ? 3 : 1;
	if (im_type > 2); // Successfully done above
	else if (new_window_type == 1) // Layer
		layer_new(nw, nh, bpp, nc, pal, CMASK_IMAGE);
	else // Image
	{
		/* Nothing to undo if image got deleted already */
		err = do_new_one(nw, nh, nc, pal, bpp, undo_load && mem_img[CHN_IMAGE]);
		if (err > 0)
		{
			/* System was unable to allocate memory for
			 * image, using 8x8 instead */
			nw = mem_width;
			nh = mem_height;  
		}

		inifile_set_gint32("lastnewWidth", nw );
		inifile_set_gint32("lastnewHeight", nh );
		inifile_set_gint32("lastnewCols", nc );
		inifile_set_gint32("lastnewType", im_type );
	}

	gtk_widget_destroy(new_window);
}

void generic_new_window(int type)	// 0=New image, 1=New layer
{
	char *rad_txt[] = {_("24 bit RGB"), _("Greyscale"), _("Indexed Palette"),
		_("From Clipboard"), _("Grab Screenshot")},
		*title_txt[] = {_("New Image"), _("New Layer")};
	GtkWidget *vbox1, *table1;
	int w = mem_width, h = mem_height, c = mem_cols;


	if (!type && (check_for_changes() == 1)) return;

	new_window_type = type;
	new_window = add_a_window( GTK_WINDOW_TOPLEVEL, title_txt[type], GTK_WIN_POS_CENTER, TRUE );
	vbox1 = add_vbox(new_window);

	table1 = add_a_table( 3, 2, 5, vbox1 );

	if (!type)
	{
		w = inifile_get_gint32("lastnewWidth", DEFAULT_WIDTH);
		h = inifile_get_gint32("lastnewHeight", DEFAULT_HEIGHT);
		c = inifile_get_gint32("lastnewCols", 256);
		im_type = inifile_get_gint32("lastnewType", 2);
		if ( im_type<0 || im_type>2 ) im_type = 0;
	}
	else im_type = 3 - mem_img_bpp;

	spinbutton_width = spin_to_table(table1, 0, 1, 5, w, MIN_WIDTH, MAX_WIDTH);
	spinbutton_height = spin_to_table(table1, 1, 1, 5, h, MIN_WIDTH, MAX_HEIGHT);
	spinbutton_cols = spin_to_table(table1, 2, 1, 5, c, 2, 256);

	add_to_table( _("Width"), table1, 0, 0, 5 );
	add_to_table( _("Height"), table1, 1, 0, 5 );
	add_to_table( _("Colours"), table1, 2, 0, 5 );

	xpack(vbox1, wj_radio_pack(rad_txt, 5, 0, im_type, &im_type, NULL));
	tog_undo = type ? NULL : add_a_toggle(_("Undoable"), vbox1, undo_load);

	add_hseparator( vbox1, 200, 10 );

	pack(vbox1, OK_box(5, new_window, _("Create"), GTK_SIGNAL_FUNC(create_new),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));

	gtk_window_set_transient_for( GTK_WINDOW(new_window), GTK_WINDOW(main_window) );
	gtk_widget_show (new_window);
}


///	PATTERN & BRUSH CHOOSER WINDOW

static GtkWidget *pat_window, *draw_pat;
static int pat_brush;
static unsigned char *mem_patch;

#define PAL_SLOT_SIZE 10

static unsigned char *render_color_grid(int w, int h, int cellsize, int channel)
{
	unsigned char *rgb, *tmp;
	int i, j, k, col, row;


	row = w * 3;
	rgb = calloc(1, h * row);
	if (!rgb) return (NULL);

	for (col = i = 0; i < h; i += cellsize)
	{
		tmp = rgb + i * row;
		for (j = 0; j < row; j += cellsize * 3 , col++)
		{
			if (channel == CHN_IMAGE) /* Palette as such */
			{
				if (col < mem_cols) /* Draw only existing colors */
				{
					tmp[j + 0] = mem_pal[col].red;
					tmp[j + 1] = mem_pal[col].green;
					tmp[j + 2] = mem_pal[col].blue;
				}
			}
			else if (channel >= 0) /* Utility */
			{
				k = channel_rgb[channel][0] * col;
				tmp[j + 0] = (k + (k >> 8) + 1) >> 8;
				k = channel_rgb[channel][1] * col;
				tmp[j + 1] = (k + (k >> 8) + 1) >> 8;
				k = channel_rgb[channel][2] * col;
				tmp[j + 2] = (k + (k >> 8) + 1) >> 8;
			}
			else /* Opacity */
			{
				tmp[j + 0] = tmp[j + 1] = tmp[j + 2] = col;
			}
			for (k = j + 3; k < j + cellsize * 3 - 3; k++)
				tmp[k] = tmp[k - 3];
		}
		for (j = i + 1; j < i + cellsize - 1; j++)
			memcpy(rgb + j * row, tmp, row);
	}
	return (rgb);
}

static gboolean delete_pat(GtkWidget *widget)
{
	gtk_widget_destroy(widget);
	if (pat_brush != CHOOSE_BRUSH) free(mem_patch);
	mem_patch = NULL;
	pat_window = NULL;

	return (FALSE);
}

static gboolean key_pat(GtkWidget *widget, GdkEventKey *event)
{
	/* xine-ui sends bogus keypresses so don't delete on this */
	if (!XINE_FAKERY(event->keyval)) delete_pat(widget);

	return (TRUE);
}

static gboolean click_pat(GtkWidget *widget, GdkEventButton *event)
{
	int pat_no, mx = event->x, my = event->y;


	if (pat_brush == CHOOSE_COLOR)
	{
		int ab;

		if (!(ab = event->button == 3) && (event->button != 1))
			return (FALSE); // Only left or right click
		pat_no = mx / PAL_SLOT_SIZE + 16 * (my / PAL_SLOT_SIZE);
		pat_no = pat_no < 0 ? 0 : pat_no >= mem_cols ? mem_cols - 1 : pat_no;
		mem_col_[ab] = pat_no;
		mem_col_24[ab] = mem_pal[pat_no];
		update_stuff(UPD_AB);
	}
	else if (event->button != 1) return (FALSE); // Left click only
	else if (pat_brush == CHOOSE_PATTERN)
	{
		pat_no = mx / (8 * 4 + 4) + PATTERN_GRID_W * (my / (8 * 4 + 4));
		pat_no = pat_no < 0 ? 0 : pat_no >= PATTERN_GRID_W * PATTERN_GRID_H ?
			PATTERN_GRID_W * PATTERN_GRID_H - 1 : pat_no;
		mem_tool_pat = pat_no;
		update_stuff(UPD_PAT);
	}
	else /* if (pat_brush == CHOOSE_BRUSH) */
	{
		pat_no = mx / (PATCH_WIDTH/9) + 9*( my / (PATCH_HEIGHT/9) );
		pat_no = pat_no < 0 ? 0 : pat_no > 80 ? 80 : pat_no;
		mem_set_brush(pat_no);
		change_to_tool(TTB_PAINT);
		update_stuff(UPD_BRUSH);
	}

	return (TRUE);
}

static gboolean expose_pat(GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
	int w = (int)user_data;
	gdk_draw_rgb_image( draw_pat->window, draw_pat->style->black_gc,
		event->area.x, event->area.y, event->area.width, event->area.height,
		GDK_RGB_DITHER_NONE,
		mem_patch + 3 * (event->area.x + w * event->area.y), w * 3);
	return (TRUE);
}

void choose_pattern(int typ)	// Bring up pattern chooser (0) or brush (1)
{
	int w, h;

	if (pat_window) return; // Already displayed
	pat_brush = typ;
	pat_window = add_a_window(GTK_WINDOW_POPUP, _("Pattern Chooser"),
		GTK_WIN_POS_MOUSE, TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(pat_window), 4);

	draw_pat = gtk_drawing_area_new();

	if (typ == CHOOSE_PATTERN)
	{
		mem_patch = render_patterns();
		w = PATTERN_GRID_W * (8 * 4 + 4);
		h = PATTERN_GRID_H * (8 * 4 + 4);
	}
	else if (typ == CHOOSE_BRUSH)
	{
		mem_patch = mem_brushes;
		w = PATCH_WIDTH;
		h = PATCH_HEIGHT;
	}
	else /* if (typ == CHOOSE_COLOR) */
	{
		w = h = 16 * PAL_SLOT_SIZE - 1;
		mem_patch = render_color_grid(w, w, PAL_SLOT_SIZE, CHN_IMAGE);
	}
	gtk_widget_set_usize(draw_pat, w, h);

	gtk_container_add(GTK_CONTAINER(pat_window), draw_pat);
	gtk_signal_connect(GTK_OBJECT(draw_pat), "expose_event",
		GTK_SIGNAL_FUNC(expose_pat), (gpointer)w);
	gtk_signal_connect(GTK_OBJECT(draw_pat), "button_press_event",
		GTK_SIGNAL_FUNC(click_pat), NULL);
	/* !!! Given the window is modal, this makes it closeable by button
	 * release anywhere over mtPaint's main window - WJ */
	gtk_signal_connect(GTK_OBJECT(pat_window), "button_release_event",
		GTK_SIGNAL_FUNC(delete_pat), NULL);
	gtk_signal_connect(GTK_OBJECT(pat_window), "key_press_event",
		GTK_SIGNAL_FUNC(key_pat), NULL);
	gtk_widget_set_events(draw_pat, GDK_ALL_EVENTS_MASK);

	gtk_widget_show_all(pat_window);
}



///	ADD COLOURS TO PALETTE WINDOW

static int do_add_cols(GtkWidget *spin, gpointer fdata)
{
	int i;

	i = read_spin(spin);
	if (i != mem_cols)
	{
		spot_undo(UNDO_PAL);

		if (i > mem_cols) memset(mem_pal + mem_cols, 0,
			(i - mem_cols) * sizeof(png_color));

		mem_cols = i;
		update_stuff(UPD_ADDPAL);
	}

	return TRUE;
}

void pressed_add_cols()
{
	GtkWidget *spin = add_a_spin(256, 2, 256);
	filter_window(_("Set Palette Size"), spin, do_add_cols, NULL, FALSE);
}

/* Generic code to handle UI needs of common image transform tasks */

typedef struct {
	GtkWidget *cont;
	filter_hook func;
	gpointer data;
} filter_wrap;

void run_filter(GtkWidget *widget, gpointer user_data)
{
	filter_wrap *fw = gtk_object_get_user_data(GTK_OBJECT(widget));
	if (fw->func(fw->cont, fw->data)) destroy_dialog(widget);
	update_stuff(UPD_IMG);
}

void filter_window(gchar *title, GtkWidget *content, filter_hook filt, gpointer fdata, int istool)
{
	filter_wrap *fw;
	GtkWidget *win, *vbox;
	GtkWindowPosition pos = istool && !inifile_get_gboolean("centerSettings", TRUE) ?
		GTK_WIN_POS_MOUSE : GTK_WIN_POS_CENTER;

	win = add_a_window(GTK_WINDOW_TOPLEVEL, title, pos, TRUE);
	fw = bound_malloc(win, sizeof(filter_wrap));
	gtk_object_set_user_data(GTK_OBJECT(win), (gpointer)fw);
	gtk_window_set_default_size(GTK_WINDOW(win), 300, -1);

	fw->cont = content;
	fw->func = filt;
	fw->data = fdata;

	vbox = add_vbox(win);

	add_hseparator(vbox, -2, 10);
	pack5(vbox, content);
	add_hseparator(vbox, -2, 10);

	pack(vbox, OK_box(0, win, _("Apply"), GTK_SIGNAL_FUNC(run_filter),
		_("Cancel"), GTK_SIGNAL_FUNC(destroy_dialog)));

	gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(main_window));
	gtk_widget_show(win);
}

///	BACTERIA EFFECT

static int do_bacteria(GtkWidget *spin, gpointer fdata)
{
	int i;

	i = read_spin(spin);
	spot_undo(UNDO_FILT);
	mem_bacteria(i);
	mem_undo_prepare();

	return FALSE;
}

void pressed_bacteria()
{
	GtkWidget *spin = add_a_spin(10, 1, 100);
	filter_window(_("Bacteria Effect"), spin, do_bacteria, NULL, FALSE);
}


///	SORT PALETTE COLOURS

static GtkWidget *spal_window, *spal_spins[2], *spal_rev;
int spal_mode;

static void click_spal_apply()
{
	int index1, index2, reverse;


	reverse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(spal_rev));
	inifile_set_gboolean( "palrevSort", reverse );

	index1 = read_spin(spal_spins[0]);
	index2 = read_spin(spal_spins[1]);

	if ( index1 == index2 ) return;

	spot_undo(UNDO_XPAL);
	mem_pal_sort(spal_mode, index1, index2, reverse);
	mem_undo_prepare();
	update_stuff(UPD_PAL);
}

static void click_spal_ok()
{
	click_spal_apply();
	gtk_widget_destroy(spal_window);
}

void pressed_sort_pal()
{
	char *rad_txt[] = {
		_("Hue"), _("Saturation"), _("Luminance"), _("Brightness"),
			_("Distance to A"),
		_("Red"), _("Green"), _("Blue"), _("Projection to A->B"),
			_("Frequency")};


	GtkWidget *vbox1, *hbox3, *table1;

	spal_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Sort Palette Colours"),
		GTK_WIN_POS_CENTER, TRUE );
	vbox1 = add_vbox(spal_window);

	table1 = add_a_table(2, 2, 5, vbox1);

	spal_spins[0] = spin_to_table(table1, 0, 1, 5, 0, 0, mem_cols - 1);
	spal_spins[1] = spin_to_table(table1, 1, 1, 5, mem_cols - 1, 0, mem_cols - 1);

	add_to_table( _("Start Index"), table1, 0, 0, 5 );
	add_to_table( _("End Index"), table1, 1, 0, 5 );

	table1 = pack(vbox1, wj_radio_pack(rad_txt, mem_img_bpp == 3 ? 9 : 10, 5,
		spal_mode, &spal_mode, NULL));
	gtk_container_set_border_width(GTK_CONTAINER(table1), 5);

	spal_rev = add_a_toggle(_("Reverse Order"), vbox1,
		inifile_get_gboolean("palrevSort", FALSE));

	add_hseparator( vbox1, 200, 10 );

	/* Cancel / Apply / OK */

	hbox3 = pack(vbox1, OK_box(5, spal_window, _("OK"), GTK_SIGNAL_FUNC(click_spal_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));
	OK_box_add(hbox3, _("Apply"), GTK_SIGNAL_FUNC(click_spal_apply));

	gtk_widget_show (spal_window);
}


///	BRIGHTNESS-CONTRAST-SATURATION WINDOW

#define BRCOSA_ITEMS 6
#define BRCOSA_POSTERIZE 3
#define BRCOSA_INDEX(i) (((i) == BRCOSA_POSTERIZE) && posterize_mode ? \
	BRCOSA_ITEMS : (i))

static GtkWidget *brcosa_window,
		*brcosa_view,	// Auto-preview toggle
		*brcosa_img, *brcosa_clip, *brcosa_pal,	// What is affected
		*brcosa_rgb[3],
		*brcosa_spins[BRCOSA_ITEMS],
		*brcosa_pspins[2],	// Palette limits
		*brcosa_buttons[5];

static int brcosa_values[BRCOSA_ITEMS], brcosa_pal_lim[2],
	brcosa_values_default[BRCOSA_ITEMS + 1] = {0, 0, 0, 8, 100, 0, 256};
int mem_preview, mem_preview_clip, brcosa_auto;
png_color brcosa_palette[256];

// Set 4 brcosa button as sensitive if the user has assigned changes
static void brcosa_buttons_sensitive()
{
	int i, state;

	if (!brcosa_buttons[0]) return;

	for (state = i = 0; i < BRCOSA_ITEMS; i++)
		state |= brcosa_values[i] != brcosa_values_default[BRCOSA_INDEX(i)];
	for (i = 2; i < 5; i++) gtk_widget_set_sensitive(brcosa_buttons[i], state);
}

static void click_brcosa_preview(GtkWidget *widget)
{
	int i, j, update = 0;
	int do_pal = FALSE;	// RGB palette processing

	mem_pal_copy(mem_pal, brcosa_palette);	// Get back normal palette
	if (mem_img_bpp == 3)
	{
		do_pal = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(brcosa_pal));
		// If user has just cleared toggle
		if (!do_pal && (widget == brcosa_pal)) update = UPD_PAL;
	}

	for (i = 0; i < BRCOSA_ITEMS; i++)
		mem_prev_bcsp[i] = brcosa_values[i];

	for (i = 0; i < 3; i++)
	{
		mem_brcosa_allow[i] = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(brcosa_rgb[i]));
	}

	if ((mem_img_bpp == 1) || do_pal)
	{
		j = brcosa_pal_lim[0] > brcosa_pal_lim[1];
		transform_pal(mem_pal, brcosa_palette,
			brcosa_pal_lim[j], brcosa_pal_lim[j ^ 1]);
		update |= UPD_PAL;
	}
	if (mem_img_bpp == 3) update |= UPD_RENDER;
	if (update) update_stuff(update);
}

static void brcosa_pal_lim_change()
{
	int i;

	for (i = 0; i < 2; i++)
		brcosa_pal_lim[i] = gtk_spin_button_get_value_as_int(
			GTK_SPIN_BUTTON(brcosa_pspins[i]));

	click_brcosa_preview(NULL);	// Update everything
}

static void click_brcosa_preview_toggle()
{
	if (!brcosa_buttons[0]) return;	// Traps call during initialisation

	brcosa_auto = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(brcosa_view));
	if (brcosa_auto)
	{
		click_brcosa_preview(NULL);
		gtk_widget_hide(brcosa_buttons[1]);
	}
	else gtk_widget_show(brcosa_buttons[1]);
}

static void click_brcosa_RGB_toggle(GtkToggleButton *button, gpointer user_data)
{
	*(int *)user_data = gtk_toggle_button_get_active(button);
	click_brcosa_preview(NULL);
}

static void brcosa_spinslide_moved(GtkAdjustment *adj, gpointer user_data)
{
	brcosa_values[(int)user_data] = ADJ2INT(adj);
	brcosa_buttons_sensitive();

	if (brcosa_auto) click_brcosa_preview(NULL);
}

static void delete_brcosa()
{
	// If in RGB mode this is required to disable live preview
	mem_preview = mem_preview_clip = FALSE;
	gtk_widget_destroy(brcosa_window);
}

static gboolean click_brcosa_cancel()
{
	mem_pal_copy(mem_pal, brcosa_palette);
	delete_brcosa();
	update_stuff(mem_img_bpp == 3 ? UPD_PAL | UPD_RENDER : UPD_PAL);
	return (FALSE);
}

static void click_brcosa_apply(GtkWidget *widget)
{
	unsigned char *mask = NULL, *mask0, *tmp;
	int i, j;

	mem_pal_copy(mem_pal, brcosa_palette);

	for (i = j = 0; i < BRCOSA_ITEMS; i++)
		j |= brcosa_values[i] ^ brcosa_values_default[BRCOSA_INDEX(i)];
	if (!j) return; // Nothing changed from default state

	spot_undo(UNDO_COL);

	click_brcosa_preview(NULL); // This modifies palette
	if (mem_preview && (mem_img_bpp == 3))
	{
		mask = malloc(mem_width);
		if (mask) // This modifies image
		{
			mask0 = NULL;
			if (!channel_dis[CHN_MASK]) mask0 = mem_img[CHN_MASK];
			tmp = mem_img[CHN_IMAGE];
			for (i = 0; i < mem_height; i++)
			{
				prep_mask(0, 1, mem_width, mask, mask0, tmp);
				do_transform(0, 1, mem_width, mask, tmp, tmp);
				if (mask0) mask0 += mem_width;
				tmp += mem_width * 3;
			}
			free(mask);
		}
	}
	if (mem_preview_clip && (mem_img_bpp == 3) && (mem_clip_bpp == 3))
	{
		// This modifies clipboard
		do_transform(0, 1, mem_clip_w * mem_clip_h, NULL,
			mem_clipboard, mem_clipboard);
	}
	mem_undo_prepare();
	// Disable preview for final update
	if (!widget) mem_preview = mem_preview_clip = FALSE;
	update_stuff(mask ? UPD_PAL | UPD_IMG : UPD_PAL);

	if (widget) // Don't need this when clicking OK
	{
		// Reload palette and redo preview
		mem_pal_copy(brcosa_palette, mem_pal);
		click_brcosa_preview(NULL);
	}
}

static void click_brcosa_show_toggle(GtkWidget *widget, gpointer data)
{
	int toggle = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

	inifile_set_gboolean("transcol_show", toggle);
	(toggle ? gtk_widget_show : gtk_widget_hide)(GTK_WIDGET(data));
}

/*
static void click_brcosa_store()
{
}
*/

static void click_brcosa_ok()
{
	click_brcosa_apply(NULL);
	delete_brcosa();
}

static void click_brcosa_reset()
{
	int i;

	mem_pal_copy(mem_pal, brcosa_palette);

	for (i = 0; i < BRCOSA_ITEMS; i++)
	{
		mt_spinslide_set_value(brcosa_spins[i],
			brcosa_values_default[BRCOSA_INDEX(i)]);
	}
	update_stuff(mem_img_bpp == 3 ? UPD_PAL | UPD_RENDER : UPD_PAL);
}

static void brcosa_posterize_changed(GtkMenuItem *menuitem, gpointer user_data)
{
	GtkWidget *spin = brcosa_spins[BRCOSA_POSTERIZE];
	int i, v, oldv = posterize_mode;

	posterize_mode = (int)gtk_object_get_user_data(GTK_OBJECT(menuitem));
	if (!oldv ^ !posterize_mode) // From/to bitwise
	{
		v = mt_spinslide_read_value(spin);
		if (oldv)// To bitwise
		{
			for (i = 0 , v -= 1; v ; i++ , v >>= 1);
			v = i;
		}
		else v = 1 << v; // From bitwise
		mt_spinslide_set_range(spin, oldv ? 1 : 2, oldv ? 8 : 256);
		mt_spinslide_set_value(spin, v);
	}
	else if (brcosa_auto) click_brcosa_preview(NULL);
}

void pressed_brcosa()
{
	static int mins[] = {-255, -100, -100, 1, 20, -1529, 2},
		maxs[] = {255, 100, 100, 8, 500, 1529, 256},
		order[] = {1, 2, 3, 5, 0, 4};
	char *rgb_txt[] = { _("Red"), _("Green"), _("Blue") };
	char *tab_txt[] = { _("Brightness"), _("Contrast"), _("Saturation"),
		_("Posterize"), _("Gamma"), _("Hue") };
	char *pos_txt[] = { _("Bitwise"), _("Truncated"), _("Rounded") };
	GtkWidget *vbox, *table, *table2, *hbox, *button;
	GtkAccelGroup* ag = gtk_accel_group_new();
	int i, j;


	mem_pal_copy(brcosa_palette, mem_pal);	// Remember original palette

	for (i = 0; i < BRCOSA_ITEMS; i++)
		mem_prev_bcsp[i] = brcosa_values_default[i];

	/* Enables preview_toggle code to detect an initialisation call
	 * (should not happen now, but let's play it safe) */
	brcosa_buttons[0] = NULL;

	mem_preview = TRUE;	// If in RGB mode this is required to enable live preview

	brcosa_window = add_a_window(GTK_WINDOW_TOPLEVEL, _("Transform Colour"),
		GTK_WIN_POS_MOUSE, TRUE);

	gtk_window_set_policy(GTK_WINDOW(brcosa_window), FALSE, FALSE, TRUE);
			// Automatically grow/shrink window

	vbox = add_vbox(brcosa_window);

	table2 = add_a_table(6, 2, 10, vbox );
	for (i = 0; i < BRCOSA_ITEMS; i++)
	{
		j = BRCOSA_INDEX(i);
		add_to_table(tab_txt[i], table2, order[i], 0, 0);
		brcosa_values[i] = brcosa_values_default[j];
		brcosa_spins[i] = mt_spinslide_new(255, 20);
		gtk_table_attach(GTK_TABLE(table2), brcosa_spins[i], 1, 2,
			order[i], order[i] + 1, GTK_EXPAND | GTK_FILL,
			GTK_FILL, 0, 0);
		mt_spinslide_set_range(brcosa_spins[i], mins[j], maxs[j]);
		mt_spinslide_set_value(brcosa_spins[i], brcosa_values[i]);
		mt_spinslide_connect(brcosa_spins[i],
			GTK_SIGNAL_FUNC(brcosa_spinslide_moved), (gpointer)i);
	}



///	MIDDLE SECTION

	add_hseparator(vbox, -2, 10);

	hbox = pack(vbox, gtk_hbox_new(FALSE, 0));
	gtk_widget_show(hbox);

	table = add_a_table(4, 4, 0, vbox);
	button = add_a_toggle(_("Show Detail"), hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "toggled",
		GTK_SIGNAL_FUNC(click_brcosa_show_toggle), table);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),
		inifile_get_gboolean("transcol_show", FALSE));
#if 0
	button = gtk_button_new_with_label(_("Store Values"));
	gtk_widget_show (button);
	gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 4);

	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_store), NULL);
#endif

///	OPTIONAL SECTION

	add_to_table(_("Posterize type"), table, 0, 0, 5);
	to_table_l(wj_option_menu(pos_txt, 3, posterize_mode, &posterize_mode,
		GTK_SIGNAL_FUNC(brcosa_posterize_changed)), table, 0, 1, 2, 0);

	if (mem_img_bpp == 1) add_to_table(_("Palette"), table, 2, 0, 5);
	else
	{
		brcosa_pal = sig_toggle(_("Palette"), FALSE, NULL,
			GTK_SIGNAL_FUNC(click_brcosa_preview));
		to_table(brcosa_pal, table, 2, 0, 0);

		brcosa_img = sig_toggle(_("Image"), TRUE, &mem_preview,
			GTK_SIGNAL_FUNC(click_brcosa_RGB_toggle));
		to_table(brcosa_img, table, 1, 0, 0);

		if (mem_clipboard && (mem_clip_bpp == 3))
		{
			brcosa_clip = sig_toggle(_("Clipboard"), FALSE,
				&mem_preview_clip,
				GTK_SIGNAL_FUNC(click_brcosa_RGB_toggle));
			to_table_l(brcosa_clip, table, 1, 1, 2, 0);
		}
	}
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	to_table_l(hbox, table, 2, 1, 2, 0);
	brcosa_pal_lim[0] = 0;
	brcosa_pal_lim[1] = mem_cols - 1;
	for (i = 0; i < 2; i++)
	{
		brcosa_pspins[i] = xpack(hbox,add_a_spin(brcosa_pal_lim[i],
			0, mem_cols - 1));
		spin_connect(brcosa_pspins[i],
			GTK_SIGNAL_FUNC(brcosa_pal_lim_change), NULL);
	}

	brcosa_view = sig_toggle(_("Auto-Preview"), brcosa_auto, NULL,
		GTK_SIGNAL_FUNC(click_brcosa_preview_toggle));
	to_table(brcosa_view, table, 3, 0, 0);
	for (i = 0; i < 3; i++)
	{
		brcosa_rgb[i] = sig_toggle(rgb_txt[i], TRUE, NULL,
			GTK_SIGNAL_FUNC(click_brcosa_preview));
		to_table(brcosa_rgb[i], table, 3, 1 + i, 0);
	}



///	BOTTOM AREA

	add_hseparator(vbox, -2, 10);

	hbox = pack(vbox, gtk_hbox_new(FALSE, 0));
	gtk_widget_show(hbox);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);

	button = add_a_button(_("Cancel"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_cancel), NULL);
	gtk_signal_connect(GTK_OBJECT(brcosa_window), "delete_event",
		GTK_SIGNAL_FUNC(click_brcosa_cancel), NULL);
	gtk_widget_add_accelerator(button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags)0);
	brcosa_buttons[0] = button;

	button = add_a_button(_("Preview"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_preview), NULL);
	brcosa_buttons[1] = button;

	button = add_a_button(_("Reset"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_reset), NULL);
	brcosa_buttons[2] = button;

	button = add_a_button(_("Apply"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_apply), NULL);
	brcosa_buttons[3] = button;

	button = add_a_button(_("OK"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_ok), NULL);
	gtk_widget_add_accelerator(button, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags)0);
	gtk_widget_add_accelerator(button, "clicked", ag, GDK_Return, 0, (GtkAccelFlags)0);
	brcosa_buttons[4] = button;

	click_brcosa_preview_toggle();		// Show/hide preview button
	brcosa_buttons_sensitive();		// Disable buttons
	gtk_window_set_transient_for(GTK_WINDOW(brcosa_window), GTK_WINDOW(main_window));

	gtk_widget_show(brcosa_window);
	gtk_window_add_accel_group(GTK_WINDOW(brcosa_window), ag);

#if GTK_MAJOR_VERSION == 1
	/* To make Smooth theme engine render sliders properly */
	gtk_widget_queue_resize(brcosa_window);
#endif
}


///	RESIZE/RESCALE WINDOWS

GtkWidget *sisca_window, *sisca_table;
GtkWidget *sisca_spins[6], *sisca_toggles[2], *sisca_gc;
gboolean sisca_scale;


static void sisca_moved(GtkAdjustment *adjustment, gpointer user_data)
{
	int w, h, nw, idx = (int)user_data;


	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sisca_toggles[0])))
		return;
	w = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sisca_spins[0]));
	h = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sisca_spins[1]));
	if (idx)
	{
		nw = rint(h * mem_width / (float)mem_height);
		nw = nw < 1 ? 1 : nw > MAX_WIDTH ? MAX_WIDTH : nw;
		if (nw == w) return;
	}
	else
	{
		nw = rint(w * mem_height / (float)mem_width);
		nw = nw < 1 ? 1 : nw > MAX_HEIGHT ? MAX_HEIGHT : nw;
		if (nw == h) return;
	}
	idx ^= 1; /* Other coordinate */
	gtk_spin_button_update(GTK_SPIN_BUTTON(sisca_spins[idx]));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(sisca_spins[idx]), nw);
}

static void alert_same_geometry()
{
	alert_box(_("Error"), _("New geometry is the same as now - nothing to do."), NULL);
}

static int scale_mode = 6;
static int resize_mode = 0;
static int boundary_mode = BOUND_MIRROR;
int sharper_reduce;

static void click_sisca_ok(GtkWidget *widget, gpointer user_data)
{
	int nw, nh, ox = 0, oy = 0, res = 1, scale_type = 0, gcor = FALSE;

	read_spin(sisca_spins[1]); // For aspect ratio handling
	nw = read_spin(sisca_spins[0]);
	nh = read_spin(sisca_spins[1]);
	if (!sisca_scale)
	{
		ox = read_spin(sisca_spins[2]);
		oy = read_spin(sisca_spins[3]);
	}

	if ((nw == mem_width) && (nh == mem_height) && !ox && !oy)
	{
		alert_same_geometry();
		return;
	}

	if (sisca_scale)
	{
		if (mem_img_bpp == 3)
		{
			scale_type = scale_mode;
			gcor = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sisca_gc));
		}
		res = mem_image_scale(nw, nh, scale_type, gcor, sharper_reduce,
			boundary_mode);
	}
	else res = mem_image_resize(nw, nh, ox, oy, resize_mode);

	if (!res)
	{
		update_stuff(UPD_GEOM);
		destroy_dialog(sisca_window);
	}
	else memory_errors(res);
}

void memory_errors(int type)
{
	if ( type == 1 )
		alert_box(_("Error"), _("The operating system cannot allocate the memory for this operation."), NULL);
	if ( type == 2 )
		alert_box(_("Error"), _("You have not allocated enough memory in the Preferences window for this operation."), NULL);
}

gint click_sisca_centre( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int nw = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[0]) );
	int nh = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[1]) );

	nw = (nw - mem_width) / 2;
	nh = (nh - mem_height) / 2;

	gtk_spin_button_set_value( GTK_SPIN_BUTTON(sisca_spins[2]), nw );
	gtk_spin_button_set_value( GTK_SPIN_BUTTON(sisca_spins[3]), nh );

	return FALSE;
}

static GtkWidget *filter_pack(int am, int idx, int *var)
{
	char *fnames[] = {
		_("Nearest Neighbour"),
		am ? _("Bilinear / Area Mapping") : _("Bilinear"),
		_("Bicubic"),
		_("Bicubic edged"),
		_("Bicubic better"),
		_("Bicubic sharper"),
		_("Blackman-Harris"),
		NULL
	};

	return (wj_radio_pack(fnames, -1, 0, idx, var, NULL));
}

void sisca_init( char *title )
{
	GtkWidget *button_centre, *sisca_vbox, *sisca_hbox;
	GtkWidget *wvbox, *wvbox2, *notebook, *page0, *page1, *button;

	sisca_window = add_a_window( GTK_WINDOW_TOPLEVEL, title, GTK_WIN_POS_CENTER, TRUE );
	sisca_vbox = add_vbox(sisca_window);

	sisca_table = add_a_table(3, 3, 5, sisca_vbox);

	add_to_table( _("Width     "), sisca_table, 0, 1, 0 );
	add_to_table( _("Height    "), sisca_table, 0, 2, 0 );

	add_to_table( _("Original      "), sisca_table, 1, 0, 0);
	sisca_spins[0] = spin_to_table(sisca_table, 1, 1, 5, mem_width, mem_width, mem_width);
	sisca_spins[1] = spin_to_table(sisca_table, 1, 2, 5, mem_height, mem_height, mem_height);
	GTK_WIDGET_UNSET_FLAGS (sisca_spins[0], GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS (sisca_spins[1], GTK_CAN_FOCUS);

	add_to_table( _("New"), sisca_table, 2, 0, 0 );
	sisca_spins[0] = spin_to_table(sisca_table, 2, 1, 5, mem_width, 1, MAX_WIDTH);
	sisca_spins[1] = spin_to_table(sisca_table, 2, 2, 5, mem_height, 1, MAX_HEIGHT);

	spin_connect(sisca_spins[0], GTK_SIGNAL_FUNC(sisca_moved), (gpointer)0);
	spin_connect(sisca_spins[1], GTK_SIGNAL_FUNC(sisca_moved), (gpointer)1);

	if ( !sisca_scale )
	{
		add_to_table( _("Offset"), sisca_table, 3, 0, 0 );
		sisca_spins[2] = spin_to_table(sisca_table, 3, 1, 5, 0,
			-MAX_WIDTH, MAX_WIDTH);
		sisca_spins[3] = spin_to_table(sisca_table, 3, 2, 5, 0,
			-MAX_HEIGHT, MAX_HEIGHT);

		button_centre = gtk_button_new_with_label(_("Centre"));

		gtk_widget_show(button_centre);
		gtk_table_attach (GTK_TABLE (sisca_table), button_centre, 0, 1, 4, 5,
			(GtkAttachOptions) (GTK_FILL),
			(GtkAttachOptions) (0), 5, 5);
		gtk_signal_connect(GTK_OBJECT(button_centre), "clicked",
			GTK_SIGNAL_FUNC(click_sisca_centre), NULL);
	}
	add_hseparator( sisca_vbox, -2, 10 );

	sisca_toggles[0] = sig_toggle(_("Fix Aspect Ratio"), TRUE, (gpointer)0,
		GTK_SIGNAL_FUNC(sisca_moved));

	sisca_hbox = sisca_gc = NULL;
	wvbox = page0 = sisca_vbox;

	/* Resize */
	if (!sisca_scale)
	{
		char *resize_modes[] = { _("Clear"), _("Tile"),
			_("Mirror tile"), NULL };

		sisca_hbox = wj_radio_pack(resize_modes, -1, 0, resize_mode,
			&resize_mode, NULL);
	}

	/* RGB rescale */
	else if (mem_img_bpp == 3)
	{
		char *bound_modes[] = { _("Mirror"), _("Tile"), _("Void") };

		sisca_hbox = pack(sisca_vbox, gtk_hbox_new(FALSE, 0));
		wvbox = pack(sisca_hbox, gtk_vbox_new(FALSE, 0));

		sisca_gc = pack_end(wvbox, gamma_toggle());

		notebook = pack(sisca_vbox, buttoned_book(&page0, &page1,
			&button, _("Settings")));
		wvbox2 = pack_end(sisca_hbox, gtk_vbox_new(FALSE, 0));
		pack(wvbox2, button);

		add_hseparator(page1, -2, 10);
		pack(page1, sig_toggle(_("Sharper image reduction"),
			sharper_reduce, &sharper_reduce, NULL));
		add_with_frame(page1, _("Boundary extension"), wj_radio_pack(
			bound_modes, 3, 0, boundary_mode, &boundary_mode, NULL));

		sisca_hbox = filter_pack(TRUE, scale_mode, &scale_mode);
	}

	pack(wvbox, sisca_toggles[0]);

	if (sisca_hbox)
	{
		add_hseparator(page0, -2, 10);
		xpack(page0, sisca_hbox);
	}
	add_hseparator(page0, -2, 10);

	pack(sisca_vbox, OK_box(5, sisca_window, _("OK"), GTK_SIGNAL_FUNC(click_sisca_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));

	gtk_window_set_transient_for(GTK_WINDOW(sisca_window), GTK_WINDOW(main_window));
	gtk_widget_show_all(sisca_window);
}

void pressed_scale_size(int mode)
{
	sisca_scale = mode;
	sisca_init(mode ? _("Scale Canvas") : _("Resize Canvas"));
}


///	PALETTE EDITOR WINDOW

enum {
	CHOOK_CANCEL = 0,
	CHOOK_PREVIEW,
	CHOOK_OK,
	CHOOK_SELECT,
	CHOOK_SET,
	CHOOK_UNVIEW,
	CHOOK_CHANGE
};

static chanlist ctable_;
/* "Alpha" here is in fact opacity, so is separate from channels */
static unsigned char *opctable;
static GtkWidget *allcol_window, *allcol_list;
static int allcol_idx, allcol_preview;
static colour_hook allcol_hook;


static void allcol_ok(GtkWidget *window)
{
	update_window_spin(window);
	allcol_hook(CHOOK_OK);
	gtk_widget_destroy(window);
	free(ctable_[CHN_IMAGE]);
}

static void allcol_preview_toggle(GtkToggleButton *button, GtkWidget *window)
{
	allcol_hook((allcol_preview = button->active) ? CHOOK_PREVIEW : CHOOK_UNVIEW);
}

static gboolean allcol_cancel(GtkWidget *window)
{
	allcol_hook(CHOOK_CANCEL);
	gtk_widget_destroy(window);
	free(ctable_[CHN_IMAGE]);
	return (FALSE);
}

static void color_refresh()
{
	gtk_list_select_item(GTK_LIST(allcol_list), allcol_idx);

	/* Stupid GTK+ does nothing for gtk_widget_queue_draw(allcol_list) */
	gtk_container_foreach(GTK_CONTAINER(allcol_list),
		(GtkCallback)gtk_widget_queue_draw, NULL);
}

static gboolean color_expose( GtkWidget *widget, GdkEventExpose *event, gpointer user_data )
{
	unsigned char *rgb, *lc = ctable_[CHN_IMAGE] + (int)user_data * 3;
	int x = event->area.x, y = event->area.y, w = event->area.width, h = event->area.height;
	int i, j = w * 3;

	rgb = malloc(j);
	if (rgb)
	{
		for (i = 0; i < j; i += 3)
		{
			rgb[i + 0] = lc[0];
			rgb[i + 1] = lc[1];
			rgb[i + 2] = lc[2];
		}
		gdk_draw_rgb_image(widget->window, widget->style->black_gc,
			x, y, w, h, GDK_RGB_DITHER_NONE, rgb, 0);
		free(rgb);
	}

	return FALSE;
}


static void color_set(GtkWidget *cs, gpointer user_data)
{
	unsigned char *lc;
	int idx, rgb, opacity;
	GtkWidget *widget;
	GdkColor c;

	rgb = cpick_get_colour(cs, &opacity);
	widget = GTK_WIDGET(gtk_object_get_user_data(GTK_OBJECT(cs)));

	idx = (int)gtk_object_get_user_data(GTK_OBJECT(widget));
	lc = ctable_[CHN_IMAGE] + idx * 3;
	lc[0] = INT_2_R(rgb);
	lc[1] = INT_2_G(rgb);
	lc[2] = INT_2_B(rgb);
	opctable[idx] = opacity;
	c.pixel = 0;
	c.red = lc[0] * 257; c.green = lc[1] * 257; c.blue = lc[2] * 257;
	gdk_colormap_alloc_color( gdk_colormap_get_system(), &c, FALSE, TRUE );
	gtk_widget_queue_draw(widget);
	allcol_hook(CHOOK_SET);
}

static void color_select( GtkList *list, GtkWidget *widget, gpointer user_data )
{
	unsigned char *lc;
	int rgb, idx = (int)gtk_object_get_user_data(GTK_OBJECT(widget));
	GtkWidget *cs = GTK_WIDGET(user_data);

	gtk_object_set_user_data( GTK_OBJECT(cs), widget );
	gtk_signal_handler_block_by_func( GTK_OBJECT(cs), GTK_SIGNAL_FUNC(color_set), NULL );

	lc = ctable_[CHN_IMAGE] + idx * 3;
	rgb = MEM_2_INT(lc, 0);
	cpick_set_colour_previous(cs, rgb, opctable[idx]);
	cpick_set_colour(cs, rgb, opctable[idx]);
	allcol_idx = idx;

	gtk_signal_handler_unblock_by_func( GTK_OBJECT(cs), GTK_SIGNAL_FUNC(color_set), NULL );
	allcol_hook(CHOOK_SELECT);
}

static void colour_window(GtkWidget *win, GtkWidget *extbox, int cnt, int idx,
	char **cnames, int alpha, colour_hook chook, GtkSignalFunc lhook)
{
	GtkWidget *vbox, *hbox, *hbut;
	GtkWidget *col_list, *l_item, *hbox2, *label, *drw, *swindow, *viewport;
	GtkWidget *cs;
	GtkAdjustment *adj;
	char txt[64], *tmp = txt;
	int i;


	if (idx >= cnt) idx = 0;
	cs = cpick_create();
	cpick_set_opacity_visibility( cs, alpha );

	gtk_signal_connect( GTK_OBJECT(cs), "color_changed", GTK_SIGNAL_FUNC(color_set), NULL );

	allcol_window = win;
	allcol_hook = chook;
	allcol_preview = FALSE;

	swindow = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_show(swindow);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swindow),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

	viewport = gtk_viewport_new (NULL, NULL);
	gtk_widget_show(viewport);
	gtk_container_add (GTK_CONTAINER (swindow), viewport);

	allcol_idx = idx;
	allcol_list = col_list = gtk_list_new();
	gtk_container_add ( GTK_CONTAINER(viewport), col_list );
	gtk_widget_show( col_list );

	for (i = 0; i < cnt; i++)
	{
		l_item = gtk_list_item_new();
		gtk_object_set_user_data(GTK_OBJECT(l_item), (gpointer)i);
		if (lhook) gtk_signal_connect(GTK_OBJECT(l_item),
			"button_press_event", lhook, (gpointer)i);
		gtk_container_add( GTK_CONTAINER(col_list), l_item );
		gtk_widget_show( l_item );

		hbox2 = gtk_hbox_new( FALSE, 3 );
		gtk_widget_show( hbox2 );
		gtk_container_set_border_width( GTK_CONTAINER(hbox2), 3 );
		gtk_container_add( GTK_CONTAINER(l_item), hbox2 );

		drw = pack(hbox2, gtk_drawing_area_new());
		gtk_drawing_area_size( GTK_DRAWING_AREA(drw), 20, 20 );
		gtk_signal_connect(GTK_OBJECT(drw), "expose_event",
			GTK_SIGNAL_FUNC(color_expose), (gpointer)i);
		gtk_widget_show(drw);

		if (cnames) tmp = cnames[i];
		else sprintf(txt, "%i", i);
		label = xpack(hbox2, gtk_label_new(tmp));
		gtk_widget_show( label );
		gtk_misc_set_alignment( GTK_MISC(label), 0.0, 1.0 );
	}
	gtk_list_set_selection_mode(GTK_LIST(col_list), GTK_SELECTION_BROWSE);
	/* gtk_list_select_*() don't work in GTK_SELECTION_BROWSE mode */
	gtk_container_set_focus_child(GTK_CONTAINER(col_list),
		GTK_WIDGET(g_list_nth(GTK_LIST(col_list)->children, idx)->data));
	gtk_signal_connect(GTK_OBJECT(col_list), "select_child",
		GTK_SIGNAL_FUNC(color_select), cs);

	/* Make listbox top-to-bottom when it's useful or just not harmful */
	if ((cnt > 6) || !extbox)
	{
		hbox = gtk_hbox_new(FALSE, 5);
		gtk_container_add( GTK_CONTAINER(allcol_window), hbox );
		pack(hbox, swindow);
		vbox = xpack(hbox, gtk_vbox_new(FALSE, 5));
		gtk_container_set_border_width( GTK_CONTAINER(vbox), 5 );
		gtk_box_pack_start (GTK_BOX (vbox), cs, TRUE, FALSE, 0);
	}
	else
	{
		vbox = gtk_vbox_new( FALSE, 5 );
		gtk_container_set_border_width( GTK_CONTAINER(vbox), 5 );
		gtk_container_add( GTK_CONTAINER(allcol_window), vbox );
		hbox = xpack(vbox, gtk_hbox_new(FALSE, 10));
		pack(hbox, swindow);
		xpack(hbox, cs);
	}
	gtk_widget_show(vbox);
	gtk_widget_show(hbox);

	if (extbox) pack(vbox, extbox);

	hbut = OK_box(0, allcol_window, _("OK"), GTK_SIGNAL_FUNC(allcol_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(allcol_cancel));
	OK_box_add_toggle(hbut, _("Preview"), GTK_SIGNAL_FUNC(allcol_preview_toggle));

	hbox = pack(vbox, gtk_hbox_new(FALSE, 0));
	gtk_widget_show(hbox);
	pack_end(hbox, widget_align_minsize(hbut, 260, -1));

	gtk_widget_show( cs );
	gtk_window_set_transient_for( GTK_WINDOW(allcol_window), GTK_WINDOW(main_window) );
	gtk_widget_show( allcol_window );

#if GTK_MAJOR_VERSION == 1
	handle_events();
	/* GTK2 calls select handler on its own, GTK1 needs prodding */
	gtk_list_select_item(GTK_LIST(col_list), idx);
	/* Re-render sliders, adjust option menu */
	gtk_widget_queue_resize(allcol_window);
#endif

	/* Scroll list to selected item */
	adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(swindow));
	if (adj->upper > adj->page_size)
	{
		float f = adj->upper * (idx + 0.5) / cnt - adj->page_size * 0.5;
		adj->value = f < 0.0 ? 0.0 : f > adj->upper - adj->page_size ?
			adj->upper - adj->page_size : f;
		gtk_adjustment_value_changed(adj);
	}
}

static void do_allcol()
{
	int i;

	for (i = 0; i < mem_cols; i++)
	{
		mem_pal[i].red = ctable_[CHN_IMAGE][i * 3 + 0];
		mem_pal[i].green = ctable_[CHN_IMAGE][i * 3 + 1];
		mem_pal[i].blue = ctable_[CHN_IMAGE][i * 3 + 2];
	}
	update_stuff(UPD_PAL);
}

static void do_allover(int idx)
{
	unsigned char *lc = ctable_[CHN_IMAGE] + idx * 3;
	int i;

	for (i = 0; i < NUM_CHANNELS; i++)
	{
		channel_rgb[i][0] = lc[i * 3 + 0];
		channel_rgb[i][1] = lc[i * 3 + 1];
		channel_rgb[i][2] = lc[i * 3 + 2];
		channel_opacity[i] = opctable[idx + i];
	}
	update_stuff(UPD_RENDER);
}

static void do_AB(int idx)
{
	unsigned char *lc = ctable_[CHN_IMAGE] + idx * 3;
	png_color *A0, *B0;
	A0 = mem_img_bpp == 1 ? &mem_pal[mem_col_A] : &mem_col_A24;
	B0 = mem_img_bpp == 1 ? &mem_pal[mem_col_B] : &mem_col_B24;

	A0->red = lc[0]; A0->green = lc[1]; A0->blue = lc[2];
	B0->red = lc[3]; B0->green = lc[4]; B0->blue = lc[5];
	update_stuff(mem_img_bpp == 1 ? UPD_PAL : UPD_AB);
}

static void set_csel()
{
	unsigned char *lc = ctable_[CHN_IMAGE];
	csel_data->center = RGB_2_INT(lc[0], lc[1], lc[2]);
	csel_data->center_a = ctable_[CHN_ALPHA][0];
	csel_data->limit = RGB_2_INT(lc[3], lc[4], lc[5]);
	csel_data->limit_a = ctable_[CHN_ALPHA][1];
	csel_preview = RGB_2_INT(lc[6], lc[7], lc[8]);
/* !!! Alpha is disabled for now - later will be opacity !!! */
//	csel_preview_a = opctable[2];
}

static GtkWidget *range_spins[2];

static void select_colour(int what)
{
	switch (what)
	{
	case CHOOK_UNVIEW: /* Disable preview */
	case CHOOK_CANCEL: /* Cancel */
		mem_pal_copy(mem_pal, brcosa_palette);
		update_stuff(UPD_PAL);
		break;
	case CHOOK_SET: /* Set */
		if (!allcol_preview) break;
	case CHOOK_PREVIEW: /* Preview */
		do_allcol();
		break;
	case CHOOK_OK: /* OK */
		mem_pal_copy(mem_pal, brcosa_palette);
		spot_undo(UNDO_PAL);
		do_allcol();
		break;
	}
}

static gint click_colour(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	/* Middle click sets "from" */
	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 2))
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(range_spins[0]),
			(int)user_data);

	/* Right click sets "to" */
	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 3))
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(range_spins[1]),
			(int)user_data);

	/* Let click processing continue */
	return (FALSE);
}

static void set_range_spin(GtkButton *button, GtkWidget *spin)
{
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), allcol_idx);
}

static void make_cscale(GtkButton *button, gpointer user_data)
{
	int i, n, start, stop, start0, mode;
	unsigned char *c0, *c1, *lc;
	double d;

	mode = wj_option_menu_get_history(GTK_WIDGET(user_data));
	start = start0 = read_spin(range_spins[0]);
	stop = read_spin(range_spins[1]);

	if (mode <= 2) /* RGB/sRGB/HSV */
	{
		if (start > stop) { i = start; start = stop; stop = i; }
		if (stop < start + 2) return;
	}
	else if (stop == start) return; /* Gradient */

	c0 = lc = ctable_[CHN_IMAGE] + start * 3;
	c1 = ctable_[CHN_IMAGE] + stop * 3;
	d = n = stop - start;

	switch (mode)
	{
	case 0: /* RGB */
	{
		double r0, g0, b0, dr, dg, db;

		dr = ((int)c1[0] - c0[0]) / d;
		r0 = c0[0];
		dg = ((int)c1[1] - c0[1]) / d;
		g0 = c0[1];
		db = ((int)c1[2] - c0[2]) / d;
		b0 = c0[2];

		for (i = 1; i < n; i++)
		{
			lc += 3;
			lc[0] = rint(r0 + dr * i);
			lc[1] = rint(g0 + dg * i);
			lc[2] = rint(b0 + db * i);
		}
		break;
	}
	case 1: /* sRGB */
	{
		double r0, g0, b0, dr, dg, db, rr, gg, bb;

		dr = (gamma256[c1[0]] - (r0 = gamma256[c0[0]])) / d;
		dg = (gamma256[c1[1]] - (g0 = gamma256[c0[1]])) / d;
		db = (gamma256[c1[2]] - (b0 = gamma256[c0[2]])) / d;

		for (i = 1; i < n; i++)
		{
			lc += 3;
			rr = r0 + dr * i;
			lc[0] = UNGAMMA256(rr);
			gg = g0 + dg * i;
			lc[1] = UNGAMMA256(gg);
			bb = b0 + db * i;
			lc[2] = UNGAMMA256(bb);
		}
		break;
	}
	case 2: /* HSV */
	{
		int t;
		double h0, dh, s0, ds, v0, dv, hsv[6], hh, ss, vv;

		rgb2hsv(c0, hsv + 0);
		rgb2hsv(c1, hsv + 3);
		/* Grey has no hue */
		if (hsv[1] == 0.0) hsv[0] = hsv[3];
		if (hsv[4] == 0.0) hsv[3] = hsv[0];

		/* Always go from 1st to 2nd hue in ascending order */
		t = start == start0 ? 0 : 3;
		if (hsv[t] > hsv[t ^ 3]) hsv[t] -= 6.0;

		dh = (hsv[3] - hsv[0]) / d;
		h0 = hsv[0];
		ds = (hsv[4] - hsv[1]) / d;
		s0 = hsv[1];
		dv = (hsv[5] - hsv[2]) / d;
		v0 = hsv[2];

		for (i = 1; i < n; i++)
		{
			vv = v0 + dv * i;
			ss = vv - vv * (s0 + ds * i);
			hh = h0 + dh * i;
			if (hh < 0.0) hh += 6.0;
			t = hh;
			hh = (hh - t) * (vv - ss);
			if (t & 1) { vv -= hh; hh += vv; }
			else hh += ss;
			t >>= 1;
			lc += 3;
			lc[t] = rint(vv);
			lc[(t + 1) % 3] = rint(hh);
			lc[(t + 2) % 3] = rint(ss);
		}
		break;
	}
	default: /* Gradient */
	{
		int j, op, c[NUM_CHANNELS + 3];

		j = start < stop ? 1 : -1;
		for (i = 0; i != n + j; i += j , lc += j * 3)
		{
			op = grad_value(c, 0, i / d);
			/* Zero opacity - empty slot */
			if (!op) lc[0] = lc[1] = lc[2] = 0;
			else
			{
				lc[0] = (c[0] + 128) >> 8;
				lc[1] = (c[1] + 128) >> 8;
				lc[2] = (c[2] + 128) >> 8;
			}
		}
		break;
	}
	}
	color_refresh();
	select_colour(CHOOK_SET);
}

static void select_overlay(int what)
{
	char txt[64];
	int i, j;

	switch (what)
	{
	case CHOOK_UNVIEW: /* Disable preview */
	case CHOOK_CANCEL: /* Cancel */
		do_allover(NUM_CHANNELS);	// Restore original values
		break;
	case CHOOK_SET: /* Set */
		if (!allcol_preview) break;
	case CHOOK_PREVIEW: /* Preview */
		do_allover(0);
		break;
	case CHOOK_OK: /* OK */
		do_allover(0);
		for (i = 0; i < NUM_CHANNELS; i++)	// Save all settings to ini file
		{
			for (j = 0; j < 4; j++)
			{
				sprintf(txt, "overlay%i%i", i, j);
				inifile_set_gint32(txt, j < 3 ? channel_rgb[i][j] :
					channel_opacity[i]);
			}
		}
		break;
	}
}

static GtkWidget *AB_spins[NUM_CHANNELS];

static void select_AB(int what)
{
	int i;

	switch (what)
	{
	case CHOOK_UNVIEW: /* Disable preview */
	case CHOOK_CANCEL: /* Cancel */
		do_AB(2);
		break;
	case CHOOK_OK: /* OK */
		do_AB(2);
		spot_undo(UNDO_PAL);
		for (i = CHN_ALPHA; i < NUM_CHANNELS; i++)
		{
			channel_col_A[i] = ctable_[i][0];
			channel_col_B[i] = ctable_[i][1];
		}
		do_AB(0);
		break;
	case CHOOK_SET: /* Set */
		if (!allcol_preview) break;
	case CHOOK_PREVIEW: /* Preview */
		do_AB(0);
		break;
	case CHOOK_SELECT: /* Select */
		for (i = CHN_ALPHA; i < NUM_CHANNELS; i++)
		{
			mt_spinslide_set_value(AB_spins[i], ctable_[i][allcol_idx]);
		}
		break;
	}
}

static void AB_spin_moved(GtkAdjustment *adj, gpointer user_data)
{
	ctable_[(int)user_data][allcol_idx] = ADJ2INT(adj);
}

static void posterize_AB(GtkButton *button, gpointer user_data)
{
	static const int posm[8] = {0, 0xFF00, 0x5500, 0x2480, 0x1100,
				 0x0840, 0x0410, 0x0204};
	unsigned char *lc = ctable_[CHN_IMAGE];
	int i, pm, ps;

	ps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(user_data));
	inifile_set_gint32("posterizeInt", ps);
	if (ps >= 8) return;
	pm = posm[ps]; ps = 8 - ps;

	for (i = 0; i < 6; i++) lc[i] = ((lc[i] >> ps) * pm) >> 8;
	color_refresh();
	select_AB(CHOOK_SET);
}

static GtkWidget *csel_spin, *csel_toggle;
static unsigned char csel_save[CSEL_SVSIZE];
static int csel_preview0, csel_preview_a0;

static void select_csel(int what)
{
	int old_over = csel_overlay;

	switch (what)
	{
	case CHOOK_CANCEL: /* Cancel */
		memcpy(csel_data, csel_save, CSEL_SVSIZE);
		csel_preview = csel_preview0;
		csel_preview_a = csel_preview_a0;
		csel_reset(csel_data);
	case CHOOK_UNVIEW: /* Disable preview */
		csel_overlay = 0;
		if (old_over) update_stuff(UPD_RENDER);
		break;
	case CHOOK_PREVIEW: /* Preview */
		csel_overlay = 1;
	case CHOOK_CHANGE: /* Range/mode/invert controls changed */
		if (!csel_overlay) break; // No preview
	case CHOOK_OK: /* OK */
		csel_data->range = read_float_spin(csel_spin);
		csel_data->invert = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(csel_toggle));
		set_csel();
		csel_reset(csel_data);
		if (what == CHOOK_OK)
		{
			csel_overlay = 0;
			update_stuff(UPD_RENDER | UPD_MODE);
		}
		else update_stuff(UPD_RENDER);
		break;
	case CHOOK_SET: /* Set */
		set_csel();
		if (allcol_idx == 1) /* Limit color changed */
		{
			// This triggers CHOOK_CHANGE which will redraw
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(csel_spin),
				csel_eval(csel_data->mode, csel_data->center, csel_data->limit));
		}
		else if (csel_overlay)
		{
			/* Center color changed */
			if (allcol_idx == 0) csel_reset(csel_data);
			/* Else, overlay color changed */
			update_stuff(UPD_RENDER);
		}
		break;
	}
}

static void csel_controls_changed()
{
	select_csel(CHOOK_CHANGE);
}

static void csel_mode_changed(GtkToggleButton *widget, gpointer user_data)
{
	if (!gtk_toggle_button_get_active(widget)) return;
	if (csel_data->mode == (int)user_data) return;
	csel_data->mode = (int)user_data;
	set_csel();
	// This triggers CHOOK_CHANGE which will redraw
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(csel_spin),
		csel_eval(csel_data->mode, csel_data->center, csel_data->limit));
}

typedef struct {
	GtkWidget *ctoggle, *szspin;
	GtkWidget *ttoggle, *twspin, *thspin;
	int color0[GRID_MAX];
	int ctoggle0, size0;
	int ttoggle0, tw0, th0;
} grid_widgets;

static void select_grid(int what)
{
	grid_widgets *gw = gtk_object_get_user_data(GTK_OBJECT(allcol_window));
	int i;

	switch (what)
	{
	case CHOOK_UNVIEW: /* Disable preview */
	case CHOOK_CANCEL: /* Cancel */
		// Restore original values
		memcpy(grid_rgb, gw->color0, sizeof(gw->color0));
		color_grid = gw->ctoggle0;
		mem_grid_min = gw->size0;
		show_tile_grid = gw->ttoggle0;
		tgrid_dx = gw->tw0;
		tgrid_dy = gw->th0;
		break;
	case CHOOK_CHANGE: /* Grid controls changed */
	case CHOOK_SET: /* Set */
		if (!allcol_preview) return;
	case CHOOK_PREVIEW: /* Preview */
	case CHOOK_OK: /* OK */
		for (i = 0; i < GRID_MAX; i++)
		{
			unsigned char *tmp = ctable_[CHN_IMAGE] + i * 3;
			grid_rgb[i] = MEM_2_INT(tmp, 0);
		}
		if (what == CHOOK_SET) break; // Color, not controls, changed
		color_grid = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gw->ctoggle));
		mem_grid_min = read_spin(gw->szspin);
		show_tile_grid = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gw->ttoggle));
		tgrid_dx = read_spin(gw->twspin);
		tgrid_dy = read_spin(gw->thspin);
		break;
	default: return;
	}
	update_stuff(UPD_RENDER);
}

static void grid_controls_changed()
{
	select_grid(CHOOK_CHANGE);
}

static int alloc_ctable(int nslots)
{
	int i, sz = nslots * (NUM_CHANNELS + 3);
	unsigned char *mem = calloc(1, sz);
	if (!mem) return (FALSE);
	ctable_[CHN_IMAGE] = mem;
	mem += nslots * 3;
	for (i = CHN_IMAGE + 1; i < NUM_CHANNELS; i++)
	{
		ctable_[i] = mem;
		mem += nslots;
	}
	opctable = mem;
	return (TRUE);
}

void colour_selector( int cs_type )		// Bring up GTK+ colour wheel
{
	unsigned char *lc;
	GtkWidget *win, *extbox, *button, *spin, *opt;
	int i, j;

	if (cs_type >= COLSEL_EDIT_ALL)
	{
		char *fromto[] = { _("From"), _("To") };
		char *scales[] = { _("RGB"), _("sRGB"), _("HSV"), _("Gradient") };

		if (!alloc_ctable(256)) return;
		lc = ctable_[CHN_IMAGE];
		mem_pal_copy(brcosa_palette, mem_pal);	// Remember old settings
		for (i = 0; i < mem_cols; i++)
		{
			lc[i * 3 + 0] = mem_pal[i].red;
			lc[i * 3 + 1] = mem_pal[i].green;
			lc[i * 3 + 2] = mem_pal[i].blue;
			opctable[i] = 255;
		}

		/* Prepare range controls */
		extbox = gtk_hbox_new(FALSE, 0);
		pack(extbox, gtk_label_new(_("Scale")));
		for (i = 0; i < 2; i++)
		{
			button = add_a_button(fromto[i], 4, extbox, TRUE);
			spin = range_spins[i] = pack(extbox, add_a_spin(0, 0, 255));
			gtk_signal_connect(GTK_OBJECT(button), "clicked",
				GTK_SIGNAL_FUNC(set_range_spin), spin);
		}
		opt = xpack(extbox, wj_option_menu(scales, 4, 0, NULL, NULL));
		gtk_container_set_border_width(GTK_CONTAINER(opt), 4);
		button = add_a_button(_("Create"), 4, extbox, TRUE);
		gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(make_cscale), (gpointer)opt);
		gtk_widget_show_all(extbox);

		win = add_a_window(GTK_WINDOW_TOPLEVEL, _("Palette Editor"),
			GTK_WIN_POS_MOUSE, TRUE);
		colour_window(win, extbox, mem_cols, cs_type - COLSEL_EDIT_ALL,
			NULL, FALSE, select_colour, GTK_SIGNAL_FUNC(click_colour));
	}

	else if (cs_type == COLSEL_OVERLAYS)
	{
		if (!alloc_ctable(NUM_CHANNELS * 2)) return;
		lc = ctable_[CHN_IMAGE];
		for (i = 0; i < NUM_CHANNELS; i++)
		{
			lc[i * 3 + 0] = channel_rgb[i][0];
			lc[i * 3 + 1] = channel_rgb[i][1];
			lc[i * 3 + 2] = channel_rgb[i][2];
			opctable[i] = channel_opacity[i];
		}
		/* Save old values in the same array */
		memcpy(lc + NUM_CHANNELS * 3, lc, NUM_CHANNELS * 3);
		memcpy(opctable + NUM_CHANNELS, opctable, NUM_CHANNELS);

		win = add_a_window(GTK_WINDOW_TOPLEVEL, _("Configure Overlays"),
			GTK_WIN_POS_CENTER, TRUE);
		colour_window(win, NULL, NUM_CHANNELS, 0,
			allchannames, TRUE, select_overlay, NULL);
	}

	else if (cs_type == COLSEL_EDIT_AB)
	{
		static char *AB_txt[] = { "A", "B" };
		png_color *A0, *B0;
		A0 = mem_img_bpp == 1 ? &mem_pal[mem_col_A] : &mem_col_A24;
		B0 = mem_img_bpp == 1 ? &mem_pal[mem_col_B] : &mem_col_B24;

		if (!alloc_ctable(4)) return;
		lc = ctable_[CHN_IMAGE];
		lc[0] = A0->red; lc[1] = A0->green; lc[2] = A0->blue;
		lc[3] = B0->red; lc[4] = B0->green; lc[5] = B0->blue;
		/* Save previous values right here */
		memcpy(lc + 2 * 3, lc, 2 * 3);
		opctable[0] = opctable[2] = 255;
		opctable[1] = opctable[3] = 255;
		for (i = CHN_ALPHA; i < NUM_CHANNELS; i++)
		{
			ctable_[i][0] = ctable_[i][2] = channel_col_A[i];
			ctable_[i][1] = ctable_[i][3] = channel_col_B[i];
		}

		/* Prepare posterize controls */
		
		extbox = gtk_table_new(3, 3, FALSE);
		win = gtk_hbox_new(FALSE, 0);
		gtk_table_attach(GTK_TABLE(extbox), win, 0, 3, 0, 1,
			GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
		button = add_a_button(_("Posterize"), 4, win, TRUE);
		spin = pack(win, add_a_spin(inifile_get_gint32("posterizeInt", 1), 1, 8));
		gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(posterize_AB), spin);

		/* Prepare channel A/B controls */

		for (i = CHN_ALPHA; i < NUM_CHANNELS; i++)
		{
			j = i - CHN_ALPHA;
			spin = gtk_label_new(allchannames[i]);
			to_table(spin, extbox, 1, j, 0);
			gtk_misc_set_alignment(GTK_MISC(spin), 1.0 / 3.0, 0.5);
			spin = AB_spins[i] = mt_spinslide_new(-1, -1);
			gtk_table_attach(GTK_TABLE(extbox), spin, j, j + 1,
				2, 3, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
			mt_spinslide_set_range(spin, 0, 255);
			mt_spinslide_set_value(spin, channel_col_A[i]);
			mt_spinslide_connect(spin,
				GTK_SIGNAL_FUNC(AB_spin_moved), (gpointer)i);
		}
		gtk_widget_show_all(extbox);

		win = add_a_window(GTK_WINDOW_TOPLEVEL, _("Colour Editor"),
			GTK_WIN_POS_MOUSE, TRUE);
		colour_window(win, extbox, 2, 0,
			AB_txt, FALSE, select_AB, NULL);
	}

	else if (cs_type == COLSEL_EDIT_CSEL)
	{
		char *csel_txt[] = { _("Centre"), _("Limit"), _("Preview") };
		char *csel_modes[] = { _("Sphere"), _("Angle"), _("Cube"), NULL };

		if (!csel_data)
		{
			csel_init();
			if (!csel_data) return;
		}
		/* Save previous values */
		memcpy(csel_save, csel_data, CSEL_SVSIZE);
		csel_preview0 = csel_preview;
		csel_preview_a0 = csel_preview_a;

		if (!alloc_ctable(3)) return;
		lc = ctable_[CHN_IMAGE];
		lc[0] = INT_2_R(csel_data->center);
		lc[1] = INT_2_G(csel_data->center);
		lc[2] = INT_2_B(csel_data->center);
		ctable_[CHN_ALPHA][0] = csel_data->center_a;
		lc[3] = INT_2_R(csel_data->limit);
		lc[4] = INT_2_G(csel_data->limit);
		lc[5] = INT_2_B(csel_data->limit);
		ctable_[CHN_ALPHA][1] = csel_data->limit_a;
		lc[6] = INT_2_R(csel_preview);
		lc[7] = INT_2_G(csel_preview);
		lc[8] = INT_2_B(csel_preview);
		opctable[0] = opctable[1] = 255;
		opctable[2] = csel_preview_a;

		/* Prepare extra controls */
		extbox = gtk_hbox_new(FALSE, 0);
		pack(extbox, gtk_label_new(_("Range")));
		csel_spin = pack(extbox, add_float_spin(csel_data->range, 0, 765));
		csel_toggle = add_a_toggle(_("Inverse"), extbox, csel_data->invert);
		track_updates(GTK_SIGNAL_FUNC(csel_controls_changed),
			csel_spin, csel_toggle, NULL);
		pack(extbox, wj_radio_pack(csel_modes, -1, 1, csel_data->mode,
			NULL, GTK_SIGNAL_FUNC(csel_mode_changed)));
		gtk_widget_show_all(extbox);

		win = add_a_window(GTK_WINDOW_TOPLEVEL, _("Colour-Selective Mode"),
			GTK_WIN_POS_CENTER, TRUE);
		colour_window(win, extbox, 3, 0,
/* !!! Alpha ranges not implemented yet !!! */
			csel_txt, FALSE /* TRUE */, select_csel, NULL);
	}

	else if (cs_type == COLSEL_GRID)
	{
		char *grid_txt[GRID_MAX] = { _("Opaque"), _("Border"),
			_("Transparent"), _("Tile "), _("Segment") };
/* !!! "Tile " has a trailing space to be distinct from "Tile" in "Resize Canvas" */
		grid_widgets *gw;

		if (!alloc_ctable(GRID_MAX)) return;
		lc = ctable_[CHN_IMAGE];
		for (i = 0; i < GRID_MAX; i++)
		{
			lc[i * 3 + 0] = INT_2_R(grid_rgb[i]);
			lc[i * 3 + 1] = INT_2_G(grid_rgb[i]);
			lc[i * 3 + 2] = INT_2_B(grid_rgb[i]);
			opctable[i] = 255;
		}

		/* Prepare extra controls */
		extbox = gtk_table_new(6, 2, FALSE);
		gw = bound_malloc(extbox, sizeof(grid_widgets));
		gw->ctoggle = sig_toggle(_("Smart grid"), color_grid, NULL, NULL);
		to_table_l(gw->ctoggle, extbox, 0, 0, 1, 0);
		gw->ttoggle = sig_toggle(_("Tile grid"), show_tile_grid, NULL, NULL);
		to_table_l(gw->ttoggle, extbox, 0, 1, 2, 0);
		add_to_table(_("Minimum grid zoom"), extbox, 1, 0, 5);
		gw->szspin = spin_to_table(extbox, 1, 1, 0, mem_grid_min, 2, 12);
		add_to_table(_("Tile width"), extbox, 1, 2, 5);
		gw->twspin = spin_to_table(extbox, 1, 3, 0, tgrid_dx, 2, MAX_WIDTH);
		add_to_table(_("Tile height"), extbox, 1, 4, 5);
		gw->thspin = spin_to_table(extbox, 1, 5, 0, tgrid_dy, 2, MAX_HEIGHT);
		track_updates(GTK_SIGNAL_FUNC(grid_controls_changed),
			gw->ctoggle, gw->ttoggle, gw->szspin, gw->twspin, gw->thspin, NULL);
		gtk_widget_show_all(extbox);

		/* Save old values */
		memcpy(gw->color0, grid_rgb, sizeof(gw->color0));
		gw->ctoggle0 = color_grid;
		gw->size0 = mem_grid_min;
		gw->ttoggle0 = show_tile_grid;
		gw->tw0 = tgrid_dx;
		gw->th0 = tgrid_dy;

		win = add_a_window(GTK_WINDOW_TOPLEVEL, _("Configure Grid"),
			GTK_WIN_POS_CENTER, TRUE);
		gtk_object_set_user_data(GTK_OBJECT(win), gw);
		colour_window(win, extbox, GRID_MAX, 0,
			grid_txt, FALSE, select_grid, NULL);
	}
}

///	QUANTIZE WINDOW

#define QUAN_EXACT   0
#define QUAN_CURRENT 1
#define QUAN_PNN     2
#define QUAN_WU      3
#define QUAN_MAXMIN  4
#define QUAN_MAX     5

#define DITH_NONE	0
#define DITH_FS		1
#define DITH_STUCKI	2
#define DITH_ORDERED	3
#define DITH_DUMBFS	4
#define DITH_OLDDITHER	5
#define DITH_OLDSCATTER	6
#define DITH_MAX	7

static GtkWidget *quantize_window, *quantize_spin, *quantize_dither, *quantize_book;
static GtkWidget *dither_serpent, *dither_spin, *dither_err;
static int quantize_cols;

/* Quantization & dither settings - persistent */
static int quantize_mode = -1, dither_mode = -1;
static int quantize_tp;
static int dither_cspace = CSPACE_SRGB, dither_dist = DIST_L2, dither_limit;
static int dither_scan = TRUE, dither_8b, dither_sel;
static double dither_fract[2] = {1.0, 0.0};

static void click_quantize_radio(GtkWidget *widget, gpointer data)
{
	int c0 = 1, c1 = 256, n = (int)data;

	if (widget && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;

	quantize_mode = n;
	gtk_notebook_set_page(GTK_NOTEBOOK(quantize_book),
		(n == QUAN_PNN) || (n == QUAN_WU) ? 2 : n == QUAN_CURRENT ? 1 : 0);

	if (n == QUAN_EXACT) c0 = c1 = quantize_cols;
	else if (n == QUAN_CURRENT) c1 = mem_cols;
	spin_set_range(quantize_spin, c0, c1);

	/* No dither for exact conversion */
	if (quantize_dither) gtk_widget_set_sensitive(quantize_dither, n != QUAN_EXACT);
}

static void click_quantize_ok(GtkWidget *widget, gpointer data)
{
	int i, dither, new_cols, have_image = !!quantize_dither, err = 0;
	png_color newpal[256];
	unsigned char *old_image = mem_img[CHN_IMAGE];
	double efrac = 0.0;

	/* Dithering filters */
	/* Floyd-Steinberg dither */
	static short fs_dither[16] =
		{ 16,  0, 0, 0, 7, 0,  0, 3, 5, 1, 0,  0, 0, 0, 0, 0 };
	/* Stucki dither */
	static short s_dither[16] =
		{ 42,  0, 0, 0, 8, 4,  2, 4, 8, 4, 2,  1, 2, 4, 2, 1 };

	dither = quantize_mode != QUAN_EXACT ? dither_mode : DITH_NONE;
	new_cols = read_spin(quantize_spin);
	if (have_image) /* Work on image */
	{
		dither_scan = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(dither_serpent));
		dither_8b = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(dither_err));
		efrac = 0.01 * read_spin(dither_spin);
		dither_fract[dither_sel ? 1 : 0] = efrac;
	}

	gtk_widget_destroy(quantize_window);

	/* Paranoia */
	if ((quantize_mode >= QUAN_MAX) || (dither >= DITH_MAX)) return;
	if (!have_image && (quantize_mode == QUAN_CURRENT)) return;

	i = undo_next_core(UC_NOCOPY, mem_width, mem_height,
		have_image ? 1 : mem_img_bpp, have_image ? CMASK_IMAGE : CMASK_NONE);
	if (i)
	{
		memory_errors(i);
		return;
	}

	switch (quantize_mode)
	{
	case QUAN_EXACT: /* Use image colours */
		new_cols = quantize_cols;
		mem_cols_found(newpal);
		if (have_image) err = mem_convert_indexed();
		dither = DITH_MAX;
		break;
	default:
	case QUAN_CURRENT: /* Use current palette */
		break;
	case QUAN_PNN: /* PNN quantizer */
		err = pnnquan(old_image, mem_width, mem_height, new_cols, newpal);
		break;
	case QUAN_WU: /* Wu quantizer */
		err = wu_quant(old_image, mem_width, mem_height, new_cols, newpal);
		break;
	case QUAN_MAXMIN: /* Max-Min quantizer */
		err = maxminquan(old_image, mem_width, mem_height, new_cols, newpal);
		break;
	}

	if (err) dither = DITH_MAX;
	else if (quantize_mode != QUAN_CURRENT)
	{
		memcpy(mem_pal, newpal, new_cols * sizeof(*mem_pal));
		mem_cols = new_cols;
	}
	else if (quantize_tp) mem_cols = new_cols; // Truncate palette

	if (!have_image) /* Palette only */
	{
		if (err < 0) memory_errors(1);
		update_stuff(UPD_PAL | CF_MENU);
		return;
	}

	switch (dither)
	{
	case DITH_NONE:
	case DITH_FS:
	case DITH_STUCKI:
		err = mem_dither(old_image, new_cols, dither == DITH_NONE ?
			NULL : dither == DITH_FS ? fs_dither : s_dither,
			dither_cspace, dither_dist, dither_limit, dither_sel,
			dither_scan, dither_8b, efrac);
		break;

// !!! No code yet - temporarily disabled !!!
//	case DITH_ORDERED:

	case DITH_DUMBFS:
		err = mem_dumb_dither(old_image, mem_img[CHN_IMAGE],
			mem_pal, mem_width, mem_height, new_cols, TRUE);
		break;
	case DITH_OLDDITHER:
		err = mem_quantize(old_image, new_cols, 2);
		break;
	case DITH_OLDSCATTER:
		err = mem_quantize(old_image, new_cols, 3);
		break;
	case DITH_MAX: /* Stay silent unless a memory error happened */
		err = err < 0;
		break;
	}
	if (err) memory_errors(1);

	/* Image was converted */
	mem_col_A = mem_cols > 1 ? 1 : 0;
	mem_col_B = 0;
	update_stuff(UPD_2IDX);
}

static void choose_selective(GtkMenuItem *menuitem, gpointer user_data)
{
	int i = (int)gtk_object_get_user_data(GTK_OBJECT(menuitem));

	if (dither_sel == i) return;

	/* Selectivity state toggled */
	if ((dither_sel == 0) ^ (i == 0))
	{
		dither_fract[dither_sel ? 1 : 0] = 0.01 * read_spin(dither_spin);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(dither_spin),
			100.0 * dither_fract[i ? 1 : 0]);
	}
	dither_sel = i;
}

static GtkWidget *cspace_frame(int idx, gpointer var, GtkSignalFunc handler)
{
	return (add_with_frame(NULL, _("Colour space"),
		wj_radio_pack(cspnames, NUM_CSPACES, 1, idx, var, handler)));
}

static GtkWidget *difference_frame(int idx, gpointer var, GtkSignalFunc handler)
{
	char *dist_txt[] = {_("Largest (Linf)"), _("Sum (L1)"), _("Euclidean (L2)")};
	return (add_with_frame(NULL, _("Difference measure"),
		wj_radio_pack(dist_txt, 3, 1, idx, var, handler)));
}

void pressed_quantize(int palette)
{
	GtkWidget *mainbox, *topbox, *notebook, *page0, *page1, *button;
	GtkWidget *vbox, *hbox, *table, *pages[3];

	char *rad_txt[] = {_("Exact Conversion"), _("Use Current Palette"),
		_("PNN Quantize (slow, better quality)"),
		_("Wu Quantize (fast)"),
		_("Max-Min Quantize (best for small palettes and dithering)"), NULL
		};

	char *rad_txt2[] = {_("None"), _("Floyd-Steinberg"), _("Stucki"),
// !!! "Ordered" not done yet !!!
		/* _("Ordered") */ "",
		_("Floyd-Steinberg (quick)"), _("Dithered (effect)"),
		_("Scattered (effect)"), NULL
		};

	char *clamp_txt[] = {_("Gamut"), _("Weakly"), _("Strongly")};
	char *err_txt[] = {_("Off"), _("Separate/Sum"), _("Separate/Split"),
		_("Length/Sum"), _("Length/Split"), NULL};


	quantize_cols = mem_cols_used(257);

	quantize_window = add_a_window(GTK_WINDOW_TOPLEVEL, palette ?
		_("Create Quantized") : _("Convert To Indexed"),
		GTK_WIN_POS_CENTER, TRUE);
	mainbox = add_vbox(quantize_window);

	/* Colours spin */

	topbox = pack(mainbox, gtk_hbox_new(FALSE, 5));
	gtk_container_set_border_width(GTK_CONTAINER(topbox), 10);
	pack(topbox, gtk_label_new(_("Indexed Colours To Use")));
	quantize_spin = xpack(topbox, add_a_spin(quantize_cols, 1, 256));

	/* Notebook */

	if (!palette)
	{
		notebook = xpack(mainbox, buttoned_book(&page0, &page1, &button,
			_("Settings")));
		pack_end(topbox, button);
	}
	else page0 = mainbox;

	/* Main page - Palette frame */

	/* No exact transfer if too many colours */
	if (quantize_cols > 256) rad_txt[QUAN_EXACT] = "";
	if (palette) rad_txt[QUAN_CURRENT] = "";
	if ((quantize_mode < 0) || !rad_txt[quantize_mode][0]) // Use default mode
	{
		quantize_mode = palette || (quantize_cols > 256) ? QUAN_WU : QUAN_EXACT;
		if (!palette) dither_mode = -1; // Reset dither too
	}
	vbox = wj_radio_pack(rad_txt, -1, 0, quantize_mode, &quantize_mode,
		GTK_SIGNAL_FUNC(click_quantize_radio));
	/* Settings subnotebook */
	quantize_book = pack(vbox, plain_book(pages, 3));
	pack(pages[1], sig_toggle(_("Truncate palette"), quantize_tp, &quantize_tp, NULL));
	pack(pages[2], sig_toggle(_("Diameter based weighting"), quan_sqrt, &quan_sqrt, NULL));
	add_with_frame(page0, _("Palette"), vbox);

	/* Main page - Dither frame */

	quantize_dither = NULL;
	if (!palette)
	{
		if (dither_mode < 0) dither_mode = quantize_cols > 256 ?
			DITH_DUMBFS : DITH_NONE;
		hbox = wj_radio_pack(rad_txt2, -1, DITH_MAX / 2, dither_mode,
			&dither_mode, NULL);
// !!! If want to make both columns same width
//		gtk_box_set_homogeneous(GTK_BOX(hbox), TRUE);
		quantize_dither = add_with_frame(page0, _("Dither"), hbox);

		/* Settings page */

		pack(page1, cspace_frame(dither_cspace, &dither_cspace, NULL));
		pack(page1, difference_frame(dither_dist, &dither_dist, NULL));

		hbox = wj_radio_pack(clamp_txt, 3, 1, dither_limit, &dither_limit, NULL);
		add_with_frame(page1, _("Reduce colour bleed"), hbox);

		dither_serpent = add_a_toggle(_("Serpentine scan"), page1, dither_scan);

		table = add_a_table(2, 2, 5, page1);
		add_to_table(_("Error propagation, %"), table, 0, 0, 5);
		add_to_table(_("Selective error propagation"), table, 1, 0, 5);
		dither_spin = spin_to_table(table, 0, 1, 5, 100, 0, 100);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(dither_spin),
			(dither_sel ? dither_fract[1] : dither_fract[0]) * 100.0);
		hbox = wj_option_menu(err_txt, -1, dither_sel, &dither_sel,
			GTK_SIGNAL_FUNC(choose_selective));
		to_table(hbox, table, 1, 1, 0);

		dither_err = add_a_toggle(_("Full error precision"), page1, dither_8b);
	}

	/* OK / Cancel */

	pack(mainbox, OK_box(0, quantize_window, _("OK"), GTK_SIGNAL_FUNC(click_quantize_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));

	click_quantize_radio(NULL, (gpointer)quantize_mode); // Update widgets

	gtk_window_set_transient_for(GTK_WINDOW(quantize_window),
		GTK_WINDOW(main_window));
	gtk_widget_show_all(quantize_window);
}

///	GRADIENT WINDOW

static int grad_channel;
static grad_info grad_temps[NUM_CHANNELS];
static grad_map grad_tmaps[NUM_CHANNELS + 1];
static grad_store grad_tbytes;

static GtkWidget *grad_window;
static GtkWidget *grad_spin_len, *grad_spin_rep, *grad_spin_ofs;
static GtkWidget *grad_opt_type, *grad_opt_bound;
static GtkWidget *grad_ss_pre;
static GtkWidget *grad_opt_gtype, *grad_opt_otype;
static GtkWidget *grad_check_grev, *grad_check_orev;

static unsigned char grad_pad[GRAD_POINTS * 3], grad_mpad[GRAD_POINTS];
static int grad_cnt, grad_ofs, grad_slot, grad_mode;

static GtkWidget *grad_ed_cs, *grad_ed_opt, *grad_ed_ss, *grad_ed_tog, *grad_ed_pm;
static GtkWidget *grad_ed_len, *grad_ed_bar[16], *grad_ed_left, *grad_ed_right;

#define SLOT_SIZE 15

#define PPAD_SLOT 11
#define PPAD_XSZ 32
#define PPAD_YSZ 8
#define PPAD_WIDTH(X) (PPAD_XSZ * (X) - 1)
#define PPAD_HEIGHT(X) (PPAD_YSZ * (X) - 1)

static void palette_pad_set(int value)
{
	wjpixmap_move_cursor(grad_ed_pm, (value % PPAD_XSZ) * PPAD_SLOT,
		(value / PPAD_XSZ) * PPAD_SLOT);
}

static void palette_pad_draw(GtkWidget *widget, gpointer user_data)
{
	unsigned char *rgb;
	int w, h, col, cellsize = PPAD_SLOT;

	if (!wjpixmap_pixmap(widget)) return;
	w = PPAD_WIDTH(cellsize);
	h = PPAD_HEIGHT(cellsize);
	rgb = render_color_grid(w, h, cellsize, (int)user_data);
	if (!rgb) return;
	palette_pad_set(0);
	wjpixmap_draw_rgb(widget, 0, 0, w, h, rgb, w * 3);
	col = (cellsize >> 1) - 1;
	wjpixmap_set_cursor(widget, xbm_ring4_bits, xbm_ring4_mask_bits,
		xbm_ring4_width, xbm_ring4_height,
		xbm_ring4_x_hot - col, xbm_ring4_y_hot - col, TRUE);

	free(rgb);
}

static void grad_edit_set_rgb(GtkWidget *selection, gpointer user_data);
static void palette_pad_select(int i, int mode)
{
	palette_pad_set(i);
	/* Indexed / utility / opacity */
	if (!mode)
	{
		/* !!! Signal needs be sent even if value stays the same */
		GtkAdjustment *adj = SPINSLIDE_ADJUSTMENT(grad_ed_ss);
		adj->value = i;
		gtk_adjustment_value_changed(adj);
	}
	else if (i < mem_cols) /* Valid RGB */
	{
		cpick_set_colour(grad_ed_cs, PNG_2_INT(mem_pal[i]), 255);
// !!! GTK+2 emits "color_changed" when setting color, others need explicit call
#if GTK_MAJOR_VERSION == 1 || defined U_CPICK_MTPAINT
		grad_edit_set_rgb(grad_ed_cs, NULL);
#endif
	}
}

static gboolean palette_pad_click(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	int x = event->x, y = event->y;

	gtk_widget_grab_focus(widget);
	/* Only single clicks */
	if (event->type != GDK_BUTTON_PRESS) return (TRUE);
	x /= PPAD_SLOT; y /= PPAD_SLOT;
	palette_pad_select(y * PPAD_XSZ + x, (int)user_data);
	return (TRUE);
}

static gboolean palette_pad_key(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data)
{
	int x, y, dx, dy;

	if (!arrow_key(event, &dx, &dy, 0)) return (FALSE);
	wjpixmap_cursor(widget, &x, &y);
	x = x / PPAD_SLOT + dx; y = y / PPAD_SLOT + dy;
	y = y < 0 ? 0 : y >= PPAD_YSZ ? PPAD_YSZ - 1 : y;
	y = y * PPAD_XSZ + x;
	y = y < 0 ? 0 : y > 255 ? 255 : y;
	palette_pad_select(y, (int)user_data);
#if GTK_MAJOR_VERSION == 1
	/* Return value alone doesn't stop GTK1 from running other handlers */
	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
#endif
	return (TRUE);
}

static GtkWidget *grad_interp_menu(int value, int allow_const, GtkSignalFunc handler)
{
	char *interp[] = { _("RGB"), _("sRGB"), _("HSV"), _("Backward HSV"),
		_("Constant") };

	return (wj_option_menu(interp, allow_const ? 5 : 4, value, NULL, handler));
}

static void click_grad_edit_ok(GtkWidget *widget)
{
	int idx = (grad_channel == CHN_IMAGE) && (mem_img_bpp == 3) ? 0 :
		grad_channel + 1;

	if (grad_mode < 0) /* Opacity */
	{
		memcpy(grad_tbytes + GRAD_CUSTOM_OPAC(idx), grad_pad, GRAD_POINTS);
		memcpy(grad_tbytes + GRAD_CUSTOM_OMAP(idx), grad_mpad, GRAD_POINTS);
		grad_tmaps[idx].coplen = grad_cnt;
	}
	else /* Gradient */
	{
		memcpy(grad_tbytes + GRAD_CUSTOM_DATA(idx), grad_pad,
			idx ? GRAD_POINTS : GRAD_POINTS * 3);
		memcpy(grad_tbytes + GRAD_CUSTOM_DMAP(idx), grad_mpad, GRAD_POINTS);
		grad_tmaps[idx].cvslen = grad_cnt;
	}
	gtk_widget_destroy(widget);
}

static void grad_load_slot(int slot)
{
	if (slot >= grad_cnt) /* Empty slot */
	{
		grad_slot = slot;
		return;
	}
	grad_slot = -1; /* Block circular signal calls */

	if (!grad_mode) /* RGB */
	{
		unsigned char *gp = grad_pad + slot * 3;
		int rgb = MEM_2_INT(gp, 0);
		cpick_set_colour(grad_ed_cs, rgb, 255);
		cpick_set_colour_previous(grad_ed_cs, rgb, 255);

		gtk_option_menu_set_history(GTK_OPTION_MENU(grad_ed_opt),
			grad_mpad[slot]);
	}
	else /* Indexed / utility / opacity */
	{
		mt_spinslide_set_value(grad_ed_ss, grad_pad[slot]);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grad_ed_tog),
			grad_mpad[slot] == GRAD_TYPE_CONST);
		palette_pad_set(grad_pad[slot]);
	}
	grad_slot = slot;
}

static void grad_redraw_slots(int idx0, int idx1)
{
	if (idx0 < grad_ofs) idx0 = grad_ofs;
	if (++idx1 > grad_ofs + 16) idx1 = grad_ofs + 16;
	for (; idx0 < idx1; idx0++)
		gtk_widget_queue_draw(GTK_BIN(grad_ed_bar[idx0 - grad_ofs])->child);
}

static void grad_edit_set_mode()
{
	int mode, ix0 = grad_slot;

	if (grad_slot < 0) return; /* Blocked */
	if (!grad_mode) mode = wj_option_menu_get_history(grad_ed_opt);
	else mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grad_ed_tog)) ?
		GRAD_TYPE_CONST : GRAD_TYPE_RGB;
	grad_mpad[grad_slot] = mode;

	if (grad_cnt <= grad_slot)
	{
		ix0 = grad_cnt;
		grad_cnt = grad_slot + 1;
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(grad_ed_len), grad_cnt);
	}
	grad_redraw_slots(ix0, grad_slot);
}

static void grad_edit_set_rgb(GtkWidget *selection, gpointer user_data)
{
	int i, rgb;

	if (grad_slot < 0) return; /* Blocked */
	rgb = cpick_get_colour(selection, NULL);
	i = grad_slot * 3;
	grad_pad[i++] = INT_2_R(rgb);
	grad_pad[i++] = INT_2_G(rgb);
	grad_pad[i  ] = INT_2_B(rgb);
	grad_edit_set_mode();
}

static void grad_edit_move_slide(GtkAdjustment *adj, gpointer user_data)
{
	if (grad_slot < 0) return; /* Blocked */
	palette_pad_set(grad_pad[grad_slot] = ADJ2INT(adj));
	grad_edit_set_mode();
}

static void grad_edit_length(GtkAdjustment *adj, gpointer user_data)
{
	int l0 = grad_cnt;
	grad_cnt = ADJ2INT(adj);
	if (l0 < grad_cnt) grad_redraw_slots(l0, grad_cnt - 1);
	else if (l0 > grad_cnt) grad_redraw_slots(grad_cnt, l0 - 1);
}

static void grad_edit_scroll(GtkButton *button, gpointer user_data)
{
	int dir = (int)user_data;
	
	grad_ofs += dir;
	gtk_widget_set_sensitive(grad_ed_left, !!grad_ofs);
	gtk_widget_set_sensitive(grad_ed_right, grad_ofs < GRAD_POINTS - 16);
	grad_redraw_slots(0, GRAD_POINTS);
}

static void grad_edit_slot(GtkWidget *btn, gpointer user_data)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) return;
	grad_load_slot((int)user_data + grad_ofs);
}

static gboolean grad_draw_slot(GtkWidget *widget, GdkEventExpose *event,
	gpointer idx)
{
	unsigned char rgb[SLOT_SIZE * 2 * 3];
	int i, n = (int)idx + grad_ofs, mode = n >= grad_cnt ? -2 : grad_mode;

	switch (mode)
	{
	default: /* Error */
	case -2: /* Empty */
		rgb[0] = rgb[1] = rgb[2] = 178;
		break;
	case -1: /* Opacity */
		rgb[0] = rgb[1] = rgb[2] = grad_pad[n];
		break;
	case 0: /* RGB */
		rgb[0] = grad_pad[n * 3 + 0];
		rgb[1] = grad_pad[n * 3 + 1];
		rgb[2] = grad_pad[n * 3 + 2];
		break;
	case CHN_IMAGE + 1: /* Indexed */
		i = grad_pad[n];
		if (i >= mem_cols) rgb[0] = rgb[1] = rgb[2] = 0;
		else
		{
			rgb[0] = mem_pal[i].red;
			rgb[1] = mem_pal[i].green;
			rgb[2] = mem_pal[i].blue;
		}
		break;
	case CHN_ALPHA + 1: /* Alpha */
	case CHN_SEL + 1: /* Selection */
	case CHN_MASK + 1: /* Mask */
		i = channel_rgb[mode - 1][0] * grad_pad[n];
		rgb[0] = (i + (i >> 8) + 1) >> 8;
		i = channel_rgb[mode - 1][1] * grad_pad[n];
		rgb[1] = (i + (i >> 8) + 1) >> 8;
		i = channel_rgb[mode - 1][2] * grad_pad[n];
		rgb[2] = (i + (i >> 8) + 1) >> 8;
		break;
	}
	for (i = 3; i < SLOT_SIZE * 2 * 3; i++) rgb[i] = rgb[i - 3];
	if (mode == -2) /* Empty slot - show that */
		memset(rgb, 128, SLOT_SIZE * 3);

	gdk_draw_rgb_image(widget->window, widget->style->black_gc,
		0, 0, SLOT_SIZE, SLOT_SIZE, GDK_RGB_DITHER_NONE,
		rgb + SLOT_SIZE * 3, -3);

	return (TRUE);
}

static void grad_edit(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *win, *mainbox, *hbox, *hbox2, *pix, *cs, *ss, *sw, *btn;
	int i, idx, opac = (int)user_data != 0;

	idx = (grad_channel == CHN_IMAGE) && (mem_img_bpp == 3) ? 0 :
		grad_channel + 1;

	/* Copy to temp */
	if (opac)
	{
		memcpy(grad_pad, grad_tbytes + GRAD_CUSTOM_OPAC(idx), GRAD_POINTS);
		memcpy(grad_mpad, grad_tbytes + GRAD_CUSTOM_OMAP(idx), GRAD_POINTS);
		grad_cnt = grad_tmaps[idx].coplen;
	}
	else
	{
		memcpy(grad_pad, grad_tbytes + GRAD_CUSTOM_DATA(idx),
			idx ? GRAD_POINTS : GRAD_POINTS * 3);
		memcpy(grad_mpad, grad_tbytes + GRAD_CUSTOM_DMAP(idx), GRAD_POINTS);
		grad_cnt = grad_tmaps[idx].cvslen;
	}
	if (grad_cnt < 2) grad_cnt = 2;
	grad_ofs = grad_slot = 0;
	grad_mode = opac ? -1 : idx;

	win = add_a_window(GTK_WINDOW_TOPLEVEL, _("Edit Gradient"),
		GTK_WIN_POS_CENTER, TRUE);
	mainbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(mainbox), 5);
	gtk_container_add(GTK_CONTAINER(win), mainbox);

	/* Palette pad */

	pix = grad_ed_pm = pack(mainbox, wjpixmap_new(PPAD_WIDTH(PPAD_SLOT),
		PPAD_HEIGHT(PPAD_SLOT)));
	gtk_signal_connect(GTK_OBJECT(pix), "realize",
		GTK_SIGNAL_FUNC(palette_pad_draw),
		(gpointer)(opac ? -1 : grad_channel));
	gtk_signal_connect(GTK_OBJECT(pix), "button_press_event",
		GTK_SIGNAL_FUNC(palette_pad_click), (gpointer)!grad_mode);
	gtk_signal_connect(GTK_OBJECT(pix), "key_press_event",
		GTK_SIGNAL_FUNC(palette_pad_key), (gpointer)!grad_mode);
	add_hseparator(mainbox, -2, 10);

	/* Editor widgets */

	if (!grad_mode) /* RGB */
	{
		grad_ed_cs = cs = pack(mainbox, cpick_create());
		cpick_set_opacity_visibility( cs, FALSE );

		gtk_signal_connect(GTK_OBJECT(cs), "color_changed",
			GTK_SIGNAL_FUNC(grad_edit_set_rgb), NULL);
		grad_ed_opt = sw = grad_interp_menu(0, TRUE,
			GTK_SIGNAL_FUNC(grad_edit_set_mode));
	}
	else /* Indexed / utility / opacity */
	{
		grad_ed_ss = ss = pack(mainbox, mt_spinslide_new(-2, -2));
		mt_spinslide_set_range(ss, 0, grad_mode == CHN_IMAGE + 1 ?
			mem_cols - 1 : 255);
		mt_spinslide_connect(ss, GTK_SIGNAL_FUNC(grad_edit_move_slide), NULL);
		grad_ed_tog = sw = sig_toggle(_("Constant"), FALSE, NULL,
			GTK_SIGNAL_FUNC(grad_edit_set_mode));
	}
	hbox = pack(mainbox, gtk_hbox_new(TRUE, 5));
	xpack(hbox, sw);
	hbox2 = xpack(hbox, gtk_hbox_new(FALSE, 5));
	pack(hbox2, gtk_label_new(_("Points:")));
	grad_ed_len = sw = pack(hbox2, add_a_spin(grad_cnt, 2, GRAD_POINTS));
	spin_connect(sw, GTK_SIGNAL_FUNC(grad_edit_length), NULL);

	/* Gradient bar */

	hbox2 = pack(mainbox, gtk_hbox_new(TRUE, 0));
	grad_ed_left = btn = xpack(hbox2, gtk_button_new());
	gtk_container_add(GTK_CONTAINER(btn), gtk_arrow_new(GTK_ARROW_LEFT,
		GTK_SHADOW_NONE));
	gtk_widget_set_sensitive(btn, FALSE);
	gtk_signal_connect(GTK_OBJECT(btn), "clicked",
		GTK_SIGNAL_FUNC(grad_edit_scroll), (gpointer)(-1));
	btn = NULL;
	for (i = 0; i < 16; i++)
	{
		grad_ed_bar[i] = btn = xpack(hbox2, gtk_radio_button_new_from_widget(
			GTK_RADIO_BUTTON_0(btn)));
		gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(btn), FALSE);
		gtk_signal_connect(GTK_OBJECT(btn), "toggled",
			GTK_SIGNAL_FUNC(grad_edit_slot), (gpointer)i);
		sw = gtk_drawing_area_new();
		gtk_container_add(GTK_CONTAINER(btn), sw);
		gtk_widget_set_usize(sw, SLOT_SIZE, SLOT_SIZE);
		gtk_signal_connect(GTK_OBJECT(sw), "expose_event",
			GTK_SIGNAL_FUNC(grad_draw_slot), (gpointer)i);
	}
	grad_ed_right = btn = xpack(hbox2, gtk_button_new());
	gtk_container_add(GTK_CONTAINER(btn), gtk_arrow_new(GTK_ARROW_RIGHT,
		GTK_SHADOW_NONE));
	gtk_signal_connect(GTK_OBJECT(btn), "clicked",
		GTK_SIGNAL_FUNC(grad_edit_scroll), (gpointer)1);

	pack(mainbox, OK_box(0, win, _("OK"), GTK_SIGNAL_FUNC(click_grad_edit_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));

#ifndef U_CPICK_MTPAINT
	grad_load_slot(0);
#endif
	gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(grad_window));
	gtk_widget_show_all(win);
#ifdef U_CPICK_MTPAINT
	grad_load_slot(0);
#endif

#if GTK_MAJOR_VERSION == 1
	gtk_widget_queue_resize(win); /* Re-render sliders */
#endif
}

#define NUM_GTYPES 7
#define NUM_OTYPES 3
static const char gtmap[NUM_GTYPES * 2] = { GRAD_TYPE_RGB, 1, GRAD_TYPE_RGB, 2,
	GRAD_TYPE_SRGB, 2, GRAD_TYPE_HSV, 2, GRAD_TYPE_BK_HSV, 2,
	GRAD_TYPE_CONST, 3, GRAD_TYPE_CUSTOM, 3 };
static const char opmap[NUM_OTYPES] = { GRAD_TYPE_RGB, GRAD_TYPE_CONST,
	GRAD_TYPE_CUSTOM };

static void grad_reset_menu(int mode, int bpp)
{
	GList *items = GTK_MENU_SHELL(gtk_option_menu_get_menu(
		GTK_OPTION_MENU(grad_opt_gtype)))->children;
	char f = bpp == 1 ? 1 : 2;
	int i, j;

	for (j = NUM_GTYPES - 1; j >= 0; j--)
	{
		if ((gtmap[j * 2] == mode) && (gtmap[j * 2 + 1] & f)) break;
	}

	for (; items; items = items->next)
	{
		i = (int)gtk_object_get_user_data(GTK_OBJECT(items->data));
		(gtmap[2 * i + 1] & f ? gtk_widget_show : gtk_widget_hide)
			(GTK_WIDGET(items->data));
	}
	gtk_option_menu_set_history(GTK_OPTION_MENU(grad_opt_gtype), j);
}

static void store_channel_gradient(int channel)
{
	grad_info *grad = grad_temps + channel;
	grad_map *gmap = grad_tmaps + channel + 1;

	if (channel < 0) return;
	if ((channel == CHN_IMAGE) && (mem_img_bpp == 3)) gmap = grad_tmaps;

	grad->len = read_spin(grad_spin_len);
	grad->gmode = wj_option_menu_get_history(grad_opt_type) + GRAD_MODE_LINEAR;
	grad->rep = read_spin(grad_spin_rep);
	grad->rmode = wj_option_menu_get_history(grad_opt_bound);
	grad->ofs = read_spin(grad_spin_ofs);

	gmap->gtype = gtmap[2 * wj_option_menu_get_history(grad_opt_gtype)];
	gmap->grev = !!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grad_check_grev));
	gmap->otype = opmap[wj_option_menu_get_history(grad_opt_otype)];
	gmap->orev = !!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grad_check_orev));
}

static void show_channel_gradient(int channel)
{
	grad_info *grad = grad_temps + channel;
	grad_map *gmap;
	int i, idx = channel + 1, bpp = BPP(channel);

	if (bpp == 3) --idx;
	gmap = grad_tmaps + idx;

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(grad_spin_len), grad->len);
	gtk_option_menu_set_history(GTK_OPTION_MENU(grad_opt_type),
		grad->gmode - GRAD_MODE_LINEAR);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(grad_spin_rep), grad->rep);
	gtk_option_menu_set_history(GTK_OPTION_MENU(grad_opt_bound), grad->rmode);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(grad_spin_ofs), grad->ofs);

	grad_reset_menu(gmap->gtype, bpp);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grad_check_grev), gmap->grev);
	for (i = NUM_OTYPES - 1; (i >= 0) && (opmap[i] != gmap->otype); i--);
	gtk_option_menu_set_history(GTK_OPTION_MENU(grad_opt_otype), i);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grad_check_orev), gmap->orev);
}

static void click_grad_apply(GtkWidget *widget)
{
	int i;

	store_channel_gradient(grad_channel);
	memcpy(gradient, grad_temps, sizeof(grad_temps));
	memcpy(graddata, grad_tmaps, sizeof(grad_tmaps));
	memcpy(gradbytes, grad_tbytes, sizeof(grad_tbytes));

	grad_opacity = mt_spinslide_get_value(grad_ss_pre);

	for (i = 0; i < NUM_CHANNELS; i++) grad_update(gradient + i);
	for (i = 0; i <= NUM_CHANNELS; i++) gmap_setup(graddata + i, gradbytes, i);
	update_stuff(UPD_GRAD);
}

static void click_grad_ok(GtkWidget *widget)
{
	click_grad_apply(widget);
	gtk_widget_destroy(widget);
}

static void grad_channel_changed(GtkToggleButton *widget, gpointer user_data)
{
	if ((int)user_data == grad_channel) return;
	if (!gtk_toggle_button_get_active(widget)) return;
	store_channel_gradient(grad_channel);
	grad_channel = -1;
	show_channel_gradient((int)user_data);
	grad_channel = (int)user_data;
}

static void grad_selector_box(GtkWidget *box, char **mtext, int op)
{
	GtkWidget *vbox, *hbox, *menu, *rev, *btn;

	vbox = gtk_vbox_new(FALSE, 0);
	add_with_frame_x(box, op ? _("Opacity") : _("Gradient"), vbox, 5, TRUE);
	menu = pack(vbox, wj_option_menu(mtext, -1, 0, NULL, NULL));
	gtk_container_set_border_width(GTK_CONTAINER(menu), 5);
	hbox = pack(vbox, gtk_hbox_new(TRUE, 0));
	rev = add_a_toggle(_("Reverse"), hbox, FALSE);
	btn = add_a_button(_("Edit Custom"), 5, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(btn), "clicked",
		GTK_SIGNAL_FUNC(grad_edit), (gpointer)op);
	if (op)
	{
		grad_opt_otype = menu;
		grad_check_orev = rev;
	}
	else
	{
		grad_opt_gtype = menu;
		grad_check_grev = rev;
	}
}

void gradient_setup(int mode)
{
	char *gtypes[] = {_("Linear"), _("Bilinear"), _("Radial"), _("Square"),
		_("Angular"), _("Conical")};
	char *rtypes[] = {_("None"), _("Level"), _("Repeat"), _("Mirror")};
	char *gradtypes[] = {_("A to B"), _("A to B (RGB)"), _("A to B (sRGB)"),
		_("A to B (HSV)"), _("A to B (backward HSV)"), _("A only"),
		_("Custom"), NULL};
	char *optypes[] = {_("Current to 0"), _("Current only"), _("Custom"), NULL};
	GtkWidget *win, *mainbox, *hbox, *table, *align;
	GtkWindowPosition pos = !mode && !inifile_get_gboolean("centerSettings", TRUE) ?
		GTK_WIN_POS_MOUSE : GTK_WIN_POS_CENTER;

	memcpy(grad_temps, gradient, sizeof(grad_temps));
	memcpy(grad_tmaps, graddata, sizeof(grad_tmaps));
	memcpy(grad_tbytes, gradbytes, sizeof(grad_tbytes));
	grad_channel = mem_channel;

	grad_window = win = add_a_window(GTK_WINDOW_TOPLEVEL,
		_("Configure Gradient"), pos, TRUE);
	mainbox = add_vbox(win);

	/* Channel box */

	hbox = wj_radio_pack(allchannames, 4, 1, mem_channel, NULL,
		GTK_SIGNAL_FUNC(grad_channel_changed));
	add_with_frame(mainbox, _("Channel"), hbox);

	/* Setup block */

	table = add_a_table(3, 4, 5, mainbox);
	add_to_table(_("Length"), table, 0, 0, 5);
	grad_spin_len = spin_to_table(table, 0, 1, 5, 0, 0, MAX_GRAD);
	add_to_table(_("Repeat length"), table, 1, 0, 5);
	grad_spin_rep = spin_to_table(table, 1, 1, 5, 0, 0, MAX_GRAD);
	add_to_table(_("Offset"), table, 2, 0, 5);
	grad_spin_ofs = spin_to_table(table, 2, 1, 5, 0, -MAX_GRAD, MAX_GRAD);
	add_to_table(_("Gradient type"), table, 0, 2, 5);
	grad_opt_type = wj_option_menu(gtypes, 6, 0, NULL, NULL);
	to_table(grad_opt_type, table, 0, 3, 5);
	add_to_table(_("Extension type"), table, 1, 2, 5);
	grad_opt_bound = wj_option_menu(rtypes, 4, 0, NULL, NULL);
	to_table(grad_opt_bound, table, 1, 3, 5);
	add_to_table(_("Preview opacity"), table, 2, 2, 5);
	grad_ss_pre = mt_spinslide_new(-1, -1);
	mt_spinslide_set_range(grad_ss_pre, 0, 255);
	mt_spinslide_set_value(grad_ss_pre, grad_opacity);
	/* !!! Box derivatives can't have their "natural" size set directly */
	align = widget_align_minsize(grad_ss_pre, 200, -2);
	to_table(align, table, 2, 3, 5);

	/* Select page */

	hbox = pack(mainbox, gtk_hbox_new(TRUE, 0));
	grad_selector_box(hbox, gradtypes, 0);
	grad_selector_box(hbox, optypes, 1);

	/* Cancel / Apply / OK */

	align = pack(mainbox, OK_box(0, win, _("OK"), GTK_SIGNAL_FUNC(click_grad_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));
	OK_box_add(align, _("Apply"), GTK_SIGNAL_FUNC(click_grad_apply));

	/* Fill in values */

	gtk_widget_show_all(mainbox);
	show_channel_gradient(mem_channel);

	gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(main_window));
	gtk_widget_show(win);

#if GTK_MAJOR_VERSION == 1
	/* Re-render sliders, adjust option menus */
	gtk_widget_queue_resize(win);
#endif
}

/// GRADIENT PICKER

static int pickg_grad = GRAD_TYPE_RGB, pickg_cspace = CSPACE_LXN;

static int do_pick_gradient(GtkWidget *table, gpointer fdata)
{
	unsigned char buf[256];
	int len;

	pickg_grad = wj_option_menu_get_history(table_slot(table, 0, 1));
	pickg_cspace = wj_option_menu_get_history(table_slot(table, 1, 1));

	len = mem_pick_gradient(buf, pickg_cspace, pickg_grad);

	mem_clip_new(len, 1, 1, CMASK_IMAGE, FALSE);
	if (mem_clipboard) memcpy(mem_clipboard, buf, len);

	update_stuff(UPD_XCOPY);
	pressed_paste(TRUE);

	return TRUE;
}

void pressed_pick_gradient()
{
	GtkWidget *table = gtk_table_new(2, 2, FALSE);

	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	to_table(grad_interp_menu(pickg_grad, FALSE, NULL), table, 0, 1, 5);
	to_table(wj_option_menu(cspnames, NUM_CSPACES, pickg_cspace, NULL, NULL),
		table, 1, 1, 5);
	add_to_table(_("Gradient"), table, 0, 0, 5);
	add_to_table(_("Colour space"), table, 1, 0, 5);
	gtk_widget_show_all(table);
	filter_window(_("Pick Gradient"), table, do_pick_gradient, NULL, FALSE);
}

/// SKEW WINDOW

typedef struct {
	GtkWidget *angle[2], *ofs[2], *dist[2], *gc;
	int angles, lock;
} skew_widgets;

static int skew_mode = 6;

static void click_skew_ok(GtkWidget *widget, gpointer user_data)
{
	skew_widgets *sw = gtk_object_get_user_data(GTK_OBJECT(widget));
	double xskew, yskew;
	int res, ftype = 0, gcor = FALSE;

	if ((sw->angles & 1) || GTK_WIDGET_HAS_FOCUS(sw->angle[0]))
		xskew = tan(read_float_spin(sw->angle[0]) * (M_PI / 180.0));
	else xskew = read_float_spin(sw->ofs[0]) / (double)read_spin(sw->dist[0]);
	if ((sw->angles & 2) || GTK_WIDGET_HAS_FOCUS(sw->angle[1]))
		yskew = tan(read_float_spin(sw->angle[1]) * (M_PI / 180.0));
	else yskew = read_float_spin(sw->ofs[1]) / (double)read_spin(sw->dist[1]);

	if (!xskew && !yskew)
	{
		alert_same_geometry();
		return;
	}

	if (mem_img_bpp == 3)
	{
		ftype = skew_mode;
		gcor = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sw->gc));
	}

	res = mem_skew(xskew, yskew, ftype, gcor);
	if (!res)
	{
		update_stuff(UPD_GEOM);
		destroy_dialog(widget);
	}
	else memory_errors(res);
}

static void skew_moved(GtkAdjustment *adjustment, gpointer user_data)
{
	skew_widgets *sw = user_data;
	int i;

	if (sw->lock) return; // Avoid recursion
	sw->lock = TRUE;

	for (i = 0; i < 2; i++)
	{
		/* Offset for angle */
		if (adjustment == GTK_SPIN_BUTTON(sw->angle[i])->adjustment)
		{
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(sw->ofs[i]),
				ADJ2INT(GTK_SPIN_BUTTON(sw->dist[i])->adjustment) *
				tan(adjustment->value * (M_PI / 180.0)));
			sw->angles |= 1 << i;
		}
		/* Angle for offset */
		else if ((adjustment == GTK_SPIN_BUTTON(sw->ofs[i])->adjustment) ||
			(adjustment == GTK_SPIN_BUTTON(sw->dist[i])->adjustment))
		{
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(sw->angle[i]),
				atan(GTK_SPIN_BUTTON(sw->ofs[i])->adjustment->value /
				ADJ2INT(GTK_SPIN_BUTTON(sw->dist[i])->adjustment)) *
				(180.0 / M_PI));
			sw->angles &= ~(1 << i);
		}
	}

	sw->lock = FALSE;
}

void pressed_skew()
{
	GtkWidget *skew_window, *vbox, *table;
	skew_widgets *sw;


	skew_window = add_a_window(GTK_WINDOW_TOPLEVEL, _("Skew"), GTK_WIN_POS_CENTER, TRUE);
	sw = bound_malloc(skew_window, sizeof(skew_widgets));
	gtk_object_set_user_data(GTK_OBJECT(skew_window), (gpointer)sw);
	vbox = add_vbox(skew_window);

	table = add_a_table(3, 4, 5, vbox);
	add_to_table(_("Angle"), table, 0, 1, 0);
	add_to_table(_("Offset"), table, 0, 2, 0);
	add_to_table(_("At distance"), table, 0, 3, 0);
	add_to_table(_("Horizontal "), table, 1, 0, 0);
	add_to_table(_("Vertical"), table, 2, 0, 0);
	sw->angle[0] = float_spin_to_table(table, 1, 1, 5, 0, -89.99, 89.99);
	sw->angle[1] = float_spin_to_table(table, 2, 1, 5, 0, -89.99, 89.99);
	sw->ofs[0] = float_spin_to_table(table, 1, 2, 5, 0, -MAX_WIDTH, MAX_WIDTH);
	sw->ofs[1] = float_spin_to_table(table, 2, 2, 5, 0, -MAX_HEIGHT, MAX_HEIGHT);
	sw->dist[0] = spin_to_table(table, 1, 3, 5, mem_height, 1, MAX_HEIGHT);
	sw->dist[1] = spin_to_table(table, 2, 3, 5, mem_width, 1, MAX_WIDTH);
	spin_connect(sw->angle[0], GTK_SIGNAL_FUNC(skew_moved), (gpointer)sw);
	spin_connect(sw->ofs[0], GTK_SIGNAL_FUNC(skew_moved), (gpointer)sw);
	spin_connect(sw->dist[0], GTK_SIGNAL_FUNC(skew_moved), (gpointer)sw);
	spin_connect(sw->angle[1], GTK_SIGNAL_FUNC(skew_moved), (gpointer)sw);
	spin_connect(sw->ofs[1], GTK_SIGNAL_FUNC(skew_moved), (gpointer)sw);
	spin_connect(sw->dist[1], GTK_SIGNAL_FUNC(skew_moved), (gpointer)sw);
	add_hseparator(vbox, -2, 10);

	if (mem_img_bpp == 3)
	{
		sw->gc = pack(vbox, gamma_toggle());
		add_hseparator(vbox, -2, 10);
		xpack(vbox, filter_pack(FALSE, skew_mode, &skew_mode));
		add_hseparator(vbox, -2, 10);
	}

	pack(vbox, OK_box(5, skew_window, _("OK"), GTK_SIGNAL_FUNC(click_skew_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));
	gtk_window_set_transient_for(GTK_WINDOW(skew_window), GTK_WINDOW(main_window));
	gtk_widget_show_all(skew_window);
}

/// TRACING IMAGE WINDOW

typedef struct {
	GtkWidget *wspin, *hspin, *xspin, *yspin, *zspin, *opt, *tog;
	int src, x, y, scale, state;
} bkg_widgets;

static void bkg_update_widgets(bkg_widgets *bw)
{
	int w = 0, h = 0;

	bw->src = wj_option_menu_get_history(bw->opt);
	switch (bw->src)
	{
	case 0: w = bkg_w; h = bkg_h; break;
	case 1: break;
	case 2: w = mem_width; h = mem_height; break;
	case 3: if (mem_clipboard) w = mem_clip_w , h = mem_clip_h;
		break;
	}

	spin_set_range(bw->wspin, w, w);
	spin_set_range(bw->hspin, h, h);

	bw->x = read_spin(bw->xspin);
	bw->y = read_spin(bw->yspin);
	bw->scale = read_spin(bw->zspin);
	bw->state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(bw->tog));
}

static void click_bkg_apply(GtkWidget *widget)
{
	bkg_widgets *bw = gtk_object_get_user_data(GTK_OBJECT(widget));

	bkg_update_widgets(bw);
	bkg_x = bw->x;
	bkg_y = bw->y;
	bkg_scale = bw->scale;
	bkg_flag = bw->state;
	if (!config_bkg(bw->src)) memory_errors(1);
	update_stuff(UPD_RENDER);
}

static void click_bkg_option(GtkMenuItem *menuitem, gpointer user_data)
{
	bkg_update_widgets(user_data);
}

static void click_bkg_ok(GtkWidget *widget)
{
	click_bkg_apply(widget);
	gtk_widget_destroy(widget);
}

void bkg_setup()
{
	char *srcs[4] = { _("Unchanged"), _("None"), _("Image"), _("Clipboard") };
	GtkWidget *win, *vbox, *table, *hbox;
	bkg_widgets *bw;


	win = add_a_window(GTK_WINDOW_TOPLEVEL, _("Tracing Image"), GTK_WIN_POS_CENTER, TRUE);
	bw = bound_malloc(win, sizeof(bkg_widgets));
	gtk_object_set_user_data(GTK_OBJECT(win), (gpointer)bw);

	bw->src = 0;
	bw->state = bkg_flag;

	vbox = add_vbox(win);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);

	table = add_a_table(4, 3, 5, vbox);
	add_to_table(_("Source"), table, 0, 0, 5);
	add_to_table(_("Size"), table, 1, 0, 5);
	add_to_table(_("Origin"), table, 2, 0, 5);
	add_to_table(_("Relative scale"), table, 3, 0, 5);
	bw->opt = wj_option_menu(srcs, 4, 0, bw, GTK_SIGNAL_FUNC(click_bkg_option));
	to_table_l(bw->opt, table, 0, 1, 2, 5);
	bw->wspin = spin_to_table(table, 1, 1, 5, 0, 0, 0);
	bw->hspin = spin_to_table(table, 1, 2, 5, 0, 0, 0);
	GTK_WIDGET_UNSET_FLAGS(bw->wspin, GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS(bw->hspin, GTK_CAN_FOCUS);
	bw->xspin = spin_to_table(table, 2, 1, 5, bkg_x, -MAX_WIDTH, MAX_WIDTH);
	bw->yspin = spin_to_table(table, 2, 2, 5, bkg_y, -MAX_HEIGHT, MAX_HEIGHT);
	bw->zspin = spin_to_table(table, 3, 1, 5, bkg_scale, 1, MAX_ZOOM);

	bw->tog = add_a_toggle(_("Display"), vbox, bkg_flag);

	add_hseparator(vbox, -2, 10);

	/* Cancel / Apply / OK */

	hbox = pack(vbox, OK_box(5, win, _("OK"), GTK_SIGNAL_FUNC(click_bkg_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));
	OK_box_add(hbox, _("Apply"), GTK_SIGNAL_FUNC(click_bkg_apply));
	bkg_update_widgets(bw);
	gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(main_window));
	gtk_widget_show_all(win);
}

/// SEGMENTATION WINDOW

seg_state *seg_preview;

typedef struct {
	char ids[3]; // For binding updaters
	GtkWidget *win, *tspin, *pbutton;
	seg_state *s;
	int vars[2]; // Colorspace and distance
	int progress;
	int step; // Calculation step of 2nd phase
} seg_widgets;

static int seg_cspace = CSPACE_LXN, seg_dist = DIST_LINF;
static int seg_rank = 4, seg_minsize = 1;
static guint seg_idle;

/* Change colorspace or distance measure, causing full recalculation */
static void seg_mode_toggled(GtkWidget *btn, gpointer idx)
{
	seg_widgets *sw;
	char *cp;

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) return;
	cp = gtk_object_get_user_data(GTK_OBJECT(btn->parent));
	sw = (seg_widgets *)(cp - *cp);
	if (sw->vars[(int)*cp] == (int)idx) return;

	sw->vars[(int)*cp] = (int)idx;
	mem_seg_prepare(sw->s, mem_img[CHN_IMAGE], mem_width, mem_height,
		sw->progress, sw->vars[0], sw->vars[1]);
	/* Disable preview if cancelled, change threshold if not */
	if (!sw->s->phase) gtk_toggle_button_set_active(
		GTK_TOGGLE_BUTTON(sw->pbutton), FALSE);
	else gtk_spin_button_set_value(GTK_SPIN_BUTTON(sw->tspin),
		mem_seg_threshold(sw->s));
}

/* Do phase 2 (segmentation) in the background */
static gboolean seg_process_idle(seg_widgets *sw)
{
	if (seg_preview && (sw->s->phase == 1))
	{
#define SEG_STEP 100000
		sw->step = mem_seg_process_chunk(sw->step, SEG_STEP, sw->s);
#undef SEG_STEP
		if (!(sw->s->phase & 2)) return (TRUE); // Not yet completed
		gdk_window_set_cursor(sw->win->window, NULL);
		seg_idle = 0; // In case update_stuff() ever calls main loop
		update_stuff(UPD_RENDER);
	}
	seg_idle = 0;
	return (FALSE);
}

/* Change segmentation limits, causing phase 2 restart */
static void seg_spin_changed(GtkAdjustment *adjustment, char *cp)
{
	seg_widgets *sw = (seg_widgets *)(cp - *cp);
	seg_state *s = sw->s;

	if (!*cp) s->threshold = adjustment->value;
	else *(*cp == 1 ? &s->minrank : &s->minsize) = ADJ2INT(adjustment);
	s->phase &= 1; // Need phase 2 rerun
	sw->step = 0; // Restart phase 2 afresh
	if (seg_preview)
	{
		if (sw->progress) gdk_window_set_cursor(sw->win->window, busy_cursor);
		if (!seg_idle) seg_idle = threads_idle_add_priority(
			GTK_PRIORITY_REDRAW + 5, (GtkFunction)seg_process_idle, sw);
	}
}

/* Finish all calculations (preparation and segmentation) */
static int seg_process(seg_widgets *sw)
{
	/* Run phase 1 if necessary */
	if (!sw->s->phase) mem_seg_prepare(sw->s, mem_img[CHN_IMAGE],
		mem_width, mem_height, sw->progress, sw->vars[0], sw->vars[1]);
	/* Run phase 2 if possible & necessary */
	if (sw->s->phase == 1) mem_seg_process(sw->s);
	/* Return whether job is done */
	return (sw->s->phase > 1);
}

/* Toggle interactive preview */
static void seg_preview_toggle(GtkToggleButton *button, GtkWidget *window)
{
	seg_state *oldp = seg_preview;
	seg_widgets *sw = NULL;


	if ((button && button->active) ^ !oldp) return; // Nothing to do
	if (window) sw = gtk_object_get_user_data(GTK_OBJECT(window));
	if (seg_idle) gtk_idle_remove(seg_idle);
	seg_idle = 0;
	if (oldp) // Disable
	{
// !!! Maybe better to add & use an integrated progressbar?
		if (sw) gdk_window_set_cursor(sw->win->window, NULL);
		seg_preview = NULL;
	}
	else // Enable
	{
		/* Do segmentation conspicuously at first */
		if (!seg_process(sw)) gtk_toggle_button_set_active(button, FALSE);
		else seg_preview = sw->s;
	}
	if (oldp != seg_preview) update_stuff(UPD_RENDER);
}

static gboolean seg_cancel(GtkWidget *window)
{
	seg_widgets *sw = gtk_object_get_user_data(GTK_OBJECT(window));

	seg_preview_toggle(NULL, NULL);	// Clear the preview if enabled
	destroy_dialog(window);
	free(sw->s);
	free(sw);
	return (FALSE);
}

static void seg_ok(GtkWidget *window)
{
	seg_widgets *sw = gtk_object_get_user_data(GTK_OBJECT(window));

	/* First, disable preview */
	seg_preview_toggle(NULL, NULL);

	/* Then, update parameters */
	update_window_spin(window);
	seg_cspace = sw->vars[0];
	seg_dist = sw->vars[1];
	seg_rank = sw->s->minrank;
	seg_minsize = sw->s->minsize;

	/* Now, finish segmentation & render results */
	if (seg_process(sw))
	{
		spot_undo(UNDO_FILT);
		mem_seg_render(mem_img[CHN_IMAGE], sw->s);
		mem_undo_prepare();
		update_stuff(UPD_IMG);
	}

	seg_cancel(window);
}

void pressed_segment()
{
	GtkWidget *seg_window, *mainbox, *table, *hbox, *spin;
	seg_widgets *sw;
	seg_state *s;
	int progress = 0, sz = mem_width * mem_height;


	if (sz == 1) return; /* 1 pixel in image is trivial - do nothing */
	if (sz >= 1024 * 1024) progress = SEG_PROGRESS;

	s = mem_seg_prepare(NULL, mem_img[CHN_IMAGE], mem_width, mem_height,
		progress, seg_cspace, seg_dist);
	if (!s)
	{
		memory_errors(1);
		return;
	}
	if (!s->phase) return; // Terminated by user
	s->threshold = mem_seg_threshold(s);

	sw = calloc(1, sizeof(seg_widgets));
	sw->ids[1] = 1; sw->ids[2] = 2;
	sw->s = s;
	sw->progress = progress;

	sw->win = seg_window = add_a_window(GTK_WINDOW_TOPLEVEL,
		_("Segment Image"), GTK_WIN_POS_CENTER, TRUE);
	gtk_object_set_user_data(GTK_OBJECT(seg_window), sw);
	mainbox = add_vbox(seg_window);

	pack(mainbox, cspace_frame(sw->vars[0] = seg_cspace, sw->ids + 0,
		GTK_SIGNAL_FUNC(seg_mode_toggled)));
	pack(mainbox, difference_frame(sw->vars[1] = seg_dist, sw->ids + 1,
		GTK_SIGNAL_FUNC(seg_mode_toggled)));

	table = add_a_table(3, 2, 5, mainbox);
	add_to_table(_("Threshold"), table, 0, 0, 5);
	sw->tspin = float_spin_to_table(table, 0, 1, 5, s->threshold, 0, 5000.0);
	spin_connect(sw->tspin, GTK_SIGNAL_FUNC(seg_spin_changed), sw->ids + 0);
	add_to_table(_("Level"), table, 1, 0, 5);
	spin = spin_to_table(table, 1, 1, 5, s->minrank = seg_rank, 0, 32);
	spin_connect(spin, GTK_SIGNAL_FUNC(seg_spin_changed), sw->ids + 1);
	add_to_table(_("Minimum size"), table, 2, 0, 5);
	spin = spin_to_table(table, 2, 1, 5, s->minsize = seg_minsize, 1, sz);
	spin_connect(spin, GTK_SIGNAL_FUNC(seg_spin_changed), sw->ids + 2);

	hbox = pack(mainbox, OK_box(0, seg_window, _("Apply"), GTK_SIGNAL_FUNC(seg_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(seg_cancel)));
	sw->pbutton = OK_box_add_toggle(hbox, _("Preview"), GTK_SIGNAL_FUNC(seg_preview_toggle));

	gtk_window_set_transient_for(GTK_WINDOW(seg_window), GTK_WINDOW(main_window));
	gtk_widget_show(seg_window);
}
