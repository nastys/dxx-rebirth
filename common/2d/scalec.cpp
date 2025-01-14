/*
 * Portions of this file are copyright Rebirth contributors and licensed as
 * described in COPYING.txt.
 * Portions of this file are copyright Parallax Software and licensed
 * according to the Parallax license below.
 * See COPYING.txt for license details.

THE COMPUTER CODE CONTAINED HEREIN IS THE SOLE PROPERTY OF PARALLAX
SOFTWARE CORPORATION ("PARALLAX").  PARALLAX, IN DISTRIBUTING THE CODE TO
END-USERS, AND SUBJECT TO ALL OF THE TERMS AND CONDITIONS HEREIN, GRANTS A
ROYALTY-FREE, PERPETUAL LICENSE TO SUCH END-USERS FOR USE BY SUCH END-USERS
IN USING, DISPLAYING,  AND CREATING DERIVATIVE WORKS THEREOF, SO LONG AS
SUCH USE, DISPLAY OR CREATION IS FOR NON-COMMERCIAL, ROYALTY OR REVENUE
FREE PURPOSES.  IN NO EVENT SHALL THE END-USER USE THE COMPUTER CODE
CONTAINED HEREIN FOR REVENUE-BEARING PURPOSES.  THE END-USER UNDERSTANDS
AND AGREES TO THE TERMS HEREIN AND ACCEPTS THE SAME BY USE OF THIS FILE.
COPYRIGHT 1993-1998 PARALLAX SOFTWARE CORPORATION.  ALL RIGHTS RESERVED.
*/

#include <stdlib.h>
#include "rle.h"
#include "compiler-span.h"

namespace dcx {

namespace {

// John's new stuff below here....

static int scale_error_term;
static int scale_initial_pixel_count;
static int scale_adj_up;
static int scale_adj_down;
static int scale_final_pixel_count;
static int scale_ydelta_minus_1;
static int scale_whole_step;

static void rls_stretch_scanline_setup( int XDelta, int YDelta );
static void rls_stretch_scanline(const uint8_t *, uint8_t *);

static void decode_row(const grs_bitmap &bmp, std::array<color_palette_index, 640> &scale_rle_data, const uint_fast32_t y)
{
	int offset=4+bmp.bm_h;

	for (const uint_fast32_t b : span(&bmp.bm_data[4u], y))
		offset += b;
	gr_rle_decode(&bmp.bm_data[offset], scale_rle_data.data(), rle_end(bmp, scale_rle_data));
}

static void scale_up_bitmap(const grs_bitmap &source_bmp, grs_bitmap &dest_bmp, int x0, int y0, int x1, int y1, fix u0, fix v0,  fix u1, fix v1, int orientation  )
{
	fix dv, v;
	if (orientation & 1) {
		int	t;
		t = u0;	u0 = u1;	u1 = t;
	}

	if (orientation & 2) {
		int	t;
		t = v0;	v0 = v1;	v1 = t;
		if (v1 < v0)
			v0--;
	}

	v = v0;

	dv = (v1-v0) / (y1-y0);

	rls_stretch_scanline_setup(x1 - x0, f2i(u1) - f2i(u0));
	if ( scale_ydelta_minus_1 < 1 ) return;

	v = v0;

	for (int y=y0; y<=y1; y++ ) {
		rls_stretch_scanline(&source_bmp.get_bitmap_data()[source_bmp.bm_rowsize*f2i(v)+f2i(u0)], &dest_bmp.get_bitmap_data()[dest_bmp.bm_rowsize*y+x0]);
		v += dv;
	}
}

static void scale_up_bitmap_rle(const grs_bitmap &source_bmp, grs_bitmap &dest_bmp, int x0, int y0, int x1, int y1, fix u0, fix v0,  fix u1, fix v1, int orientation  )
{
	fix dv, v;
	int last_row = -1;

	if (orientation & 1) {
		int	t;
		t = u0;	u0 = u1;	u1 = t;
	}

	if (orientation & 2) {
		int	t;
		t = v0;	v0 = v1;	v1 = t;
		if (v1 < v0)
			v0--;
	}

	dv = (v1-v0) / (y1-y0);

	rls_stretch_scanline_setup(x1 - x0, f2i(u1) - f2i(u0));
	if ( scale_ydelta_minus_1 < 1 ) return;

	v = v0;

	std::array<color_palette_index, 640> scale_rle_data;
	for (int y=y0; y<=y1; y++ ) {
		if ( f2i(v) != last_row )	{
			last_row = f2i(v);
			decode_row(source_bmp, scale_rle_data, last_row );
		}
		rls_stretch_scanline(&scale_rle_data[f2i(u0)], &dest_bmp.get_bitmap_data()[dest_bmp.bm_rowsize*y+x0]);
		v += dv;
	}
}

static void rls_stretch_scanline_setup( int XDelta, int YDelta )
{
	  scale_ydelta_minus_1 = YDelta - 1;

      /* X major line */
      /* Minimum # of pixels in a run in this line */
      scale_whole_step = XDelta / YDelta;

      /* Error term adjust each time Y steps by 1; used to tell when one
         extra pixel should be drawn as part of a run, to account for
         fractional steps along the X axis per 1-pixel steps along Y */
      scale_adj_up = (XDelta % YDelta) * 2;

      /* Error term adjust when the error term turns over, used to factor
         out the X step made at that time */
      scale_adj_down = YDelta * 2;

      /* Initial error term; reflects an initial step of 0.5 along the Y
         axis */
      scale_error_term = (XDelta % YDelta) - (YDelta * 2);

      /* The initial and last runs are partial, because Y advances only 0.5
         for these runs, rather than 1. Divide one full run, plus the
         initial pixel, between the initial and last runs */
      scale_initial_pixel_count = (scale_whole_step / 2) + 1;
      scale_final_pixel_count = scale_initial_pixel_count;

      /* If the basic run length is even and there's no fractional
         advance, we have one pixel that could go to either the initial
         or last partial run, which we'll arbitrarily allocate to the
         last run */
      if ((scale_adj_up == 0) && ((scale_whole_step & 0x01) == 0))
      {
         scale_initial_pixel_count--;
      }
     /* If there're an odd number of pixels per run, we have 1 pixel that can't
     be allocated to either the initial or last partial run, so we'll add 0.5
     to error term so this pixel will be handled by the normal full-run loop */
      if ((scale_whole_step & 0x01) != 0)
      {
         scale_error_term += YDelta;
      }

}

static void rls_stretch_scanline(const color_palette_index *scale_source_ptr, color_palette_index *scale_dest_ptr)
{
	int ErrorTerm, initial_count, final_count;

	// Draw the first, partial run of pixels

	auto src_ptr = scale_source_ptr;
	auto dest_ptr = scale_dest_ptr;
	ErrorTerm = scale_error_term;
	initial_count = scale_initial_pixel_count;
	final_count = scale_final_pixel_count;

	const auto process_line = [&dest_ptr, &src_ptr](const unsigned len)
	{
		const auto c = *src_ptr++;
		if (c != TRANSPARENCY_COLOR)
			std::fill_n(dest_ptr, len, c);
		dest_ptr += len;
	};
	process_line(initial_count);

	// Draw all full runs

	for (int j=0; j<scale_ydelta_minus_1; j++) {
		unsigned len = scale_whole_step;     // run is at least this long

 		// Advance the error term and add an extra pixel if the error term so indicates
		if ((ErrorTerm += scale_adj_up) > 0)    {
			len++;
			ErrorTerm -= scale_adj_down;   // reset the error term
		}

		// Draw this run o' pixels
		process_line(len);
	}

	// Draw the final run of pixels
	process_line(final_count);
}

// old stuff here...

static void scale_bitmap_c(const grs_bitmap &source_bmp, grs_bitmap &dest_bmp, int x0, int y0, int x1, int y1, fix u0, fix v0,  fix u1, fix v1, int orientation  )
{
	fix u, v, du, dv;

	du = (u1-u0) / (x1-x0);
	dv = (v1-v0) / (y1-y0);

	if (orientation & 1) {
		u0 = u1;
		du = -du;
	}

	if (orientation & 2) {
		v0 = v1;
		dv = -dv;
		if (dv < 0)
			v0--;
	}

	v = v0;

	for (int y=y0; y<=y1; y++ ) {
		auto sbits = &source_bmp.get_bitmap_data()[source_bmp.bm_rowsize*f2i(v)];
		auto dbits = &dest_bmp.get_bitmap_data()[dest_bmp.bm_rowsize*y+x0];
		u = u0;
		v += dv;
		for (int x=x0; x<=x1; x++ ) {
			auto c = sbits[u >> 16];
			if (c != TRANSPARENCY_COLOR)
				*dbits = c;
			dbits++;
			u += du;
		}
	}
}

static void scale_row_transparent(const std::array<ubyte, 640> &sbits, color_palette_index *dbits, int width, fix u, fix du )
{
	const auto dbits_end = &dbits[width-1];

	if ( du < F1_0 )	{
		// Scaling up.
		fix next_u;
		int next_u_int;

		next_u_int = f2i(u)+1;
		auto c = sbits[next_u_int];
		next_u = i2f(next_u_int);
		if ( c != TRANSPARENCY_COLOR ) goto NonTransparent;

Transparent:
		while (1)	{
			dbits++;
			if ( dbits > dbits_end ) return;
			u += du;
			if ( u > next_u )	{
				next_u_int = f2i(u)+1;
				c = sbits[ next_u_int ];
				next_u = i2f(next_u_int);
				if ( c != TRANSPARENCY_COLOR ) goto NonTransparent;
			}
		}
		return;

NonTransparent:
		while (1)	{
			*dbits++ = c;
			if ( dbits > dbits_end ) return;
			u += du;
			if ( u > next_u )	{
				next_u_int = f2i(u)+1;
				c = sbits[ next_u_int ];
				next_u = i2f(next_u_int);
				if ( c == TRANSPARENCY_COLOR ) goto Transparent;
			}
		}
		return;



	} else {
		for ( int i=0; i<width; i++ ) {
			const auto c = sbits[ f2i(u) ];

			if ( c != TRANSPARENCY_COLOR )
				*dbits = c;

			dbits++;
			u += du;
		}
	}
}

static void scale_bitmap_c_rle(const grs_bitmap &source_bmp, grs_bitmap &dest_bmp, int x0, int y0, int x1, int y1, fix u0, fix v0,  fix u1, fix v1, int orientation  )
{
	fix du, dv, v;
	int last_row=-1;

//	Rotation doesn't work because explosions are not square!
// -- 	if (orientation & 4) {
// -- 		int	t;
// -- 		t = u0;	u0 = v0;	v0 = t;
// -- 		t = u1;	u1 = v1;	v1 = t;
// -- 	}

	du = (u1-u0) / (x1-x0);
	dv = (v1-v0) / (y1-y0);

	if (orientation & 1) {
		u0 = u1;
		du = -du;
	}

	if (orientation & 2) {
		v0 = v1;
		dv = -dv;
		if (dv < 0)
			v0--;
	}

	v = v0;

	if (v<0) {	//was: Assert(v >= 0);
		//Int3();   //this should be checked in higher-level routine
		return;
	}

	std::array<ubyte, 640> scale_rle_data;
	for (int y=y0; y<=y1; y++ ) {
		if ( f2i(v) != last_row )	{
			last_row = f2i(v);
			decode_row(source_bmp, scale_rle_data, last_row );
		}
		scale_row_transparent( scale_rle_data, &dest_bmp.get_bitmap_data()[dest_bmp.bm_rowsize*y+x0], x1-x0+1, u0, du );
		v += dv;
	}
}

}

#define FIND_SCALED_NUM(x,x0,x1,y0,y1) (fixmuldiv((x)-(x0),(y1)-(y0),(x1)-(x0))+(y0))

// Scales bitmap, bp, into vertbuf[0] to vertbuf[1]
void scale_bitmap(const grs_bitmap &bp, const std::array<grs_point, 3> &vertbuf, int orientation, grs_bitmap &dbp)
{
	fix x0, y0, x1, y1;
	fix u0, v0, u1, v1;
	fix clipped_x0, clipped_y0, clipped_x1, clipped_y1;
	fix clipped_u0, clipped_v0, clipped_u1, clipped_v1;
	fix xmin, xmax, ymin, ymax;
	int dx0, dy0, dx1, dy1;
	int dtemp;
	// Set initial variables....

	x0 = vertbuf[0].x; y0 = vertbuf[0].y;
	x1 = vertbuf[2].x; y1 = vertbuf[2].y;

	xmin = 0; ymin = 0;
	xmax = i2f(dbp.bm_w)-fl2f(.5); ymax = i2f(dbp.bm_h)-fl2f(.5);

	u0 = i2f(0); v0 = i2f(0);
	u1 = i2f(bp.bm_w-1); v1 = i2f(bp.bm_h-1);

	// Check for obviously offscreen bitmaps...
	if ( (y1<=y0) || (x1<=x0) ) return;
	if ( (x1<0 ) || (x0>=xmax) ) return;
	if ( (y1<0 ) || (y0>=ymax) ) return;

	clipped_u0 = u0; clipped_v0 = v0;
	clipped_u1 = u1; clipped_v1 = v1;

	clipped_x0 = x0; clipped_y0 = y0;
	clipped_x1 = x1; clipped_y1 = y1;

	// Clip the left, moving u0 right as necessary
	if ( x0 < xmin ) 	{
		clipped_u0 = FIND_SCALED_NUM(xmin,x0,x1,u0,u1);
		clipped_x0 = xmin;
	}

	// Clip the right, moving u1 left as necessary
	if ( x1 > xmax )	{
		clipped_u1 = FIND_SCALED_NUM(xmax,x0,x1,u0,u1);
		clipped_x1 = xmax;
	}

	// Clip the top, moving v0 down as necessary
	if ( y0 < ymin ) 	{
		clipped_v0 = FIND_SCALED_NUM(ymin,y0,y1,v0,v1);
		clipped_y0 = ymin;
	}

	// Clip the bottom, moving v1 up as necessary
	if ( y1 > ymax ) 	{
		clipped_v1 = FIND_SCALED_NUM(ymax,y0,y1,v0,v1);
		clipped_y1 = ymax;
	}
	
	dx0 = f2i(clipped_x0); dx1 = f2i(clipped_x1);
	dy0 = f2i(clipped_y0); dy1 = f2i(clipped_y1);

	if (dx1<=dx0) return;
	if (dy1<=dy0) return;

	dtemp = f2i(clipped_u1)-f2i(clipped_u0);

	if (bp.get_flag_mask(BM_FLAG_RLE))
	{
		if ( (dtemp < (f2i(clipped_x1)-f2i(clipped_x0))) && (dtemp>0) )
			scale_up_bitmap_rle(bp, dbp, dx0, dy0, dx1, dy1, clipped_u0, clipped_v0, clipped_u1, clipped_v1, orientation  );
		else
			scale_bitmap_c_rle(bp, dbp, dx0, dy0, dx1, dy1, clipped_u0, clipped_v0, clipped_u1, clipped_v1, orientation  );
	} else {
		if ( (dtemp < (f2i(clipped_x1)-f2i(clipped_x0))) && (dtemp>0) )
			scale_up_bitmap(bp, dbp, dx0, dy0, dx1, dy1, clipped_u0, clipped_v0, clipped_u1, clipped_v1, orientation  );
		else
			scale_bitmap_c(bp, dbp, dx0, dy0, dx1, dy1, clipped_u0, clipped_v0, clipped_u1, clipped_v1, orientation  );
	}
}

}
