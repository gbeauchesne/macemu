/*
 *  about_window_beos.cpp - "About" window, BeOS implementation
 *
 *  SheepShaver (C) 1997-2002 Christian Bauer and Marc Hellwig
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

#include <GLView.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <stdio.h>

#include "about_window.h"
#include "video.h"
#include "version.h"
#include "user_strings.h"


// About window dimensions
static const BRect about_frame = BRect(0, 0, 383, 99);

// Special colors
const rgb_color fill_color = {216, 216, 216, 0};

// SheepShaver icon
static const uint8 sheep_icon[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xda, 0x15, 0x00,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0xff, 0x00, 0x00, 0x00, 0x16, 0xda, 0x00,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x16,
	0x00, 0x1d, 0xda, 0x1e, 0x1e, 0x1e, 0xda, 0x16, 0x00, 0x00, 0x16, 0xda, 0x16, 0xda, 0x08, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x16, 0x00, 0x00, 0x00, 0x0b, 0xff, 0xff, 0x11, 0x00, 0xda,
	0x1d, 0xda, 0x1e, 0xda, 0x1e, 0xda, 0x1e, 0xda, 0x1e, 0x16, 0x00, 0x00, 0x0c, 0xff, 0xff, 0xff,
	0xff, 0x0b, 0x00, 0x00, 0x16, 0x16, 0x00, 0x12, 0xfd, 0x1d, 0x0b, 0x00, 0x00, 0x1d, 0xfd, 0x1d,
	0xfd, 0x1e, 0xda, 0x1e, 0xda, 0x1e, 0xda, 0x3f, 0xda, 0x3f, 0xda, 0x15, 0x00, 0xff, 0xff, 0xff,
	0x16, 0x00, 0x17, 0x16, 0x00, 0x00, 0x1d, 0xfd, 0x1d, 0xfd, 0x1d, 0xfd, 0x16, 0x0f, 0x0b, 0x1d,
	0x1e, 0xfd, 0x1e, 0xda, 0x1e, 0xda, 0x3f, 0xda, 0x3f, 0xda, 0x1d, 0x5a, 0x15, 0xff, 0xff, 0xff,
	0x05, 0x17, 0x16, 0x00, 0x12, 0x1d, 0x00, 0x1d, 0xfd, 0x00, 0xfd, 0x00, 0x00, 0x16, 0x0b, 0x00,
	0x00, 0x0e, 0x1d, 0x3f, 0xda, 0x3f, 0xda, 0x1d, 0xda, 0x1b, 0x5a, 0x1b, 0x0f, 0x1d, 0xff, 0xff,
	0xff, 0x05, 0x00, 0x00, 0x1d, 0xfd, 0x1d, 0xfd, 0x1d, 0xfd, 0x1a, 0xfd, 0x00, 0x0f, 0x14, 0x14,
	0x16, 0x00, 0x15, 0xfd, 0x1d, 0xda, 0x1b, 0xda, 0x1b, 0x5a, 0x1b, 0x5a, 0x0f, 0x14, 0xff, 0xff,
	0xff, 0xff, 0x00, 0x1d, 0xfd, 0x1d, 0xfd, 0x1d, 0xfd, 0x1a, 0xfd, 0x12, 0x00, 0x16, 0x00, 0x0f,
	0x00, 0x1b, 0xfd, 0x1e, 0xfd, 0x1b, 0xda, 0x1b, 0x5a, 0x1b, 0x5a, 0x1b, 0x5a, 0x14, 0xff, 0xff,
	0xff, 0x00, 0x1d, 0xfd, 0x1d, 0xfd, 0x1d, 0xfd, 0x1a, 0xfd, 0x12, 0x00, 0x16, 0x1b, 0x16, 0x0e,
	0x1b, 0xfd, 0x1b, 0xfd, 0x1b, 0xfd, 0x1b, 0x5a, 0x18, 0x00, 0x00, 0x5a, 0x1b, 0x00, 0xff, 0xff,
	0xff, 0x00, 0xfd, 0x14, 0xfd, 0x14, 0xfd, 0x1d, 0xfd, 0x12, 0x00, 0x16, 0xfd, 0x1b, 0xfd, 0x1b,
	0xfd, 0x1b, 0xfd, 0x1e, 0xfd, 0x1b, 0x5a, 0x18, 0x5a, 0x00, 0x00, 0x1b, 0x9b, 0x00, 0xff, 0xff,
	0xff, 0x00, 0x0f, 0xfd, 0x1d, 0xfd, 0x0f, 0xfd, 0x12, 0x00, 0x16, 0xfd, 0x1b, 0xfd, 0x1b, 0xfd,
	0x1b, 0xfd, 0x18, 0xfd, 0x1b, 0xf9, 0x18, 0x5a, 0x00, 0x12, 0x00, 0x9b, 0x1b, 0x00, 0xff, 0xff,
	0xff, 0x00, 0xfd, 0x0a, 0x0a, 0x0a, 0xfd, 0x10, 0x00, 0x1b, 0xfd, 0x1b, 0xfd, 0x1b, 0xfd, 0x18,
	0xfd, 0x15, 0xfa, 0x15, 0xf9, 0x18, 0x15, 0x00, 0x12, 0x9b, 0x00, 0x1b, 0x9b, 0x00, 0x15, 0x15,
	0xff, 0xff, 0x00, 0xfd, 0x1d, 0xfd, 0x1d, 0x00, 0x16, 0x00, 0x1b, 0xfd, 0x1b, 0xfd, 0x15, 0xfd,
	0x15, 0xfa, 0x15, 0xf9, 0x15, 0x16, 0x00, 0x0f, 0x9b, 0x12, 0x00, 0x9b, 0x1b, 0x00, 0x15, 0x15,
	0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xfa, 0x1d, 0x00, 0xfa, 0x1e, 0x00, 0x1e, 0xfa, 0x15,
	0xfa, 0x15, 0x16, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x9b, 0x00, 0x00, 0x00, 0x12, 0x15, 0x15,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x1d, 0xf9, 0x00, 0x3f, 0xfa, 0x00, 0xfa, 0x1b, 0x00,
	0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x15, 0x00, 0x00, 0x12, 0x15, 0x15, 0x15, 0x15,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xf9, 0x1d, 0x00, 0xf9, 0x1b, 0x00, 0x1b, 0xf9, 0x00,
	0xff, 0xff, 0xff, 0xff, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x1d, 0xf9, 0x00, 0x3f, 0xf9, 0x00, 0xf9, 0x1b, 0x00,
	0xff, 0xff, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x16, 0x1d, 0x00, 0xf9, 0x1b, 0x00, 0x1b, 0xf9, 0x00,
	0xff, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x16, 0x00, 0x00, 0x00, 0x16, 0xf9, 0x00, 0x1b, 0x16, 0x00,
	0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x00, 0x00, 0x00, 0x00, 0x12, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x16, 0x00, 0x00, 0x16, 0x00, 0x16, 0x00, 0x16,
	0x15, 0x15, 0x15, 0x15, 0x00, 0x00, 0x15, 0x00, 0x0a, 0x19, 0x0a, 0x00, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x12, 0x00, 0x00, 0x00, 0x16, 0xff,
	0xff, 0xff, 0xff, 0xff, 0x00, 0x0a, 0x00, 0x15, 0x00, 0x19, 0x1c, 0x18, 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00,
	0x0f, 0xff, 0xff, 0xff, 0x00, 0x18, 0x11, 0x00, 0x15, 0x00, 0x1c, 0x18, 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x13, 0x00, 0x00, 0x00, 0x0b, 0x0b,
	0x00, 0x00, 0x0f, 0xff, 0x12, 0x00, 0x18, 0x19, 0x00, 0x15, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x13, 0x00, 0x00, 0x14, 0x1e, 0xfd, 0x01, 0xfd, 0x14,
	0xfa, 0xf9, 0x00, 0xff, 0xff, 0x00, 0x0a, 0x19, 0x1c, 0x00, 0x18, 0x1c, 0x00, 0x00, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x14, 0xfd, 0x1e, 0xf9, 0x1e, 0xfa, 0x1e, 0xf9,
	0x14, 0xfa, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x1c, 0x00, 0x18, 0x00, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x14, 0x1e, 0xf9, 0x14, 0xfa, 0x1e, 0xf9, 0x1e,
	0xfd, 0x1e, 0x00, 0x15, 0xff, 0xff, 0xff, 0x15, 0x15, 0x15, 0x00, 0x00, 0x18, 0x00, 0x1c, 0x00,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x13, 0x00, 0xfd, 0x1e, 0xfd, 0x14, 0xfd, 0x1e, 0xfd,
	0x1e, 0x01, 0x00, 0x15, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x15, 0x00, 0x00, 0x1c, 0x00, 0x15,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x14, 0xfa, 0x01, 0xf9, 0x1e, 0xf9, 0x14,
	0x14, 0x00, 0x0f, 0x15, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x15, 0x00, 0x00, 0x15, 0x15,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x0f, 0x15, 0x15, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x15, 0x15, 0x15, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
	0x15, 0x15, 0x15, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};


// View class
class AboutViewT : public BView {
public:
	AboutViewT(BRect r) : BView(r, "", B_FOLLOW_NONE, B_WILL_DRAW) {}

	virtual void Draw(BRect update)
	{
		char str[256];
		sprintf(str, GetString(STR_ABOUT_TEXT1), VERSION_MAJOR, VERSION_MINOR);

		SetFont(be_bold_font);
		SetDrawingMode(B_OP_OVER);
		MovePenTo(20, 20);
		DrawString(str);
		SetFont(be_plain_font);
		MovePenTo(20, 40);
		DrawString(GetString(STR_ABOUT_TEXT2));
		MovePenTo(20, 60);
		DrawString(B_UTF8_COPYRIGHT "1997-2002 Christian Bauer and Marc Hellwig");
	}

	virtual void MouseDown(BPoint point)
	{
		Window()->PostMessage(B_QUIT_REQUESTED);
	}
};


// 3D view class
class AboutView3D : public BGLView {
public:
	AboutView3D(BRect r) : BGLView(r, "", B_FOLLOW_NONE, 0, BGL_RGB | BGL_DOUBLE)
	{
		rot_x = rot_y = 0;

		if (!VideoSnapshot(64, 64, texture)) {
			uint8 *p = texture;
			const uint8 *q = sheep_icon;
			const color_map *cm = system_colors();
			for (int i=0; i<32*32; i++) {
				uint8 red = cm->color_list[*q].red;
				uint8 green = cm->color_list[*q].green;
				uint8 blue = cm->color_list[*q++].blue;
				p[0] = p[3] = p[64*3] = p[65*3] = red;
				p[1] = p[4] = p[64*3+1] = p[65*3+1] = green;
				p[2] = p[5] = p[64*3+2] = p[65*3+2] = blue;
				p += 6;
				if ((i & 31) == 31)
					p += 64*3;
			}
		}
	}

	virtual void AttachedToWindow(void)
	{
		BGLView::AttachedToWindow();
		LockGL();

		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);

		glShadeModel(GL_SMOOTH);

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		gluPerspective(30, 1, 0.5, 20);

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		GLfloat light_color[4] = {1, 1, 1, 1};
		GLfloat light_dir[4] = {1, 2, 1.5, 1};
		glLightfv(GL_LIGHT0, GL_DIFFUSE, light_color);
		glLightfv(GL_LIGHT0, GL_POSITION, light_dir);
		glEnable(GL_LIGHT0);
		glEnable(GL_LIGHTING);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glTexImage2D(GL_TEXTURE_2D, 0, 4, 64, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, texture);
		glEnable(GL_TEXTURE_2D);

		UnlockGL();

		tick_thread_active = true;
		tick_thread = spawn_thread(tick_func, "OpenGL Animation", B_NORMAL_PRIORITY, this);
		resume_thread(tick_thread);
	}

	virtual void DetachedFromWindow(void)
	{
		status_t l;
		tick_thread_active = false;
		wait_for_thread(tick_thread, &l);

		BGLView::DetachedFromWindow();
	}

	virtual void Draw(BRect update)
	{
		LockGL();
		glClear(GL_COLOR_BUFFER_BIT);
		glBegin(GL_QUADS);
		glNormal3d(0, 0, 1);
		glTexCoord2f(0, 0);
		glVertex3d(-1, 1, 0);
		glTexCoord2f(1, 0);
		glVertex3d(1, 1, 0);
		glTexCoord2f(1, 1);
		glVertex3d(1, -1, 0);
		glTexCoord2f(0, 1);
		glVertex3d(-1, -1, 0);
		glEnd();
		SwapBuffers();
		UnlockGL();
	}

	static status_t tick_func(void *arg)
	{
		AboutView3D *obj = (AboutView3D *)arg;
		while (obj->tick_thread_active) {
			obj->rot_x += 2;
			obj->rot_y += 2;
			obj->LockGL();
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			glTranslatef(0, 0, -5);
			glRotatef(obj->rot_x, 1, 0, 0);
			glRotatef(obj->rot_y, 0, 1, 0);
			obj->UnlockGL();
			if (obj->LockLooperWithTimeout(20000) == B_OK) {
				obj->Draw(obj->Bounds());
				obj->UnlockLooper();
			}
			snooze(16667);
		}
		return 0;
	}

private:
	thread_id tick_thread;
	bool tick_thread_active;

	float rot_x, rot_y;
	uint8 texture[64*64*3];
};


// Window class
class AboutWindowT : public BWindow {
public:
	AboutWindowT() : BWindow(about_frame, NULL, B_MODAL_WINDOW_LOOK, B_FLOATING_APP_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_WILL_ACCEPT_FIRST_CLICK)
	{
		Lock();
		MoveTo(100, 100);
		BRect r = Bounds();
		r.right = 100;
		AboutView3D *view_3d = new AboutView3D(r);
		AddChild(view_3d);
		r = Bounds();
		r.left = 100;
		AboutViewT *view = new AboutViewT(r);
		AddChild(view);
		view->SetHighColor(0, 0, 0);
		view->SetViewColor(fill_color);
		view->MakeFocus();
		Unlock();
		Show();
	}
};


/*
 *  Open "About" window
 */

void OpenAboutWindow(void)
{
	new AboutWindowT;
}
