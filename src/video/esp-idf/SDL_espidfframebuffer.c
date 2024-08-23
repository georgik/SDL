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

    // Prepare the RGB565 buffer
    int pixel_count = surface->w * surface->h;
    uint16_t *rgb565_buffer = (uint16_t *)malloc(pixel_count * sizeof(uint16_t));
    if (!rgb565_buffer) {
        return SDL_SetError("Failed to allocate memory for RGB565 buffer");
    }

    // Convert RGBA to RGB565
    uint32_t *pixels = (uint32_t *)surface->pixels;
    for (int i = 0; i < pixel_count; i++) {
        uint32_t rgba = pixels[i];
        uint8_t r = (rgba >> 24) & 0xFF;
        uint8_t g = (rgba >> 16) & 0xFF;
        uint8_t b = (rgba >> 8) & 0xFF;

        // Convert to RGB565
        rgb565_buffer[i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    // Send the RGB565 buffer to the display
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, surface->w, surface->h, rgb565_buffer));

    free(rgb565_buffer); // Free the allocated buffer
    return 0;
}



void SDL_ESPIDF_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_ClearProperty(SDL_GetWindowProperties(window), ESPIDF_SURFACE);
}

#endif /* SDL_VIDEO_DRIVER_ESP_IDF */
