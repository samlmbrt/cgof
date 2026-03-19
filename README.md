# cgof

Conway's Game of Life in C23 with SDL3.

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
