#include "grid.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

#ifdef __ARM_NEON
#include <arm_neon.h>

static inline uint64x2_t neon_shift_left_1(uint64x2_t v, uint64_t prev) {
  uint64x2_t shl = vshlq_n_u64(v, 1);
  uint64x2_t carry = vshrq_n_u64(v, 63);
  uint64x2_t carry_in = vextq_u64(vdupq_n_u64(prev >> 63), carry, 1);
  return vorrq_u64(shl, carry_in);
}

static inline uint64x2_t neon_shift_right_1(uint64x2_t v, uint64_t next) {
  uint64x2_t shr = vshrq_n_u64(v, 1);
  uint64x2_t carry = vshlq_n_u64(v, 63);
  uint64x2_t carry_in = vextq_u64(carry, vdupq_n_u64(next << 63), 1);
  return vorrq_u64(shr, carry_in);
}
#endif

struct Worker {
  Grid *grid;
  int start_y;
  int end_y;
  SDL_Thread *thread;
  SDL_Semaphore *start;
  SDL_Semaphore *done;
  SDL_AtomicInt quit;
};

static inline void mark_dirty(uint8_t *dirty, int wpr, int height, int y,
                               int w) {
  for (int dy = -1; dy <= 1; dy++) {
    int ny = y + dy;
    if (ny < 0 || ny >= height) {
      continue;
    }
    for (int dw = -1; dw <= 1; dw++) {
      int nw = w + dw;
      if (nw < 0 || nw >= wpr) {
        continue;
      }
      dirty[ny * wpr + nw] = 1;
    }
  }
}

static void process_rows(const Grid *grid, int start_y, int end_y) {
  int wpr = grid->words_per_row;
  int height = grid->height;
  const uint8_t *dirty = grid->dirty_cur;
  uint8_t *dirty_next = grid->dirty_next;

  for (int y = start_y; y < end_y; y++) {
    uint64_t *above = grid->cells + (y - 1) * wpr;
    uint64_t *cur = grid->cells + y * wpr;
    uint64_t *below = grid->cells + (y + 1) * wpr;
    uint64_t *out = grid->next + y * wpr;
    const uint8_t *row_dirty = dirty + y * wpr;

    int w = 0;

#ifdef __ARM_NEON
    for (; w + 1 < wpr; w += 2) {
      if (!row_dirty[w] && !row_dirty[w + 1]) {
        out[w] = cur[w];
        out[w + 1] = cur[w + 1];
        continue;
      }

      uint64x2_t a = vld1q_u64(&above[w]);
      uint64x2_t c = vld1q_u64(&cur[w]);
      uint64x2_t b = vld1q_u64(&below[w]);

      uint64_t a_prev = w > 0 ? above[w - 1] : 0;
      uint64_t a_next = w + 2 < wpr ? above[w + 2] : 0;
      uint64_t c_prev = w > 0 ? cur[w - 1] : 0;
      uint64_t c_next = w + 2 < wpr ? cur[w + 2] : 0;
      uint64_t b_prev = w > 0 ? below[w - 1] : 0;
      uint64_t b_next = w + 2 < wpr ? below[w + 2] : 0;

      uint64x2_t n0 = neon_shift_left_1(a, a_prev);
      uint64x2_t n1 = a;
      uint64x2_t n2 = neon_shift_right_1(a, a_next);
      uint64x2_t n3 = neon_shift_left_1(c, c_prev);
      uint64x2_t n4 = neon_shift_right_1(c, c_next);
      uint64x2_t n5 = neon_shift_left_1(b, b_prev);
      uint64x2_t n6 = b;
      uint64x2_t n7 = neon_shift_right_1(b, b_next);

      uint64x2_t t1 = veorq_u64(n0, n1);
      uint64x2_t s1 = veorq_u64(t1, n2);
      uint64x2_t c1 = vorrq_u64(vandq_u64(n0, n1), vandq_u64(t1, n2));

      uint64x2_t t2 = veorq_u64(n3, n4);
      uint64x2_t s2 = veorq_u64(t2, n5);
      uint64x2_t c2 = vorrq_u64(vandq_u64(n3, n4), vandq_u64(t2, n5));

      uint64x2_t t3 = veorq_u64(s1, s2);
      uint64x2_t s3 = veorq_u64(t3, n6);
      uint64x2_t c3 = vorrq_u64(vandq_u64(s1, s2), vandq_u64(t3, n6));

      uint64x2_t bit0 = veorq_u64(s3, n7);
      uint64x2_t c4 = vandq_u64(s3, n7);

      uint64x2_t t4 = veorq_u64(c1, c2);
      uint64x2_t s4 = veorq_u64(t4, c3);
      uint64x2_t c5 = vorrq_u64(vandq_u64(c1, c2), vandq_u64(t4, c3));

      uint64x2_t bit1 = veorq_u64(s4, c4);
      uint64x2_t c6 = vandq_u64(s4, c4);

      uint64x2_t bit2 = veorq_u64(c5, c6);
      uint64x2_t bit3 = vandq_u64(c5, c6);

      uint64x2_t alive = vandq_u64(bit1, vorrq_u64(bit0, c));
      alive = vbicq_u64(alive, bit2);
      alive = vbicq_u64(alive, bit3);

      vst1q_u64(&out[w], alive);

      uint64x2_t changed = veorq_u64(alive, c);
      if (vgetq_lane_u64(changed, 0)) {
        mark_dirty(dirty_next, wpr, height, y, w);
      }
      if (vgetq_lane_u64(changed, 1)) {
        mark_dirty(dirty_next, wpr, height, y, w + 1);
      }
    }
#endif

    for (; w < wpr; w++) {
      if (!row_dirty[w]) {
        out[w] = cur[w];
        continue;
      }

      uint64_t a = above[w];
      uint64_t c = cur[w];
      uint64_t b = below[w];

      uint64_t a_prev = w > 0 ? above[w - 1] : 0;
      uint64_t a_next = w < wpr - 1 ? above[w + 1] : 0;
      uint64_t c_prev = w > 0 ? cur[w - 1] : 0;
      uint64_t c_next = w < wpr - 1 ? cur[w + 1] : 0;
      uint64_t b_prev = w > 0 ? below[w - 1] : 0;
      uint64_t b_next = w < wpr - 1 ? below[w + 1] : 0;

      uint64_t n0 = (a << 1) | (a_prev >> 63);
      uint64_t n1 = a;
      uint64_t n2 = (a >> 1) | (a_next << 63);
      uint64_t n3 = (c << 1) | (c_prev >> 63);
      uint64_t n4 = (c >> 1) | (c_next << 63);
      uint64_t n5 = (b << 1) | (b_prev >> 63);
      uint64_t n6 = b;
      uint64_t n7 = (b >> 1) | (b_next << 63);

      uint64_t t1 = n0 ^ n1;
      uint64_t s1 = t1 ^ n2;
      uint64_t c1 = (n0 & n1) | (t1 & n2);

      uint64_t t2 = n3 ^ n4;
      uint64_t s2 = t2 ^ n5;
      uint64_t c2 = (n3 & n4) | (t2 & n5);

      uint64_t t3 = s1 ^ s2;
      uint64_t s3 = t3 ^ n6;
      uint64_t c3 = (s1 & s2) | (t3 & n6);

      uint64_t bit0 = s3 ^ n7;
      uint64_t c4 = s3 & n7;

      uint64_t t4 = c1 ^ c2;
      uint64_t s4 = t4 ^ c3;
      uint64_t c5 = (c1 & c2) | (t4 & c3);

      uint64_t bit1 = s4 ^ c4;
      uint64_t c6 = s4 & c4;

      uint64_t bit2 = c5 ^ c6;
      uint64_t bit3 = c5 & c6;

      out[w] = ~bit3 & ~bit2 & bit1 & (bit0 | c);

      if (out[w] != c) {
        mark_dirty(dirty_next, wpr, height, y, w);
      }
    }

    out[wpr - 1] &= grid->last_word_mask;
  }
}

static int worker_func(void *data) {
  Worker *worker = data;

  while (1) {
    SDL_WaitSemaphore(worker->start);

    if (SDL_GetAtomicInt(&worker->quit)) {
      break;
    }

    process_rows(worker->grid, worker->start_y, worker->end_y);
    SDL_SignalSemaphore(worker->done);
  }

  return 0;
}

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

  size_t dirty_size = (size_t)grid->words_per_row * (size_t)height;
  grid->dirty_cur = malloc(dirty_size);
  grid->dirty_next = calloc(dirty_size, 1);

  if (!grid->buffer_a || !grid->buffer_b || !grid->dirty_cur ||
      !grid->dirty_next) {
    free(grid->dirty_next);
    free(grid->dirty_cur);
    free(grid->buffer_b);
    free(grid->buffer_a);
    free(grid);
    return NULL;
  }

  memset(grid->dirty_cur, 1, dirty_size);

  grid->cells = grid->buffer_a + grid->words_per_row;
  grid->next = grid->buffer_b + grid->words_per_row;

  /* Set up thread pool */
  int num_threads = SDL_GetNumLogicalCPUCores();
  if (num_threads < 1) {
    num_threads = 1;
  }
  if (num_threads > height) {
    num_threads = height;
  }

  grid->num_workers = num_threads - 1;
  grid->workers = NULL;

  if (grid->num_workers > 0) {
    grid->workers = calloc((size_t)grid->num_workers, sizeof(Worker));
  }

  int rows_per_thread = height / num_threads;
  int extra_rows = height % num_threads;
  int y = 0;

  for (int i = 0; i < grid->num_workers; i++) {
    int rows = rows_per_thread + (i < extra_rows ? 1 : 0);
    grid->workers[i].grid = grid;
    grid->workers[i].start_y = y;
    grid->workers[i].end_y = y + rows;
    SDL_SetAtomicInt(&grid->workers[i].quit, 0);
    grid->workers[i].start = SDL_CreateSemaphore(0);
    grid->workers[i].done = SDL_CreateSemaphore(0);

    if (!grid->workers[i].start || !grid->workers[i].done) {
      for (int j = 0; j <= i; j++) {
        SDL_DestroySemaphore(grid->workers[j].start);
        SDL_DestroySemaphore(grid->workers[j].done);
      }
      free(grid->workers);
      grid->workers = NULL;
      grid->num_workers = 0;
      break;
    }

    char name[32];
    SDL_snprintf(name, sizeof(name), "worker_%d", i);
    grid->workers[i].thread =
        SDL_CreateThread(worker_func, name, &grid->workers[i]);

    if (!grid->workers[i].thread) {
      SDL_DestroySemaphore(grid->workers[i].start);
      SDL_DestroySemaphore(grid->workers[i].done);
      for (int j = 0; j < i; j++) {
        SDL_SetAtomicInt(&grid->workers[j].quit, 1);
        SDL_SignalSemaphore(grid->workers[j].start);
        SDL_WaitThread(grid->workers[j].thread, NULL);
        SDL_DestroySemaphore(grid->workers[j].start);
        SDL_DestroySemaphore(grid->workers[j].done);
      }
      free(grid->workers);
      grid->workers = NULL;
      grid->num_workers = 0;
      break;
    }

    y += rows;
  }

  grid->main_start_y = y;
  grid->main_end_y = height;

  return grid;
}

void grid_destroy(Grid *grid) {
  if (!grid) {
    return;
  }

  for (int i = 0; i < grid->num_workers; i++) {
    SDL_SetAtomicInt(&grid->workers[i].quit, 1);
    SDL_SignalSemaphore(grid->workers[i].start);
    SDL_WaitThread(grid->workers[i].thread, NULL);
    SDL_DestroySemaphore(grid->workers[i].start);
    SDL_DestroySemaphore(grid->workers[i].done);
  }
  free(grid->workers);

  free(grid->dirty_next);
  free(grid->dirty_cur);
  free(grid->buffer_b);
  free(grid->buffer_a);
  free(grid);
}

void grid_randomize(Grid *grid, float density) {
  size_t buf_size = (size_t)grid->words_per_row * (size_t)(grid->height + 2);
  memset(grid->buffer_a, 0, buf_size * sizeof(uint64_t));

  size_t dirty_size = (size_t)grid->words_per_row * (size_t)grid->height;
  memset(grid->dirty_cur, 1, dirty_size);
  memset(grid->dirty_next, 0, dirty_size);

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
  size_t dirty_size = (size_t)grid->words_per_row * (size_t)grid->height;
  memset(grid->dirty_next, 0, dirty_size);

  for (int i = 0; i < grid->num_workers; i++) {
    SDL_SignalSemaphore(grid->workers[i].start);
  }

  process_rows(grid, grid->main_start_y, grid->main_end_y);

  for (int i = 0; i < grid->num_workers; i++) {
    SDL_WaitSemaphore(grid->workers[i].done);
  }

  uint64_t *tmp = grid->cells;
  grid->cells = grid->next;
  grid->next = tmp;

  uint8_t *dtmp = grid->dirty_cur;
  grid->dirty_cur = grid->dirty_next;
  grid->dirty_next = dtmp;
}
