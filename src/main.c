#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <string.h>

#include "grid.h"

static const uint32_t COLOR_ALIVE = 0x00FF00FF; /* RGBA: green */
static const uint32_t COLOR_DEAD = 0x000000FF;  /* RGBA: black */

/* pixel_lut[byte] = 8 pre-computed pixels for each possible 8-bit pattern */
static uint32_t pixel_lut[256][8];

static void init_pixel_lut(void) {
  for (int i = 0; i < 256; i++) {
    for (int b = 0; b < 8; b++) {
      pixel_lut[i][b] = (i & (1 << b)) ? COLOR_ALIVE : COLOR_DEAD;
    }
  }
}

static const int WINDOW_WIDTH = 1800;
static const int WINDOW_HEIGHT = 900;
static const float INITIAL_DENSITY = 0.5f;

static const Uint32 FPS_UPDATE_INTERVAL_MS = 500;

typedef struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  Grid *grid;
  Uint64 last_fps_time;
  int frame_count;
} AppState;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  init_pixel_lut();

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  AppState *state = SDL_calloc(1, sizeof(AppState));
  if (!state) {
    return SDL_APP_FAILURE;
  }

  state->window = SDL_CreateWindow("cgof", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
  if (!state->window) {
    SDL_Log("Failed to create window: %s", SDL_GetError());
    SDL_free(state);
    return SDL_APP_FAILURE;
  }

  state->renderer = SDL_CreateRenderer(state->window, NULL);
  if (!state->renderer) {
    SDL_Log("Failed to create renderer: %s", SDL_GetError());
    SDL_DestroyWindow(state->window);
    SDL_free(state);
    return SDL_APP_FAILURE;
  }

  state->texture = SDL_CreateTexture(state->renderer, SDL_PIXELFORMAT_RGBA8888,
                                     SDL_TEXTUREACCESS_STREAMING, WINDOW_WIDTH,
                                     WINDOW_HEIGHT);
  if (!state->texture) {
    SDL_Log("Failed to create texture: %s", SDL_GetError());
    SDL_DestroyRenderer(state->renderer);
    SDL_DestroyWindow(state->window);
    SDL_free(state);
    return SDL_APP_FAILURE;
  }

  state->grid = grid_create(WINDOW_WIDTH, WINDOW_HEIGHT);
  if (!state->grid) {
    SDL_Log("Failed to create grid");
    SDL_DestroyTexture(state->texture);
    SDL_DestroyRenderer(state->renderer);
    SDL_DestroyWindow(state->window);
    SDL_free(state);
    return SDL_APP_FAILURE;
  }

  grid_randomize(state->grid, INITIAL_DENSITY);

  *appstate = state;
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  (void)appstate;

  if (event->type == SDL_EVENT_QUIT) {
    return SDL_APP_SUCCESS;
  }

  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  AppState *state = appstate;

  state->frame_count++;
  Uint64 now = SDL_GetTicks();
  Uint64 elapsed = now - state->last_fps_time;

  if (elapsed >= FPS_UPDATE_INTERVAL_MS) {
    double fps = (double)state->frame_count / ((double)elapsed / 1000.0);
    char title[64];

    SDL_snprintf(title, sizeof(title), "cgof - %.0f FPS", fps);
    SDL_SetWindowTitle(state->window, title);

    state->frame_count = 0;
    state->last_fps_time = now;
  }

  grid_step(state->grid);

  uint8_t *pixels;
  int pitch;

  if (!SDL_LockTexture(state->texture, NULL, (void **)&pixels, &pitch)) {
    SDL_Log("Failed to lock texture: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  int width = state->grid->width;
  int height = state->grid->height;
  int wpr = state->grid->words_per_row;
  int full_words = width / 64;
  int tail_bits = width % 64;

  for (int y = 0; y < height; y++) {
    uint64_t *grid_row = state->grid->cells + y * wpr;
    uint32_t *pixel_row = (uint32_t *)(pixels + y * pitch);

    for (int w = 0; w < full_words; w++) {
      uint64_t word = grid_row[w];
      uint32_t *dst = pixel_row + w * 64;

      for (int i = 0; i < 8; i++) {
        memcpy(dst + i * 8, pixel_lut[(uint8_t)(word & 0xFF)],
               8 * sizeof(uint32_t));
        word >>= 8;
      }
    }

    if (tail_bits) {
      uint64_t word = grid_row[full_words];
      uint32_t *dst = pixel_row + full_words * 64;
      int remaining = width - full_words * 64;

      while (remaining > 0) {
        int n = remaining < 8 ? remaining : 8;
        memcpy(dst, pixel_lut[(uint8_t)(word & 0xFF)],
               (size_t)n * sizeof(uint32_t));
        dst += n;
        remaining -= n;
        word >>= 8;
      }
    }
  }

  SDL_UnlockTexture(state->texture);
  SDL_RenderTexture(state->renderer, state->texture, NULL, NULL);
  SDL_RenderPresent(state->renderer);

  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  (void)result;

  if (appstate) {
    AppState *state = appstate;
    grid_destroy(state->grid);
    SDL_DestroyTexture(state->texture);
    SDL_DestroyRenderer(state->renderer);
    SDL_DestroyWindow(state->window);
    SDL_free(state);
  }
}
