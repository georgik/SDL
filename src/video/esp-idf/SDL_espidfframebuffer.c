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

static uint16_t *rgb_buffer = NULL;

static SemaphoreHandle_t lcd_semaphore;
static int max_chunk_height = 4;  // Configurable chunk height

#ifdef CONFIG_IDF_TARGET_ESP32P4
static bool lcd_event_callback(esp_lcd_panel_handle_t panel_io, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    // Give the semaphore to signal the completion of the transfer
    xSemaphoreGive(lcd_semaphore);
    return false;
}
#else
static void lcd_event_callback(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event_data, void *user_ctx)
{
    // Give the semaphore to signal the completion of the transfer
    xSemaphoreGive(lcd_semaphore);
}
#endif

void esp_idf_log_free_dma(void) {
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    ESP_LOGI(TAG, "Free DMA memory: %d bytes", free_dma);
}

void *allocate_rgb565_chunk_buffer(size_t width, size_t chunk_height) {

    esp_idf_log_free_dma();
    size_t buffer_size = width * chunk_height * sizeof(uint16_t);  // Smaller buffer based on chunk height

    // First attempt to allocate in IRAM (internal memory)
    uint16_t *rgb565_buffer = (uint16_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!rgb565_buffer) {
        // If IRAM allocation fails, attempt to allocate in PSRAM
        printf("Failed to allocate graphical chunk buffer in IRAM. Trying PSRAM...\n");
        rgb565_buffer = (uint16_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);

        if (!rgb565_buffer) {
            // If PSRAM allocation also fails, return an error
            printf("Failed to allocate memory for RGB565 chunk buffer in both IRAM and PSRAM\n");
            return NULL;
        } else {
            printf("Allocation of graphical chunk buffer in PSRAM succeeded.\n");
        }
    }

    // Allocation was successful
    printf("Framebuffer memory allocation successful for buffer of size %zu bytes\n", buffer_size);
    esp_idf_log_free_dma();

    return rgb565_buffer;
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

    // Allocate buffer only for the current chunk, not the entire framebuffer
    rgb_buffer = allocate_rgb565_chunk_buffer(w, max_chunk_height);
    if (!rgb_buffer) {
        SDL_DestroySurface(surface);
        return SDL_SetError("Failed to allocate memory for ESP-IDF frame buffer chunk");
    }

    // Create a semaphore to synchronize LCD transactions
    lcd_semaphore = xSemaphoreCreateBinary();
    if (!lcd_semaphore) {
        heap_caps_free(rgb_buffer);
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

    // Iterate over the chunks instead of the entire framebuffer
    for (int y = 0; y < surface->h; y += max_chunk_height) {
        int height = (y + max_chunk_height > surface->h) ? (surface->h - y) : max_chunk_height;

        // Convert only the chunk of pixels to RGB565
        for (int i = 0; i < surface->w * height; i++) {
            uint16_t rgba = ((uint16_t *)surface->pixels)[y * surface->w + i];

#ifdef CONFIG_IDF_TARGET_ESP32P4
            rgb_buffer[i] = rgba;
#else
            uint8_t g = (rgba >> 11) & 0xFF;
            uint8_t r = (rgba >> 5) & 0xFF;
            uint8_t b = (rgba >> 0) & 0xFF;
            rgb_buffer[i] = ((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3);
#endif
        }

        // Send the chunk to the LCD
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, y, surface->w, y + height, rgb_buffer));
        xSemaphoreTake(lcd_semaphore, portMAX_DELAY);  // Wait for the chunk to be sent
    }

    return 0;
}

void SDL_ESPIDF_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_ClearProperty(SDL_GetWindowProperties(window), ESPIDF_SURFACE);

    // Free the RGB565 buffer
    if (rgb_buffer) {
        heap_caps_free(rgb_buffer);
        rgb_buffer = NULL;
    }

    // Delete the semaphore
    if (lcd_semaphore) {
        vSemaphoreDelete(lcd_semaphore);
        lcd_semaphore = NULL;
    }
}

#endif /* SDL_VIDEO_DRIVER_ESP_IDF */
