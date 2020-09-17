#include "../Rendering.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../Misc.h"
#include "Window/Software.h"
#include "../../Attributes.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct RenderBackend_Surface
{
	unsigned char *pixels;
	size_t width;
	size_t height;
	size_t pitch;
} RenderBackend_Surface;

typedef struct RenderBackend_GlyphAtlas
{
	unsigned char *pixels;
	size_t width;
	size_t height;
} RenderBackend_GlyphAtlas;

static RenderBackend_Surface framebuffer;

static RenderBackend_GlyphAtlas *glyph_atlas;
static RenderBackend_Surface *glyph_destination_surface;
static unsigned char glyph_colour_channels[3];

RenderBackend_Surface* RenderBackend_Init(const char *window_title, size_t screen_width, size_t screen_height, bool fullscreen)
{
	if (WindowBackend_Software_CreateWindow(window_title, screen_width, screen_height, fullscreen))
	{
		framebuffer.pixels = WindowBackend_Software_GetFramebuffer(&framebuffer.pitch);
		framebuffer.width = screen_width;
		framebuffer.height = screen_height;

		return &framebuffer;
	}
	else
	{
		Backend_PrintError("Failed to create window");
	}

	return NULL;
}

void RenderBackend_Deinit(void)
{
	WindowBackend_Software_DestroyWindow();
}

void RenderBackend_DrawScreen(void)
{
	WindowBackend_Software_Display();

	// Backends may use double-buffering, so fetch a new framebuffer just in case
	framebuffer.pixels = WindowBackend_Software_GetFramebuffer(&framebuffer.pitch);
}

RenderBackend_Surface* RenderBackend_CreateSurface(size_t width, size_t height, bool render_target)
{
	(void)render_target;

	RenderBackend_Surface *surface = (RenderBackend_Surface*)malloc(sizeof(RenderBackend_Surface));

	if (surface == NULL)
		return NULL;

	surface->pixels = (unsigned char*)malloc(width * height * 3);

	if (surface->pixels == NULL)
	{
		free(surface);
		return NULL;
	}

	surface->width = width;
	surface->height = height;
	surface->pitch = width * 3;

	return surface;
}

void RenderBackend_FreeSurface(RenderBackend_Surface *surface)
{
	free(surface->pixels);
	free(surface);
}

bool RenderBackend_IsSurfaceLost(RenderBackend_Surface *surface)
{
	(void)surface;

	return false;
}

void RenderBackend_RestoreSurface(RenderBackend_Surface *surface)
{
	(void)surface;
}

void RenderBackend_UploadSurface(RenderBackend_Surface *surface, const unsigned char *pixels, size_t width, size_t height)
{
	for (size_t y = 0; y < height; ++y)
		memcpy(&surface->pixels[y * surface->pitch], &pixels[y * width * 3], width * 3);
}

ATTRIBUTE_HOT void RenderBackend_Blit(RenderBackend_Surface *source_surface, const RenderBackend_Rect *rect, RenderBackend_Surface *destination_surface, long x, long y, bool colour_key)
{
	RenderBackend_Rect rect_clamped;

	rect_clamped.left = rect->left;
	rect_clamped.top = rect->top;
	rect_clamped.right = rect->right;
	rect_clamped.bottom = rect->bottom;

	// Clamp the rect and coordinates so we don't write outside the pixel buffer
	long overflow;

	overflow = 0 - x;
	if (overflow > 0)
	{
		rect_clamped.left += overflow;
		x += overflow;
	}

	overflow = 0 - y;
	if (overflow > 0)
	{
		rect_clamped.top += overflow;
		y += overflow;
	}

	overflow = (x + (rect_clamped.right - rect_clamped.left)) - destination_surface->width;
	if (overflow > 0)
		rect_clamped.right -= overflow;

	overflow = (y + (rect_clamped.bottom - rect_clamped.top)) - destination_surface->height;
	if (overflow > 0)
		rect_clamped.bottom -= overflow;

	if (rect_clamped.bottom - rect_clamped.top <= 0)
		return;

	if (rect_clamped.right - rect_clamped.left <= 0)
		return;

	// Do the actual blitting
	if (colour_key)
	{
		for (long j = 0; j < rect_clamped.bottom - rect_clamped.top; ++j)
		{
			const unsigned char *source_pointer = &source_surface->pixels[((rect_clamped.top + j) * source_surface->pitch) + (rect_clamped.left * 3)];
			unsigned char *destination_pointer = &destination_surface->pixels[((y + j) * destination_surface->pitch) + (x * 3)];

			for (long i = 0; i < rect_clamped.right - rect_clamped.left; ++i)
			{
				if (UNLIKELY(source_pointer[0] == 0 && source_pointer[1] == 0 && source_pointer[2] == 0))	// Assumes the colour key will always be #000000 (black)
				{
					source_pointer += 3;
					destination_pointer += 3;
				}
				else
				{
					*destination_pointer++ = *source_pointer++;
					*destination_pointer++ = *source_pointer++;
					*destination_pointer++ = *source_pointer++;
				}
			}
		}
	}
	else
	{
		const unsigned char *source_pointer = &source_surface->pixels[(rect_clamped.top * source_surface->pitch) + (rect_clamped.left * 3)];
		unsigned char *destination_pointer = &destination_surface->pixels[(y * destination_surface->pitch) + (x * 3)];

		for (long j = 0; j < rect_clamped.bottom - rect_clamped.top; ++j)
		{
			memcpy(destination_pointer, source_pointer, (rect_clamped.right - rect_clamped.left) * 3);

			source_pointer += source_surface->pitch;
			destination_pointer += destination_surface->pitch;
		}
	}
}

ATTRIBUTE_HOT void RenderBackend_ColourFill(RenderBackend_Surface *surface, const RenderBackend_Rect *rect, unsigned char red, unsigned char green, unsigned char blue)
{
	RenderBackend_Rect rect_clamped;

	rect_clamped.left = rect->left;
	rect_clamped.top = rect->top;
	rect_clamped.right = rect->right;
	rect_clamped.bottom = rect->bottom;

	// Clamp the rect so it doesn't write outside the pixel buffer
	long overflow;

	overflow = 0 - rect_clamped.left;
	if (overflow > 0)
		rect_clamped.left += overflow;

	overflow = 0 - rect_clamped.top;
	if (overflow > 0)
		rect_clamped.top += overflow;

	overflow = rect_clamped.right - surface->width;
	if (overflow > 0)
		rect_clamped.right -= overflow;

	overflow = rect_clamped.bottom - surface->height;
	if (overflow > 0)
		rect_clamped.bottom -= overflow;

	if (rect_clamped.bottom - rect_clamped.top <= 0)
		return;

	if (rect_clamped.right - rect_clamped.left <= 0)
		return;

	for (long j = 0; j < rect_clamped.bottom - rect_clamped.top; ++j)
	{
		unsigned char *destination_pointer = &surface->pixels[((rect_clamped.top + j) * surface->pitch) + (rect_clamped.left * 3)];

		for (long i = 0; i < rect_clamped.right - rect_clamped.left; ++i)
		{
			*destination_pointer++ = red;
			*destination_pointer++ = green;
			*destination_pointer++ = blue;
		}
	}
}

RenderBackend_GlyphAtlas* RenderBackend_CreateGlyphAtlas(size_t width, size_t height)
{
	RenderBackend_GlyphAtlas *atlas = (RenderBackend_GlyphAtlas*)malloc(sizeof(RenderBackend_GlyphAtlas));

	if (atlas != NULL)
	{
		atlas->pixels = (unsigned char*)malloc(width * height);

		if (atlas->pixels != NULL)
		{
			atlas->width = width;
			atlas->height = height;

			return atlas;
		}

		free(atlas);
	}

	return NULL;	
}

void RenderBackend_DestroyGlyphAtlas(RenderBackend_GlyphAtlas *atlas)
{
	free(atlas->pixels);
	free(atlas);
}

void RenderBackend_UploadGlyph(RenderBackend_GlyphAtlas *atlas, size_t x, size_t y, const unsigned char *pixels, size_t width, size_t height, size_t pitch)
{
	for (size_t i = 0; i < height; ++i)
		memcpy(&atlas->pixels[(y + i) * atlas->width + x], &pixels[i * pitch], width);
}

void RenderBackend_PrepareToDrawGlyphs(RenderBackend_GlyphAtlas *atlas, RenderBackend_Surface *destination_surface, unsigned char red, unsigned char green, unsigned char blue)
{
	glyph_atlas = atlas;
	glyph_destination_surface = destination_surface;

	glyph_colour_channels[0] = red;
	glyph_colour_channels[1] = green;
	glyph_colour_channels[2] = blue;
}

void RenderBackend_DrawGlyph(long x, long y, size_t glyph_x, size_t glyph_y, size_t glyph_width, size_t glyph_height)
{
	size_t surface_x;
	size_t surface_y;

	// Clamp to within the destination surface's boundaries
	if (x < 0)
	{
		surface_x = -x;

		if (surface_x >= glyph_width)
			return; // Glyph is offscreen to the left

		glyph_x += surface_x;
		glyph_width -= surface_x;
	}
	else
	{
		surface_x = x;
	}

	if (surface_x >= glyph_destination_surface->width)
		return; // Glyph is offscreen to the right

	if (glyph_width >= glyph_destination_surface->width - surface_x)
		glyph_width = glyph_destination_surface->width - surface_x;

	if (y < 0)
	{
		surface_y = -y;

		if (surface_y >= glyph_height)
			return; // Glyph is offscreen to the top

		glyph_y += surface_y;
		glyph_height -= surface_y;
	}
	else
	{
		surface_y = y;
	}

	if (surface_y >= glyph_destination_surface->height)
		return; // Glyph is offscreen to the bottom

	if (glyph_height >= glyph_destination_surface->height - surface_y)
		glyph_height = glyph_destination_surface->height - surface_y;

	// Do the actual drawing
	for (size_t iy = 0; iy < glyph_height; ++iy)
	{
		for (size_t ix = 0; ix < glyph_width; ++ix)
		{
			const unsigned char alpha_int = glyph_atlas->pixels[(glyph_y + iy) * glyph_atlas->width + (glyph_x + ix)];

			if (alpha_int != 0)
			{
				const float alpha = alpha_int / 255.0f;

				unsigned char *bitmap_pixel = &glyph_destination_surface->pixels[(surface_y + iy) * glyph_destination_surface->pitch + (surface_x + ix) * 3];

				for (unsigned int j = 0; j < 3; ++j)
					bitmap_pixel[j] = (unsigned char)((glyph_colour_channels[j] * alpha) + (bitmap_pixel[j] * (1.0f - alpha)));	// Alpha blending
			}
		}
	}
}

void RenderBackend_HandleRenderTargetLoss(void)
{
	// No problem for us
}

void RenderBackend_HandleWindowResize(size_t width, size_t height)
{
	WindowBackend_Software_HandleWindowResize(width, height);
}
