/*
Copyright (C) 2016-2019 The University of Notre Dame
This software is distributed under the GNU General Public License.
See the file LICENSE for details.
*/

/*
Simple window manager runs a list of programs and distributes
events to each based on which one currently has the focus.
*/

#include "library/syscalls.h"
#include "library/string.h"
#include "library/stdio.h"
#include "library/kernel_object_string.h"
#include "library/nwindow.h"
#include "library/errno.h"

// if this increases, you must increase the bitmap size
#define MAX_WINDOWS 32

#define WINDOW_TITLE_HEIGHT 14
#define WINDOW_TITLE_ACTIVE_COLOR 100,100,255
#define WINDOW_TITLE_INACTIVE_COLOR 25,25,50
#define WINDOW_TITLE_TEXT_COLOR 255,255,255
#define WINDOW_BORDER_COLOR 200,200,200
#define WINDOW_BORDER 3
#define WINDOW_TEXT_PADDING 3

#define CLOSE_BOX_PADDING 3
#define CLOSE_BOX_SIZE (WINDOW_TITLE_HEIGHT-CLOSE_BOX_PADDING*2)
#define CLOSE_BOX_COLOR 100,100,100

#define NORMAL_MODE 0
#define COMMAND_MODE 1
#define MOVE_MODE 2

struct window {
	int w, h, x, y;
	struct nwindow *nw;
	int console_mode;
	const char *exec;
	const char *arg;
	int argc;
	int pid;
	int fds[6];
};

struct nwindow *nw = 0;
struct window windows[MAX_WINDOWS];
uint32_t window_bitmap = 0;
char mode = NORMAL_MODE;
int active = 0;

void draw_border(struct window *win, int is_active) {
	int x = win->x;
	int y = win->y;
	int h = win->h;
	int w = win->w;
	char *str;
	switch (mode) {
		case NORMAL_MODE:
			str = (char *) win->exec;
			break;
		case COMMAND_MODE:
			str = "Command (~MNQXtab)";
			break;
		case MOVE_MODE:
			str = "Move (~WASD)";
			break;
		default:
			str = "Unknown (bug?)";
			break;
	}

	// Title bar
	if (is_active) {
		nw_bgcolor(nw, WINDOW_TITLE_ACTIVE_COLOR);
	} else {
		nw_bgcolor(nw, WINDOW_TITLE_INACTIVE_COLOR);
	}
	nw_clear(nw, x, y, w, WINDOW_TITLE_HEIGHT);

	// Close box
	nw_fgcolor(nw, CLOSE_BOX_COLOR);
	nw_rect(nw, x + CLOSE_BOX_PADDING, y + CLOSE_BOX_PADDING, CLOSE_BOX_SIZE, CLOSE_BOX_SIZE);
	// Title text
	nw_fgcolor(nw, WINDOW_TITLE_TEXT_COLOR);
	nw_string(nw, x + CLOSE_BOX_SIZE+CLOSE_BOX_PADDING * 2, y + WINDOW_TEXT_PADDING, str);

	// Border box
	nw_fgcolor(nw, WINDOW_BORDER_COLOR);
	nw_line(nw, x, y, w, 0);
	nw_line(nw, x, y + WINDOW_TITLE_HEIGHT - 1, w, 0);

	nw_line(nw, x, y, 0, h);
	nw_line(nw, x + 1, y, 0, h);

	nw_line(nw, x, y + h, w, 0);
	nw_line(nw, x + 1, y + h, w, 0);

	nw_line(nw, x + w, y, 0, h);
	nw_line(nw, x + w + 1, y, 0, h);

	nw_bgcolor(nw, 0, 0, 0);
}

void new_window(char *exec, int x, int y, int width, int height, char *arg, int console_mode) {
	int i = 0xFFFF;
	for (int j = 0; j < MAX_WINDOWS; j++) {
		if (!(window_bitmap & (1 << j))) {
			i = j;
			break;
		}
	}
	if (i == 0xFFFF) return;
	window_bitmap |= 1 << i;

	windows[i].exec = exec;
	windows[i].x = x;
	windows[i].y = y;
	windows[i].w = width;
	windows[i].h = height;
	windows[i].arg = arg;
	windows[i].argc = arg == 0 ? 1 : 2;
	windows[i].console_mode = console_mode;

	struct window *w = &windows[i];
	struct nwindow *child = nw_create_child(
		nw,
		w->x + WINDOW_BORDER,
		w->y + WINDOW_TITLE_HEIGHT,
		w->w - WINDOW_BORDER * 2,
		w->h - WINDOW_BORDER - WINDOW_TITLE_HEIGHT
	);
	int window_fd = nw_fd(child);

	windows[i].nw = child;

	if (w->console_mode) {
		w->fds[0] = syscall_open_console(window_fd);
		w->fds[1] = w->fds[0];
		w->fds[2] = w->fds[0];
		w->fds[3] = window_fd; // doesn't need a window fd
		w->fds[4] = 4;
		w->fds[5] = 5;
	} else {
		w->fds[0] = -1; // doesn't need stdin/stdout
		w->fds[1] = -1;
		w->fds[2] = -1;
		w->fds[3] = window_fd;
		w->fds[4] = 4;
		w->fds[5] = 5;
	}

	draw_border(w, 0);
	nw_bgcolor(child, 0, 0, 0);
	nw_flush(nw);

	const char *args[3];
	args[0] = w->exec;
	args[1] = w->arg;
	args[2] = 0;

	int pfd = syscall_open_file(KNO_STDDIR, w->exec, 0, 0);
	if (pfd >= 0) {
		w->pid = syscall_process_wrun(pfd, w->argc, args, w->fds, 6);
		if(w->pid < 0) {
			printf("couldn't run %s: %s\n",w->exec,strerror(pfd));
			return;
		}
	} else {
		printf("couldn't find %s: %s\n",w->exec,strerror(pfd));
		return;
	}
}

void next_window() {
	int i = (active + 1) % MAX_WINDOWS;
	int attempt = 2;
	while (attempt) {
		for (; i < MAX_WINDOWS; i++) {
			if (window_bitmap & (1 << i)) {
				active = i;
				return;
			}
		}
		i = 0;
		attempt--;
	}
}

void close_window(int window) {
	if (!(window_bitmap & (1 << window))) return;
	window_bitmap &= ~(1 << window);
	syscall_process_kill(windows[window].pid);
	syscall_process_reap(windows[window].pid);
	next_window();
}

void move_window_relative(int window, int dx, int dy) {
	nw_clear(
		nw,
		windows[window].x,
		windows[window].y,
		windows[window].w + WINDOW_BORDER,
		windows[window].h + WINDOW_TITLE_HEIGHT
	);
	windows[window].x += dx;
	windows[window].y += dy;
	nw_move(
		windows[window].nw,
		windows[window].x + WINDOW_BORDER,
		windows[window].y + WINDOW_TITLE_HEIGHT
	);
	draw_border(&windows[window], 1);
	nw_flush(nw);
}

int main(int argc, char *argv[]) {
	nw = nw_create_default();

	nw_clear(nw, 0, 0, nw_width(nw), nw_height(nw));
	nw_flush(nw);

	new_window("/bin/shell.exe", 16, 16, 384, 384, 0, 1);
	draw_border(&windows[active], 1);
	nw_flush(nw);

	struct event e;
	while (nw_next_event(nw, &e)) {
		if (e.type == EVENT_CLOSE) break;
		if (e.type != EVENT_KEY_DOWN) continue;

		char c = e.code;

		if (mode == COMMAND_MODE) {
			// in command mode
			if (c == '~') {
				// toggle command mode
				mode = NORMAL_MODE;
				draw_border(&windows[active], 1);
				nw_flush(nw);
			} else if (c == '\t') {
				// go to the next process
				draw_border(&windows[active], 0);
				nw_flush(nw);
				next_window();
				draw_border(&windows[active], 1);
				nw_flush(nw);
			} else if (c == 'q') {
				// quit process
				close_window(active);
			} else if (c == 'm') {
				// move mode
				mode = MOVE_MODE;
				draw_border(&windows[active], 1);
				nw_flush(nw);
			} else if (c == 'n') {
				// new shell
				mode = NORMAL_MODE;
				draw_border(&windows[active], 0);
				nw_flush(nw);
				new_window("/bin/shell.exe", 16, 16, 384, 384, 0, 1);
				next_window();
				draw_border(&windows[active], 1);
				nw_flush(nw);
			} else if (c == 'x') {
				// exit
				break;
			}
		} else if (mode == MOVE_MODE) {
			// in move mode
			if (c == '~') {
				// toggle move mode
				mode = NORMAL_MODE;
				draw_border(&windows[active], 1);
				nw_flush(nw);
			} else if (c == 'w') {
				// move up
				move_window_relative(active, 0, -4);
			} else if (c == 's') {
				// move down
				move_window_relative(active, 0, 4);
			} else if (c == 'a') {
				// move left
				move_window_relative(active, -4, 0);
			} else if (c == 'd') {
				// move right
				move_window_relative(active, 4, 0);
			}
		} else {
			// in normal mode
			if (c == '~') {
				// toggle command mode
				mode = COMMAND_MODE;
				draw_border(&windows[active], 1);
				nw_flush(nw);
			} else {
				if (windows[active].console_mode) {
					// post a single character to the console
					syscall_object_write(windows[active].fds[KNO_STDIN], &c, 1, KERNEL_IO_POST);
				} else {
					// post a complete event to the window
					syscall_object_write(windows[active].fds[KNO_STDWIN], &e, sizeof(e), KERNEL_IO_POST);
				}
			}
		}
	}

	// kill and reap all children processes
	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (window_bitmap & (1 << i)) {
			syscall_process_kill(windows[i].pid);
			syscall_process_reap(windows[i].pid);
		}
	}

	// clean up the window
	nw_clear(nw, 0, 0, nw_width(nw), nw_height(nw));
	nw_flush(nw);
	return 0;
}
