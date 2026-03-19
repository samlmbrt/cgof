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
  grid->cells = calloc((size_t)width * (size_t)height, sizeof(uint8_t));
  grid->next = calloc((size_t)width * (size_t)height, sizeof(uint8_t));

  if (!grid->cells || !grid->next) {
    free(grid->next);
    free(grid->cells);
    free(grid);
    return NULL;
  }

  return grid;
}

void grid_destroy(Grid *grid) {
  if (!grid) {
    return;
  }

  free(grid->next);
  free(grid->cells);
  free(grid);
}

void grid_randomize(Grid *grid, float density) {
  for (int y = 0; y < grid->height; y++) {
    for (int x = 0; x < grid->width; x++) {
      grid->cells[y * grid->width + x] = SDL_randf() < density ? 1 : 0;
    }
  }
}

static int count_neighbors(const Grid *grid, int x, int y) {
  int count = 0;

  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dy == 0) {
        continue;
      }
      int nx = x + dx;
      int ny = y + dy;
      if (nx >= 0 && nx < grid->width && ny >= 0 && ny < grid->height) {
        count += grid->cells[ny * grid->width + nx];
      }
    }
  }

  return count;
}

void grid_step(Grid *grid) {
  for (int y = 0; y < grid->height; y++) {
    for (int x = 0; x < grid->width; x++) {
      int neighbors = count_neighbors(grid, x, y);
      int idx = y * grid->width + x;
      uint8_t alive = grid->cells[idx];

      if (alive) {
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
