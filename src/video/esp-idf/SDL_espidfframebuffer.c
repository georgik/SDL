#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_ESP_IDF

#include "../SDL_sysvideo.h"
#include "../../SDL_properties_c.h"
#include "SDL_espidfframebuffer.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "SDL_espidfshared.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"

#define ESPIDF_SURFACE "SDL.internal.window.surface"

static uint16_t *rgb565_buffer = NULL;

static SemaphoreHandle_t lcd_semaphore;

static void lcd_event_callback(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event_data, void *user_ctx)
{
    // Give the semaphore to signal the completion of the transfer
    xSemaphoreGive(lcd_semaphore);
}

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

    // Allocate RGB565 buffer in IRAM
    rgb565_buffer = heap_caps_malloc(w * 40 * sizeof(uint16_t), MALLOC_CAP_32BIT | MALLOC_CAP_INTERNAL);
    if (!rgb565_buffer) {
        SDL_DestroySurface(surface);
        return SDL_SetError("Failed to allocate memory for RGB565 buffer");
    }

    // Create a semaphore to synchronize LCD transactions
    lcd_semaphore = xSemaphoreCreateBinary();
    if (!lcd_semaphore) {
        heap_caps_free(rgb565_buffer);
        SDL_DestroySurface(surface);
        return SDL_SetError("Failed to create semaphore");
    }

    // Register the callback
    esp_lcd_panel_io_register_event_callbacks(panel_io_handle, &(esp_lcd_panel_io_callbacks_t){ .on_color_trans_done = lcd_event_callback }, NULL);

    return 0;
}


IRAM_ATTR int SDL_ESPIDF_UpdateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_Surface *surface = (SDL_Surface *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), ESPIDF_SURFACE, NULL);
    if (!surface) {
        return SDL_SetError("Couldn't find ESPIDF surface for window");
    }

    int chunk_height = 40;  // Process 40 lines at a time
    for (int y = 0; y < surface->h; y += chunk_height) {
        int height = (y + chunk_height > surface->h) ? (surface->h - y) : chunk_height;

        // Convert to RGB565
        for (int i = 0; i < 320 * height; i++) {
            uint32_t rgba = ((uint32_t *)surface->pixels)[y * 320 + i];
            uint8_t g = (rgba >> 16) & 0xFF;
            uint8_t r = (rgba >> 8) & 0xFF;
            uint8_t b = (rgba >> 0) & 0xFF;
            rgb565_buffer[i] = ((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3);
        }

        // Send the chunk and wait for completion
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, y, surface->w, y + height, rgb565_buffer));
        xSemaphoreTake(lcd_semaphore, portMAX_DELAY); // Wait for the current chunk to be fully sent
    }

    return 0;
}

void SDL_ESPIDF_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_ClearProperty(SDL_GetWindowProperties(window), ESPIDF_SURFACE);

    // Free the RGB565 buffer
    if (rgb565_buffer) {
        heap_caps_free(rgb565_buffer);
        rgb565_buffer = NULL;
    }

    // Delete the semaphore
    if (lcd_semaphore) {
        vSemaphoreDelete(lcd_semaphore);
        lcd_semaphore = NULL;
    }
}

#endif /* SDL_VIDEO_DRIVER_ESP_IDF */
