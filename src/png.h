/*	png.h
	Copyright (C) 2004-2008 Mark Tyler and Dmitry Groshev

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

//	Loading/Saving errors

#define WRONG_FORMAT -10
#define TOO_BIG -11
#define EXPLODE_FAILED -12

#define FILE_LIB_ERROR  0xBAD01
#define FILE_MEM_ERROR  0xBAD02
#define FILE_HAS_FRAMES 0xBAD03
#define FILE_TOO_LONG   0xBAD04
#define FILE_EXP_BREAK  0xBAD05

#ifdef WIN32
	#define DIR_SEP '\\'
#else
	#define DIR_SEP '/'
#endif

/* File types */
enum {
	FT_NONE = 0,
	FT_PNG,
	FT_JPEG,
	FT_JP2,
	FT_J2K,
	FT_TIFF,
	FT_GIF,
	FT_BMP,
	FT_XPM,
	FT_XBM,
	FT_LSS,
	FT_TGA,
	FT_PCX,
	FT_PBM,
	FT_PGM,
	FT_PPM,
	FT_PAM,
	FT_GPL,
	FT_TXT,
	FT_PAL,
	FT_LAYERS1,
	FT_LAYERS2,
	FT_PIXMAP,
	FT_SVG,
	NUM_FTYPES
};

#define FTM_FTYPE   0x00FF /* File type mask */
#define FTM_EXTEND  0x0100 /* Allow extended format */
#define FTM_UNDO    0x0200 /* Allow undoing */
#define FTM_FRAMES  0x0400 /* Allow frameset use */

/* Features supported by file formats */
#define FF_BW      0x00001 /* Black and white */
#define FF_16      0x00002 /* 16 colors */
#define FF_256     0x00004 /* 256 colors */
#define FF_IDX     0x00007 /* Indexed image */
#define FF_RGB     0x00008 /* Truecolor */
#define FF_IMAGE   0x0000F /* Image of any kind */
#define FF_ANIM    0x00010 /* Animation */
#define FF_ALPHAI  0x00020 /* Alpha channel for indexed images */
#define FF_ALPHAR  0x00040 /* Alpha channel for RGB images */
#define FF_ALPHA   0x00060 /* Alpha channel for all images */
#define FF_MULTI   0x00080 /* Multiple channels */
#define FF_TRANS   0x00100 /* Indexed transparency */
#define FF_COMP    0x00E00 /* Configurable compression */
#define FF_COMPJ   0x00200 /* JPEG compression */
#define FF_COMPZ   0x00400 /* zlib compression */
#define FF_COMPR   0x00600 /* RLE compression */
#define FF_COMPJ2  0x00800 /* JPEG2000 compression */
#define FF_SPOT    0x01000 /* "Hot spot" */
#define FF_LAYER   0x02000 /* Layered images */
#define FF_PALETTE 0x04000 /* Palette file (not image) */
#define FF_RMEM    0x08000 /* Can be read from memory */
#define FF_WMEM    0x10000 /* Can be written to memory */
#define FF_MEM     0x18000 /* Both of the above */
#define FF_NOSAVE  0x20000 /* Can be read but not written */
#define FF_SCALE   0x40000 /* Freely scalable (vector format) */

#define FF_SAVE_MASK (mem_img_bpp == 3 ? FF_RGB : mem_cols > 16 ? FF_256 : \
	mem_cols > 2 ? FF_16 | FF_256 : FF_IDX)
#define FF_SAVE_MASK_FOR(X) ((X).bpp == 3 ? FF_RGB : (X).colors > 16 ? FF_256 : \
	(X).colors > 2 ? FF_16 | FF_256 : FF_IDX)

/* Animation loading modes */
#define ANM_RAW    0 /* Raw frames (as written) */
#define ANM_COMP   1 /* Composited frames (as displayed) */
#define ANM_NOZERO 2 /* Composited frames with nonzero delays (as seen) */

#define LONGEST_EXT 5

typedef struct {
	char *name, *ext, *ext2;
	unsigned int flags;
} fformat;

extern fformat file_formats[];

/* All-in-one transport container for save/load */
typedef struct {
	/* Configuration data */
	int mode, ftype;
	int xpm_trans;
	int hot_x, hot_y;
	int req_w, req_h; // Size request for scalable formats
	int jpeg_quality;
	int png_compression;
	int tga_RLE;
	int jp2_rate;
	int gif_delay;
	int rgb_trans;
	int silent;
	/* Image data */
	chanlist img;
	png_color *pal;
	int width, height, bpp, colors;
	int x, y;
	/* Extra data */
	int icc_size;
	char *icc;
} ls_settings;

int silence_limit, jpeg_quality, png_compression;
int tga_RLE, tga_565, tga_defdir, jp2_rate;
int apply_icc;

int file_type_by_ext(char *name, guint32 mask);

int save_image(char *file_name, ls_settings *settings);
int save_mem_image(unsigned char **buf, int *len, ls_settings *settings);

int load_image(char *file_name, int mode, int ftype);
int load_mem_image(unsigned char *buf, int len, int mode, int ftype);

// !!! The only allowed mode for now is FS_LAYER_LOAD
int load_frameset(frameset *frames, int ani_mode, char *file_name, int mode,
	int ftype);
int explode_frames(char *dest_path, int ani_mode, char *file_name, int ftype,
	int desttype);

int export_undo(char *file_name, ls_settings *settings);
int export_ascii ( char *file_name );

int detect_file_format(char *name, int need_palette);
#define detect_image_format(X) detect_file_format(X, FALSE)
#define detect_palette_format(X) detect_file_format(X, TRUE)

int valid_file(char *filename);		// Can this file be opened for reading?
