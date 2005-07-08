/* Basic vplot operations. */
/*
  Copyright (C) 2004 University of Texas at Austin
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <math.h>
#include <stdio.h>

#include <unistd.h>

#include <rsf.h>

#include "vplot.h"
#include "coltab.h"

#ifndef _vp_vplot_h

#define VP_SCREEN_RATIO 0.75 
/* aspect ratio, default window */
#define VP_ROTATED_HEIGHT 7.5
/* height in inches, rotated */    
#define VP_STANDARD_HEIGHT 10.24
/* height in inches, default device */ 
#define VP_MAX 54.6           
/* absolute maximum x or y in inches */
#define VP_BSIZE 256
/* number of pixels for scalebars */
/*^*/

enum {
    RPERIN=600,         /* vplot units per inch */
    HATCHPERIN=100,	/* Hatch units per inch */
    TXPERIN=33,	        /* Text units per inch */
    FATPERIN=200,	/* Fatness units per inch */
    MAX_GUN=255,	/* Maximum color gun strength */
    /*
     * This is the factor we scale our path and up vectors by before
     * running them through the local text coordinate transformation.
     * (The coordinate transformation, being in terms of device units,
     * gets done in integers. If we don't scale up we get severe roundoff
     * problems for small text sizes at odd angles. We can't make this
     * factor too big, though, or we risk very large text overflowing
     * the maximum possible integer.)
     */
    TEXTVECSCALE=10,
};
/*^*/

enum {TH_NORMAL, TH_LEFT, TH_CENTER, TH_RIGHT, TH_SYMBOL};
enum {TV_NORMAL, TV_BOTTOM, TV_BASE, TV_HALF, TV_CAP, TV_TOP, TV_SYMBOL};
/*^*/

enum {
    VP_SETSTYLE         = 'S',
    VP_MOVE             = 'm',
    VP_DRAW	        = 'd',
    VP_PLINE	    	= 'L',
    VP_PMARK	   	= 'M',
    VP_TEXT		= 'T',
    VP_GTEXT		= 'G',
    VP_AREA		= 'A',
    VP_OLDAREA		= 'a',
    VP_BYTE_RASTER	= 'R',
    VP_BIT_RASTER	= 'r',    
    VP_ERASE		= 'e',
    VP_BREAK		= 'b',
    VP_PURGE		= 'p',
    VP_NOOP		= 'n',    
    VP_ORIGIN		= 'o',
    VP_WINDOW		= 'w',    
    VP_FAT		= 'f',
    VP_SETDASH		= 's',
    VP_COLOR		= 'c',
    VP_SET_COLOR_TABLE	= 'C',
    VP_TXALIGN		= 'J',
    VP_TXFONTPREC	= 'F',
    VP_PATLOAD		= 'l',
    VP_OVERLAY		= 'v',    
    VP_MESSAGE		= 'z',
    VP_BEGIN_GROUP	= '[',
    VP_END_GROUP	= ']',    
    VP_OLDTEXT		= 't'
};
/*^*/

typedef enum {
    VP_NO_STYLE_YET=-1,
    VP_STANDARD,
    VP_ROTATED,
    VP_NORM,
    VP_ABSOLUTE
} vp_plotstyle;
/*^*/

enum {
    VP_BLACK,
    VP_BLUE,
    VP_RED,
    VP_PURPLE,
    VP_GREEN,
    VP_CYAN,
    VP_YELLOW,
    VP_WHITE
};
/*^*/

enum {
    VP_NO_CHANGE=-1,
    VP_STRING,
    VP_CHAR,
    VP_STROKE
};
/*^*/

enum {OVLY_NORMAL, OVLY_BOX, OVLY_SHADE, OVLY_SHADE_BOX};    
/* text overlay */
/*^*/

#endif

static float fx=0.0, fy=0.0;             /* origin in inches */
static float ufx=0.0, ufy=0.0;           /* origin in user units */
static float xold=0.0, yold=0.0;         /* old pen position (in inches) */
static float dashpos=0.0;                /* position in dashes (in inches) */
static float ddef[4]={0.0,0.0,0.0,0.0};  /* definition of dashes (in inches) */
static float xscl=1.0, yscl=1.0;         /* scaling from user units to inches */
static bool pendown=false;               /* is pen down (or up) */
static bool dashon=false;                /* true if dashed line */
static vp_plotstyle style=VP_STANDARD;          
static int font=-1, prec=-1, ovly=-1;    /* font, precision, and overlay */
static int xjust=0, yjust=0;             /* x and y text justification */

static void pout (float xp, float  yp, bool down);

void vp_init(void)
/*< Initialize output to vplot >*/
{
    if (isatty(fileno(stdout)))
	sf_error("You don't want to dump binary to terminal.");
}

int vp_getint (void)
/*< Extract and decode an integer (for debugging) >*/
{
    unsigned short w;

    w = getchar ();
    w += (getchar () << 8);

    return ((int) ((short) w));
}

void vp_putint (int w)
/*< Output an encoded integer >*/
{
    char j;
    
    j = w & 255;        /* low order byte of halfword value */
    (void) putchar (j);
    
    j = (w >> 8) & 255;	/* high order byte of halfword value */
    (void) putchar (j);
}

void vp_putfloat (float w)
/*< Output an encoded float with scaling >*/
{
    w *= RPERIN;
    vp_putint((int) ((w < 0.0)? w-0.5 : w+0.5));
}

void vp_putfloat0 (float w)
/*< Output an encoded float without scaling >*/
{
    vp_putint((int) ((w < 0.0)? w-0.5 : w+0.5));
}

void vp_egroup (void)
/*< end group >*/
{
    (void) putchar (VP_END_GROUP);
}

void vp_erase (void)
/*< erase screen >*/
{
    (void) putchar (VP_ERASE);
}

void vp_fat (int f)
/*< set fat >*/
{
    (void) putchar (VP_FAT);
    vp_putint (f);
}

void vp_fill (const float *xp /* [np] */, 
	      const float *yp /* [np] */, 
	      int  np         /* number of points */)
/*< fill polygon >*/
{
    int i;

    (void) putchar (VP_AREA);
    vp_putint (np);

    for (i = 0; i < np; i++) {
	vp_putfloat (xp[i]);
	vp_putfloat (yp[i]);
    }
}

void vp_ufill (const float *xp /* [np] */, 
	       const float *yp /* [np] */, 
	       int  np         /* number of points */)
/*< fill polygon defined in user coordinates >*/
{
    int i;
    float x, y;

    (void) putchar (VP_AREA);
    vp_putint (np);

    for (i = 0; i < np; i++) {
	x = fx + (xp[i]-ufx) * xscl;
	y = fy + (yp[i]-ufy) * yscl;
	vp_putfloat (x);
	vp_putfloat (y);
    }
}

void vp_area (const float *xp, const float *yp, int np, 
	      int fat, int xmask, int ymask)
/*< old-style polygon >*/
{
    int i;

    (void) putchar (VP_OLDAREA);
    vp_putint (np);
    vp_putint (fat);
    vp_putint (xmask);
    vp_putint (ymask);
    for (i = 0; i < np; i++) {
	vp_putfloat (xp[i]);
	vp_putfloat (yp[i]);
    }
}

void vp_uarea (const float *xp, const float *yp, int np, 
	       int fat, int xmask, int ymask)
/*< old-style polygon defined in user coordinates >*/
{
    int i;
    float x, y;

    (void) putchar (VP_OLDAREA);
    vp_putint (np);
    vp_putint (fat);
    vp_putint (xmask);
    vp_putint (ymask);
    for (i = 0; i < np; i++) {
	x = fx + (xp[i]-ufx) * xscl;
	y = fy + (yp[i]-ufy) * yscl;
	vp_putfloat (x);
	vp_putfloat (y);
    }
}

void vp_coltab (int color /* color index */, 
		float r   /* red */, 
		float g   /* green */, 
		float b   /* blue */)
/*< set a color table entry >*/
{
    r *= MAX_GUN; r += (r < 0.0)? -0.5: +0.5;
    g *= MAX_GUN; g += (g < 0.0)? -0.5: +0.5;
    b *= MAX_GUN; b += (b < 0.0)? -0.5: +0.5;

    (void) putchar (VP_SET_COLOR_TABLE);
    vp_putint (color);
    vp_putint ((int) r);
    vp_putint ((int) g);
    vp_putint ((int) b);
}


void vp_gtext (float x, float y, 
	       float xpath, float ypath, 
	       float xup, float yup, const char *string)
/*< output text string >*/
{
    pout (x, y, 0);
    (void) putchar (VP_GTEXT);
    vp_putfloat (TEXTVECSCALE * xpath);
    vp_putfloat (TEXTVECSCALE * ypath);
    vp_putfloat (TEXTVECSCALE * xup);
    vp_putfloat (TEXTVECSCALE * yup);

    do {
	(void) putchar (*string);
    } while (*string++ != '\0');
}

void vp_ugtext (float x, float y, 
		float xpath, float ypath,
		float xup, float yup, const char *string)
/*< output text string in user coordinates >*/
{
    x = fx + (x - ufx) * xscl;
    y = fy + (y - ufy) * yscl;
    xpath *= xscl;
    ypath *= yscl;
    xup *= xscl;
    yup *= yscl;

    vp_ugtext (x, y, xpath, ypath, xup, yup, string);
}


void vp_hatchload (int angle,int nhatch, int ihatch, int *hatch)
/*< ? >*/
{
    int c, i;

    (void) putchar (VP_PATLOAD);
    vp_putint (angle);
    vp_putint (-1);
    vp_putint (nhatch);
    vp_putint (ihatch);

    for (i = 0; i < 2 * 4 * nhatch; i++) {
	c = hatch[i];
	if (i % 4 > 1) c *= RPERIN / HATCHPERIN;
	vp_putint (c);
    }
}

void vp_message (const char *string)
/*< output message >*/
{
    (void) putchar (VP_MESSAGE);

    do {
	(void) putchar (*string);
    } while (*string++);
}

void vp_move (float x,float  y)
/*< move to a point >*/
{
    vp_plot (x, y, false);
}

void vp_umove (float x,float  y)
/*< move to a point in user coordinates >*/
{
    x = fx + (x - ufx) * xscl;
    y = fy + (y - ufy) * yscl;
    vp_plot (x, y, false);
}

void vp_orig (float x,float  y)
/*< set the origin >*/
{
    fx = x;
    fy = y;
}

void vp_uorig (float x,float  y)
/*< set the origin in user coordinates >*/
{
    ufx = x;
    ufy = y;
}

void vp_patload (int ppi, int  nx, int ny, int ipat, int *col)
/*< ? >*/
{
    int c, i;

    (void) putchar (VP_PATLOAD);
    c = ppi;
    vp_putint (c);
    c = nx;
    vp_putint (c);
    c = ny;
    vp_putint (c);
    c = ipat;
    vp_putint (c);

    for (i = 0; i < nx * ny; i++) {
	c = col[i];
	vp_putint (c);
    }
}

void vp_pendn (float x, float y)
/*< go to location (x,y) and then put the pen down >*/
{
    vp_plot (x, y, pendown);
    pendown = true;
}

void vp_upendn (float x, float y)
/*< go to location (x,y) in user coordinates and then put the pen down >*/
{
    vp_uplot (x, y, pendown);
    pendown = true;
}

void vp_penup (void)
/*< put pen up >*/
{
    pendown = false;
}

void vp_pline (const float *xp /* [np] */, 
	       const float *yp /* [np] */, 
	       int np          /* number of points */)
/*< draw a line >*/
{
    int i;

    (void) putchar (VP_PLINE);
    vp_putint (np);
    for (i = 0; i < np; i++) {
	vp_putfloat (xp[i]);
	vp_putfloat (yp[i]);
    }
}

void vp_upline (const float *xp /* [np] */, 
		const float *yp /* [np] */, 
		int np          /* number of points */)
/*< draw a line in user coordinates >*/
{
    int i;
    float x, y;

    (void) putchar (VP_PLINE);
    vp_putint (np);
    for (i = 0; i < np; i++) {
	x = fx + (xp[i]-ufx) * xscl;
	y = fy + (yp[i]-ufy) * yscl;
	vp_putfloat (x);
	vp_putfloat (y);
    }
}

void vp_plot (float x, float y, bool  down)
/*< line drawing >*/
{
    float dx, dy, dist, dpos, xp, yp, tonext, cosine, sine;
    int i;

/* if(vp_pc._pltout==0){ vp_plot_init();} */

    if (!down || !dashon) { /* if move or no dashes */
	pout (x, y, down);	/* output a move or draw */
	xold = x;
	yold = y;		/* save old x and y */
	dashpos = 0.0;		/* reset position in dashes */
	return;
    }
    
    dx = x - xold;
    dy = y - yold;	/* change in x and y */
    dist = hypotf (dx,dy);	/* distance */
    if (dist == 0.) return;	/* return if no change */
    
    cosine = dx / dist;
    sine = dy / dist;
    dpos = dashpos;		/* current position in dashes */
    dashpos = fmodf (dpos + dist, ddef[3]);	/* next position in
							 * dashes */
    for (i = 0; i < 4 && ddef[i] <= dpos; i++);	/* index to dash def */
    xp = xold;
    yp = yold;		/* initialize xp and yp */
    while (dist > 0.0) {
	tonext = ddef[i] - dpos;	/* dist to next gap or dash */
	if (tonext > dist) tonext = dist;
	xp += tonext * cosine;
	yp += tonext * sine;
	pout (xp, yp, !(i % 2));
	dpos = ddef[i];	/* new position */
	i = (i + 1) % 4;	/* i = 0,1,2, or 3 */
	if (i == 0) dpos = 0.0;		/* back to start of dashes */
	dist -= tonext;
    }
    xold = xp;
    yold = yp;
}

void vp_uplot (float x, float y, bool down)
/*< line drawing in user coordinates >*/
{
    x = fx + (x - ufx) * xscl;
    y = fy + (y - ufy) * yscl;
    vp_plot (x, y, down);
}

static void pout (float xp, float  yp, bool down)
{
    
    if      (xp >  VP_MAX) xp =  VP_MAX;
    else if (xp < -VP_MAX) xp = -VP_MAX;
    if      (yp >  VP_MAX) yp =  VP_MAX;
    else if (yp < -VP_MAX) yp = -VP_MAX;
    (void) putchar ((down ? VP_DRAW : VP_MOVE));
    vp_putfloat (xp);
    vp_putfloat (yp);
}

void vp_draw (float x,float  y)
/*< line drawing step >*/
{
    vp_plot (x, y, true);
}

void vp_udraw (float x,float  y)
/*< line drawing step in user coordinates >*/
{
    x = fx + (x - ufx) * xscl;
    y = fy + (y - ufy) * yscl;
    vp_plot (x, y, true);
}

void vp_pmark (int npts, int mtype, int msize, 
	       const float *xp, const float *yp)
/*< ? >*/
{
    int i;

    (void) putchar (VP_PMARK);
    vp_putint (npts);
    vp_putint (mtype);
    vp_putint (msize);
    
    for (i = 0; i < npts; i++) {
	vp_putfloat (xp[i]);
	vp_putfloat (yp[i]);
    }
}

void vp_purge (void)
/*< purge >*/
{
    (void) putchar (VP_PURGE);
    (void) fflush (stdout);
}

void vp_rascoltab (int nreserve, const char *colname)
/*< set a raster color table >*/
{
    int i, j, k, incr, smap[256];
/*
 * float arrays with numbers in the range 0. through 1.,
 * that define our "raster" color table.
 */
    float red[256], green[256], blue[256];

/*
 * This call makes a color table given its name. We ask for
 * a color table 256 long.
 */
    vp_name2coltab (colname, 256, red, green, blue);

    /*
     * In colors 256 through 512, we define our "raster" color table,
     * unscrambled and with a full range of values. When we call "vp_raster",
     * we'll give an offset of 256 to shift 0-255 into this range. 
     */
    for (i = 0; i < 256; i++)
    {
	vp_coltab (i+256, red[i], green[i], blue[i]);
    }


    /*
     * set the lower "scrambled" scale, which will give us the optimal raster
     * color scale possible for any device, no matter how many color table
     * entries it has. 
     */
    smap[0] = 0;
    smap[1] = 255;

    /*
     * This obscure code does the scrambling. A uniform grey scale, for
     * example, that goes "0, 1, 2, 3, ..., 255" gets reordered to "0, 255,
     * 128, 64, 196, 32, 96, 160, ... 1, 3, ... 251, 253". This way the
     * "most important" colors are closer to the front. This way the first
     * colors lost due to a less-than-256-color device or less than 256
     * colors set aside for raster are the least important ones. 
     */    
    for (i=2, incr=2; incr < 512; incr *= 2) {
	for (k = 1; k < incr && i < 256; k += 2, i++) {
	    j = k * 256 / incr;
	    smap[i] = j;
	}
    }

    for (i = 0; i < 256 - nreserve; i++) {
	j = smap[i];
	vp_coltab (i + nreserve, red[j],green[j],blue[j]);
    }
}


void vp_raster (unsigned char **array, bool bit, int offset, 
		int xpix, int ypix, 
		float xll, float yll, float xur,float yur, int orient)
/*< Output a raster array >*/
{
    int i, n, nn;
    unsigned char obyte;

    (void) putchar (bit? VP_BIT_RASTER: VP_BYTE_RASTER);

    if (orient >= 0) {
	orient %= 4;
    } else {
	orient = ((orient % 4) + 4) % 4;
    }

    vp_putint (orient);
    vp_putint (offset);
    vp_putfloat (xll);
    vp_putfloat (yll);
    vp_putfloat (xur);
    vp_putfloat (yur);

    vp_putint (xpix);
    vp_putint (ypix);

    for (i = 0; i < ypix; i++) {
	vp_putint (1);
	vp_putint (1);
 
	vp_putint (xpix);     
	if (bit) {
	    for (n = 0; n + 7 < xpix; n += 8) {
		obyte =
		    ((array[i][n+0] != 0) << 7) |
		    ((array[i][n+1] != 0) << 6) |
		    ((array[i][n+2] != 0) << 5) |
		    ((array[i][n+3] != 0) << 4) |
		    ((array[i][n+4] != 0) << 3) |
		    ((array[i][n+5] != 0) << 2) |
		    ((array[i][n+6] != 0) << 1) |
		    ((array[i][n+7] != 0) << 0);
		(void) putchar ((char) obyte);
	    }
	    if (n < xpix) {
		obyte = 0x00;
		for (nn = 7; n < xpix; n++, nn--) {
		    obyte |= ((array[i][n] != 0) << nn);
		}
		(void) putchar ((char) obyte);
	    }
	} else {
	    for (n = 0; n < xpix; n++) {
		(void) putchar ((char) array[i][n]);
	    }
	}
    }
}

void vp_uraster (unsigned char **array, bool bit, int offset,
		 int xpix, int ypix, 
		 float xll, float yll, float xur, float yur, int orient)
/*< Output a raster array in user coordinates >*/
{
    float x1, y1, x2, y2;
    
    x1 = fx + (xll - ufx) * xscl;
    y1 = fy + (yll - ufy) * yscl;

    x2 = fx + (xur - ufx) * xscl;
    y2 = fy + (yur - ufy) * yscl;

    vp_raster (array, bit, offset, 
	       xpix, ypix, x1, y1, x2, y2, orient);
}

void vp_scale (float xscale, float  yscale)
/*< set scaling >*/
{
    xscl = xscale;
    yscl = yscale;
}

void vp_stretch (float xmin, float ymin, float xmax, float ymax) 
/*< set scale and origin for a rectangular area >*/
{
    vp_uorig (xmin, ymin);
    vp_orig (0., 0.);

    if (VP_ROTATED == style) {
	vp_scale (VP_ROTATED_HEIGHT / (xmax - xmin), 
		  (VP_ROTATED_HEIGHT / VP_SCREEN_RATIO) / (ymax - ymin));
    } else {
	vp_scale ((VP_STANDARD_HEIGHT / VP_SCREEN_RATIO) / (xmax - xmin), 
		  VP_STANDARD_HEIGHT / (ymax - ymin));
    }
}

void vp_style (vp_plotstyle st) 
/*< set vpplot style >*/
{
    style = st;
    (void) putchar (VP_SETSTYLE);
    switch (style) {
	case VP_ROTATED:
	    (void) putchar ('r');
	    break;
	case VP_ABSOLUTE:
	    (void) putchar ('a');
	    break;
	case VP_STANDARD:
	default:
	    (void) putchar ('s');
	    break;
    }
}

void vp_text (float x, float y, int size, int orient, const char *string)
/*< output text string >*/
{
    if (0 == size) return;

    pout (x, y, 0);
    (void) putchar (VP_TEXT);
    vp_putint (size);
    vp_putint (orient);

    do {
	(void) putchar (*string);
    }
    while (*string++);
}

void vp_utext (float x, float y, int size, int orient, const char *string)
/*< output text string in user coordinates >*/
{
    x = fx + (x - ufx) * xscl;
    y = fy + (y - ufy) * yscl;
    vp_text (x, y, size, orient, string);
}

void vp_tfont (int font1, int prec1, int ovly1)
/*< set text font >*/
{
    font = font1;
    prec = prec1;
    ovly = ovly1;

    (void) putchar (VP_TXFONTPREC);
    vp_putint (font);
    vp_putint (prec);
    vp_putint (ovly);
}

void vp_tjust (int xjust1, int yjust1)
/*< set text alignment >*/
{
    xjust = xjust1;
    yjust = yjust1;

    (void) putchar (VP_TXALIGN);
    vp_putint (xjust);
    vp_putint (yjust);
}

void vp_clip (float xmin, float ymin, float xmax, float ymax)
/*< set rectangular clip >*/
{
    (void) putchar (VP_WINDOW);
    vp_putfloat (xmin);
    vp_putfloat (ymin);
    vp_putfloat (xmax);
    vp_putfloat (ymax);
}


void vp_uclip (float xmin, float ymin, float xmax, float ymax)
/*< set rectangular clip in user coordinates >*/
{
    xmin = fx + (xmin - ufx) * xscl;
    ymin = fy + (ymin - ufy) * yscl;
    xmax = fx + (xmax - ufx) * xscl;
    ymax = fy + (ymax - ufy) * yscl;
    vp_clip (xmin, ymin, xmax, ymax);
}

void vp_where (float *x, float *y)
/*< output current pen location >*/
{
    *x = xold;
    *y = yold;
}

void vp_color (int col)
/*< set pen color >*/
{
    (void) putchar (VP_COLOR);
    vp_putint (col);
}

void vp_arrow (float x1, float y1, float x, float y, float r)
/*< plot an arrow from (x1,y1) to (x,y) with arrow-size r >*/
{
    float beta, alpha, xp[4], yp[4];
    const float pio4=0.785398;
    bool flag;

    flag = (r < 0.);
    if (flag) r = -r;

    if (x == x1 && y == y1) {
	xp[0] = x - r / 3.;
	yp[0] = y - r / 3.;
	xp[1] = x - r / 3.;
	yp[1] = y + r / 3.;
	xp[2] = x + r / 3.;
	yp[2] = y + r / 3.;
	xp[3] = x + r / 3.;
	yp[3] = y - r / 3.;
	if (flag) {
	    vp_area (xp, yp, 4, 0, 0, 0);
	} else {
	    vp_area (xp, yp, 4, 0, 1, 1);
	}
    } else {
	vp_move (x1, y1);
	vp_draw (x, y);

	beta = atan2f (y - y1, x - x1);

	xp[0] = x;
	yp[0] = y;
	alpha = pio4 + beta;
	xp[1] = x - r * cosf (alpha);
	yp[1] = y - r * sinf (alpha);
	alpha = pio4 - beta;
	xp[2] = x - r * cosf (alpha);
	yp[2] = y + r * sinf (alpha);
	if (flag) {
	    vp_area (xp, yp, 3, 0, 0, 0);
	} else {
	    vp_area (xp, yp, 3, 0, 1, 1);
	}
    }
}

void vp_uarrow (float x1, float y1, float x, float y, float r)
/*< plot an arrow in user coordinates >*/
{
    float beta, alpha, xp[4], yp[4];
    const float pio4=0.785398;
    bool flag;

    flag = (r < 0.);
    if (flag) r = -r;

    if (x == x1 && y == y1) {
	xp[0] = x - r / 3.;
	yp[0] = y - r / 3.;
	xp[1] = x - r / 3.;
	yp[1] = y + r / 3.;
	xp[2] = x + r / 3.;
	yp[2] = y + r / 3.;
	xp[3] = x + r / 3.;
	yp[3] = y - r / 3.;
	if (flag) {
	    vp_uarea (xp, yp, 4, 0, 0, 0);
	} else {
	    vp_uarea (xp, yp, 4, 0, 1, 1);
	}
    } else {
	vp_umove (x1, y1);
	vp_udraw (x, y);

	beta = atan2f (y - y1, x - x1);

	xp[0] = x;
	yp[0] = y;
	alpha = pio4 + beta;
	xp[1] = x - r * cosf (alpha);
	yp[1] = y - r * sinf (alpha);
	alpha = pio4 - beta;
	xp[2] = x - r * cosf (alpha);
	yp[2] = y + r * sinf (alpha);
	if (flag) {
	    vp_uarea (xp, yp, 3, 0, 0, 0);
	} else {
	    vp_uarea (xp, yp, 3, 0, 1, 1);
	}
    }
}

void vp_dash (float dash1, float gap1, float dash2, float gap2)
/*< set dash pattern >*/
{
    if (dash1 < 0. || gap1 < 0. || dash2 < 0. || gap2 < 0.) {
	dashon = false;
	return;
    }

    ddef[0] = dash1;
    ddef[1] = dash1 + gap1;
    ddef[2] = ddef[1] + dash2;
    ddef[3] = ddef[2] + gap2;

    if (ddef[3] <= 0.) {
	dashon = false;
    } else {
	dashon = true;
	dashpos = 0.0;
    }
}

void vp_setdash (const float *dash, const float *gapp, int np)
/*< set dash pattern >*/
{
    int i;

    (void) putchar (VP_SETDASH);
    vp_putint (np);
    for (i = 0; i < np; i++) {
	vp_putfloat (dash[i]);
	vp_putfloat (gapp[i]);
    }
}
