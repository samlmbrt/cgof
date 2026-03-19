#ifndef GRID_H
#define GRID_H

#include <stdint.h>

typedef struct {
  int width;
  int height;
  int words_per_row;
  uint64_t last_word_mask;
  uint64_t *cells;
  uint64_t *next;
  uint64_t *buffer_a;
  uint64_t *buffer_b;
} Grid;

Grid *grid_create(int width, int height);
void grid_destroy(Grid *grid);
void grid_randomize(Grid *grid, float density);
void grid_step(Grid *grid);

#endif
