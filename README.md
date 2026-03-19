# cgof

Conway's Game of Life in C23 with SDL3.

## How It Works

Each cell is packed as a single bit in a `uint64_t` word, so 64 cells are processed per operation. The grid is allocated with a 1-cell dead border (one extra row above/below), eliminating bounds checks in the inner loop.

**Neighbor counting** uses bit shifts across word boundaries to produce 8 neighbor bitmaps (above-left through below-right). These are summed into a 4-bit per-cell count using a full-adder/half-adder chain — no loops or branching per cell.

**The Game of Life rule** (B3/S23) reduces to a single bitwise expression:

```
result = ~bit3 & ~bit2 & bit1 & (bit0 | cell)
```

**Rendering** maps the bit-packed grid to pixels via a 256-entry lookup table that converts 8 bits to 8 pixels at once, avoiding per-bit branching.

## Prerequisites

- CMake 3.28+
- C23 compiler (Clang or GCC)
- SDL3

### macOS

```sh
brew install cmake sdl3
```

### Ubuntu/Debian

```sh
apt install cmake gcc libsdl3-dev
```

### Arch Linux

```sh
pacman -S cmake gcc sdl3
```

## Build & Run

Configure both debug and release builds:

```sh
make configure
```

Build and run in debug mode (includes AddressSanitizer and UBSan):

```sh
make debug
```

Build and run in release mode (includes LTO and `-march=native`):

```sh
make release
```

## Other Commands

Format source code:

```sh
make format
```

Clean build artifacts:

```sh
make clean
```
