#ifndef GRID_H
#define GRID_H

#include <stdint.h>

typedef struct {
  int width;
  int height;
  int stride;
  uint8_t *cells;
  uint8_t *next;
  uint8_t *buffer_a;
  uint8_t *buffer_b;
} Grid;

Grid *grid_create(int width, int height);
void grid_destroy(Grid *grid);
void grid_randomize(Grid *grid, float density);
void grid_step(Grid *grid);

#endif
