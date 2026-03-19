#include "grid.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

Grid *grid_create(int width, int height) {
  Grid *grid = calloc(1, sizeof(Grid));

  if (!grid) {
    return NULL;
  }

  grid->width = width;
  grid->height = height;
  grid->words_per_row = (width + 63) / 64;

  if (width % 64 == 0) {
    grid->last_word_mask = ~(uint64_t)0;
  } else {
    grid->last_word_mask = ((uint64_t)1 << (width % 64)) - 1;
  }

  size_t buf_size = (size_t)grid->words_per_row * (size_t)(height + 2);
  grid->buffer_a = calloc(buf_size, sizeof(uint64_t));
  grid->buffer_b = calloc(buf_size, sizeof(uint64_t));

  if (!grid->buffer_a || !grid->buffer_b) {
    free(grid->buffer_b);
    free(grid->buffer_a);
    free(grid);
    return NULL;
  }

  grid->cells = grid->buffer_a + grid->words_per_row;
  grid->next = grid->buffer_b + grid->words_per_row;

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
  size_t buf_size = (size_t)grid->words_per_row * (size_t)(grid->height + 2);
  memset(grid->buffer_a, 0, buf_size * sizeof(uint64_t));

  for (int y = 0; y < grid->height; y++) {
    for (int x = 0; x < grid->width; x++) {
      if (SDL_randf() < density) {
        grid->cells[y * grid->words_per_row + x / 64] |=
            (uint64_t)1 << (x % 64);
      }
    }
  }
}

void grid_step(Grid *grid) {
  int wpr = grid->words_per_row;

  for (int y = 0; y < grid->height; y++) {
    uint64_t *above = grid->cells + (y - 1) * wpr;
    uint64_t *cur = grid->cells + y * wpr;
    uint64_t *below = grid->cells + (y + 1) * wpr;
    uint64_t *out = grid->next + y * wpr;

    for (int w = 0; w < wpr; w++) {
      uint64_t a = above[w];
      uint64_t c = cur[w];
      uint64_t b = below[w];

      uint64_t a_prev = w > 0 ? above[w - 1] : 0;
      uint64_t a_next = w < wpr - 1 ? above[w + 1] : 0;
      uint64_t c_prev = w > 0 ? cur[w - 1] : 0;
      uint64_t c_next = w < wpr - 1 ? cur[w + 1] : 0;
      uint64_t b_prev = w > 0 ? below[w - 1] : 0;
      uint64_t b_next = w < wpr - 1 ? below[w + 1] : 0;

      /* 8 neighbor bitmaps via shifts */
      uint64_t n0 = (a << 1) | (a_prev >> 63); /* above-left  */
      uint64_t n1 = a;                          /* above       */
      uint64_t n2 = (a >> 1) | (a_next << 63); /* above-right */
      uint64_t n3 = (c << 1) | (c_prev >> 63); /* left        */
      uint64_t n4 = (c >> 1) | (c_next << 63); /* right       */
      uint64_t n5 = (b << 1) | (b_prev >> 63); /* below-left  */
      uint64_t n6 = b;                          /* below       */
      uint64_t n7 = (b >> 1) | (b_next << 63); /* below-right */

      /*
       * Sum 8 neighbor bits into a 4-bit count (bit3..bit0) using
       * a full-adder / half-adder chain. Each full adder takes 3
       * bit-vectors and produces (carry, sum).
       */

      /* FA1(n0, n1, n2) */
      uint64_t t1 = n0 ^ n1;
      uint64_t s1 = t1 ^ n2;
      uint64_t c1 = (n0 & n1) | (t1 & n2);

      /* FA2(n3, n4, n5) */
      uint64_t t2 = n3 ^ n4;
      uint64_t s2 = t2 ^ n5;
      uint64_t c2 = (n3 & n4) | (t2 & n5);

      /* FA3(s1, s2, n6) — sum the ones-bits */
      uint64_t t3 = s1 ^ s2;
      uint64_t s3 = t3 ^ n6;
      uint64_t c3 = (s1 & s2) | (t3 & n6);

      /* HA1(s3, n7) — final ones-bit */
      uint64_t bit0 = s3 ^ n7;
      uint64_t c4 = s3 & n7;

      /* FA4(c1, c2, c3) — sum the twos-bits */
      uint64_t t4 = c1 ^ c2;
      uint64_t s4 = t4 ^ c3;
      uint64_t c5 = (c1 & c2) | (t4 & c3);

      /* HA2(s4, c4) — final twos-bit */
      uint64_t bit1 = s4 ^ c4;
      uint64_t c6 = s4 & c4;

      /* fours-bits */
      uint64_t bit2 = c5 ^ c6;
      uint64_t bit3 = c5 & c6;

      /*
       * Game of Life rule:
       *   alive if (count == 3) || (count == 2 && cell is alive)
       *   count == 2: bit3=0 bit2=0 bit1=1 bit0=0
       *   count == 3: bit3=0 bit2=0 bit1=1 bit0=1
       *   result = ~bit3 & ~bit2 & bit1 & (bit0 | cell)
       */
      out[w] = ~bit3 & ~bit2 & bit1 & (bit0 | c);
    }

    /* Mask off bits beyond grid width in the last word */
    out[wpr - 1] &= grid->last_word_mask;
  }

  uint64_t *tmp = grid->cells;
  grid->cells = grid->next;
  grid->next = tmp;
}
