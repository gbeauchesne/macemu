/*
 *  video_x.cpp - Video/graphics emulation, X11 specific stuff
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

/*
 *  NOTES:
 *    The Ctrl key works like a qualifier for special actions:
 *      Ctrl-Tab = suspend DGA mode
 *      Ctrl-Esc = emergency quit
 *      Ctrl-F1 = mount floppy
 */

#include "sysdeps.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>

#ifdef HAVE_PTHREADS
# include <pthread.h>
#endif

#ifdef ENABLE_XF86_DGA
# include <X11/extensions/xf86dga.h>
#endif

#ifdef ENABLE_XF86_VIDMODE
# include <X11/extensions/xf86vmode.h>
#endif

#ifdef ENABLE_FBDEV_DGA
# include <sys/mman.h>
#endif

#ifdef ENABLE_VOSF
# include <unistd.h>
# include <signal.h>
# include <fcntl.h>
# include <sys/mman.h>
#endif

#include "cpu_emulation.h"
#include "main.h"
#include "adb.h"
#include "macos_util.h"
#include "prefs.h"
#include "user_strings.h"
#include "video.h"

#define DEBUG 0
#include "debug.h"


// Display types
enum {
	DISPLAY_WINDOW,	// X11 window, using MIT SHM extensions if possible
	DISPLAY_DGA		// DGA fullscreen display
};

// Constants
const char KEYCODE_FILE_NAME[] = DATADIR "/keycodes";
const char FBDEVICES_FILE_NAME[] = DATADIR "/fbdevices";


// Global variables
static int32 frame_skip;							// Prefs items
static int16 mouse_wheel_mode = 1;
static int16 mouse_wheel_lines = 3;

static int display_type = DISPLAY_WINDOW;			// See enum above
static bool local_X11;								// Flag: X server running on local machine?
static uint8 *the_buffer;							// Mac frame buffer

#ifdef HAVE_PTHREADS
static bool redraw_thread_active = false;			// Flag: Redraw thread installed
static volatile bool redraw_thread_cancel = false;	// Flag: Cancel Redraw thread
static pthread_t redraw_thread;						// Redraw thread
#endif

static bool has_dga = false;						// Flag: Video DGA capable
static bool has_vidmode = false;					// Flag: VidMode extension available

static bool ctrl_down = false;						// Flag: Ctrl key pressed
static bool caps_on = false;						// Flag: Caps Lock on
static bool quit_full_screen = false;				// Flag: DGA close requested from redraw thread
static bool emerg_quit = false;						// Flag: Ctrl-Esc pressed, emergency quit requested from MacOS thread
static bool emul_suspended = false;					// Flag: Emulator suspended

static bool classic_mode = false;					// Flag: Classic Mac video mode

static bool use_keycodes = false;					// Flag: Use keycodes rather than keysyms
static int keycode_table[256];						// X keycode -> Mac keycode translation table

// X11 variables
static int screen;									// Screen number
static int xdepth;									// Depth of X screen
static int depth;									// Depth of Mac frame buffer
static Window rootwin, the_win;						// Root window and our window
static XVisualInfo visualInfo;
static Visual *vis;
static Colormap cmap[2];							// Two colormaps (DGA) for 8-bit mode
static XColor black, white;
static unsigned long black_pixel, white_pixel;
static int eventmask;
static const int win_eventmask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | EnterWindowMask | FocusChangeMask | ExposureMask;
static const int dga_eventmask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask;

static XColor palette[256];							// Color palette for 8-bit mode
static bool palette_changed = false;				// Flag: Palette changed, redraw thread must set new colors
#ifdef HAVE_PTHREADS
static pthread_mutex_t palette_lock = PTHREAD_MUTEX_INITIALIZER;	// Mutex to protect palette
#endif

// Variables for window mode
static GC the_gc;
static XImage *img = NULL;
static XShmSegmentInfo shminfo;
static Cursor mac_cursor;
static uint8 *the_buffer_copy = NULL;				// Copy of Mac frame buffer
static bool have_shm = false;						// Flag: SHM extensions available
static bool updt_box[17][17];						// Flag for Update
static int nr_boxes;
static const int sm_uptd[] = {4,1,6,3,0,5,2,7};
static int sm_no_boxes[] = {1,8,32,64,128,300};

// Variables for XF86 DGA mode
static int current_dga_cmap;						// Number (0 or 1) of currently installed DGA colormap
static Window suspend_win;							// "Suspend" window
static void *fb_save = NULL;						// Saved frame buffer for suspend
#ifdef HAVE_PTHREADS
static pthread_mutex_t frame_buffer_lock = PTHREAD_MUTEX_INITIALIZER;	// Mutex to protect frame buffer
#endif

// Variables for fbdev DGA mode
const char FBDEVICE_FILE_NAME[] = "/dev/fb";
static int fbdev_fd;

#ifdef ENABLE_XF86_VIDMODE
// Variables for XF86 VidMode support
static XF86VidModeModeInfo **x_video_modes;			// Array of all available modes
static int num_x_video_modes;
#endif

#ifdef ENABLE_VOSF
static bool use_vosf = true;						// Flag: VOSF enabled
#else
static const bool use_vosf = false;					// Flag: VOSF enabled
#endif

#ifdef ENABLE_VOSF
static uint8 * the_host_buffer;						// Host frame buffer in VOSF mode
static uint32 the_buffer_size;						// Size of allocated the_buffer
#endif

#ifdef ENABLE_VOSF
// Variables for Video on SEGV support (taken from the Win32 port)
struct ScreenPageInfo {
    int top, bottom;			// Mapping between this virtual page and Mac scanlines
};

struct ScreenInfo {
    uint32 memBase;				// Real start address 
    uint32 memStart;			// Start address aligned to page boundary
    uint32 memEnd;				// Address of one-past-the-end of the screen
    uint32 memLength;			// Length of the memory addressed by the screen pages
    
    uint32 pageSize;			// Size of a page
    int pageBits;				// Shift count to get the page number
    uint32 pageCount;			// Number of pages allocated to the screen
    
    uint8 * dirtyPages;			// Table of flags set if page was altered
    ScreenPageInfo * pageInfo;	// Table of mappings page -> Mac scanlines
};

static ScreenInfo mainBuffer;

#define PFLAG_SET(page)			mainBuffer.dirtyPages[page] = 1
#define PFLAG_CLEAR(page)		mainBuffer.dirtyPages[page] = 0
#define PFLAG_ISSET(page)		mainBuffer.dirtyPages[page]
#define PFLAG_ISCLEAR(page)		(mainBuffer.dirtyPages[page] == 0)
#ifdef UNALIGNED_PROFITABLE
# define PFLAG_ISCLEAR_4(page)	(*((uint32 *)(mainBuffer.dirtyPages + page)) == 0)
#else
# define PFLAG_ISCLEAR_4(page)	\
		(mainBuffer.dirtyPages[page  ] == 0) \
	&&	(mainBuffer.dirtyPages[page+1] == 0) \
	&&	(mainBuffer.dirtyPages[page+2] == 0) \
	&&	(mainBuffer.dirtyPages[page+3] == 0)
#endif
#define PFLAG_CLEAR_ALL			memset(mainBuffer.dirtyPages, 0, mainBuffer.pageCount)
#define PFLAG_SET_ALL			memset(mainBuffer.dirtyPages, 1, mainBuffer.pageCount)

static int zero_fd = -1;
static bool Screen_fault_handler_init();
static struct sigaction vosf_sa;

#ifdef HAVE_PTHREADS
static pthread_mutex_t Screen_draw_lock = PTHREAD_MUTEX_INITIALIZER;	// Mutex to protect frame buffer (dirtyPages in fact)
#endif

static int log_base_2(uint32 x)
{
	uint32 mask = 0x80000000;
	int l = 31;
	while (l >= 0 && (x & mask) == 0) {
		mask >>= 1;
		l--;
	}
	return l;
}

#endif

// VideoRefresh function
void VideoRefreshInit(void);
static void (*video_refresh)(void);

// Prototypes
static void *redraw_func(void *arg);
static int event2keycode(XKeyEvent *ev);


// From main_unix.cpp
extern char *x_display_name;
extern Display *x_display;

// From sys_unix.cpp
extern void SysMountFirstFloppy(void);

#ifdef ENABLE_VOSF
# include "video_vosf.h"
#endif


/*
 *  Initialization
 */

// Set VideoMonitor according to video mode
void set_video_monitor(int width, int height, int bytes_per_row, bool native_byte_order)
{
#if !REAL_ADDRESSING && !DIRECT_ADDRESSING
	int layout = FLAYOUT_DIRECT;
	switch (depth) {
		case 1:
			layout = FLAYOUT_DIRECT;
			break;
		case 8:
			layout = FLAYOUT_DIRECT;
			break;
		case 15:
			layout = FLAYOUT_HOST_555;
			break;
		case 16:
			layout = FLAYOUT_HOST_565;
			break;
		case 24:
		case 32:
			layout = FLAYOUT_HOST_888;
			break;
	}
	if (native_byte_order)
		MacFrameLayout = layout;
	else
		MacFrameLayout = FLAYOUT_DIRECT;
#endif
	switch (depth) {
		case 1:
			VideoMonitor.mode = VMODE_1BIT;
			break;
		case 8:
			VideoMonitor.mode = VMODE_8BIT;
			break;
		case 15:
			VideoMonitor.mode = VMODE_16BIT;
			break;
		case 16:
			VideoMonitor.mode = VMODE_16BIT;
			break;
		case 24:
		case 32:
			VideoMonitor.mode = VMODE_32BIT;
			break;
	}
	VideoMonitor.x = width;
	VideoMonitor.y = height;
	VideoMonitor.bytes_per_row = bytes_per_row;
}

// Trap SHM errors
static bool shm_error = false;
static int (*old_error_handler)(Display *, XErrorEvent *);

static int error_handler(Display *d, XErrorEvent *e)
{
	if (e->error_code == BadAccess) {
		shm_error = true;
		return 0;
	} else
		return old_error_handler(d, e);
}

// Init window mode
static bool init_window(int width, int height)
{
	int aligned_width = (width + 15) & ~15;
	int aligned_height = (height + 15) & ~15;

	// Set absolute mouse mode
	ADBSetRelMouseMode(false);

	// Read frame skip prefs
	frame_skip = PrefsFindInt32("frameskip");

	// Create window
	XSetWindowAttributes wattr;
	wattr.event_mask = eventmask = win_eventmask;
	wattr.background_pixel = black_pixel;

	XSync(x_display, false);
	the_win = XCreateWindow(x_display, rootwin, 0, 0, width, height, 0, xdepth,
		InputOutput, vis, CWEventMask | CWBackPixel, &wattr);

	// Indicate that we want keyboard input
	{
		XWMHints *hints = XAllocWMHints();
		if (hints) {
			hints->input = True;
			hints->flags = InputHint;
			XSetWMHints(x_display, the_win, hints);
			XFree((char *)hints);
		}
	}

	// Make window unresizable
	{
		XSizeHints *hints = XAllocSizeHints();
		if (hints) {
			hints->min_width = width;
			hints->max_width = width;
			hints->min_height = height;
			hints->max_height = height;
			hints->flags = PMinSize | PMaxSize;
			XSetWMNormalHints(x_display, the_win, hints);
			XFree((char *)hints);
		}
	}
	
	// Set window title
	XStoreName(x_display, the_win, GetString(STR_WINDOW_TITLE));

	// Set window class
	{
		XClassHint *hints;
		hints = XAllocClassHint();
		if (hints) {
			hints->res_name = "BasiliskII";
			hints->res_class = "BasiliskII";
			XSetClassHint(x_display, the_win, hints);
			XFree((char *)hints);
		}
	}

	// Show window
	XSync(x_display, false);
	XMapRaised(x_display, the_win);
	XFlush(x_display);

	// Set colormap
	if (depth == 8)
		XSetWindowColormap(x_display, the_win, cmap[0]);

	// Try to create and attach SHM image
	have_shm = false;
	if (depth != 1 && local_X11 && XShmQueryExtension(x_display)) {

		// Create SHM image ("height + 2" for safety)
		img = XShmCreateImage(x_display, vis, depth, depth == 1 ? XYBitmap : ZPixmap, 0, &shminfo, width, height);
		shminfo.shmid = shmget(IPC_PRIVATE, (aligned_height + 2) * img->bytes_per_line, IPC_CREAT | 0777);
		the_buffer_copy = (uint8 *)shmat(shminfo.shmid, 0, 0);
		shminfo.shmaddr = img->data = (char *)the_buffer_copy;
		shminfo.readOnly = False;

		// Try to attach SHM image, catching errors
		shm_error = false;
		old_error_handler = XSetErrorHandler(error_handler);
		XShmAttach(x_display, &shminfo);
		XSync(x_display, false);
		XSetErrorHandler(old_error_handler);
		if (shm_error) {
			shmdt(shminfo.shmaddr);
			XDestroyImage(img);
			shminfo.shmid = -1;
		} else {
			have_shm = true;
			shmctl(shminfo.shmid, IPC_RMID, 0);
		}
	}
	
	// Create normal X image if SHM doesn't work ("height + 2" for safety)
	if (!have_shm) {
		int bytes_per_row = aligned_width;
		switch (depth) {
			case 1:
				bytes_per_row /= 8;
				break;
			case 15:
			case 16:
				bytes_per_row *= 2;
				break;
			case 24:
			case 32:
				bytes_per_row *= 4;
				break;
		}
		the_buffer_copy = (uint8 *)malloc((aligned_height + 2) * bytes_per_row);
		img = XCreateImage(x_display, vis, depth, depth == 1 ? XYBitmap : ZPixmap, 0, (char *)the_buffer_copy, aligned_width, aligned_height, 32, bytes_per_row);
	}

	// 1-Bit mode is big-endian
	if (depth == 1) {
		img->byte_order = MSBFirst;
		img->bitmap_bit_order = MSBFirst;
	}

#ifdef ENABLE_VOSF
	// Allocate a page-aligned chunk of memory for frame buffer
	the_buffer_size = align_on_page_boundary((aligned_height + 2) * img->bytes_per_line);
	the_host_buffer = the_buffer_copy;
	
	the_buffer_copy = (uint8 *)allocate_framebuffer(the_buffer_size);
	memset(the_buffer_copy, 0, the_buffer_size);
	
	the_buffer = (uint8 *)allocate_framebuffer(the_buffer_size);
	memset(the_buffer, 0, the_buffer_size);
#else
	// Allocate memory for frame buffer
	the_buffer = (uint8 *)malloc((aligned_height + 2) * img->bytes_per_line);
#endif

	// Create GC
	the_gc = XCreateGC(x_display, the_win, 0, 0);
	XSetState(x_display, the_gc, black_pixel, white_pixel, GXcopy, AllPlanes);

	// Create no_cursor
	mac_cursor = XCreatePixmapCursor(x_display,
	   XCreatePixmap(x_display, the_win, 1, 1, 1),
	   XCreatePixmap(x_display, the_win, 1, 1, 1),
	   &black, &white, 0, 0);
	XDefineCursor(x_display, the_win, mac_cursor);

	// Set VideoMonitor
	bool native_byte_order;
#ifdef WORDS_BIGENDIAN
	native_byte_order = (img->bitmap_bit_order == MSBFirst);
#else
	native_byte_order = (img->bitmap_bit_order == LSBFirst);
#endif
#ifdef ENABLE_VOSF
	do_update_framebuffer = GET_FBCOPY_FUNC(depth, native_byte_order, DISPLAY_WINDOW);
#endif
	set_video_monitor(width, height, img->bytes_per_line, native_byte_order);
	
#if REAL_ADDRESSING || DIRECT_ADDRESSING
	VideoMonitor.mac_frame_base = Host2MacAddr(the_buffer);
#else
	VideoMonitor.mac_frame_base = MacFrameBaseMac;
#endif
	return true;
}

// Init fbdev DGA display
static bool init_fbdev_dga(char *in_fb_name)
{
#ifdef ENABLE_FBDEV_DGA
	// Find the maximum depth available
	int ndepths, max_depth(0);
	int *depths = XListDepths(x_display, screen, &ndepths);
	if (depths == NULL) {
		printf("FATAL: Could not determine the maximal depth available\n");
		return false;
	} else {
		while (ndepths-- > 0) {
			if (depths[ndepths] > max_depth)
				max_depth = depths[ndepths];
		}
	}
	
	// Get fbdevices file path from preferences
	const char *fbd_path = PrefsFindString("fbdevicefile");
	
	// Open fbdevices file
	FILE *fp = fopen(fbd_path ? fbd_path : FBDEVICES_FILE_NAME, "r");
	if (fp == NULL) {
		char str[256];
		sprintf(str, GetString(STR_NO_FBDEVICE_FILE_ERR), fbd_path ? fbd_path : FBDEVICES_FILE_NAME, strerror(errno));
		ErrorAlert(str);
		return false;
	}
	
	int fb_depth;		// supported depth
	uint32 fb_offset;	// offset used for mmap(2)
	char fb_name[20];
	char line[256];
	bool device_found = false;
	while (fgets(line, 255, fp)) {
		// Read line
		int len = strlen(line);
		if (len == 0)
			continue;
		line[len - 1] = '\0';
		
		// Comments begin with "#" or ";"
		if ((line[0] == '#') || (line[0] == ';') || (line[0] == '\0'))
			continue;
		
		if ((sscanf(line, "%19s %d %x", &fb_name, &fb_depth, &fb_offset) == 3)
		&& (strcmp(fb_name, in_fb_name) == 0) && (fb_depth == max_depth)) {
			device_found = true;
			break;
		}
	}
	
	// fbdevices file completely read
	fclose(fp);
	
	// Frame buffer name not found ? Then, display warning
	if (!device_found) {
		char str[256];
		sprintf(str, GetString(STR_FBDEV_NAME_ERR), in_fb_name, max_depth);
		ErrorAlert(str);
		return false;
	}
	
	int width = DisplayWidth(x_display, screen);
	int height = DisplayHeight(x_display, screen);
	depth = fb_depth; // max_depth
	
	// Set relative mouse mode
	ADBSetRelMouseMode(false);
	
	// Create window
	XSetWindowAttributes wattr;
	wattr.event_mask		= eventmask = dga_eventmask;
	wattr.background_pixel	= white_pixel;
	wattr.override_redirect	= True;
	
	XSync(x_display, false);
	the_win = XCreateWindow(x_display, rootwin,
		0, 0, width, height,
		0, xdepth, InputOutput, vis,
		CWEventMask | CWBackPixel | CWOverrideRedirect,
		&wattr);
	XSync(x_display, false);
	XMapRaised(x_display, the_win);
	XSync(x_display, false);
	
	// Grab mouse and keyboard
	XGrabKeyboard(x_display, the_win, True,
		GrabModeAsync, GrabModeAsync, CurrentTime);
	XGrabPointer(x_display, the_win, True,
		PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
		GrabModeAsync, GrabModeAsync, the_win, None, CurrentTime);
	
	// Set colormap
	if (depth == 8)
		XSetWindowColormap(x_display, the_win, cmap[0]);
	
	// Set VideoMonitor
	int bytes_per_row = width;
	switch (depth) {
		case 1:
			bytes_per_row = ((width | 7) & ~7) >> 3;
			break;
		case 15:
		case 16:
			bytes_per_row *= 2;
			break;
		case 24:
		case 32:
			bytes_per_row *= 4;
			break;
	}
	
	if ((the_buffer = (uint8 *) mmap(NULL, height * bytes_per_row, PROT_READ | PROT_WRITE, MAP_PRIVATE, fbdev_fd, fb_offset)) == MAP_FAILED) {
		if ((the_buffer = (uint8 *) mmap(NULL, height * bytes_per_row, PROT_READ | PROT_WRITE, MAP_SHARED, fbdev_fd, fb_offset)) == MAP_FAILED) {
			char str[256];
			sprintf(str, GetString(STR_FBDEV_MMAP_ERR), strerror(errno));
			ErrorAlert(str);
			return false;
		}
	}
	
#if ENABLE_VOSF
#if REAL_ADDRESSING || DIRECT_ADDRESSING
	// If the blit function is null, i.e. just a copy of the buffer,
	// we first try to avoid the allocation of a temporary frame buffer
	use_vosf = true;
	do_update_framebuffer = GET_FBCOPY_FUNC(depth, true, DISPLAY_DGA);
	if (do_update_framebuffer == FBCOPY_FUNC(fbcopy_raw))
		use_vosf = false;
	
	if (use_vosf) {
		the_host_buffer = the_buffer;
		the_buffer_size = align_on_page_boundary((height + 2) * bytes_per_row);
		the_buffer_copy = (uint8 *)malloc(the_buffer_size);
		memset(the_buffer_copy, 0, the_buffer_size);
		the_buffer = (uint8 *)allocate_framebuffer(the_buffer_size);
		memset(the_buffer, 0, the_buffer_size);
	}
#else
	use_vosf = false;
#endif
#endif
	
	set_video_monitor(width, height, bytes_per_row, true);
#if REAL_ADDRESSING || DIRECT_ADDRESSING
	VideoMonitor.mac_frame_base = Host2MacAddr(the_buffer);
#else
	VideoMonitor.mac_frame_base = MacFrameBaseMac;
#endif
	return true;
#else
	ErrorAlert("Basilisk II has been compiled with fbdev DGA support disabled.");
	return false;
#endif
}

// Init XF86 DGA display
static bool init_xf86_dga(int width, int height)
{
#ifdef ENABLE_XF86_DGA
	// Set relative mouse mode
	ADBSetRelMouseMode(true);

#ifdef ENABLE_XF86_VIDMODE
	// Switch to best mode
	if (has_vidmode) {
		int best = 0;
		for (int i=1; i<num_x_video_modes; i++) {
			if (x_video_modes[i]->hdisplay >= width && x_video_modes[i]->vdisplay >= height &&
				x_video_modes[i]->hdisplay <= x_video_modes[best]->hdisplay && x_video_modes[i]->vdisplay <= x_video_modes[best]->vdisplay) {
				best = i;
			}
		}
		XF86VidModeSwitchToMode(x_display, screen, x_video_modes[best]);
		XF86VidModeSetViewPort(x_display, screen, 0, 0);
	}
#endif

	// Create window
	XSetWindowAttributes wattr;
	wattr.event_mask = eventmask = dga_eventmask;
	wattr.override_redirect = True;

	XSync(x_display, false);
	the_win = XCreateWindow(x_display, rootwin, 0, 0, width, height, 0, xdepth,
		InputOutput, vis, CWEventMask | CWOverrideRedirect, &wattr);
	XSync(x_display, false);
	XStoreName(x_display, the_win, GetString(STR_WINDOW_TITLE));
	XMapRaised(x_display, the_win);
	XSync(x_display, false);

	// Establish direct screen connection
	XMoveResizeWindow(x_display, the_win, 0, 0, width, height);
	XWarpPointer(x_display, None, rootwin, 0, 0, 0, 0, 0, 0);
	XGrabKeyboard(x_display, rootwin, True, GrabModeAsync, GrabModeAsync, CurrentTime);
	XGrabPointer(x_display, rootwin, True, PointerMotionMask | ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

	int v_width, v_bank, v_size;
	XF86DGAGetVideo(x_display, screen, (char **)&the_buffer, &v_width, &v_bank, &v_size);
	XF86DGADirectVideo(x_display, screen, XF86DGADirectGraphics | XF86DGADirectKeyb | XF86DGADirectMouse);
	XF86DGASetViewPort(x_display, screen, 0, 0);
	XF86DGASetVidPage(x_display, screen, 0);

	// Set colormap
	if (depth == 8) {
		XSetWindowColormap(x_display, the_win, cmap[current_dga_cmap = 0]);
		XF86DGAInstallColormap(x_display, screen, cmap[current_dga_cmap]);
	}

	// Set VideoMonitor
	int bytes_per_row = (v_width + 7) & ~7;
	switch (depth) {
		case 1:
			bytes_per_row /= 8;
			break;
		case 15:
		case 16:
			bytes_per_row *= 2;
			break;
		case 24:
		case 32:
			bytes_per_row *= 4;
			break;
	}
	
#if REAL_ADDRESSING || DIRECT_ADDRESSING
	// If the blit function is null, i.e. just a copy of the buffer,
	// we first try to avoid the allocation of a temporary frame buffer
	use_vosf = true;
	do_update_framebuffer = GET_FBCOPY_FUNC(depth, true, DISPLAY_DGA);
	if (do_update_framebuffer == FBCOPY_FUNC(fbcopy_raw))
		use_vosf = false;
	
	if (use_vosf) {
		the_host_buffer = the_buffer;
		the_buffer_size = align_on_page_boundary((height + 2) * bytes_per_row);
		the_buffer_copy = (uint8 *)malloc(the_buffer_size);
		memset(the_buffer_copy, 0, the_buffer_size);
		the_buffer = (uint8 *)allocate_framebuffer(the_buffer_size);
		memset(the_buffer, 0, the_buffer_size);
	}
#elif defined(ENABLE_VOSF)
	// The UAE memory handlers will already handle color conversion, if needed.
	use_vosf = false;
#endif
	
	set_video_monitor(width, height, bytes_per_row, true);
#if REAL_ADDRESSING || DIRECT_ADDRESSING
	VideoMonitor.mac_frame_base = Host2MacAddr(the_buffer);
//	MacFrameLayout = FLAYOUT_DIRECT;
#else
	VideoMonitor.mac_frame_base = MacFrameBaseMac;
#endif
	return true;
#else
	ErrorAlert("Basilisk II has been compiled with XF86 DGA support disabled.");
	return false;
#endif
}

// Init keycode translation table
static void keycode_init(void)
{
	bool use_kc = PrefsFindBool("keycodes");
	if (use_kc) {

		// Get keycode file path from preferences
		const char *kc_path = PrefsFindString("keycodefile");

		// Open keycode table
		FILE *f = fopen(kc_path ? kc_path : KEYCODE_FILE_NAME, "r");
		if (f == NULL) {
			char str[256];
			sprintf(str, GetString(STR_KEYCODE_FILE_WARN), kc_path ? kc_path : KEYCODE_FILE_NAME, strerror(errno));
			WarningAlert(str);
			return;
		}

		// Default translation table
		for (int i=0; i<256; i++)
			keycode_table[i] = -1;

		// Search for server vendor string, then read keycodes
		const char *vendor = ServerVendor(x_display);
		bool vendor_found = false;
		char line[256];
		while (fgets(line, 255, f)) {
			// Read line
			int len = strlen(line);
			if (len == 0)
				continue;
			line[len-1] = 0;

			// Comments begin with "#" or ";"
			if (line[0] == '#' || line[0] == ';' || line[0] == 0)
				continue;

			if (vendor_found) {
				// Read keycode
				int x_code, mac_code;
				if (sscanf(line, "%d %d", &x_code, &mac_code) == 2)
					keycode_table[x_code & 0xff] = mac_code;
				else
					break;
			} else {
				// Search for vendor string
				if (strstr(vendor, line) == vendor)
					vendor_found = true;
			}
		}

		// Keycode file completely read
		fclose(f);
		use_keycodes = vendor_found;

		// Vendor not found? Then display warning
		if (!vendor_found) {
			char str[256];
			sprintf(str, GetString(STR_KEYCODE_VENDOR_WARN), vendor, kc_path ? kc_path : KEYCODE_FILE_NAME);
			WarningAlert(str);
			return;
		}
	}
}

bool VideoInitBuffer()
{
#ifdef ENABLE_VOSF
	if (use_vosf) {
		const uint32 page_size	= getpagesize();
		const uint32 page_mask	= page_size - 1;
		
		mainBuffer.memBase      = (uint32) the_buffer;
		// Align the frame buffer on page boundary
		mainBuffer.memStart		= (uint32)((((unsigned long) the_buffer) + page_mask) & ~page_mask);
		mainBuffer.memLength	= the_buffer_size;
		mainBuffer.memEnd       = mainBuffer.memStart + mainBuffer.memLength;

		mainBuffer.pageSize     = page_size;
		mainBuffer.pageCount	= (mainBuffer.memLength + page_mask)/mainBuffer.pageSize;
		mainBuffer.pageBits     = log_base_2(mainBuffer.pageSize);

		if (mainBuffer.dirtyPages != 0)
			free(mainBuffer.dirtyPages);

		mainBuffer.dirtyPages = (uint8 *) malloc(mainBuffer.pageCount);

		if (mainBuffer.pageInfo != 0)
			free(mainBuffer.pageInfo);

		mainBuffer.pageInfo = (ScreenPageInfo *) malloc(mainBuffer.pageCount * sizeof(ScreenPageInfo));

		if ((mainBuffer.dirtyPages == 0) || (mainBuffer.pageInfo == 0))
			return false;

		PFLAG_CLEAR_ALL;

		uint32 a = 0;
		for (int i = 0; i < mainBuffer.pageCount; i++) {
			int y1 = a / VideoMonitor.bytes_per_row;
			if (y1 >= VideoMonitor.y)
				y1 = VideoMonitor.y - 1;

			int y2 = (a + mainBuffer.pageSize) / VideoMonitor.bytes_per_row;
			if (y2 >= VideoMonitor.y)
				y2 = VideoMonitor.y - 1;

			mainBuffer.pageInfo[i].top = y1;
			mainBuffer.pageInfo[i].bottom = y2;

			a += mainBuffer.pageSize;
			if (a > mainBuffer.memLength)
				a = mainBuffer.memLength;
		}
		
		// We can now write-protect the frame buffer
		if (mprotect((caddr_t)mainBuffer.memStart, mainBuffer.memLength, PROT_READ) != 0)
			return false;
	}
#endif
	return true;
}

bool VideoInit(bool classic)
{
#ifdef ENABLE_VOSF
	// Open /dev/zero
	zero_fd = open("/dev/zero", O_RDWR);
	if (zero_fd < 0) {
        char str[256];
		sprintf(str, GetString(STR_NO_DEV_ZERO_ERR), strerror(errno));
		ErrorAlert(str);
        return false;
	}
	
	// Zero the mainBuffer structure
	mainBuffer.dirtyPages = 0;
	mainBuffer.pageInfo = 0;
#endif
	
	// Check if X server runs on local machine
	local_X11 = (strncmp(XDisplayName(x_display_name), ":", 1) == 0)
	         || (strncmp(XDisplayName(x_display_name), "unix:", 5) == 0);
    
	// Init keycode translation
	keycode_init();

	// Read prefs
	mouse_wheel_mode = PrefsFindInt16("mousewheelmode");
	mouse_wheel_lines = PrefsFindInt16("mousewheellines");

	// Find screen and root window
	screen = XDefaultScreen(x_display);
	rootwin = XRootWindow(x_display, screen);
	
	// Get screen depth
	xdepth = DefaultDepth(x_display, screen);
	
#ifdef ENABLE_FBDEV_DGA
	// Frame buffer name
	char fb_name[20];
	
	// Could do fbdev dga ?
	if ((fbdev_fd = open(FBDEVICE_FILE_NAME, O_RDWR)) != -1)
		has_dga = true;
	else
		has_dga = false;
#endif

#ifdef ENABLE_XF86_DGA
	// DGA available?
	int dga_event_base, dga_error_base;
	if (local_X11 && XF86DGAQueryExtension(x_display, &dga_event_base, &dga_error_base)) {
		int dga_flags = 0;
		XF86DGAQueryDirectVideo(x_display, screen, &dga_flags);
		has_dga = dga_flags & XF86DGADirectPresent;
	} else
		has_dga = false;
#endif

#ifdef ENABLE_XF86_VIDMODE
	// VidMode available?
	int vm_event_base, vm_error_base;
	has_vidmode = XF86VidModeQueryExtension(x_display, &vm_event_base, &vm_error_base);
	if (has_vidmode)
		XF86VidModeGetAllModeLines(x_display, screen, &num_x_video_modes, &x_video_modes);
#endif
	
	// Find black and white colors
	XParseColor(x_display, DefaultColormap(x_display, screen), "rgb:00/00/00", &black);
	XAllocColor(x_display, DefaultColormap(x_display, screen), &black);
	XParseColor(x_display, DefaultColormap(x_display, screen), "rgb:ff/ff/ff", &white);
	XAllocColor(x_display, DefaultColormap(x_display, screen), &white);
	black_pixel = BlackPixel(x_display, screen);
	white_pixel = WhitePixel(x_display, screen);

	// Get appropriate visual
	int color_class;
	switch (xdepth) {
		case 1:
			color_class = StaticGray;
			break;
		case 8:
			color_class = PseudoColor;
			break;
		case 15:
		case 16:
		case 24:
		case 32:
			color_class = TrueColor;
			break;
		default:
			ErrorAlert(GetString(STR_UNSUPP_DEPTH_ERR));
			return false;
	}
	if (!XMatchVisualInfo(x_display, screen, xdepth, color_class, &visualInfo)) {
		ErrorAlert(GetString(STR_NO_XVISUAL_ERR));
		return false;
	}
	if (visualInfo.depth != xdepth) {
		ErrorAlert(GetString(STR_NO_XVISUAL_ERR));
		return false;
	}
	vis = visualInfo.visual;

	// Mac screen depth is always 1 bit in Classic mode, but follows X depth otherwise
	classic_mode = classic;
	if (classic)
		depth = 1;
	else
		depth = xdepth;

	// Create color maps for 8 bit mode
	if (depth == 8) {
		cmap[0] = XCreateColormap(x_display, rootwin, vis, AllocAll);
		cmap[1] = XCreateColormap(x_display, rootwin, vis, AllocAll);
		XInstallColormap(x_display, cmap[0]);
		XInstallColormap(x_display, cmap[1]);
	}

	// Get screen mode from preferences
	const char *mode_str;
	if (classic)
		mode_str = "win/512/342";
	else
		mode_str = PrefsFindString("screen");

	// Determine type and mode
	int width = 512, height = 384;
	display_type = DISPLAY_WINDOW;
	if (mode_str) {
		if (sscanf(mode_str, "win/%d/%d", &width, &height) == 2)
			display_type = DISPLAY_WINDOW;
#ifdef ENABLE_FBDEV_DGA
		else if (has_dga && sscanf(mode_str, "dga/%19s", fb_name) == 1) {
#else
		else if (has_dga && sscanf(mode_str, "dga/%d/%d", &width, &height) == 2) {
#endif
			display_type = DISPLAY_DGA;
			if (width > DisplayWidth(x_display, screen))
				width = DisplayWidth(x_display, screen);
			if (height > DisplayHeight(x_display, screen))
				height = DisplayHeight(x_display, screen);
		}
		if (width <= 0)
			width = DisplayWidth(x_display, screen);
		if (height <= 0)
			height = DisplayHeight(x_display, screen);
	}

	// Initialize according to display type
	switch (display_type) {
		case DISPLAY_WINDOW:
			if (!init_window(width, height))
				return false;
			break;
		case DISPLAY_DGA:
#ifdef ENABLE_FBDEV_DGA
			if (!init_fbdev_dga(fb_name))
#else
			if (!init_xf86_dga(width, height))
#endif
				return false;
			break;
	}

#ifdef HAVE_PTHREADS
	// Lock down frame buffer
	pthread_mutex_lock(&frame_buffer_lock);
#endif

#if !REAL_ADDRESSING && !DIRECT_ADDRESSING
	// Set variables for UAE memory mapping
	MacFrameBaseHost = the_buffer;
	MacFrameSize = VideoMonitor.bytes_per_row * VideoMonitor.y;

	// No special frame buffer in Classic mode (frame buffer is in Mac RAM)
	if (classic)
		MacFrameLayout = FLAYOUT_NONE;
#endif

#ifdef ENABLE_VOSF
	if (use_vosf) {
		// Initialize the mainBuffer structure
		if (!VideoInitBuffer()) {
			// TODO: STR_VOSF_INIT_ERR ?
			ErrorAlert("Could not initialize Video on SEGV signals");
        	return false;
		}

		// Initialize the handler for SIGSEGV
		if (!Screen_fault_handler_init()) {
			// TODO: STR_VOSF_INIT_ERR ?
			ErrorAlert("Could not initialize Video on SEGV signals");
			return false;
		}
	}
#endif
	
	// Initialize VideoRefresh function
	VideoRefreshInit();
	
	XSync(x_display, false);

#ifdef HAVE_PTHREADS
	// Start redraw/input thread
	redraw_thread_active = (pthread_create(&redraw_thread, NULL, redraw_func, NULL) == 0);
	if (!redraw_thread_active) {
		printf("FATAL: cannot create redraw thread\n");
		return false;
	}
#endif
	return true;
}


/*
 *  Deinitialization
 */

void VideoExit(void)
{
#ifdef HAVE_PTHREADS
	// Stop redraw thread
	if (redraw_thread_active) {
		redraw_thread_cancel = true;
#ifdef HAVE_PTHREAD_CANCEL
		pthread_cancel(redraw_thread);
#endif
		pthread_join(redraw_thread, NULL);
		redraw_thread_active = false;
	}
#endif

#ifdef HAVE_PTHREADS
	// Unlock frame buffer
	pthread_mutex_unlock(&frame_buffer_lock);
#endif

	// Close window and server connection
	if (x_display != NULL) {
		XSync(x_display, false);

#ifdef ENABLE_XF86_DGA
		if (display_type == DISPLAY_DGA) {
			XF86DGADirectVideo(x_display, screen, 0);
			XUngrabPointer(x_display, CurrentTime);
			XUngrabKeyboard(x_display, CurrentTime);
		}
#endif

#ifdef ENABLE_XF86_VIDMODE
		if (has_vidmode && display_type == DISPLAY_DGA)
			XF86VidModeSwitchToMode(x_display, screen, x_video_modes[0]);
#endif

#ifdef ENABLE_FBDEV_DGA
		if (display_type == DISPLAY_DGA) {
			XUngrabPointer(x_display, CurrentTime);
			XUngrabKeyboard(x_display, CurrentTime);
			close(fbdev_fd);
		}
#endif

		XFlush(x_display);
		XSync(x_display, false);
		if (depth == 8) {
			XFreeColormap(x_display, cmap[0]);
			XFreeColormap(x_display, cmap[1]);
		}
		
		if (!use_vosf) {
			if (the_buffer) {
				free(the_buffer);
				the_buffer = NULL;
			}

			if (!have_shm && the_buffer_copy) {
				free(the_buffer_copy);
				the_buffer_copy = NULL;
			}
		}
#ifdef ENABLE_VOSF
		else {
			if (the_buffer != (uint8 *)MAP_FAILED) {
				munmap((caddr_t)the_buffer, the_buffer_size);
				the_buffer = 0;
			}
			
			if (the_buffer_copy != (uint8 *)MAP_FAILED) {
				munmap((caddr_t)the_buffer_copy, the_buffer_size);
				the_buffer_copy = 0;
			}
		}
#endif
	}
	
#ifdef ENABLE_VOSF
	if (use_vosf) {
		// Clear mainBuffer data
		if (mainBuffer.pageInfo) {
			free(mainBuffer.pageInfo);
			mainBuffer.pageInfo = 0;
		}

		if (mainBuffer.dirtyPages) {
			free(mainBuffer.dirtyPages);
			mainBuffer.dirtyPages = 0;
		}
	}

	// Close /dev/zero
	if (zero_fd > 0)
		close(zero_fd);
#endif
}


/*
 *  Close down full-screen mode (if bringing up error alerts is unsafe while in full-screen mode)
 */

void VideoQuitFullScreen(void)
{
	D(bug("VideoQuitFullScreen()\n"));
	if (display_type == DISPLAY_DGA)
		quit_full_screen = true;
}


/*
 *  Mac VBL interrupt
 */

void VideoInterrupt(void)
{
	// Emergency quit requested? Then quit
	if (emerg_quit)
		QuitEmulator();

#ifdef HAVE_PTHREADS
	// Temporarily give up frame buffer lock (this is the point where
	// we are suspended when the user presses Ctrl-Tab)
	pthread_mutex_unlock(&frame_buffer_lock);
	pthread_mutex_lock(&frame_buffer_lock);
#endif
}


/*
 *  Set palette
 */

void video_set_palette(uint8 *pal)
{
#ifdef HAVE_PTHREADS
	pthread_mutex_lock(&palette_lock);
#endif

	// Convert colors to XColor array
	for (int i=0; i<256; i++) {
		palette[i].pixel = i;
		palette[i].red = pal[i*3] * 0x0101;
		palette[i].green = pal[i*3+1] * 0x0101;
		palette[i].blue = pal[i*3+2] * 0x0101;
		palette[i].flags = DoRed | DoGreen | DoBlue;
	}

	// Tell redraw thread to change palette
	palette_changed = true;

#ifdef HAVE_PTHREADS
	pthread_mutex_unlock(&palette_lock);
#endif
}


/*
 *  Suspend/resume emulator
 */

#if defined(ENABLE_XF86_DGA) || defined(ENABLE_FBDEV_DGA)
static void suspend_emul(void)
{
	if (display_type == DISPLAY_DGA) {
		// Release ctrl key
		ADBKeyUp(0x36);
		ctrl_down = false;

#ifdef HAVE_PTHREADS
		// Lock frame buffer (this will stop the MacOS thread)
		pthread_mutex_lock(&frame_buffer_lock);
#endif

		// Save frame buffer
		fb_save = malloc(VideoMonitor.y * VideoMonitor.bytes_per_row);
		if (fb_save)
			memcpy(fb_save, the_buffer, VideoMonitor.y * VideoMonitor.bytes_per_row);

		// Close full screen display
#ifdef ENABLE_XF86_DGA
		XF86DGADirectVideo(x_display, screen, 0);
#endif
		XUngrabPointer(x_display, CurrentTime);
		XUngrabKeyboard(x_display, CurrentTime);
		XUnmapWindow(x_display, the_win);
		XSync(x_display, false);

		// Open "suspend" window
		XSetWindowAttributes wattr;
		wattr.event_mask = KeyPressMask;
		wattr.background_pixel = black_pixel;
		wattr.backing_store = WhenMapped;
		wattr.backing_planes = xdepth;
		wattr.colormap = DefaultColormap(x_display, screen);
		
		XSync(x_display, false);
		suspend_win = XCreateWindow(x_display, rootwin, 0, 0, 512, 1, 0, xdepth,
			InputOutput, vis, CWEventMask | CWBackPixel | CWBackingStore |
			CWBackingPlanes | (xdepth == 8 ? CWColormap : 0), &wattr);
		XSync(x_display, false);
		XStoreName(x_display, suspend_win, GetString(STR_SUSPEND_WINDOW_TITLE));
		XMapRaised(x_display, suspend_win);
		XSync(x_display, false);
		emul_suspended = true;
	}
}

static void resume_emul(void)
{
	// Close "suspend" window
	XDestroyWindow(x_display, suspend_win);
	XSync(x_display, false);

	// Reopen full screen display
	XMapRaised(x_display, the_win);
	XWarpPointer(x_display, None, rootwin, 0, 0, 0, 0, 0, 0);
	XSync(x_display, false);
	XGrabKeyboard(x_display, rootwin, 1, GrabModeAsync, GrabModeAsync, CurrentTime);
	XGrabPointer(x_display, rootwin, 1, PointerMotionMask | ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
#ifdef ENABLE_XF86_DGA
	XF86DGADirectVideo(x_display, screen, XF86DGADirectGraphics | XF86DGADirectKeyb | XF86DGADirectMouse);
	XF86DGASetViewPort(x_display, screen, 0, 0);
#endif
	XSync(x_display, false);
	
	// the_buffer already contains the data to restore. i.e. since a temporary
	// frame buffer is used when VOSF is actually used, fb_save is therefore
	// not necessary.
#ifdef ENABLE_VOSF
	if (use_vosf) {
#ifdef HAVE_PTHREADS
		pthread_mutex_lock(&Screen_draw_lock);
#endif
		PFLAG_SET_ALL;
#ifdef HAVE_PTHREADS
		pthread_mutex_unlock(&Screen_draw_lock);
#endif
		memset(the_buffer_copy, 0, VideoMonitor.bytes_per_row * VideoMonitor.y);
	}
#endif
	
	// Restore frame buffer
	if (fb_save) {
#ifdef ENABLE_VOSF
		// Don't copy fb_save to the temporary frame buffer in VOSF mode
		if (!use_vosf)
#endif
		memcpy(the_buffer, fb_save, VideoMonitor.y * VideoMonitor.bytes_per_row);
		free(fb_save);
		fb_save = NULL;
	}
	
#ifdef ENABLE_XF86_DGA
	if (depth == 8)
		XF86DGAInstallColormap(x_display, screen, cmap[current_dga_cmap]);
#endif

#ifdef HAVE_PTHREADS
	// Unlock frame buffer (and continue MacOS thread)
	pthread_mutex_unlock(&frame_buffer_lock);
	emul_suspended = false;
#endif
}
#endif


/*
 *  Translate key event to Mac keycode
 */

static int kc_decode(KeySym ks)
{
	switch (ks) {
		case XK_A: case XK_a: return 0x00;
		case XK_B: case XK_b: return 0x0b;
		case XK_C: case XK_c: return 0x08;
		case XK_D: case XK_d: return 0x02;
		case XK_E: case XK_e: return 0x0e;
		case XK_F: case XK_f: return 0x03;
		case XK_G: case XK_g: return 0x05;
		case XK_H: case XK_h: return 0x04;
		case XK_I: case XK_i: return 0x22;
		case XK_J: case XK_j: return 0x26;
		case XK_K: case XK_k: return 0x28;
		case XK_L: case XK_l: return 0x25;
		case XK_M: case XK_m: return 0x2e;
		case XK_N: case XK_n: return 0x2d;
		case XK_O: case XK_o: return 0x1f;
		case XK_P: case XK_p: return 0x23;
		case XK_Q: case XK_q: return 0x0c;
		case XK_R: case XK_r: return 0x0f;
		case XK_S: case XK_s: return 0x01;
		case XK_T: case XK_t: return 0x11;
		case XK_U: case XK_u: return 0x20;
		case XK_V: case XK_v: return 0x09;
		case XK_W: case XK_w: return 0x0d;
		case XK_X: case XK_x: return 0x07;
		case XK_Y: case XK_y: return 0x10;
		case XK_Z: case XK_z: return 0x06;

		case XK_1: case XK_exclam: return 0x12;
		case XK_2: case XK_at: return 0x13;
		case XK_3: case XK_numbersign: return 0x14;
		case XK_4: case XK_dollar: return 0x15;
		case XK_5: case XK_percent: return 0x17;
		case XK_6: return 0x16;
		case XK_7: return 0x1a;
		case XK_8: return 0x1c;
		case XK_9: return 0x19;
		case XK_0: return 0x1d;

		case XK_grave: case XK_asciitilde: return 0x0a;
		case XK_minus: case XK_underscore: return 0x1b;
		case XK_equal: case XK_plus: return 0x18;
		case XK_bracketleft: case XK_braceleft: return 0x21;
		case XK_bracketright: case XK_braceright: return 0x1e;
		case XK_backslash: case XK_bar: return 0x2a;
		case XK_semicolon: case XK_colon: return 0x29;
		case XK_apostrophe: case XK_quotedbl: return 0x27;
		case XK_comma: case XK_less: return 0x2b;
		case XK_period: case XK_greater: return 0x2f;
		case XK_slash: case XK_question: return 0x2c;

#if defined(ENABLE_XF86_DGA) || defined(ENABLE_FBDEV_DGA)
		case XK_Tab: if (ctrl_down) {suspend_emul(); return -1;} else return 0x30;
#else
		case XK_Tab: return 0x30;
#endif
		case XK_Return: return 0x24;
		case XK_space: return 0x31;
		case XK_BackSpace: return 0x33;

		case XK_Delete: return 0x75;
		case XK_Insert: return 0x72;
		case XK_Home: case XK_Help: return 0x73;
		case XK_End: return 0x77;
#ifdef __hpux
		case XK_Prior: return 0x74;
		case XK_Next: return 0x79;
#else
		case XK_Page_Up: return 0x74;
		case XK_Page_Down: return 0x79;
#endif

		case XK_Control_L: return 0x36;
		case XK_Control_R: return 0x36;
		case XK_Shift_L: return 0x38;
		case XK_Shift_R: return 0x38;
		case XK_Alt_L: return 0x37;
		case XK_Alt_R: return 0x37;
		case XK_Meta_L: return 0x3a;
		case XK_Meta_R: return 0x3a;
		case XK_Menu: return 0x32;
		case XK_Caps_Lock: return 0x39;
		case XK_Num_Lock: return 0x47;

		case XK_Up: return 0x3e;
		case XK_Down: return 0x3d;
		case XK_Left: return 0x3b;
		case XK_Right: return 0x3c;

		case XK_Escape: if (ctrl_down) {quit_full_screen = true; emerg_quit = true; return -1;} else return 0x35;

		case XK_F1: if (ctrl_down) {SysMountFirstFloppy(); return -1;} else return 0x7a;
		case XK_F2: return 0x78;
		case XK_F3: return 0x63;
		case XK_F4: return 0x76;
		case XK_F5: return 0x60;
		case XK_F6: return 0x61;
		case XK_F7: return 0x62;
		case XK_F8: return 0x64;
		case XK_F9: return 0x65;
		case XK_F10: return 0x6d;
		case XK_F11: return 0x67;
		case XK_F12: return 0x6f;

		case XK_Print: return 0x69;
		case XK_Scroll_Lock: return 0x6b;
		case XK_Pause: return 0x71;

#if defined(XK_KP_Prior) && defined(XK_KP_Left) && defined(XK_KP_Insert) && defined (XK_KP_End)
		case XK_KP_0: case XK_KP_Insert: return 0x52;
		case XK_KP_1: case XK_KP_End: return 0x53;
		case XK_KP_2: case XK_KP_Down: return 0x54;
		case XK_KP_3: case XK_KP_Next: return 0x55;
		case XK_KP_4: case XK_KP_Left: return 0x56;
		case XK_KP_5: case XK_KP_Begin: return 0x57;
		case XK_KP_6: case XK_KP_Right: return 0x58;
		case XK_KP_7: case XK_KP_Home: return 0x59;
		case XK_KP_8: case XK_KP_Up: return 0x5b;
		case XK_KP_9: case XK_KP_Prior: return 0x5c;
		case XK_KP_Decimal: case XK_KP_Delete: return 0x41;
#else
		case XK_KP_0: return 0x52;
		case XK_KP_1: return 0x53;
		case XK_KP_2: return 0x54;
		case XK_KP_3: return 0x55;
		case XK_KP_4: return 0x56;
		case XK_KP_5: return 0x57;
		case XK_KP_6: return 0x58;
		case XK_KP_7: return 0x59;
		case XK_KP_8: return 0x5b;
		case XK_KP_9: return 0x5c;
		case XK_KP_Decimal: return 0x41;
#endif
		case XK_KP_Add: return 0x45;
		case XK_KP_Subtract: return 0x4e;
		case XK_KP_Multiply: return 0x43;
		case XK_KP_Divide: return 0x4b;
		case XK_KP_Enter: return 0x4c;
		case XK_KP_Equal: return 0x51;
	}
	return -1;
}

static int event2keycode(XKeyEvent *ev)
{
	KeySym ks;
	int as;
	int i = 0;

	do {
		ks = XLookupKeysym(ev, i++);
		as = kc_decode(ks);
		if (as != -1)
			return as;
	} while (ks != NoSymbol);

	return -1;
}


/*
 *  X event handling
 */

static void handle_events(void)
{
	XEvent event;
	for (;;) {
		if (!XCheckMaskEvent(x_display, eventmask, &event))
			break;

		switch (event.type) {
			// Mouse button
			case ButtonPress: {
				unsigned int button = ((XButtonEvent *)&event)->button;
				if (button < 4)
					ADBMouseDown(button - 1);
				else if (button < 6) {	// Wheel mouse
					if (mouse_wheel_mode == 0) {
						int key = (button == 5) ? 0x79 : 0x74;	// Page up/down
						ADBKeyDown(key);
						ADBKeyUp(key);
					} else {
						int key = (button == 5) ? 0x3d : 0x3e;	// Cursor up/down
						for(int i=0; i<mouse_wheel_lines; i++) {
							ADBKeyDown(key);
							ADBKeyUp(key);
						}
					}
				}
				break;
			}
			case ButtonRelease: {
				unsigned int button = ((XButtonEvent *)&event)->button;
				if (button < 4)
					ADBMouseUp(button - 1);
				break;
			}

			// Mouse moved
			case EnterNotify:
				ADBMouseMoved(((XMotionEvent *)&event)->x, ((XMotionEvent *)&event)->y);
				break;
			case MotionNotify:
				ADBMouseMoved(((XMotionEvent *)&event)->x, ((XMotionEvent *)&event)->y);
				break;

			// Keyboard
			case KeyPress: {
				int code;
				if (use_keycodes) {
					event2keycode((XKeyEvent *)&event);	// This is called to process the hotkeys
					code = keycode_table[((XKeyEvent *)&event)->keycode & 0xff];
				} else
					code = event2keycode((XKeyEvent *)&event);
				if (code != -1) {
					if (!emul_suspended) {
						if (code == 0x39) {	// Caps Lock pressed
							if (caps_on) {
								ADBKeyUp(code);
								caps_on = false;
							} else {
								ADBKeyDown(code);
								caps_on = true;
							}
						} else
							ADBKeyDown(code);
						if (code == 0x36)
							ctrl_down = true;
					} else {
#if defined(ENABLE_XF86_DGA) || defined(ENABLE_FBDEV_DGA)
						if (code == 0x31)
							resume_emul();	// Space wakes us up
#endif
					}
				}
				break;
			}
			case KeyRelease: {
				int code;
				if (use_keycodes) {
					event2keycode((XKeyEvent *)&event);	// This is called to process the hotkeys
					code = keycode_table[((XKeyEvent *)&event)->keycode & 0xff];
				} else
					code = event2keycode((XKeyEvent *)&event);
				if (code != -1 && code != 0x39) {	// Don't propagate Caps Lock releases
					ADBKeyUp(code);
					if (code == 0x36)
						ctrl_down = false;
				}
				break;
			}

			// Hidden parts exposed, force complete refresh of window
			case Expose:
				if (display_type == DISPLAY_WINDOW) {
#ifdef ENABLE_VOSF
					if (use_vosf) {			// VOSF refresh
#ifdef HAVE_PTHREADS
						pthread_mutex_lock(&Screen_draw_lock);
#endif
						PFLAG_SET_ALL;
#ifdef HAVE_PTHREADS
						pthread_mutex_unlock(&Screen_draw_lock);
#endif
						memset(the_buffer_copy, 0, VideoMonitor.bytes_per_row * VideoMonitor.y);
					}
					else
#endif
					if (frame_skip == 0) {	// Dynamic refresh
						int x1, y1;
						for (y1=0; y1<16; y1++)
						for (x1=0; x1<16; x1++)
							updt_box[x1][y1] = true;
						nr_boxes = 16 * 16;
					} else					// Static refresh
						memset(the_buffer_copy, 0, VideoMonitor.bytes_per_row * VideoMonitor.y);
				}
				break;

			case FocusIn:
			case FocusOut:
				break;
		}
	}
}


/*
 *  Window display update
 */

// Dynamic display update (variable frame rate for each box)
static void update_display_dynamic(int ticker)
{
	int y1, y2, y2s, y2a, i, x1, xm, xmo, ymo, yo, yi, yil, xi;
	int xil = 0;
	int rxm = 0, rxmo = 0;
	int bytes_per_row = VideoMonitor.bytes_per_row;
	int bytes_per_pixel = VideoMonitor.bytes_per_row / VideoMonitor.x;
	int rx = VideoMonitor.bytes_per_row / 16;
	int ry = VideoMonitor.y / 16;
	int max_box;

	y2s = sm_uptd[ticker % 8];
	y2a = 8;
	for (i = 0; i < 6; i++)
		if (ticker % (2 << i))
			break;
	max_box = sm_no_boxes[i];

	if (y2a) {
		for (y1=0; y1<16; y1++) {
			for (y2=y2s; y2 < ry; y2 += y2a) {
				i = ((y1 * ry) + y2) * bytes_per_row; 
				for (x1=0; x1<16; x1++, i += rx) {
					if (updt_box[x1][y1] == false) {
						if (memcmp(&the_buffer_copy[i], &the_buffer[i], rx)) {
							updt_box[x1][y1] = true;
							nr_boxes++;
						}
					}
				}
			}
		}
	}

	if ((nr_boxes <= max_box) && (nr_boxes)) {
		for (y1=0; y1<16; y1++) {
			for (x1=0; x1<16; x1++) {
				if (updt_box[x1][y1] == true) {
					if (rxm == 0)
						xm = x1;
					rxm += rx; 
					updt_box[x1][y1] = false;
				}
				if (((updt_box[x1+1][y1] == false) || (x1 == 15)) && (rxm)) {
					if ((rxmo != rxm) || (xmo != xm) || (yo != y1 - 1)) {
						if (rxmo) {
							xi = xmo * rx;
							yi = ymo * ry;
							xil = rxmo;
							yil = (yo - ymo +1) * ry;
						}
						rxmo = rxm;
						xmo = xm;
						ymo = y1;
					}
					rxm = 0;
					yo = y1;
				}	
				if (xil) {
					i = (yi * bytes_per_row) + xi;
					for (y2=0; y2 < yil; y2++, i += bytes_per_row)
						memcpy(&the_buffer_copy[i], &the_buffer[i], xil);
					if (depth == 1) {
						if (have_shm)
							XShmPutImage(x_display, the_win, the_gc, img, xi * 8, yi, xi * 8, yi, xil * 8, yil, 0);
						else
							XPutImage(x_display, the_win, the_gc, img, xi * 8, yi, xi * 8, yi, xil * 8, yil);
					} else {
						if (have_shm)
							XShmPutImage(x_display, the_win, the_gc, img, xi / bytes_per_pixel, yi, xi / bytes_per_pixel, yi, xil / bytes_per_pixel, yil, 0);
						else
							XPutImage(x_display, the_win, the_gc, img, xi / bytes_per_pixel, yi, xi / bytes_per_pixel, yi, xil / bytes_per_pixel, yil);
					}
					xil = 0;
				}
				if ((x1 == 15) && (y1 == 15) && (rxmo)) {
					x1--;
					xi = xmo * rx;
					yi = ymo * ry;
					xil = rxmo;
					yil = (yo - ymo +1) * ry;
					rxmo = 0;
				}
			}
		}
		nr_boxes = 0;
	}
}

// Static display update (fixed frame rate, but incremental)
static void update_display_static(void)
{
	// Incremental update code
	int wide = 0, high = 0, x1, x2, y1, y2, i, j;
	int bytes_per_row = VideoMonitor.bytes_per_row;
	int bytes_per_pixel = VideoMonitor.bytes_per_row / VideoMonitor.x;
	uint8 *p, *p2;

	// Check for first line from top and first line from bottom that have changed
	y1 = 0;
	for (j=0; j<VideoMonitor.y; j++) {
		if (memcmp(&the_buffer[j * bytes_per_row], &the_buffer_copy[j * bytes_per_row], bytes_per_row)) {
			y1 = j;
			break;
		}
	}
	y2 = y1 - 1;
	for (j=VideoMonitor.y-1; j>=y1; j--) {
		if (memcmp(&the_buffer[j * bytes_per_row], &the_buffer_copy[j * bytes_per_row], bytes_per_row)) {
			y2 = j;
			break;
		}
	}
	high = y2 - y1 + 1;

	// Check for first column from left and first column from right that have changed
	if (high) {
		if (depth == 1) {
			x1 = VideoMonitor.x - 1;
			for (j=y1; j<=y2; j++) {
				p = &the_buffer[j * bytes_per_row];
				p2 = &the_buffer_copy[j * bytes_per_row];
				for (i=0; i<(x1>>3); i++) {
					if (*p != *p2) {
						x1 = i << 3;
						break;
					}
					p++; p2++;
				}
			}
			x2 = x1;
			for (j=y1; j<=y2; j++) {
				p = &the_buffer[j * bytes_per_row];
				p2 = &the_buffer_copy[j * bytes_per_row];
				p += bytes_per_row;
				p2 += bytes_per_row;
				for (i=(VideoMonitor.x>>3); i>(x2>>3); i--) {
					p--; p2--;
					if (*p != *p2) {
						x2 = (i << 3) + 7;
						break;
					}
				}
			}
			wide = x2 - x1 + 1;

			// Update copy of the_buffer
			if (high && wide) {
				for (j=y1; j<=y2; j++) {
					i = j * bytes_per_row + (x1 >> 3);
					memcpy(the_buffer_copy + i, the_buffer + i, wide >> 3);
				}
			}

		} else {
			x1 = VideoMonitor.x;
			for (j=y1; j<=y2; j++) {
				p = &the_buffer[j * bytes_per_row];
				p2 = &the_buffer_copy[j * bytes_per_row];
				for (i=0; i<x1*bytes_per_pixel; i++) {
					if (*p != *p2) {
						x1 = i / bytes_per_pixel;
						break;
					}
					p++; p2++;
				}
			}
			x2 = x1;
			for (j=y1; j<=y2; j++) {
				p = &the_buffer[j * bytes_per_row];
				p2 = &the_buffer_copy[j * bytes_per_row];
				p += bytes_per_row;
				p2 += bytes_per_row;
				for (i=VideoMonitor.x*bytes_per_pixel; i>x2*bytes_per_pixel; i--) {
					p--;
					p2--;
					if (*p != *p2) {
						x2 = i / bytes_per_pixel;
						break;
					}
				}
			}
			wide = x2 - x1;

			// Update copy of the_buffer
			if (high && wide) {
				for (j=y1; j<=y2; j++) {
					i = j * bytes_per_row + x1 * bytes_per_pixel;
					memcpy(the_buffer_copy + i, the_buffer + i, bytes_per_pixel * wide);
				}
			}
		}
	}

	// Refresh display
	if (high && wide) {
		if (have_shm)
			XShmPutImage(x_display, the_win, the_gc, img, x1, y1, x1, y1, wide, high, 0);
		else
			XPutImage(x_display, the_win, the_gc, img, x1, y1, x1, y1, wide, high);
	}
}


/*
 *	Screen refresh functions
 */

// The specialisations hereunder are meant to enable VOSF with DGA in direct
// addressing mode in case the address spaces (RAM, ROM, FrameBuffer) could
// not get mapped correctly with respect to the predetermined host frame
// buffer base address.
//
// Hmm, in other words, when in direct addressing mode and DGA is requested,
// we first try to "triple allocate" the address spaces according to the real
// host frame buffer address. Then, if it fails, we will use a temporary
// frame buffer thus making the real host frame buffer updated when pages
// of the temp frame buffer are altered.
//
// As a side effect, a little speed gain in screen updates could be noticed
// for other modes than DGA.
//
// The following two functions below are inline so that a clever compiler
// could specialise the code according to the current screen depth and
// display type. A more clever compiler would the job by itself though...
// (update_display_vosf is inlined as well)

static inline void possibly_quit_dga_mode()
{
#if defined(ENABLE_XF86_DGA) || defined(ENABLE_FBDEV_DGA)
	// Quit DGA mode if requested
	if (quit_full_screen) {
		quit_full_screen = false;
#ifdef ENABLE_XF86_DGA
		XF86DGADirectVideo(x_display, screen, 0);
#endif
		XUngrabPointer(x_display, CurrentTime);
		XUngrabKeyboard(x_display, CurrentTime);
		XUnmapWindow(x_display, the_win);
		XSync(x_display, false);
	}
#endif
}

static inline void handle_palette_changes(int depth, int display_type)
{
#ifdef HAVE_PTHREADS
	pthread_mutex_lock(&palette_lock);
#endif
	if (palette_changed) {
		palette_changed = false;
		if (depth == 8) {
			XStoreColors(x_display, cmap[0], palette, 256);
			XStoreColors(x_display, cmap[1], palette, 256);
				
#ifdef ENABLE_XF86_DGA
			if (display_type == DISPLAY_DGA) {
				current_dga_cmap ^= 1;
				XF86DGAInstallColormap(x_display, screen, cmap[current_dga_cmap]);
			}
#endif
		}
	}
#ifdef HAVE_PTHREADS
	pthread_mutex_unlock(&palette_lock);
#endif
}

static void video_refresh_dga(void)
{
	// Quit DGA mode if requested
	possibly_quit_dga_mode();
	
	// Handle X events
	handle_events();
	
	// Handle palette changes
	handle_palette_changes(depth, DISPLAY_DGA);
}

#ifdef ENABLE_VOSF
#if REAL_ADDRESSING || DIRECT_ADDRESSING
static void video_refresh_dga_vosf(void)
{
	// Quit DGA mode if requested
	possibly_quit_dga_mode();
	
	// Handle X events
	handle_events();
	
	// Handle palette changes
	handle_palette_changes(depth, DISPLAY_DGA);
	
	// Update display (VOSF variant)
	static int tick_counter = 0;
	if (++tick_counter >= frame_skip) {
		tick_counter = 0;
#ifdef HAVE_PTHREADS
		pthread_mutex_lock(&Screen_draw_lock);
#endif
		update_display_dga_vosf();
#ifdef HAVE_PTHREADS
		pthread_mutex_unlock(&Screen_draw_lock);
#endif
	}
}
#endif

static void video_refresh_window_vosf(void)
{
	// Quit DGA mode if requested
	possibly_quit_dga_mode();
	
	// Handle X events
	handle_events();
	
	// Handle palette changes
	handle_palette_changes(depth, DISPLAY_WINDOW);
	
	// Update display (VOSF variant)
	static int tick_counter = 0;
	if (++tick_counter >= frame_skip) {
		tick_counter = 0;
#ifdef HAVE_PTHREADS
		pthread_mutex_lock(&Screen_draw_lock);
#endif
		update_display_window_vosf();
#ifdef HAVE_PTHREADS
		pthread_mutex_unlock(&Screen_draw_lock);
#endif
	}
}
#endif // def ENABLE_VOSF

static void video_refresh_window_static(void)
{
	// Handle X events
	handle_events();
	
	// Handle_palette changes
	handle_palette_changes(depth, DISPLAY_WINDOW);
	
	// Update display (static variant)
	static int tick_counter = 0;
	if (++tick_counter >= frame_skip) {
		tick_counter = 0;
		update_display_static();
	}
}

static void video_refresh_window_dynamic(void)
{
	// Handle X events
	handle_events();
	
	// Handle_palette changes
	handle_palette_changes(depth, DISPLAY_WINDOW);
	
	// Update display (dynamic variant)
	static int tick_counter = 0;
	tick_counter++;
	update_display_dynamic(tick_counter);
}


/*
 *  Thread for screen refresh, input handling etc.
 */

void VideoRefreshInit(void)
{
	// TODO: set up specialised 8bpp VideoRefresh handlers ?
	if (display_type == DISPLAY_DGA) {
#if ENABLE_VOSF && (REAL_ADDRESSING || DIRECT_ADDRESSING)
		if (use_vosf)
			video_refresh = video_refresh_dga_vosf;
		else
#endif
			video_refresh = video_refresh_dga;
	}
	else {
#ifdef ENABLE_VOSF
		if (use_vosf)
			video_refresh = video_refresh_window_vosf;
		else
#endif
		if (frame_skip == 0)
			video_refresh = video_refresh_window_dynamic;
		else
			video_refresh = video_refresh_window_static;
	}
}

void VideoRefresh(void)
{
	// TODO: make main_unix/VideoRefresh call directly video_refresh() ?
	video_refresh();
}

#if 0
void VideoRefresh(void)
{
#if defined(ENABLE_XF86_DGA) || defined(ENABLE_FBDEV_DGA)
	// Quit DGA mode if requested
	if (quit_full_screen) {
		quit_full_screen = false;
		if (display_type == DISPLAY_DGA) {
#ifdef ENABLE_XF86_DGA
			XF86DGADirectVideo(x_display, screen, 0);
#endif
			XUngrabPointer(x_display, CurrentTime);
			XUngrabKeyboard(x_display, CurrentTime);
			XUnmapWindow(x_display, the_win);
			XSync(x_display, false);
		}
	}
#endif

	// Handle X events
	handle_events();

	// Handle palette changes
#ifdef HAVE_PTHREADS
	pthread_mutex_lock(&palette_lock);
#endif
	if (palette_changed) {
		palette_changed = false;
		if (depth == 8) {
			XStoreColors(x_display, cmap[0], palette, 256);
			XStoreColors(x_display, cmap[1], palette, 256);
				
#ifdef ENABLE_XF86_DGA
			if (display_type == DISPLAY_DGA) {
				current_dga_cmap ^= 1;
				XF86DGAInstallColormap(x_display, screen, cmap[current_dga_cmap]);
			}
#endif
		}
	}
#ifdef HAVE_PTHREADS
	pthread_mutex_unlock(&palette_lock);
#endif

	// In window mode, update display
	static int tick_counter = 0;
	if (display_type == DISPLAY_WINDOW) {
		tick_counter++;
		if (frame_skip == 0)
			update_display_dynamic(tick_counter);
		else if (tick_counter >= frame_skip) {
			tick_counter = 0;
			update_display_static();
		}
	}
}
#endif

#ifdef HAVE_PTHREADS
static void *redraw_func(void *arg)
{
	uint64 start = GetTicks_usec();
	int64 ticks = 0;
	uint64 next = GetTicks_usec();
	while (!redraw_thread_cancel) {
//		VideoRefresh();
		video_refresh();
		next += 16667;
		int64 delay = next - GetTicks_usec();
		if (delay > 0)
			Delay_usec(delay);
		else if (delay < -16667)
			next = GetTicks_usec();
		ticks++;
	}
	uint64 end = GetTicks_usec();
	printf("%Ld ticks in %Ld usec = %Ld ticks/sec\n", ticks, end - start, (end - start) / ticks);
	return NULL;
}
#endif
