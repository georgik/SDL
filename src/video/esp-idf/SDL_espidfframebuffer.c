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

static const char *TAG = "SDL_espidfframebuffer";

#define ESPIDF_SURFACE "SDL.internal.window.surface"

static SemaphoreHandle_t lcd_semaphore;
static int max_chunk_height = 4;  // Configurable chunk height

#ifdef CONFIG_IDF_TARGET_ESP32P4
static bool lcd_event_callback(esp_lcd_panel_handle_t panel_io, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    xSemaphoreGive(lcd_semaphore);
    return false;
}
#else
static void lcd_event_callback(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event_data, void *user_ctx)
{
    xSemaphoreGive(lcd_semaphore);
}
#endif

void esp_idf_log_free_dma(void) {
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    ESP_LOGI(TAG, "Free DMA memory: %d bytes", free_dma);
}

int SDL_ESPIDF_CreateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, SDL_PixelFormat *format, void **pixels, int *pitch)
{
    SDL_Surface *surface;
    int w, h;

    SDL_GetWindowSizeInPixels(window, &w, &h);
    surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGB565);
    if (!surface) {
        return -1;
    }

    SDL_SetSurfaceProperty(SDL_GetWindowProperties(window), ESPIDF_SURFACE, surface);
    *format = SDL_PIXELFORMAT_RGB565;
    *pixels = surface->pixels;
    *pitch = surface->pitch;

    // Create a semaphore to synchronize LCD transactions
    lcd_semaphore = xSemaphoreCreateBinary();
    if (!lcd_semaphore) {
        SDL_DestroySurface(surface);
        return SDL_SetError("Failed to create semaphore");
    }

    // Register the callback
#ifdef CONFIG_IDF_TARGET_ESP32P4
    const esp_lcd_dpi_panel_event_callbacks_t callback = {
       .on_color_trans_done = lcd_event_callback,
   };
    esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &callback, NULL);
#else
    esp_lcd_panel_io_register_event_callbacks(panel_io_handle, &(esp_lcd_panel_io_callbacks_t){ .on_color_trans_done = lcd_event_callback }, NULL);
#endif

    return 0;
}

IRAM_ATTR int SDL_ESPIDF_UpdateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_Surface *surface = (SDL_Surface *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), ESPIDF_SURFACE, NULL);
    if (!surface) {
        return SDL_SetError("Couldn't find ESPIDF surface for window");
    }

    // Iterate over the framebuffer in chunks
    for (int y = 0; y < surface->h; y += max_chunk_height) {
        int height = (y + max_chunk_height > surface->h) ? (surface->h - y) : max_chunk_height;
        uint16_t *src_pixels = (uint16_t *)surface->pixels + (y * surface->w);

        // Directly send the chunk to the LCD from src_pixels (eliminating intermediate buffer)
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, y, surface->w, y + height, src_pixels));

        // Wait for the current chunk to finish transmission
        xSemaphoreTake(lcd_semaphore, portMAX_DELAY);
    }

    return 0;
}

void SDL_ESPIDF_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_ClearProperty(SDL_GetWindowProperties(window), ESPIDF_SURFACE);

    // Delete the semaphore
    if (lcd_semaphore) {
        vSemaphoreDelete(lcd_semaphore);
        lcd_semaphore = NULL;
    }
}

#endif /* SDL_VIDEO_DRIVER_ESP_IDF */
