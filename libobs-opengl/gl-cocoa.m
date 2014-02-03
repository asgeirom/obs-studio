/******************************************************************************
    Copyright (C) 2013 by Ruwen Hahn <palana@stunned.de>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "gl-subsystem.h"
#include <OpenGL/OpenGL.h>

#import <Cocoa/Cocoa.h>
#import <AppKit/AppKit.h>


//#include "util/darray.h"


struct gl_windowinfo {
	NSView *view;
};

struct gl_platform {
	NSOpenGLContext *context;
	struct gs_swap_chain swap;
};

static NSOpenGLContext *gl_context_create(struct gs_init_data *info)
{
	unsigned attrib_count = 0;

#define ADD_ATTR(x) \
	{ attributes[attrib_count++] = (NSOpenGLPixelFormatAttribute)x; }
#define ADD_ATTR2(x, y) { ADD_ATTR(x); ADD_ATTR(y); }

	NSOpenGLPixelFormatAttribute attributes[40];

	switch(info->num_backbuffers) {
		case 0:
			break;
		case 1:
			ADD_ATTR(NSOpenGLPFADoubleBuffer);
			break;
		case 2:
			ADD_ATTR(NSOpenGLPFATripleBuffer);
			break;
		default:
			blog(LOG_ERROR, "Requested backbuffers (%d) not "
			                "supported", info->num_backbuffers);
	}

	ADD_ATTR(NSOpenGLPFAClosestPolicy);
	ADD_ATTR2(NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core);

	int color_bits = 0;//get_color_format_bits(info->format);
	if(color_bits == 0) color_bits = 24;
	else if(color_bits < 15) color_bits = 15;

	ADD_ATTR2(NSOpenGLPFAColorSize, color_bits);

	ADD_ATTR2(NSOpenGLPFAAlphaSize, 8);

	ADD_ATTR2(NSOpenGLPFADepthSize, 16);

	ADD_ATTR(0);

#undef ADD_ATTR2
#undef ADD_ATTR

	NSOpenGLPixelFormat *pf;
	pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
	if(!pf) {
		blog(LOG_ERROR, "Failed to create pixel format");
		return NULL;
	}

	NSOpenGLContext *context;
	context = [[NSOpenGLContext alloc] initWithFormat:pf shareContext:nil];
	[pf release];
	if(!context) {
		blog(LOG_ERROR, "Failed to create context");
		return NULL;
	}

	[context setView:info->window.view];

	return context;
}

static bool gl_init_default_swap(struct gl_platform *plat, device_t dev,
		struct gs_init_data *info)
{
	if(!(plat->context = gl_context_create(info)))
		return false;

	plat->swap.device = dev;
	plat->swap.info	  = *info;
	plat->swap.wi     = gl_windowinfo_create(info);

	return plat->swap.wi != NULL;
}

struct gl_platform *gl_platform_create(device_t device,
		struct gs_init_data *info)
{
	struct gl_platform *plat = bmalloc(sizeof(struct gl_platform));
	memset(plat, 0, sizeof(struct gl_platform));

	if(!gl_init_default_swap(plat, device, info))
		goto fail;

	[plat->context makeCurrentContext];

	if (!ogl_LoadFunctions())
		goto fail;

	return plat;

fail:
	blog(LOG_ERROR, "gl_platform_create failed");
	gl_platform_destroy(plat);
	return NULL;
}

struct gs_swap_chain *gl_platform_getswap(struct gl_platform *platform)
{
	if(platform)
		return &platform->swap;
	return NULL;
}

void gl_platform_destroy(struct gl_platform *platform)
{
	if(!platform)
		return;

	[platform->context release];
	platform->context = nil;
	gl_windowinfo_destroy(platform->swap.wi);

	bfree(platform);
}

struct gl_windowinfo *gl_windowinfo_create(struct gs_init_data *info)
{
	if(!info)
		return NULL;

	if(!info->window.view)
		return NULL;

	struct gl_windowinfo *wi = bmalloc(sizeof(struct gl_windowinfo));
	memset(wi, 0, sizeof(struct gl_windowinfo));

	wi->view = info->window.view;

	return wi;
}

void gl_windowinfo_destroy(struct gl_windowinfo *wi)
{
	if(!wi)
		return;

	wi->view = nil;
	bfree(wi);
}

void gl_update(device_t device)
{
	[device->plat->context update];
}

void device_entercontext(device_t device)
{
	CGLLockContext([device->plat->context CGLContextObj]);
	[device->plat->context makeCurrentContext];
}

void device_leavecontext(device_t device)
{
	[NSOpenGLContext clearCurrentContext];
	CGLUnlockContext([device->plat->context CGLContextObj]);
}

void device_load_swapchain(device_t device, swapchain_t swap)
{
	if(!swap)
		swap = &device->plat->swap;

	if(device->cur_swap == swap)
		return;

	device->cur_swap = swap;
}

void device_present(device_t device)
{
	[device->plat->context flushBuffer];
}

void gl_getclientsize(struct gs_swap_chain *swap, uint32_t *width,
		uint32_t *height)
{
	if(width) *width = swap->info.cx;
	if(height) *height = swap->info.cy;
}

texture_t texture_create_from_iosurface(device_t device, void *iosurf)
{
	IOSurfaceRef ref = (IOSurfaceRef)iosurf;
	struct gs_texture_2d *tex = bmalloc(sizeof(struct gs_texture_2d));
	memset(tex, 0, sizeof(struct gs_texture_2d));

	OSType pf = IOSurfaceGetPixelFormat(ref);
	if (pf != 'BGRA')
		blog(LOG_ERROR, "Unexpected pixel format: %d (%c%c%c%c)", pf,
				pf >> 24, pf >> 16, pf >> 8, pf);

	const enum gs_color_format color_format = GS_BGRA;

	tex->base.device             = device;
	tex->base.type               = GS_TEXTURE_2D;
	tex->base.format             = GS_BGRA;
	tex->base.levels             = 1;
	tex->base.gl_format          = convert_gs_format(color_format);
	tex->base.gl_internal_format = convert_gs_internal_format(color_format);
	tex->base.gl_type            = GL_UNSIGNED_INT_8_8_8_8_REV;
	tex->base.gl_target          = GL_TEXTURE_RECTANGLE;
	tex->base.is_dynamic         = false;
	tex->base.is_render_target   = false;
	tex->base.gen_mipmaps        = false;
	tex->width                   = IOSurfaceGetWidth(ref);
	tex->height                  = IOSurfaceGetHeight(ref);

	if (!gl_gen_textures(1, &tex->base.texture))
		goto fail;

	if (!gl_bind_texture(tex->base.gl_target, tex->base.texture))
		goto fail;

	CGLError err = CGLTexImageIOSurface2D(
			[[NSOpenGLContext currentContext] CGLContextObj],
			tex->base.gl_target,
			tex->base.gl_internal_format,
			tex->width, tex->height,
			tex->base.gl_format,
			tex->base.gl_type,
			ref, 0);
	
	if(err != kCGLNoError) {
		blog(LOG_ERROR, "CGLTexImageIOSurface2D: %u, %s"
			        " (texture_create_from_iosurface)",
				err, CGLErrorString(err));

		gl_success("CGLTexImageIOSurface2D");
		goto fail;
	}

	if (!gl_tex_param_i(tex->base.gl_target,
				GL_TEXTURE_MAX_LEVEL, 0))
		goto fail;

	if (!gl_bind_texture(tex->base.gl_target, 0))
		goto fail;

	return (texture_t)tex;

fail:
	texture_destroy((texture_t)tex);
	blog(LOG_ERROR, "texture_create_from_iosurface (GL) failed");
	return NULL;
}

bool texture_rebind_iosurface(texture_t texture, void *iosurf)
{
	if (!texture)
		return false;

	if (!iosurf)
		return false;

	struct gs_texture_2d *tex = (struct gs_texture_2d*)texture;
	IOSurfaceRef ref = (IOSurfaceRef)iosurf;

	OSType pf = IOSurfaceGetPixelFormat(ref);
	if (pf != 'BGRA')
		blog(LOG_ERROR, "Unexpected pixel format: %d (%c%c%c%c)", pf,
				pf >> 24, pf >> 16, pf >> 8, pf);

	if (tex->width != IOSurfaceGetWidth(ref) ||
			tex->height != IOSurfaceGetHeight(ref))
		return false;

	if (!gl_bind_texture(tex->base.gl_target, tex->base.texture))
		return false;

	CGLError err = CGLTexImageIOSurface2D(
			[[NSOpenGLContext currentContext] CGLContextObj],
			tex->base.gl_target,
			tex->base.gl_internal_format,
			tex->width, tex->height,
			tex->base.gl_format,
			tex->base.gl_type,
			ref, 0);
	
	if(err != kCGLNoError) {
		blog(LOG_ERROR, "CGLTexImageIOSurface2D: %u, %s"
			        " (texture_rebind_iosurface)",
				err, CGLErrorString(err));

		gl_success("CGLTexImageIOSurface2D");
		return false;
	}

	if (!gl_bind_texture(tex->base.gl_target, 0))
		return false;

	return true;
}
