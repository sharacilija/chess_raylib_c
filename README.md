# Simple Raylib Chess

A local two-player chess game written in C with raylib. All chess and interface
code is kept in `src/main.c` so that a beginner can follow the entire project
without jumping between many source files.

## Features

- Legal movement and captures for every piece
- Check and checkmate
- Castling
- En passant
- Promotion to queen, rook, bishop, or knight
- Stalemate
- Threefold repetition
- 50-move rule
- Insufficient-material draws
- Legal-move highlighting
- Undo and new-game buttons

The pieces are labeled circles. This keeps the project independent of image and
font files.

## Project structure

```text
raylib-chess/
|-- src/
|   `-- main.c
|-- build/
|-- .vscode/
|   |-- c_cpp_properties.json
|   |-- tasks.json
|   `-- launch.json
`-- README.md
```

## Requirements

This setup assumes the same Windows tools you already use:

- VS Code with the Microsoft C/C++ extension
- MSYS2 MinGW64 GCC at `C:\\msys64\\mingw64\\bin\\gcc.exe`
- GDB at `C:\\msys64\\mingw64\\bin\\gdb.exe`
- raylib installed for the MinGW64 environment

## Build and run in VS Code

1. Open the `raylib-chess` folder in VS Code. Open the folder itself, not only
   `main.c`.
2. Press `Ctrl+Shift+B` to compile.
3. Press `F5` to compile and run with the debugger.

The executable is created as `build/chess.exe`.

## Build manually

Run this from the project folder in the MSYS2 MinGW64 terminal:

```bash
gcc -std=c17 -Wall -Wextra -g src/main.c -o build/chess.exe \
    -lraylib -lopengl32 -lgdi32 -lwinmm
```

Then run:

```bash
./build/chess.exe
```

## Suggested reading order for `main.c`

1. Read the enums and structs under **Chess data**.
2. Read `SetupStartingPosition()`.
3. Read `GeneratePseudoLegalMoves()` to understand how pieces move.
4. Read `GenerateLegalMoves()` to see how moves that leave the king in check
   are removed.
5. Read `ApplyMoveToPosition()`.
6. Read the mouse-input functions.
7. Read the drawing functions and finally `main()`.
