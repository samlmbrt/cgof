#include "grid.h"

#include <SDL3/SDL.h>

#include <stdlib.h>

Grid *grid_create(int width, int height) {
  Grid *grid = calloc(1, sizeof(Grid));

  if (!grid) {
    return NULL;
  }

  grid->width = width;
  grid->height = height;
  grid->stride = width + 2;

  size_t padded_size = (size_t)grid->stride * (size_t)(height + 2);
  grid->buffer_a = calloc(padded_size, sizeof(uint8_t));
  grid->buffer_b = calloc(padded_size, sizeof(uint8_t));

  if (!grid->buffer_a || !grid->buffer_b) {
    free(grid->buffer_b);
    free(grid->buffer_a);
    free(grid);
    return NULL;
  }

  grid->cells = grid->buffer_a + grid->stride + 1;
  grid->next = grid->buffer_b + grid->stride + 1;

  return grid;
}

void grid_destroy(Grid *grid) {
  if (!grid) {
    return;
  }

  free(grid->buffer_b);
  free(grid->buffer_a);
  free(grid);
}

void grid_randomize(Grid *grid, float density) {
  for (int y = 0; y < grid->height; y++) {
    for (int x = 0; x < grid->width; x++) {
      grid->cells[y * grid->stride + x] = SDL_randf() < density ? 1 : 0;
    }
  }
}

void grid_step(Grid *grid) {
  int stride = grid->stride;

  for (int y = 0; y < grid->height; y++) {
    for (int x = 0; x < grid->width; x++) {
      int idx = y * stride + x;
      int neighbors = grid->cells[idx - stride - 1] +
                      grid->cells[idx - stride] +
                      grid->cells[idx - stride + 1] + grid->cells[idx - 1] +
                      grid->cells[idx + 1] + grid->cells[idx + stride - 1] +
                      grid->cells[idx + stride] + grid->cells[idx + stride + 1];

      if (grid->cells[idx]) {
        grid->next[idx] = (neighbors == 2 || neighbors == 3) ? 1 : 0;
      } else {
        grid->next[idx] = (neighbors == 3) ? 1 : 0;
      }
    }
  }

  uint8_t *tmp = grid->cells;
  grid->cells = grid->next;
  grid->next = tmp;
}
