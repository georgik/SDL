#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_ESP_IDF

#include "../SDL_sysvideo.h"
#include "../../SDL_properties_c.h"
#include "SDL_espidfframebuffer.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "SDL_espidfshared.h"

#define ESPIDF_SURFACE "SDL.internal.window.surface"

int SDL_ESPIDF_CreateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, SDL_PixelFormat *format, void **pixels, int *pitch)
{
    SDL_Surface *surface;
    const SDL_PixelFormat surface_format = SDL_PIXELFORMAT_XRGB8888;
    int w, h;

    SDL_GetWindowSizeInPixels(window, &w, &h);
    surface = SDL_CreateSurface(w, h, surface_format);
    if (!surface) {
        return -1;
    }

    SDL_SetSurfaceProperty(SDL_GetWindowProperties(window), ESPIDF_SURFACE, surface);
    *format = surface_format;
    *pixels = surface->pixels;
    *pitch = surface->pitch;
    return 0;
}

int SDL_ESPIDF_UpdateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_Surface *surface;

    surface = (SDL_Surface *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), ESPIDF_SURFACE, NULL);
    if (!surface) {
        return SDL_SetError("Couldn't find ESPIDF surface for window");
    }

    int pixel_count = surface->w * surface->h;
    uint16_t *rgb565_buffer = (uint16_t *)malloc(pixel_count * sizeof(uint16_t));
    if (!rgb565_buffer) {
        return SDL_SetError("Failed to allocate memory for RGB565 buffer");
    }

    uint32_t *pixels = (uint32_t *)surface->pixels;
    for (int i = 0; i < pixel_count; i++) {
        uint32_t rgba = pixels[i];
        uint8_t r = (rgba >> 16) & 0xFF;  // Extract Red
        uint8_t g = (rgba >> 8) & 0xFF;   // Extract Green
        uint8_t b = (rgba >> 0) & 0xFF;   // Extract Blue

        // Correct RGB565 conversion
        uint16_t b5 = (b >> 3) & 0x1F;   // Blue 5 bits
        uint16_t g6 = (g >> 2) & 0x3F;   // Green 6 bits
        uint16_t r5 = (r >> 3) & 0x1F;   // Red 5 bits

        rgb565_buffer[i] = (b5 << 11) | (r5 << 5) | g6;  // Combine into RGB565 format
    }

    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, surface->w, surface->h, rgb565_buffer));

    free(rgb565_buffer);
    return 0;
}


void SDL_ESPIDF_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_ClearProperty(SDL_GetWindowProperties(window), ESPIDF_SURFACE);
}

#endif /* SDL_VIDEO_DRIVER_ESP_IDF */
