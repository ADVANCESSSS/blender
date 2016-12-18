/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2005 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Brecht Van Lommel.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_math_base.h"

#include "BKE_global.h"

#include "GPU_debug.h"
#include "GPU_glew.h"
#include "GPU_framebuffer.h"
#include "GPU_shader.h"
#include "GPU_texture.h"

static struct GPUFrameBufferGlobal {
	GLuint currentfb;
} GG = {0};

/* Number of maximum output slots.
 * We support 4 outputs for now (usually we wouldn't need more to preserve fill rate) */
#define GPU_FB_MAX_SLOTS 4

struct GPUFrameBuffer {
	GLuint object;
	GPUTexture *colortex[GPU_FB_MAX_SLOTS];
	GPUTexture *depthtex;
	GPURenderBuffer *colorrb[GPU_FB_MAX_SLOTS];
	GPURenderBuffer *depthrb;
};

static void GPU_print_framebuffer_error(GLenum status, char err_out[256])
{
	const char *format = "GPUFrameBuffer: framebuffer status %s";
	const char *err = "unknown";

#define format_status(X) \
	case GL_FRAMEBUFFER_##X: err = "GL_FRAMEBUFFER_"#X; \
		break;

	switch (status) {
		/* success */
		format_status(COMPLETE)
		/* errors shared by OpenGL desktop & ES */
		format_status(INCOMPLETE_ATTACHMENT)
		format_status(INCOMPLETE_MISSING_ATTACHMENT)
		format_status(UNSUPPORTED)
#if 0 /* for OpenGL ES only */
		format_status(INCOMPLETE_DIMENSIONS)
#else /* for desktop GL only */
		format_status(INCOMPLETE_DRAW_BUFFER)
		format_status(INCOMPLETE_READ_BUFFER)
		format_status(INCOMPLETE_MULTISAMPLE)
		format_status(UNDEFINED)
#endif
	}

#undef format_status

	if (err_out) {
		BLI_snprintf(err_out, 256, format, err);
	}
	else {
		fprintf(stderr, format, err);
	}
}

/* GPUFrameBuffer */

GPUFrameBuffer *GPU_framebuffer_create(void)
{
	GPUFrameBuffer *fb;

	fb = MEM_callocN(sizeof(GPUFrameBuffer), "GPUFrameBuffer");
	glGenFramebuffers(1, &fb->object);

	if (!fb->object) {
		fprintf(stderr, "GPUFFrameBuffer: framebuffer gen failed.\n");
		GPU_framebuffer_free(fb);
		return NULL;
	}

	/* make sure no read buffer is enabled, so completeness check will not fail. We set those at binding time */
	glBindFramebuffer(GL_FRAMEBUFFER, fb->object);
	glReadBuffer(GL_NONE);
	glDrawBuffer(GL_NONE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	
	return fb;
}

bool GPU_framebuffer_texture_attach(GPUFrameBuffer *fb, GPUTexture *tex, int slot)
{
	return GPU_framebuffer_texture_attach_target(fb, tex, GPU_texture_target(tex), slot);
}

int GPU_framebuffer_texture_attach_target(GPUFrameBuffer *fb, GPUTexture *tex, int target, int slot)
{
	GLenum attachment;

	if (slot >= GPU_FB_MAX_SLOTS) {
		fprintf(stderr,
		        "Attaching to index %d framebuffer slot unsupported. "
		        "Use at most %d\n", slot, GPU_FB_MAX_SLOTS);
		return false;
	}

	if ((G.debug & G_DEBUG)) {
		if (GPU_texture_bound_number(tex) != -1) {
			fprintf(stderr,
			        "Feedback loop warning!: "
			        "Attempting to attach texture to framebuffer while still bound to texture unit for drawing!\n");
		}
	}

	if (GPU_texture_depth(tex))
		attachment = GL_DEPTH_ATTACHMENT;
	else
		attachment = GL_COLOR_ATTACHMENT0 + slot;

	glBindFramebuffer(GL_FRAMEBUFFER, fb->object);
	GG.currentfb = fb->object;

	glFramebufferTexture2D(GL_FRAMEBUFFER, attachment,
		target, GPU_texture_opengl_bindcode(tex), 0);

	if (GPU_texture_depth(tex))
		fb->depthtex = tex;
	else
		fb->colortex[slot] = tex;

	GPU_texture_framebuffer_set(tex, fb, slot);

	return true;
}

void GPU_framebuffer_texture_detach(GPUTexture *tex)
{
	GPU_framebuffer_texture_detach_target(tex, GPU_texture_target(tex));
}

void GPU_framebuffer_texture_detach_target(GPUTexture *tex, int target)
{
	GLenum attachment;
	GPUFrameBuffer *fb = GPU_texture_framebuffer(tex);
	int fb_attachment = GPU_texture_framebuffer_attachment(tex);

	if (!fb)
		return;

	if (GG.currentfb != fb->object) {
		glBindFramebuffer(GL_FRAMEBUFFER, fb->object);
		GG.currentfb = fb->object;
	}

	if (GPU_texture_depth(tex)) {
		fb->depthtex = NULL;
		attachment = GL_DEPTH_ATTACHMENT;
	}
	else {
		BLI_assert(fb->colortex[fb_attachment] == tex);
		fb->colortex[fb_attachment] = NULL;
		attachment = GL_COLOR_ATTACHMENT0 + fb_attachment;
	}

	glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, target, 0, 0);

	GPU_texture_framebuffer_set(tex, NULL, -1);
}

void GPU_texture_bind_as_framebuffer(GPUTexture *tex)
{
	GPUFrameBuffer *fb = GPU_texture_framebuffer(tex);
	int fb_attachment = GPU_texture_framebuffer_attachment(tex);

	if (!fb) {
		fprintf(stderr, "Error, texture not bound to framebuffer!\n");
		return;
	}

	/* push attributes */
	glPushAttrib(GL_ENABLE_BIT | GL_VIEWPORT_BIT);
	glDisable(GL_SCISSOR_TEST);

	/* bind framebuffer */
	glBindFramebuffer(GL_FRAMEBUFFER, fb->object);

	if (GPU_texture_depth(tex)) {
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
	}
	else {
		/* last bound prevails here, better allow explicit control here too */
		glDrawBuffer(GL_COLOR_ATTACHMENT0 + fb_attachment);
		glReadBuffer(GL_COLOR_ATTACHMENT0 + fb_attachment);
	}
	
	if (GPU_texture_target(tex) == GL_TEXTURE_2D_MULTISAMPLE) {
		glEnable(GL_MULTISAMPLE);
	}

	/* push matrices and set default viewport and matrix */
	glViewport(0, 0, GPU_texture_width(tex), GPU_texture_height(tex));
	GG.currentfb = fb->object;

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
}

void GPU_framebuffer_slots_bind(GPUFrameBuffer *fb, int slot)
{
	int numslots = 0, i;
	GLenum attachments[4];
	
	if (!fb->colortex[slot]) {
		fprintf(stderr, "Error, framebuffer slot empty!\n");
		return;
	}
	
	for (i = 0; i < 4; i++) {
		if (fb->colortex[i]) {
			attachments[numslots] = GL_COLOR_ATTACHMENT0 + i;
			numslots++;
		}
	}
	
	/* push attributes */
	glPushAttrib(GL_ENABLE_BIT | GL_VIEWPORT_BIT);
	glDisable(GL_SCISSOR_TEST);

	/* bind framebuffer */
	glBindFramebuffer(GL_FRAMEBUFFER, fb->object);

	/* last bound prevails here, better allow explicit control here too */
	glDrawBuffers(numslots, attachments);
	glReadBuffer(GL_COLOR_ATTACHMENT0 + slot);

	/* push matrices and set default viewport and matrix */
	glViewport(0, 0, GPU_texture_width(fb->colortex[slot]), GPU_texture_height(fb->colortex[slot]));
	GG.currentfb = fb->object;

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
}


void GPU_framebuffer_texture_unbind(GPUFrameBuffer *UNUSED(fb), GPUTexture *UNUSED(tex))
{
	/* restore matrix */
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	/* restore attributes */
	glPopAttrib();
}

void GPU_framebuffer_bind_no_save(GPUFrameBuffer *fb, int slot)
{
	glBindFramebuffer(GL_FRAMEBUFFER, fb->object);
	/* last bound prevails here, better allow explicit control here too */
	glDrawBuffer(GL_COLOR_ATTACHMENT0 + slot);
	glReadBuffer(GL_COLOR_ATTACHMENT0 + slot);

	/* push matrices and set default viewport and matrix */
	glViewport(0, 0, GPU_texture_width(fb->colortex[slot]), GPU_texture_height(fb->colortex[slot]));
	GG.currentfb = fb->object;
	GG.currentfb = fb->object;
}

void GPU_framebuffer_bind_simple(GPUFrameBuffer *fb)
{
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb->object);
	/* last bound prevails here, better allow explicit control here too */
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);

	GG.currentfb = fb->object;
}

bool GPU_framebuffer_bound(GPUFrameBuffer *fb)
{
	return fb->object == GG.currentfb;
}

bool GPU_framebuffer_check_valid(GPUFrameBuffer *fb, char err_out[256])
{
	glBindFramebuffer(GL_FRAMEBUFFER, fb->object);
	GG.currentfb = fb->object;

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	if (status != GL_FRAMEBUFFER_COMPLETE) {
		GPU_framebuffer_restore();
		GPU_print_framebuffer_error(status, err_out);
		return false;
	}

	return true;
}

int GPU_framebuffer_renderbuffer_attach(GPUFrameBuffer *fb, GPURenderBuffer *rb, int slot)
{
	GLenum attachement;
	GLenum error;

	if (slot >= GPU_FB_MAX_SLOTS) {
		fprintf(stderr,
		        "Attaching to index %d framebuffer slot unsupported. "
		        "Use at most %d\n", slot, GPU_FB_MAX_SLOTS);
		return 0;
	}

	if (GPU_renderbuffer_depth(rb)) {
		attachement = GL_DEPTH_ATTACHMENT_EXT;
	}
	else {
		attachement = GL_COLOR_ATTACHMENT0_EXT + slot;
	}

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb->object);
	GG.currentfb = fb->object;

	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, attachement, GL_RENDERBUFFER_EXT, GPU_renderbuffer_bindcode(rb));

	if (GPU_renderbuffer_depth(rb))
		fb->depthrb = rb;
	else
		fb->colorrb[slot] = rb;

	GPU_renderbuffer_framebuffer_set(rb, fb, slot);

	return 1;
}

void GPU_framebuffer_renderbuffer_detach(GPURenderBuffer *rb)
{
	GLenum attachment;
	GPUFrameBuffer *fb = GPU_renderbuffer_framebuffer(rb);
	int fb_attachment = GPU_renderbuffer_framebuffer_attachment(rb);

	if (!fb)
		return;

	if (GG.currentfb != fb->object) {
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb->object);
		GG.currentfb = fb->object;
	}

	if (GPU_renderbuffer_depth(rb)) {
		fb->depthrb = NULL;
		attachment = GL_DEPTH_ATTACHMENT_EXT;
	}
	else {
		BLI_assert(fb->colorrb[fb_attachment] == rb);
		fb->colorrb[fb_attachment] = NULL;
		attachment = GL_COLOR_ATTACHMENT0_EXT + fb_attachment;
	}

	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, attachment, GL_RENDERBUFFER_EXT, 0);

	GPU_renderbuffer_framebuffer_set(rb, NULL, -1);
}

void GPU_framebuffer_free(GPUFrameBuffer *fb)
{
	int i;
	if (fb->depthtex)
		GPU_framebuffer_texture_detach(fb->depthtex);

	for (i = 0; i < GPU_FB_MAX_SLOTS; i++) {
		if (fb->colortex[i]) {
			GPU_framebuffer_texture_detach(fb->colortex[i]);
		}
	}

	if (fb->depthrb)
		GPU_framebuffer_renderbuffer_detach(fb->depthrb);

	for (i = 0; i < GPU_FB_MAX_SLOTS; i++) {
		if (fb->colorrb[i]) {
			GPU_framebuffer_renderbuffer_detach(fb->colorrb[i]);
		}
	}

	if (fb->object) {
		glDeleteFramebuffers(1, &fb->object);

		if (GG.currentfb == fb->object) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			GG.currentfb = 0;
		}
	}

	MEM_freeN(fb);
}

void GPU_framebuffer_restore(void)
{
	if (GG.currentfb != 0) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		GG.currentfb = 0;
	}
}

void GPU_framebuffer_blur(
        GPUFrameBuffer *fb, GPUTexture *tex,
        GPUFrameBuffer *blurfb, GPUTexture *blurtex, float sharpness)
{
	const float scaleh[2] = {(1.0f - sharpness) / GPU_texture_width(blurtex), 0.0f};
	const float scalev[2] = {0.0f, (1.0f - sharpness) / GPU_texture_height(tex)};

	GPUShader *blur_shader = GPU_shader_get_builtin_shader(GPU_SHADER_SEP_GAUSSIAN_BLUR);
	int scale_uniform, texture_source_uniform;

	if (!blur_shader)
		return;

	scale_uniform = GPU_shader_get_uniform(blur_shader, "ScaleU");
	texture_source_uniform = GPU_shader_get_uniform(blur_shader, "textureSource");
		
	/* Blurring horizontally */

	/* We do the bind ourselves rather than using GPU_framebuffer_texture_bind() to avoid
	 * pushing unnecessary matrices onto the OpenGL stack. */
	glBindFramebuffer(GL_FRAMEBUFFER, blurfb->object);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	
	/* avoid warnings from texture binding */
	GG.currentfb = blurfb->object;

	GPU_shader_bind(blur_shader);
	GPU_shader_uniform_vector(blur_shader, scale_uniform, 2, 1, scaleh);
	GPU_shader_uniform_texture(blur_shader, texture_source_uniform, tex);
	glViewport(0, 0, GPU_texture_width(blurtex), GPU_texture_height(blurtex));

	/* Preparing to draw quad */
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glDisable(GL_DEPTH_TEST);

	GPU_texture_bind(tex, 0);

	/* Drawing quad */
	glBegin(GL_QUADS);
	glTexCoord2d(0, 0); glVertex2f(1, 1);
	glTexCoord2d(1, 0); glVertex2f(-1, 1);
	glTexCoord2d(1, 1); glVertex2f(-1, -1);
	glTexCoord2d(0, 1); glVertex2f(1, -1);
	glEnd();

	/* Blurring vertically */

	glBindFramebuffer(GL_FRAMEBUFFER, fb->object);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	
	GG.currentfb = fb->object;
	
	glViewport(0, 0, GPU_texture_width(tex), GPU_texture_height(tex));
	GPU_shader_uniform_vector(blur_shader, scale_uniform, 2, 1, scalev);
	GPU_shader_uniform_texture(blur_shader, texture_source_uniform, blurtex);
	GPU_texture_bind(blurtex, 0);

	glBegin(GL_QUADS);
	glTexCoord2d(0, 0); glVertex2f(1, 1);
	glTexCoord2d(1, 0); glVertex2f(-1, 1);
	glTexCoord2d(1, 1); glVertex2f(-1, -1);
	glTexCoord2d(0, 1); glVertex2f(1, -1);
	glEnd();

	GPU_texture_unbind(blurtex);
	GPU_shader_unbind();
}

/* GPURenderBuffer */

struct GPURenderBuffer {
	int width;
	int height;
	int samples;

	GPUFrameBuffer *fb; /* GPUFramebuffer this render buffer is attached to */
	int fb_attachment;  /* slot the render buffer is attached to */
	bool depth;
	unsigned int bindcode;
};

GPURenderBuffer *GPU_renderbuffer_create(int width, int height, int samples, GPUHDRType hdrtype, GPURenderBufferType type, char err_out[256])
{
	GPURenderBuffer *rb = MEM_callocN(sizeof(GPURenderBuffer), "GPURenderBuffer");

	glGenRenderbuffers(1, &rb->bindcode);

	if (!rb->bindcode) {
		if (err_out) {
			BLI_snprintf(err_out, 256, "GPURenderBuffer: render buffer creation failed: %d",
				(int)glGetError());
		}
		else {
			fprintf(stderr, "GPURenderBuffer: render buffer creation failed: %d\n",
				(int)glGetError());
		}
		GPU_renderbuffer_free(rb);
		return NULL;
	}

	rb->width = width;
	rb->height = height;
	rb->samples = samples;

	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb->bindcode);

	if (type == GPU_RENDERBUFFER_DEPTH) {
		if (samples > 0) {
			glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, samples, GL_DEPTH_COMPONENT, width, height);
		}
		else {
			glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT, width, height);
		}
		rb->depth = true;
	}
	else {
		GLenum internalformat = GL_RGBA8;
		switch (hdrtype) {
			case GPU_HDR_NONE:
			{
				internalformat = GL_RGBA8;
				break;
			}
			/* the following formats rely on ARB_texture_float or OpenGL 3.0 */
			case GPU_HDR_HALF_FLOAT:
			{
				internalformat = GL_RGBA16F_ARB;
				break;
			}
			case GPU_HDR_FULL_FLOAT:
			{
				internalformat = GL_RGBA32F_ARB;
				break;
			}
		}
		if (samples > 0) {
			glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, samples, internalformat, width, height);
		}
		else {
			glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, internalformat, width, height);
		}
	}

	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, 0);

	return rb;
}

void GPU_renderbuffer_free(GPURenderBuffer *rb)
{
	if (rb->bindcode) {
		glDeleteRenderbuffersEXT(1, &rb->bindcode);
	}

	MEM_freeN(rb);
}

GPUFrameBuffer *GPU_renderbuffer_framebuffer(GPURenderBuffer *rb)
{
	return rb->fb;
}

int GPU_renderbuffer_framebuffer_attachment(GPURenderBuffer *rb)
{
	return rb->fb_attachment;
}

void GPU_renderbuffer_framebuffer_set(GPURenderBuffer *rb, GPUFrameBuffer *fb, int attachement)
{
	rb->fb = fb;
	rb->fb_attachment = attachement;
}

int GPU_renderbuffer_bindcode(const GPURenderBuffer *rb)
{
	return rb->bindcode;
}

bool GPU_renderbuffer_depth(const GPURenderBuffer *rb)
{
	return rb->depth;
}

int GPU_renderbuffer_width(const GPURenderBuffer *rb)
{
	return rb->width;
}

int GPU_renderbuffer_height(const GPURenderBuffer *rb)
{
	return rb->height;
}


/* GPUOffScreen */

struct GPUOffScreen {
	GPUFrameBuffer *fb;
	GPUTexture *color;
	GPUTexture *depth;
	GPURenderBuffer *rbcolor;
	GPURenderBuffer *rbdepth;
	int samples;
};

GPUOffScreen *GPU_offscreen_create(int width, int height, int samples, GPUHDRType hdrtype, int mode, char err_out[256])
{
	GPUOffScreen *ofs;

	ofs = MEM_callocN(sizeof(GPUOffScreen), "GPUOffScreen");

	ofs->fb = GPU_framebuffer_create();
	if (!ofs->fb) {
		GPU_offscreen_free(ofs);
		return NULL;
	}

	if (samples) {
		if (!GLEW_EXT_framebuffer_multisample ||
			/* Disable multisample for texture and not render buffers
			 * when it's not supported */
		    (!GLEW_ARB_texture_multisample && (!(mode & GPU_OFFSCREEN_RENDERBUFFER_COLOR) || !(mode & GPU_OFFSCREEN_RENDERBUFFER_DEPTH))) ||
		    /* Only needed for GPU_offscreen_read_pixels.
		     * We could add an arg if we intend to use multi-sample
		     * offscreen buffers w/o reading their pixels */
		    !GLEW_EXT_framebuffer_blit

	/* Some GPUs works even without this extension. */
#if 0
		    /* This is required when blitting from a multi-sampled buffers,
		     * even though we're not scaling. */
		    || !GLEW_EXT_framebuffer_multisample_blit_scaled
#endif
			)
		{
			samples = 0;
		}
	}

	ofs->samples = samples;

	if (mode & GPU_OFFSCREEN_RENDERBUFFER_COLOR) {
		ofs->rbcolor = GPU_renderbuffer_create(width, height, samples, hdrtype, GPU_RENDERBUFFER_COLOR, err_out);
		if (!ofs->rbcolor) {
			GPU_offscreen_free(ofs);
			return NULL;
		}

		if (!GPU_framebuffer_renderbuffer_attach(ofs->fb, ofs->rbcolor, 0)) {
			GPU_offscreen_free(ofs);
			return NULL;
		}
	}
	else {
		ofs->color = GPU_texture_create_2D_multisample(width, height, NULL, hdrtype, samples, err_out);
		if (!ofs->color) {
			GPU_offscreen_free(ofs);
			return NULL;
		}

		if (!GPU_framebuffer_texture_attach(ofs->fb, ofs->color, 0)) {
			GPU_offscreen_free(ofs);
			return NULL;
		}
	}

	if (mode & GPU_OFFSCREEN_RENDERBUFFER_DEPTH) {
		ofs->rbdepth = GPU_renderbuffer_create(width, height, samples, GPU_HDR_NONE, GPU_RENDERBUFFER_DEPTH, err_out);
		if (!ofs->rbdepth) {
			GPU_offscreen_free(ofs);
			return NULL;
		}

		if (!GPU_framebuffer_renderbuffer_attach(ofs->fb, ofs->rbdepth, 0)) {
			GPU_offscreen_free(ofs);
			return NULL;
		}
	}
	else {
		ofs->depth = GPU_texture_create_depth_multisample(width, height, samples, (mode & GPU_OFFSCREEN_DEPTH_COMPARE), err_out);
		if (!ofs->depth) {
			GPU_offscreen_free(ofs);
			return NULL;
		}

		if (!GPU_framebuffer_texture_attach(ofs->fb, ofs->depth, 0)) {
			GPU_offscreen_free(ofs);
			return NULL;
		}
	}

	/* check validity at the very end! */
	if (!GPU_framebuffer_check_valid(ofs->fb, err_out)) {
		GPU_offscreen_free(ofs);
		return NULL;		
	}

	GPU_framebuffer_restore();

	return ofs;
}

void GPU_offscreen_free(GPUOffScreen *ofs)
{
	if (ofs->fb)
		GPU_framebuffer_free(ofs->fb);
	if (ofs->color)
		GPU_texture_free(ofs->color);
	if (ofs->depth)
		GPU_texture_free(ofs->depth);
	if (ofs->rbcolor) {
		GPU_renderbuffer_free(ofs->rbcolor);
	}
	if (ofs->rbdepth) {
		GPU_renderbuffer_free(ofs->rbdepth);
	}
	
	MEM_freeN(ofs);
}

void GPU_offscreen_bind(GPUOffScreen *ofs, bool save)
{
	glDisable(GL_SCISSOR_TEST);
	if (save)
		GPU_texture_bind_as_framebuffer(ofs->color);
	else {
		GPU_framebuffer_bind_no_save(ofs->fb, 0);
	}
}

void GPU_offscreen_bind_simple(GPUOffScreen *ofs)
{
	GPU_framebuffer_bind_simple(ofs->fb);
}

void GPU_offscreen_unbind(GPUOffScreen *ofs, bool restore)
{
	if (restore)
		GPU_framebuffer_texture_unbind(ofs->fb, ofs->color);
	GPU_framebuffer_restore();
	glEnable(GL_SCISSOR_TEST);
}

void GPU_offscreen_read_pixels(GPUOffScreen *ofs, int type, void *pixels)
{
	const int w = GPU_texture_width(ofs->color);
	const int h = GPU_texture_height(ofs->color);

	if (GPU_texture_target(ofs->color) == GL_TEXTURE_2D_MULTISAMPLE) {
		/* For a multi-sample texture,
		 * we need to create an intermediate buffer to blit to,
		 * before its copied using 'glReadPixels' */

		/* not needed since 'ofs' needs to be bound to the framebuffer already */
// #define USE_FBO_CTX_SWITCH

		GLuint fbo_blit = 0;
		GLuint tex_blit = 0;
		GLenum status;

		/* create texture for new 'fbo_blit' */
		glGenTextures(1, &tex_blit);
		if (!tex_blit) {
			goto finally;
		}

		glBindTexture(GL_TEXTURE_2D, tex_blit);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, type, 0);

#ifdef USE_FBO_CTX_SWITCH
		/* read from multi-sample buffer */
		glBindFramebuffer(GL_READ_FRAMEBUFFER, ofs->color->fb->object);
		glFramebufferTexture2D(
		        GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + ofs->color->fb_attachment,
		        GL_TEXTURE_2D_MULTISAMPLE, ofs->color->bindcode, 0);
		status = glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			goto finally;
		}
#endif

		/* write into new single-sample buffer */
		glGenFramebuffers(1, &fbo_blit);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_blit);
		glFramebufferTexture2D(
		        GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		        GL_TEXTURE_2D, tex_blit, 0);
		status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			goto finally;
		}

		/* perform the copy */
		glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		/* read the results */
		glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_blit);
		glReadPixels(0, 0, w, h, GL_RGBA, type, pixels);

#ifdef USE_FBO_CTX_SWITCH
		/* restore the original frame-bufer */
		glBindFramebuffer(GL_FRAMEBUFFER, ofs->color->fb->object);
#undef USE_FBO_CTX_SWITCH
#endif


finally:
		/* cleanup */
		if (tex_blit) {
			glDeleteTextures(1, &tex_blit);
		}
		if (fbo_blit) {
			glDeleteFramebuffers(1, &fbo_blit);
		}
	}
	else {
		glReadPixels(0, 0, w, h, GL_RGBA, type, pixels);
	}
}

void GPU_offscreen_blit(GPUOffScreen *srcofs, GPUOffScreen *dstofs, bool color, bool depth)
{
	BLI_assert(color || depth);

	glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, srcofs->fb->object);
	glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, dstofs->fb->object);

	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);

	int height = min_ff(GPU_offscreen_height(srcofs), GPU_offscreen_height(dstofs));
	int width = min_ff(GPU_offscreen_width(srcofs), GPU_offscreen_width(dstofs));


	int mask = 0;
	if (color) {
		mask |= GL_COLOR_BUFFER_BIT;
	}
	if (depth) {
		mask |= GL_DEPTH_BUFFER_BIT;
	}

	glBlitFramebufferEXT(0, 0, width, height, 0, 0, width, height, mask, GL_NEAREST);

	// Call GPU_framebuffer_bind_simple to change GG.currentfb.
	GPU_framebuffer_bind_simple(dstofs->fb);
}

int GPU_offscreen_width(const GPUOffScreen *ofs)
{
	if (ofs->color) {
		return GPU_texture_width(ofs->color);
	}
	else if (ofs->rbcolor) {
		return GPU_renderbuffer_width(ofs->rbcolor);
	}

	// Should never happen.
	return 0;
}

int GPU_offscreen_height(const GPUOffScreen *ofs)
{
	if (ofs->color) {
		return GPU_texture_height(ofs->color);
	}
	else if (ofs->rbcolor) {
		return GPU_renderbuffer_height(ofs->rbcolor);
	}

	// Should never happen.
	return 0;
}

int GPU_offscreen_samples(const GPUOffScreen *ofs)
{
	return ofs->samples;
}

int GPU_offscreen_color_texture(const GPUOffScreen *ofs)
{
	return GPU_texture_opengl_bindcode(ofs->color);
}

GPUTexture *GPU_offscreen_texture(const GPUOffScreen *ofs)
{
	return ofs->color;
}

GPUTexture *GPU_offscreen_depth_texture(const GPUOffScreen *ofs)
{
	return ofs->depth;
}

