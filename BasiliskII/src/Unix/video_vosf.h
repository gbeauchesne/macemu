/*
 *  video_vosf.h - Video/graphics emulation, video on SEGV signals support
 *
 *  Basilisk II (C) 1997-2000 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef VIDEO_VOSF_H
#define VIDEO_VOSF_H

// Note: this file is #include'd in video_x.cpp
#ifdef ENABLE_VOSF

/*
 *  Page-aligned memory allocation
 */

// Align on page boundaries
static uint32 align_on_page_boundary(uint32 size)
{
	const uint32 page_size = getpagesize();
	const uint32 page_mask = page_size - 1;
	return (size + page_mask) & ~page_mask;
}

// Allocate memory on page boundary
static void * allocate_framebuffer(uint32 size, uint8 * hint = 0)
{
	// Remind that the system can allocate at 0x00000000...
	return mmap((caddr_t)hint, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, zero_fd, 0);
}


/*
 *	Screen depth identification
 */

enum {
	ID_DEPTH_UNKNOWN = -1,
	ID_DEPTH_1,
	ID_DEPTH_8,
	ID_DEPTH_15,
	ID_DEPTH_16,
	ID_DEPTH_24,
	ID_DEPTH_32 = ID_DEPTH_24,
	ID_DEPTH_COUNT
};

static int depth_id(int depth)
{
	int id;
	switch (depth) {
		case 1	: id = ID_DEPTH_1;	break;
		case 8	: id = ID_DEPTH_8;	break;
		case 15	: id = ID_DEPTH_15;	break;
		case 16	: id = ID_DEPTH_16;	break;
		case 24	: id = ID_DEPTH_24;	break;
		case 32	: id = ID_DEPTH_32;	break;
		default	: id = ID_DEPTH_UNKNOWN;
	}
	return id;
}


/*
 *	Frame buffer copy function templates
 */

// No conversion required

#define MEMCPY_PROFITABLE
#ifdef MEMCPY_PROFITABLE
static void do_fbcopy_raw(uint8 * dest, const uint8 * source, uint32 length)
{
	memcpy(dest, source, length);
}
#else
#error "incomplete"
#define FB_BLIT_1(dst, src)	(dst = (src))
#define FB_BLIT_2(dst, src)	(dst = (src))
#define FB_DEPTH			8
#define FB_FUNC_NAME		do_fbcopy_raw
#include "video_blit.h"
#endif


// RGB 555

#define FB_BLIT_1(dst, src) \
	(dst = (((src) >> 8) & 0xff) | (((src) & 0xff) << 8))
	
#define FB_BLIT_2(dst, src) \
	(dst = (((src) >> 8) & 0x00ff00ff) | (((src) & 0x00ff00ff) << 8))

#define	FB_DEPTH 15
#define FB_FUNC_NAME do_fbcopy_15
#include "video_blit.h"


// RGB 565

#define FB_BLIT_1(dst, src) \
	(dst = (((src) >> 8) & 0x001f) | (((src) << 9) & 0xfe00) | (((src) >> 7) & 0x01c0))
	
#define FB_BLIT_2(dst, src) \
	(dst = (((src) >> 8) & 0x001f001f) | (((src) << 9) & 0xfe00fe00) | (((src) >> 7) & 0x01c001c0))

#define FB_DEPTH 16
#define FB_FUNC_NAME do_fbcopy_16
#include "video_blit.h"


// RGB 888

#define FB_BLIT_1(dst, src) \
	(dst = (src))

#define FB_BLIT_2(dst, src) \
	(dst = (((src) >> 24) & 0xff) | (((src) >> 16) & 0xff00) | (((src) & 0xff00) << 16) | (((src) & 0xff) << 24))

#define FB_DEPTH 24
#define FB_FUNC_NAME do_fbcopy_24
#include "video_blit.h"


/*
 *	Frame buffer copy functions map table
 */

typedef void (*fbcopy_func)(uint8 *, const uint8 *, uint32);
static fbcopy_func do_update_framebuffer;

#define FBCOPY_FUNC(aHandler) do_ ## aHandler

#if REAL_ADDRESSING || DIRECT_ADDRESSING
#define WD(X) { FBCOPY_FUNC(X), FBCOPY_FUNC(X) }
#else
#define WD(X) { FBCOPY_FUNC(fbcopy_raw), FBCOPY_FUNC(fbcopy_raw) }
#endif

// fb_copy_funcs[depth_id][native_byte_order][dga_mode]
// NT  : not tested
// OK  : has been successfully tested
// NBO : native byte order
static fbcopy_func fbcopy_funcs[ID_DEPTH_COUNT][2][2] = {
#ifdef WORDS_BIGENDIAN
				/*	alt byte order	  native byte order	*/
/*  1 bpp */	{	WD(fbcopy_raw)	, WD(fbcopy_raw)	},	// NT
/*  8 bpp */	{	WD(fbcopy_raw)	, WD(fbcopy_raw)	},	// OK (NBO)
/* 15 bpp */	{	WD(fbcopy_15)	, WD(fbcopy_raw)	},	// NT
/* 16 bpp */	{	WD(fbcopy_16)	, WD(fbcopy_raw)	},	// NT
/* 24 bpp */	{	WD(fbcopy_24)	, WD(fbcopy_raw)	}	// NT
#else
/*  1 bpp */	{	WD(fbcopy_raw)	, WD(fbcopy_raw)	},	// NT
/*  8 bpp */	{	WD(fbcopy_raw)	, WD(fbcopy_raw)	},	// OK (NBO)
/* 15 bpp */	{	WD(fbcopy_15)	, WD(fbcopy_15)		},	// OK (NBO)
/* 16 bpp */	{	WD(fbcopy_16)	, WD(fbcopy_16)		},	// OK (NBO)
/* 24 bpp */	{	WD(fbcopy_24)	, WD(fbcopy_24)		}	// NT
#endif
};

#undef WD

#define FBCOPY_FUNC_ERROR \
	ErrorAlert("Invalid screen depth")

#define GET_FBCOPY_FUNC(aDepth, aNativeByteOrder, aDisplay) \
	((depth_id(aDepth) == ID_DEPTH_UNKNOWN) ? ( FBCOPY_FUNC_ERROR, (fbcopy_func)0 ) : \
	fbcopy_funcs[depth_id(aDepth)][(aNativeByteOrder)][(aDisplay) == DISPLAY_DGA ? 1 : 0])


/*
 *	Screen fault handler
 */

static inline void do_handle_screen_fault(unsigned long addr)
{
	if ((addr < mainBuffer.memStart) || (addr >= mainBuffer.memEnd)) {
		fprintf(stderr, "Segmentation fault at 0x%08X\n", addr);
		abort();
	}
	
	const int page  = (addr - mainBuffer.memStart) >> mainBuffer.pageBits;
	caddr_t page_ad = (caddr_t)(addr & ~(mainBuffer.pageSize - 1));
#ifdef HAVE_PTHREADS
	pthread_mutex_lock(&Screen_draw_lock);
#endif
	PFLAG_SET(page);
	mprotect(page_ad, mainBuffer.pageSize, PROT_READ | PROT_WRITE);
#ifdef HAVE_PTHREADS
	pthread_mutex_unlock(&Screen_draw_lock);
#endif
}

#if defined(HAVE_SIGINFO_T)
static void Screen_fault_handler(int, siginfo_t * sip, void *)
{
	D(bug("Screen_fault_handler: ADDR=0x%08X\n", sip->si_addr));
	do_handle_screen_fault((unsigned long)sip->si_addr);
}
#elif defined(HAVE_SIGCONTEXT_SUBTERFUGE)
# if defined(__i386__) && defined(__linux__)
static void Screen_fault_handler(int, struct sigcontext scs)
{
	D(bug("Screen_fault_handler: ADDR=0x%08X from IP=0x%08X\n", scs.cr2, scs.eip));
	do_handle_screen_fault((unsigned long)scs.cr2);
}
# else
#  error "No suitable subterfuge for Video on SEGV signals"
# endif
#else
# error "Can't do Video on SEGV signals"
#endif


/*
 *	Screen fault handler initialization
 */

#if defined(HAVE_SIGINFO_T)
static bool Screen_fault_handler_init()
{
	// Setup SIGSEGV handler to process writes to frame buffer
	sigemptyset(&vosf_sa.sa_mask);
	vosf_sa.sa_sigaction = Screen_fault_handler;
	vosf_sa.sa_flags = 0;
	return (sigaction(SIGSEGV, &vosf_sa, NULL) == 0);
}
#elif defined(HAVE_SIGCONTEXT_SUBTERFUGE)
static bool Screen_fault_handler_init()
{
	// Setup SIGSEGV handler to process writes to frame buffer
	sigemptyset(&vosf_sa.sa_mask);
	vosf_sa.sa_handler = (void (*)(int)) Screen_fault_handler;
	vosf_sa.sa_flags = 0;
	return (sigaction(SIGSEGV, &vosf_sa, NULL) == 0);
}
#endif


/*
 *	Update display for Windowed mode and VOSF
 */

static inline void update_display_window_vosf(void)
{
	int page = 0;
	for (;;) {
		while (PFLAG_ISCLEAR_4(page))
			page += 4;
		
		while (PFLAG_ISCLEAR(page))
			page++;
		
		if (page >= mainBuffer.pageCount)
			break;
		
		const int first_page = page;
		PFLAG_CLEAR(first_page);
		while ((++page < mainBuffer.pageCount) && PFLAG_ISSET(page))
			PFLAG_CLEAR(page);
		
		// Make the dirty pages read-only again
		const int32 offset  = first_page << mainBuffer.pageBits;
		const uint32 length = (page - first_page) << mainBuffer.pageBits;
		mprotect((caddr_t)(mainBuffer.memStart + offset), length, PROT_READ);
		
		// There is at least one line to update
		const int y1 = mainBuffer.pageInfo[first_page].top;
		const int y2 = mainBuffer.pageInfo[page - 1].bottom;
		const int height = y2 - y1 + 1;
		
		const int bytes_per_row = VideoMonitor.bytes_per_row;
		const int bytes_per_pixel = VideoMonitor.bytes_per_row / VideoMonitor.x;
		int i, j;
		
		// Check for first column from left and first column
		// from right that have changed
		int x1 = VideoMonitor.x * bytes_per_pixel - 1;
		for (j = y1; j <= y2; j++) {
			uint8 * const p1 = &the_buffer[j * bytes_per_row];
			uint8 * const p2 = &the_buffer_copy[j * bytes_per_row];
			for (i = 0; i < x1; i++) {
				if (p1[i] != p2[i]) {
					x1 = i;
					break;
				}
			}
		}
		x1 /= bytes_per_pixel;
		
		int x2 = x1 * bytes_per_pixel;
		for (j = y2; j >= y1; j--) {
			uint8 * const p1 = &the_buffer[j * bytes_per_row];
			uint8 * const p2 = &the_buffer_copy[j * bytes_per_row];
			for (i = VideoMonitor.x * bytes_per_pixel - 1; i > x2; i--) {
				if (p1[i] != p2[i]) {
					x2 = i;
					break;
				}
			}
		}
		x2 /= bytes_per_pixel;
		
		// Update the_host_buffer and copy of the_buffer
		// There is at least one pixel to copy
		const int width = x2 - x1 + 1;
		i = y1 * bytes_per_row + x1 * bytes_per_pixel;
		for (j = y1; j <= y2; j++) {
			do_update_framebuffer(the_host_buffer + i, the_buffer + i, bytes_per_pixel * width);
			memcpy(the_buffer_copy + i, the_buffer + i, bytes_per_pixel * width);
			i += bytes_per_row;
		}
		
		if (have_shm)
			XShmPutImage(x_display, the_win, the_gc, img, x1, y1, x1, y1, width, height, 0);
		else
			XPutImage(x_display, the_win, the_gc, img, x1, y1, x1, y1, width, height);
	}
}


/*
 *	Update display for DGA mode and VOSF
 *	(only in Direct Addressing mode)
 */

#if REAL_ADDRESSING || DIRECT_ADDRESSING
static inline void update_display_dga_vosf(void)
{
	int page = 0;
	for (;;) {
		while (PFLAG_ISCLEAR_4(page))
			page += 4;
		
		while (PFLAG_ISCLEAR(page))
			page++;
		
		if (page >= mainBuffer.pageCount)
			break;
		
		const int first_page = page;
		PFLAG_CLEAR(first_page);
		while ((++page < mainBuffer.pageCount) && PFLAG_ISSET(page))
			PFLAG_CLEAR(page);
		
		// Make the dirty pages read-only again
		const int32 offset  = first_page << mainBuffer.pageBits;
		const uint32 length = (page - first_page) << mainBuffer.pageBits;
		mprotect((caddr_t)(mainBuffer.memStart + offset), length, PROT_READ);
		
		// I am sure that y2 >= y1 and depth != 1
		const int y1 = mainBuffer.pageInfo[first_page].top;
		const int y2 = mainBuffer.pageInfo[page - 1].bottom;
		
		const int bytes_per_row = VideoMonitor.bytes_per_row;
		const int bytes_per_pixel = VideoMonitor.bytes_per_row / VideoMonitor.x;
		int i, j;
		
		// Check for first column from left and first column
		// from right that have changed
		int x1 = VideoMonitor.x * bytes_per_pixel - 1;
		for (j = y1; j <= y2; j++) {
			uint8 * const p1 = &the_buffer[j * bytes_per_row];
			uint8 * const p2 = &the_buffer_copy[j * bytes_per_row];
			for (i = 0; i < x1; i++) {
				if (p1[i] != p2[i]) {
					x1 = i;
					break;
				}
			}
		}
		x1 /= bytes_per_pixel;
		
		int x2 = x1 * bytes_per_pixel;
		for (j = y2; j >= y1; j--) {
			uint8 * const p1 = &the_buffer[j * bytes_per_row];
			uint8 * const p2 = &the_buffer_copy[j * bytes_per_row];
			for (i = VideoMonitor.x * bytes_per_pixel - 1; i > x2; i--) {
				if (p1[i] != p2[i]) {
					x2 = i;
					break;
				}
			}
		}
		x2 /= bytes_per_pixel;
		
		// Update the_host_buffer and copy of the_buffer
		// There should be at least one pixel to copy
		const int width = x2 - x1 + 1;
		i = y1 * bytes_per_row + x1 * bytes_per_pixel;
		for (j = y1; j <= y2; j++) {
			do_update_framebuffer(the_host_buffer + i, the_buffer + i, bytes_per_pixel * width);
			memcpy(the_buffer_copy + i, the_buffer + i, bytes_per_pixel * width);
			i += bytes_per_row;
		}
	}
}
#endif

#endif /* ENABLE_VOSF */

#endif /* VIDEO_VOSF_H */
