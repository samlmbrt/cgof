#ifndef GRID_H
#define GRID_H

#include <stdint.h>

typedef struct Worker Worker;

typedef struct {
  int width;
  int height;
  int words_per_row;
  uint64_t last_word_mask;
  uint64_t *cells;
  uint64_t *next;
  uint64_t *buffer_a;
  uint64_t *buffer_b;
  uint8_t *dirty_cur;
  uint8_t *dirty_next;
  int num_workers;
  Worker *workers;
  int main_start_y;
  int main_end_y;
} Grid;

Grid *grid_create(int width, int height);
void grid_destroy(Grid *grid);
void grid_randomize(Grid *grid, float density);
void grid_step(Grid *grid);

#endif
