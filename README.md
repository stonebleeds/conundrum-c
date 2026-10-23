# conundrum-c

A fast C port of [conundrum](https://github.com/stonebleeds/conundrum), a byte-oriented modern mock of the Enigma machine.

**Original concept, design, wheel tables, and Python implementation by Matthew K. Jensen.**  
**C port written with assistance from Grok (xAI).**

## Goal

Produce **identical output** to the original Python version for the same command-line settings and input data, while running dramatically faster.

## Features

- Reads binary data from stdin and writes results to stdout
- Supports a variable number of 256-position wheels plus a 16-bit “big wheel”
- All diagnostic and progress messages go to stderr
- Safety check that aborts if the wheel positions fully cycle back to the starting state
- Human-readable byte counts in status messages
- Single-threaded, readable, and portable C (works on macOS, Linux, and Windows)

## Building

You will need the wheel and big-wheel tables (normally provided in `precon.c` / `precon.h`).

```bash
clang -O2 -o conundrum-c conundrum.c precon.c# conundrum-c

```

## Usage

./conundrum-c 0x89AB 0xCDEF 0x3030 < input.bin > output.bin

- Every argument except the last is a 16-bit hex value: high byte = wheel number (0x00–0xFF), low byte = starting position.
- The final argument is the starting position of the big wheel (0x0000–0xFFFF).

## License
GPL-2.0-only (same as the original project)

## Original Project
https://github.com/stonebleeds/conundrum

