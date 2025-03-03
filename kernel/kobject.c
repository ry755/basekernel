/*
 * Copyright (C) 2016-2019 The University of Notre Dame This software is
 * distributed under the GNU General Public License. See the file LICENSE
 * for details.
 */

#include "console.h"
#include "kobject.h"
#include "kmalloc.h"
#include "string.h"

#include "device.h"
#include "fs.h"
#include "window.h"
#include "console.h"
#include "pipe.h"

#include "kernel/error.h"

static struct kobject *kobject_create()
{
	struct kobject *k = kmalloc(sizeof(*k));
	k->refcount = 1;
	k->offset = 0;
	k->tag = 0;
	k->data.file = 0;
	return k;
}

struct kobject *kobject_create_file(struct fs_dirent *f)
{
	struct kobject *k = kobject_create();
	k->type = KOBJECT_FILE;
	k->data.file = f;
	return k;
}

struct kobject *kobject_create_dir( struct fs_dirent *d )
{
	struct kobject *k = kobject_create();
	k->type = KOBJECT_DIR;
	k->data.dir = d;
	return k;
}

struct kobject *kobject_create_device(struct device *d)
{
	struct kobject *k = kobject_create();
	k->type = KOBJECT_DEVICE;
	k->data.device = d;
	return k;
}

struct kobject *kobject_create_window(struct window *g)
{
	struct kobject *k = kobject_create();
	k->type = KOBJECT_WINDOW;
	k->data.window = g;
	return k;
}

struct kobject *kobject_create_console(struct console *c)
{
	struct kobject *k = kobject_create();
	k->type = KOBJECT_CONSOLE;
	k->data.console = c;
	return k;
}

struct kobject *kobject_create_pipe(struct pipe *p)
{
	struct kobject *k = kobject_create();
	k->type = KOBJECT_PIPE;
	k->data.pipe = p;
	return k;
}

struct kobject *kobject_addref(struct kobject *k)
{
	k->refcount++;
	return k;
}

struct kobject * kobject_copy( struct kobject *ksrc )
{
	struct kobject *kdst = kobject_create();

	kdst->data = ksrc->data;
	kdst->type = ksrc->type;
	kdst->offset = ksrc->offset;
	kdst->refcount = 1;

	if(ksrc->tag) {
		kdst->tag = strdup(ksrc->tag);
	} else {
		kdst->tag = 0;
	}

	switch (ksrc->type) {
	case KOBJECT_WINDOW:
		window_addref(ksrc->data.window);
		break;
	case KOBJECT_CONSOLE:
		console_addref(ksrc->data.console);
		break;
	case KOBJECT_FILE:
		fs_dirent_addref(ksrc->data.file);
		break;
	case KOBJECT_DIR:
		fs_dirent_addref(ksrc->data.dir);
		break;
	case KOBJECT_DEVICE:
		device_addref(ksrc->data.device);
		break;
	case KOBJECT_PIPE:
		pipe_addref(ksrc->data.pipe);
		break;
	}

	return kdst;
}

struct kobject *kobject_create_window_from_window( struct kobject *k, int x, int y, int width, int height )
{
	if(k->type!=KOBJECT_WINDOW) return 0;

	struct window *w = window_create(k->data.window,x,y,width,height);
	if(w) {
		return kobject_create_window(w);
	} else {
		return 0;
	}
}

struct kobject *kobject_create_console_from_window( struct kobject *k )
{
	if(k->type!=KOBJECT_WINDOW) return 0;
	struct console *c = console_create(k->data.window);
	if(!c) return 0;
	return kobject_create_console(c);
}

struct kobject * kobject_create_file_from_dir( struct kobject *kobject, const char *name )
{
	if(kobject->type==KOBJECT_DIR) {
		struct fs_dirent *d = fs_dirent_mkfile(kobject->data.dir,name);
		if(d) {
			return kobject_create_file(d);
		} else {
			return 0;
		}
	} else {
		return 0;
		// XXX KERROR_NOT_IMPLEMENTED;
	}
	return 0;
}

struct kobject * kobject_create_dir_from_dir( struct kobject *kobject, const char *name )
{
	if(kobject->type==KOBJECT_DIR) {
		struct fs_dirent *d = fs_dirent_mkdir(kobject->data.dir,name);
		if(d) {
			return kobject_create_dir(d);
		} else {
			return 0;
		}
	} else {
		return 0;
		// XXX KERROR_NOT_IMPLEMENTED;
	}
	return 0;
}

int kobject_move( struct kobject *kobject, int x, int y )
{
	return window_move(kobject->data.window, x, y);
}

int kobject_read(struct kobject *kobject, void *buffer, int size, kernel_io_flags_t flags )
{
	int actual = 0;

	switch (kobject->type) {
	case KOBJECT_FILE:
		actual = fs_dirent_read(kobject->data.file, (char *) buffer, (uint32_t) size, kobject->offset);
		break;
	case KOBJECT_DIR:
		return KERROR_INVALID_REQUEST;
		break;
	case KOBJECT_DEVICE:
		if(flags&KERNEL_IO_NONBLOCK) {
			actual = device_read_nonblock(kobject->data.device, buffer, size / device_block_size(kobject->data.device), 0);
		} else {
			actual = device_read(kobject->data.device, buffer, size / device_block_size(kobject->data.device), 0);
		}
		break;
	case KOBJECT_PIPE:
		if(flags&KERNEL_IO_NONBLOCK) {
			actual = pipe_read_nonblock(kobject->data.pipe, buffer, size);
		} else {
			actual = pipe_read(kobject->data.pipe, buffer, size);
		}
		break;
	case KOBJECT_WINDOW:
		if(flags&KERNEL_IO_NONBLOCK) {
			actual = window_read_events_nonblock(kobject->data.window, buffer, size);
		} else {
			actual = window_read_events(kobject->data.window, buffer, size);
		}
		break;
	case KOBJECT_CONSOLE:
		if(flags&KERNEL_IO_NONBLOCK) {
			actual = console_read_nonblock(kobject->data.console,buffer,size);
		} else {
			actual = console_read(kobject->data.console,buffer,size);
		}

		break;
	default:
		actual = 0;
		break;
	}

	if(actual > 0) kobject->offset += actual;

	return actual;
}

int kobject_write(struct kobject *kobject, void *buffer, int size, kernel_io_flags_t flags )
{
	switch (kobject->type) {
	case KOBJECT_WINDOW:
		if(flags&KERNEL_IO_POST) {
			return window_post_events(kobject->data.window,buffer,size);
		} else {
			return window_write_graphics(kobject->data.window,buffer,size);
		}
		break;
	case KOBJECT_CONSOLE:
		if(flags&KERNEL_IO_POST) {
			return console_post(kobject->data.console,buffer,size);
		} else {
			return console_write(kobject->data.console, buffer, size );
		}
		break;
	case KOBJECT_FILE:{
			int actual = fs_dirent_write(kobject->data.file, (char *) buffer, (uint32_t) size, kobject->offset);
			if(actual > 0)
				kobject->offset += actual;
			return actual;
		}
	case KOBJECT_DEVICE:
		return device_write(kobject->data.device, buffer, size / device_block_size(kobject->data.device), 0);
	case KOBJECT_PIPE:
		if(flags&KERNEL_IO_NONBLOCK) {
			return pipe_write_nonblock(kobject->data.pipe, buffer, size);
		} else {
			return pipe_write(kobject->data.pipe, buffer, size);
		}
	default:
		return 0;
	}
	return 0;
}

int kobject_list(struct kobject *kobject, void *buffer, int size)
{
	if(kobject->type==KOBJECT_DIR) {
		return fs_dirent_list(kobject->data.dir,buffer,size);
	} else {
		return KERROR_NOT_A_DIRECTORY;
	}
}

int kobject_lookup( struct kobject *kobject, const char *name, struct kobject **newobj )
{
	if(kobject->type==KOBJECT_DIR) {
		struct fs_dirent *d;
		d = fs_dirent_traverse(kobject->data.dir,name);
		if(d) {
			if(fs_dirent_isdir(d)) {
				*newobj = kobject_create_dir(d);
			} else {
				*newobj = kobject_create_file(d);
			}
			return 0;
		} else {
			return KERROR_NOT_FOUND;
		}
	} else {
		return KERROR_NOT_IMPLEMENTED;
	}
	return 0;
}

int kobject_remove( struct kobject *kobject, const char *name )
{
	if(kobject->type==KOBJECT_DIR) {
		return fs_dirent_remove(kobject->data.dir,name);
	} else {
		return KERROR_NOT_IMPLEMENTED;
	}
	return 0;
}

int kobject_close(struct kobject *kobject)
{
	kobject->refcount--;

	if(kobject->refcount==0) {
		switch (kobject->type) {
		case KOBJECT_WINDOW:
			window_delete(kobject->data.window);
			break;
		case KOBJECT_CONSOLE:
			console_delete(kobject->data.console);
			break;
		case KOBJECT_FILE:
			fs_dirent_close(kobject->data.file);
			break;
		case KOBJECT_DIR:
			fs_dirent_close(kobject->data.dir);
			break;
		case KOBJECT_DEVICE:
			device_close(kobject->data.device);
			break;
		case KOBJECT_PIPE:
			pipe_delete(kobject->data.pipe);
			break;
		default:
			break;
		}
		if (kobject->tag)
			kfree(kobject->tag);
		kfree(kobject);
		return 0;
	} else if(kobject->refcount>1 ) {
		if(kobject->type==KOBJECT_PIPE) {
			pipe_flush(kobject->data.pipe);
		}
	}
	return 0;
}

int kobject_size(struct kobject *kobject, int *dims, int n)
{
	switch (kobject->type) {
	case KOBJECT_WINDOW:
		if(n==2) {
			dims[0] = window_width(kobject->data.window);
			dims[1] = window_height(kobject->data.window);
			return 0;
		} else {
			return KERROR_INVALID_REQUEST;
		}
	case KOBJECT_CONSOLE:
		if(n==2) {
			console_size(kobject->data.console,&dims[0],&dims[1]);
			return 0;
		} else {
			return KERROR_INVALID_REQUEST;
		}
	case KOBJECT_FILE:
		if(n==1) {
			dims[0] = fs_dirent_size(kobject->data.file);
			return 0;
		} else {
			return KERROR_INVALID_REQUEST;
		}
	case KOBJECT_DIR:
		if(n==1) {
			dims[0] = fs_dirent_size(kobject->data.dir);
			return 0;
		} else {
			return KERROR_INVALID_REQUEST;
		}
	case KOBJECT_DEVICE:
		if(n==2) {
			dims[0] = device_nblocks(kobject->data.device);
			dims[1] = device_block_size(kobject->data.device);
			return 0;
		} else {
			return KERROR_INVALID_REQUEST;
		}
	case KOBJECT_PIPE:
		if(n==1) {
			dims[0] = pipe_size(kobject->data.pipe);
			return 0;
		} else {
			return KERROR_INVALID_REQUEST;
		}
	}
	return KERROR_INVALID_REQUEST;
}

int kobject_get_type(struct kobject *kobject)
{
	return kobject->type;
}

int kobject_set_tag(struct kobject *kobject, char *new_tag)
{
	if(kobject->tag != 0) {
		kfree(kobject->tag);
	}
	kobject->tag = kmalloc(strlen(new_tag) * sizeof(char));
	strcpy(kobject->tag, new_tag);
	return 1;
}

int kobject_get_tag(struct kobject *kobject, char *buffer, int buffer_size)
{
	if(kobject->tag != 0) {
		strcpy(buffer, kobject->tag);
		return 1;
	}
	return 0;
}
