#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdlib.h>

#include "grid.h"

/* Window and simulation */
static const int WINDOW_WIDTH = 1600;
static const int WINDOW_HEIGHT = 900;
static const float INITIAL_DENSITY = 0.5f;
static const Uint32 FPS_UPDATE_INTERVAL_MS = 500;

/* Fade: how quickly dead cells dim (higher = faster fade) */
static const int FADE_DECAY = 5;

/* Still life dimming: cells alive longer than this fade to a dim level */
static const uint16_t AGE_DIM_THRESHOLD = 60;
static const uint8_t AGE_DIM_BRIGHTNESS = 40;

/* Maps fade value (0-255) to an RGBA pixel color via heatmap gradient. */
static uint32_t color_lut[256];

typedef struct {
  float pos;
  float r, g, b;
} ColorStop;

static void init_color_lut(void) {
  static const ColorStop stops[] = {
      {0.00f, 0.04f, 0.04f, 0.10f}, /* dark background */
      {0.10f, 0.05f, 0.05f, 0.30f}, /* dark blue       */
      {0.25f, 0.00f, 0.20f, 0.70f}, /* blue             */
      {0.40f, 0.00f, 0.60f, 0.50f}, /* teal             */
      {0.55f, 0.20f, 0.80f, 0.00f}, /* green            */
      {0.70f, 0.90f, 0.80f, 0.00f}, /* yellow           */
      {0.85f, 1.00f, 0.40f, 0.00f}, /* orange           */
      {0.95f, 1.00f, 0.10f, 0.00f}, /* red              */
      {1.00f, 1.00f, 1.00f, 0.90f}, /* near white       */
  };
  int num_stops = sizeof(stops) / sizeof(stops[0]);

  for (int i = 0; i < 256; i++) {
    float t = (float)i / 255.0f;

    /* Find the two stops surrounding t */
    int s = 0;
    while (s < num_stops - 2 && stops[s + 1].pos < t) {
      s++;
    }

    float range = stops[s + 1].pos - stops[s].pos;
    float frac = (range > 0.0f) ? (t - stops[s].pos) / range : 0.0f;

    float r = stops[s].r + frac * (stops[s + 1].r - stops[s].r);
    float g = stops[s].g + frac * (stops[s + 1].g - stops[s].g);
    float b = stops[s].b + frac * (stops[s + 1].b - stops[s].b);

    uint8_t ri = (uint8_t)(r * 255.0f);
    uint8_t gi = (uint8_t)(g * 255.0f);
    uint8_t bi = (uint8_t)(b * 255.0f);
    color_lut[i] = ((uint32_t)ri << 24) | ((uint32_t)gi << 16) |
                   ((uint32_t)bi << 8) | 0xFF;
  }
}

typedef struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  Grid *grid;
  uint8_t *fade;
  uint16_t *age;
  Uint64 last_fps_time;
  int frame_count;
} AppState;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  init_color_lut();

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

  state->fade = calloc((size_t)WINDOW_WIDTH * (size_t)WINDOW_HEIGHT,
                        sizeof(uint8_t));
  if (!state->fade) {
    SDL_Log("Failed to allocate fade buffer");
    grid_destroy(state->grid);
    SDL_DestroyTexture(state->texture);
    SDL_DestroyRenderer(state->renderer);
    SDL_DestroyWindow(state->window);
    SDL_free(state);
    return SDL_APP_FAILURE;
  }

  state->age = calloc((size_t)WINDOW_WIDTH * (size_t)WINDOW_HEIGHT,
                       sizeof(uint16_t));
  if (!state->age) {
    SDL_Log("Failed to allocate age buffer");
    free(state->fade);
    grid_destroy(state->grid);
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

  /* FPS counter */
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

  /* Simulation */
  grid_step(state->grid);

  /* Update fade and age buffers */
  int width = state->grid->width;
  int height = state->grid->height;
  int wpr = state->grid->words_per_row;

  for (int y = 0; y < height; y++) {
    uint64_t *grid_row = state->grid->cells + y * wpr;
    uint8_t *fade_row = state->fade + y * width;
    uint16_t *age_row = state->age + y * width;

    for (int w = 0; w < wpr; w++) {
      uint64_t word = grid_row[w];
      int base_x = w * 64;
      int count = 64;

      if (base_x + 64 > width) {
        count = width - base_x;
      }

      for (int b = 0; b < count; b++) {
        int x = base_x + b;

        if (word & 1) {
          if (age_row[x] < UINT16_MAX) {
            age_row[x]++;
          }
          if (age_row[x] > AGE_DIM_THRESHOLD) {
            fade_row[x] = AGE_DIM_BRIGHTNESS;
          } else {
            fade_row[x] = 255;
          }
        } else {
          age_row[x] = 0;
          if (fade_row[x] > 0) {
            int val = fade_row[x] - FADE_DECAY;
            fade_row[x] = val > 0 ? (uint8_t)val : 0;
          }
        }

        word >>= 1;
      }
    }
  }

  /* Rendering */
  uint8_t *pixels;
  int pitch;

  if (!SDL_LockTexture(state->texture, NULL, (void **)&pixels, &pitch)) {
    SDL_Log("Failed to lock texture: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  for (int y = 0; y < height; y++) {
    uint8_t *fade_row = state->fade + y * width;
    uint32_t *pixel_row = (uint32_t *)(pixels + y * pitch);

    for (int x = 0; x < width; x++) {
      pixel_row[x] = color_lut[fade_row[x]];
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
    free(state->age);
    free(state->fade);
    grid_destroy(state->grid);
    SDL_DestroyTexture(state->texture);
    SDL_DestroyRenderer(state->renderer);
    SDL_DestroyWindow(state->window);
    SDL_free(state);
  }
}
