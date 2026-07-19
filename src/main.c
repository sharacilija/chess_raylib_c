#include "raylib.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define BOARD_ROWS 8
#define BOARD_COLUMNS 8
#define MAX_MOVES 256
#define MAX_HISTORY 1024

#define SCREEN_WIDTH 1040
#define SCREEN_HEIGHT 760
#define BOARD_X 40
#define BOARD_Y 60
#define SQUARE_SIZE 80
#define BOARD_SIZE (SQUARE_SIZE * 8)

// -----------------------------------------------------------------------------
// Chess data
// -----------------------------------------------------------------------------

typedef enum {
    PIECE_NO_COLOR = -1,
    PIECE_WHITE,
    PIECE_BLACK
} PieceColor;

typedef enum {
    EMPTY,
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING
} PieceType;

typedef struct {
    PieceType type;
    PieceColor color;
} Piece;

typedef struct {
    int fromRow;
    int fromColumn;
    int toRow;
    int toColumn;
    PieceType promotion;
    bool enPassant;
    bool castling;
} Move;

typedef struct {
    Piece board[BOARD_ROWS][BOARD_COLUMNS];
    PieceColor turn;

    bool PIECE_WHITECastleKingSide;
    bool PIECE_WHITECastleQueenSide;
    bool PIECE_BLACKCastleKingSide;
    bool PIECE_BLACKCastleQueenSide;

    // This is the empty square behind a pawn that just moved two squares.
    // It is {-1, -1} when en passant is not available.
    int enPassantRow;
    int enPassantColumn;

    // One full move without a pawn move or capture adds 2 to this value.
    // A draw is reached when it becomes 100 half-moves.
    int halfmoveClock;
} Position;

typedef enum {
    GAME_PLAYING,
    PIECE_WHITE_WINS,
    PIECE_BLACK_WINS,
    DRAW_STALEMATE,
    DRAW_REPETITION,
    DRAW_FIFTY_MOVE,
    DRAW_INSUFFICIENT_MATERIAL
} GameResult;

typedef struct {
    Position position;

    // Saving the whole position before each move makes Undo very simple.
    Position history[MAX_HISTORY];
    Move moveHistory[MAX_HISTORY];

    // Each string describes one position and is used for threefold repetition.
    char positionKeys[MAX_HISTORY + 1][80];

    int moveCount;
    GameResult result;
} Game;

typedef struct {
    int selectedRow;
    int selectedColumn;
    Move legalMoves[MAX_MOVES];
    int legalMoveCount;

    bool choosingPromotion;
    Move pendingPromotionMove;
} InterfaceState;

// -----------------------------------------------------------------------------
// Function declarations
// -----------------------------------------------------------------------------

static Piece EmptyPiece(void);
static PieceColor OtherColor(PieceColor color);
static bool IsInsideBoard(int row, int column);

static void ResetGame(Game *game);
static void ClearSelection(InterfaceState *interfaceState);
static void CreatePositionKey(const Position *position, char key[80]);

static bool IsSquareAttacked(const Position *position, int row, int column,
                             PieceColor attacker);
static bool IsInCheck(const Position *position, PieceColor color);
static int GeneratePseudoLegalMoves(const Position *position, int row, int column,
                                    Move moves[MAX_MOVES]);
static int GenerateLegalMoves(const Position *position, int row, int column,
                              Move moves[MAX_MOVES]);
static bool HasAnyLegalMove(const Position *position);
static void ApplyMoveToPosition(Position *position, Move move);

static bool IsInsufficientMaterial(const Position *position);
static int CountCurrentPosition(const Game *game);
static void UpdateGameResult(Game *game);
static bool MakeGameMove(Game *game, Move move);
static void UndoMove(Game *game);

static void SelectSquare(const Game *game, InterfaceState *interfaceState,
                         int row, int column);
static void HandleBoardClick(Game *game, InterfaceState *interfaceState,
                             Vector2 mouse);
static void HandlePromotionClick(Game *game, InterfaceState *interfaceState,
                                 Vector2 mouse);

static Rectangle UndoButtonRectangle(void);
static Rectangle NewGameButtonRectangle(void);
static Rectangle PromotionRectangle(int index);
static bool IsMoveDestination(const InterfaceState *interfaceState,
                              int row, int column, Move *foundMove);

static char PieceLetter(PieceType type);
static void DrawPiece(Piece piece, Rectangle square);
static void DrawBoard(const Game *game, const InterfaceState *interfaceState);
static void DrawSidePanel(const Game *game, Vector2 mouse);
static void DrawPromotionWindow(const Game *game,
                                const InterfaceState *interfaceState,
                                Vector2 mouse);

// -----------------------------------------------------------------------------
// Small helper functions
// -----------------------------------------------------------------------------

static Piece EmptyPiece(void)
{
    Piece piece = { EMPTY, PIECE_NO_COLOR };
    return piece;
}

static PieceColor OtherColor(PieceColor color)
{
    return color == PIECE_WHITE ? PIECE_BLACK : PIECE_WHITE;
}

static bool IsInsideBoard(int row, int column)
{
    return row >= 0 && row < BOARD_ROWS &&
           column >= 0 && column < BOARD_COLUMNS;
}

static void AddMove(Move moves[MAX_MOVES], int *moveCount,
                    int fromRow, int fromColumn, int toRow, int toColumn,
                    bool enPassant, bool castling, PieceType promotion)
{
    if (*moveCount >= MAX_MOVES) {
        return;
    }

    moves[*moveCount] = (Move) {
        fromRow,
        fromColumn,
        toRow,
        toColumn,
        promotion,
        enPassant,
        castling
    };

    (*moveCount)++;
}

static void ClearSelection(InterfaceState *interfaceState)
{
    interfaceState->selectedRow = -1;
    interfaceState->selectedColumn = -1;
    interfaceState->legalMoveCount = 0;
}

// -----------------------------------------------------------------------------
// Starting a game and storing positions
// -----------------------------------------------------------------------------

static void SetupStartingPosition(Position *position)
{
    PieceType backRank[8] = {
        ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK
    };

    for (int row = 0; row < 8; row++) {
        for (int column = 0; column < 8; column++) {
            position->board[row][column] = EmptyPiece();
        }
    }

    for (int column = 0; column < 8; column++) {
        position->board[0][column] = (Piece) { backRank[column], PIECE_BLACK };
        position->board[1][column] = (Piece) { PAWN, PIECE_BLACK };
        position->board[6][column] = (Piece) { PAWN, PIECE_WHITE };
        position->board[7][column] = (Piece) { backRank[column], PIECE_WHITE };
    }

    position->turn = PIECE_WHITE;
    position->PIECE_WHITECastleKingSide = true;
    position->PIECE_WHITECastleQueenSide = true;
    position->PIECE_BLACKCastleKingSide = true;
    position->PIECE_BLACKCastleQueenSide = true;
    position->enPassantRow = -1;
    position->enPassantColumn = -1;
    position->halfmoveClock = 0;
}

static char PieceKeyCharacter(Piece piece)
{
    char character = '.';

    switch (piece.type) {
        case PAWN:   character = 'p'; break;
        case KNIGHT: character = 'n'; break;
        case BISHOP: character = 'b'; break;
        case ROOK:   character = 'r'; break;
        case QUEEN:  character = 'q'; break;
        case KING:   character = 'k'; break;
        case EMPTY:  return '.';
    }

    if (piece.color == PIECE_WHITE) {
        character = (char) (character - 'a' + 'A');
    }

    return character;
}

static void CreatePositionKey(const Position *position, char key[80])
{
    int index = 0;

    for (int row = 0; row < 8; row++) {
        for (int column = 0; column < 8; column++) {
            key[index++] = PieceKeyCharacter(position->board[row][column]);
        }
    }

    key[index++] = position->turn == PIECE_WHITE ? 'w' : 'b';
    key[index++] = position->PIECE_WHITECastleKingSide ? '1' : '0';
    key[index++] = position->PIECE_WHITECastleQueenSide ? '1' : '0';
    key[index++] = position->PIECE_BLACKCastleKingSide ? '1' : '0';
    key[index++] = position->PIECE_BLACKCastleQueenSide ? '1' : '0';
    key[index++] = position->enPassantRow == -1
        ? '-'
        : (char) ('0' + position->enPassantRow);
    key[index++] = position->enPassantColumn == -1
        ? '-'
        : (char) ('0' + position->enPassantColumn);
    key[index] = '\0';
}

static void ResetGame(Game *game)
{
    SetupStartingPosition(&game->position);
    game->moveCount = 0;
    game->result = GAME_PLAYING;
    CreatePositionKey(&game->position, game->positionKeys[0]);
}

// -----------------------------------------------------------------------------
// Check detection
// -----------------------------------------------------------------------------

static bool IsSquareAttacked(const Position *position, int row, int column,
                             PieceColor attacker)
{
    // Pawn attacks
    int pawnRow = row + (attacker == PIECE_WHITE ? 1 : -1);

    for (int columnOffset = -1; columnOffset <= 1; columnOffset += 2) {
        int pawnColumn = column + columnOffset;

        if (IsInsideBoard(pawnRow, pawnColumn)) {
            Piece piece = position->board[pawnRow][pawnColumn];

            if (piece.color == attacker && piece.type == PAWN) {
                return true;
            }
        }
    }

    // Knight attacks
    const int knightOffsets[8][2] = {
        {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
        { 1, -2}, { 1, 2}, { 2, -1}, { 2, 1}
    };

    for (int i = 0; i < 8; i++) {
        int knightRow = row + knightOffsets[i][0];
        int knightColumn = column + knightOffsets[i][1];

        if (IsInsideBoard(knightRow, knightColumn)) {
            Piece piece = position->board[knightRow][knightColumn];

            if (piece.color == attacker && piece.type == KNIGHT) {
                return true;
            }
        }
    }

    // King attacks
    for (int rowOffset = -1; rowOffset <= 1; rowOffset++) {
        for (int columnOffset = -1; columnOffset <= 1; columnOffset++) {
            if (rowOffset == 0 && columnOffset == 0) {
                continue;
            }

            int kingRow = row + rowOffset;
            int kingColumn = column + columnOffset;

            if (IsInsideBoard(kingRow, kingColumn)) {
                Piece piece = position->board[kingRow][kingColumn];

                if (piece.color == attacker && piece.type == KING) {
                    return true;
                }
            }
        }
    }

    // Rook and queen attacks along straight lines
    const int straightDirections[4][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}
    };

    for (int direction = 0; direction < 4; direction++) {
        int checkRow = row + straightDirections[direction][0];
        int checkColumn = column + straightDirections[direction][1];

        while (IsInsideBoard(checkRow, checkColumn)) {
            Piece piece = position->board[checkRow][checkColumn];

            if (piece.type != EMPTY) {
                if (piece.color == attacker &&
                    (piece.type == ROOK || piece.type == QUEEN)) {
                    return true;
                }
                break;
            }

            checkRow += straightDirections[direction][0];
            checkColumn += straightDirections[direction][1];
        }
    }

    // Bishop and queen attacks along diagonal lines
    const int diagonalDirections[4][2] = {
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1}
    };

    for (int direction = 0; direction < 4; direction++) {
        int checkRow = row + diagonalDirections[direction][0];
        int checkColumn = column + diagonalDirections[direction][1];

        while (IsInsideBoard(checkRow, checkColumn)) {
            Piece piece = position->board[checkRow][checkColumn];

            if (piece.type != EMPTY) {
                if (piece.color == attacker &&
                    (piece.type == BISHOP || piece.type == QUEEN)) {
                    return true;
                }
                break;
            }

            checkRow += diagonalDirections[direction][0];
            checkColumn += diagonalDirections[direction][1];
        }
    }

    return false;
}

static bool FindKing(const Position *position, PieceColor color,
                     int *kingRow, int *kingColumn)
{
    for (int row = 0; row < 8; row++) {
        for (int column = 0; column < 8; column++) {
            Piece piece = position->board[row][column];

            if (piece.color == color && piece.type == KING) {
                *kingRow = row;
                *kingColumn = column;
                return true;
            }
        }
    }

    return false;
}

static bool IsInCheck(const Position *position, PieceColor color)
{
    int kingRow;
    int kingColumn;

    if (!FindKing(position, color, &kingRow, &kingColumn)) {
        return false;
    }

    return IsSquareAttacked(position, kingRow, kingColumn, OtherColor(color));
}

// -----------------------------------------------------------------------------
// Move generation
// -----------------------------------------------------------------------------

static void AddSlidingMoves(const Position *position, int row, int column,
                            const int directions[][2], int directionCount,
                            Move moves[MAX_MOVES], int *moveCount)
{
    Piece movingPiece = position->board[row][column];

    for (int direction = 0; direction < directionCount; direction++) {
        int toRow = row + directions[direction][0];
        int toColumn = column + directions[direction][1];

        while (IsInsideBoard(toRow, toColumn)) {
            Piece target = position->board[toRow][toColumn];

            if (target.type == EMPTY) {
                AddMove(moves, moveCount, row, column, toRow, toColumn,
                        false, false, EMPTY);
            } else {
                if (target.color != movingPiece.color) {
                    AddMove(moves, moveCount, row, column, toRow, toColumn,
                            false, false, EMPTY);
                }
                break;
            }

            toRow += directions[direction][0];
            toColumn += directions[direction][1];
        }
    }
}

static void AddCastlingMoves(const Position *position, int row, int column,
                             Move moves[MAX_MOVES], int *moveCount)
{
    Piece king = position->board[row][column];
    int homeRow = king.color == PIECE_WHITE ? 7 : 0;
    PieceColor enemy = OtherColor(king.color);

    if (row != homeRow || column != 4 || IsInCheck(position, king.color)) {
        return;
    }

    bool mayCastleKingSide = king.color == PIECE_WHITE
        ? position->PIECE_WHITECastleKingSide
        : position->PIECE_BLACKCastleKingSide;

    bool mayCastleQueenSide = king.color == PIECE_WHITE
        ? position->PIECE_WHITECastleQueenSide
        : position->PIECE_BLACKCastleQueenSide;

    Piece kingSideRook = position->board[homeRow][7];

    if (mayCastleKingSide &&
        kingSideRook.type == ROOK && kingSideRook.color == king.color &&
        position->board[homeRow][5].type == EMPTY &&
        position->board[homeRow][6].type == EMPTY &&
        !IsSquareAttacked(position, homeRow, 5, enemy) &&
        !IsSquareAttacked(position, homeRow, 6, enemy)) {
        AddMove(moves, moveCount, row, column, homeRow, 6,
                false, true, EMPTY);
    }

    Piece queenSideRook = position->board[homeRow][0];

    if (mayCastleQueenSide &&
        queenSideRook.type == ROOK && queenSideRook.color == king.color &&
        position->board[homeRow][1].type == EMPTY &&
        position->board[homeRow][2].type == EMPTY &&
        position->board[homeRow][3].type == EMPTY &&
        !IsSquareAttacked(position, homeRow, 3, enemy) &&
        !IsSquareAttacked(position, homeRow, 2, enemy)) {
        AddMove(moves, moveCount, row, column, homeRow, 2,
                false, true, EMPTY);
    }
}

static int GeneratePseudoLegalMoves(const Position *position, int row, int column,
                                    Move moves[MAX_MOVES])
{
    int moveCount = 0;
    Piece piece = position->board[row][column];

    if (piece.type == EMPTY) {
        return 0;
    }

    if (piece.type == PAWN) {
        int direction = piece.color == PIECE_WHITE ? -1 : 1;
        int startingRow = piece.color == PIECE_WHITE ? 6 : 1;
        int promotionRow = piece.color == PIECE_WHITE ? 0 : 7;
        int oneStepRow = row + direction;

        if (IsInsideBoard(oneStepRow, column) &&
            position->board[oneStepRow][column].type == EMPTY) {
            PieceType promotion = oneStepRow == promotionRow ? QUEEN : EMPTY;
            AddMove(moves, &moveCount, row, column, oneStepRow, column,
                    false, false, promotion);

            int twoStepRow = row + 2 * direction;

            if (row == startingRow &&
                position->board[twoStepRow][column].type == EMPTY) {
                AddMove(moves, &moveCount, row, column, twoStepRow, column,
                        false, false, EMPTY);
            }
        }

        for (int columnOffset = -1; columnOffset <= 1; columnOffset += 2) {
            int toRow = row + direction;
            int toColumn = column + columnOffset;

            if (!IsInsideBoard(toRow, toColumn)) {
                continue;
            }

            Piece target = position->board[toRow][toColumn];

            if (target.type != EMPTY && target.color != piece.color) {
                PieceType promotion = toRow == promotionRow ? QUEEN : EMPTY;
                AddMove(moves, &moveCount, row, column, toRow, toColumn,
                        false, false, promotion);
            } else if (toRow == position->enPassantRow &&
                       toColumn == position->enPassantColumn) {
                Piece adjacentPawn = position->board[row][toColumn];

                if (adjacentPawn.type == PAWN &&
                    adjacentPawn.color == OtherColor(piece.color)) {
                    AddMove(moves, &moveCount, row, column, toRow, toColumn,
                            true, false, EMPTY);
                }
            }
        }
    }

    if (piece.type == KNIGHT) {
        const int offsets[8][2] = {
            {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
            { 1, -2}, { 1, 2}, { 2, -1}, { 2, 1}
        };

        for (int i = 0; i < 8; i++) {
            int toRow = row + offsets[i][0];
            int toColumn = column + offsets[i][1];

            if (!IsInsideBoard(toRow, toColumn)) {
                continue;
            }

            Piece target = position->board[toRow][toColumn];

            if (target.type == EMPTY || target.color != piece.color) {
                AddMove(moves, &moveCount, row, column, toRow, toColumn,
                        false, false, EMPTY);
            }
        }
    }

    const int straightDirections[4][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}
    };
    const int diagonalDirections[4][2] = {
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1}
    };

    if (piece.type == ROOK || piece.type == QUEEN) {
        AddSlidingMoves(position, row, column, straightDirections, 4,
                        moves, &moveCount);
    }

    if (piece.type == BISHOP || piece.type == QUEEN) {
        AddSlidingMoves(position, row, column, diagonalDirections, 4,
                        moves, &moveCount);
    }

    if (piece.type == KING) {
        for (int rowOffset = -1; rowOffset <= 1; rowOffset++) {
            for (int columnOffset = -1; columnOffset <= 1; columnOffset++) {
                if (rowOffset == 0 && columnOffset == 0) {
                    continue;
                }

                int toRow = row + rowOffset;
                int toColumn = column + columnOffset;

                if (!IsInsideBoard(toRow, toColumn)) {
                    continue;
                }

                Piece target = position->board[toRow][toColumn];

                if (target.type == EMPTY || target.color != piece.color) {
                    AddMove(moves, &moveCount, row, column, toRow, toColumn,
                            false, false, EMPTY);
                }
            }
        }

        AddCastlingMoves(position, row, column, moves, &moveCount);
    }

    return moveCount;
}

static void RemoveCastlingRightForCapturedRook(Position *position,
                                                Piece captured,
                                                int row, int column)
{
    if (captured.type != ROOK) {
        return;
    }

    if (captured.color == PIECE_WHITE && row == 7 && column == 0) {
        position->PIECE_WHITECastleQueenSide = false;
    } else if (captured.color == PIECE_WHITE && row == 7 && column == 7) {
        position->PIECE_WHITECastleKingSide = false;
    } else if (captured.color == PIECE_BLACK && row == 0 && column == 0) {
        position->PIECE_BLACKCastleQueenSide = false;
    } else if (captured.color == PIECE_BLACK && row == 0 && column == 7) {
        position->PIECE_BLACKCastleKingSide = false;
    }
}

static void ApplyMoveToPosition(Position *position, Move move)
{
    Piece movingPiece = position->board[move.fromRow][move.fromColumn];
    Piece capturedPiece = position->board[move.toRow][move.toColumn];
    bool isCapture = capturedPiece.type != EMPTY || move.enPassant;

    RemoveCastlingRightForCapturedRook(position, capturedPiece,
                                        move.toRow, move.toColumn);

    if (movingPiece.type == KING) {
        if (movingPiece.color == PIECE_WHITE) {
            position->PIECE_WHITECastleKingSide = false;
            position->PIECE_WHITECastleQueenSide = false;
        } else {
            position->PIECE_BLACKCastleKingSide = false;
            position->PIECE_BLACKCastleQueenSide = false;
        }
    }

    if (movingPiece.type == ROOK) {
        if (movingPiece.color == PIECE_WHITE && move.fromRow == 7) {
            if (move.fromColumn == 0) position->PIECE_WHITECastleQueenSide = false;
            if (move.fromColumn == 7) position->PIECE_WHITECastleKingSide = false;
        }

        if (movingPiece.color == PIECE_BLACK && move.fromRow == 0) {
            if (move.fromColumn == 0) position->PIECE_BLACKCastleQueenSide = false;
            if (move.fromColumn == 7) position->PIECE_BLACKCastleKingSide = false;
        }
    }

    position->board[move.toRow][move.toColumn] = movingPiece;
    position->board[move.fromRow][move.fromColumn] = EmptyPiece();

    if (move.enPassant) {
        position->board[move.fromRow][move.toColumn] = EmptyPiece();
    }

    if (move.castling) {
        if (move.toColumn == 6) {
            position->board[move.toRow][5] = position->board[move.toRow][7];
            position->board[move.toRow][7] = EmptyPiece();
        } else {
            position->board[move.toRow][3] = position->board[move.toRow][0];
            position->board[move.toRow][0] = EmptyPiece();
        }
    }

    if (movingPiece.type == PAWN &&
        (move.toRow == 0 || move.toRow == 7)) {
        PieceType promotion = move.promotion;

        if (promotion != QUEEN && promotion != ROOK &&
            promotion != BISHOP && promotion != KNIGHT) {
            promotion = QUEEN;
        }

        position->board[move.toRow][move.toColumn].type = promotion;
    }

    position->enPassantRow = -1;
    position->enPassantColumn = -1;

    if (movingPiece.type == PAWN &&
        move.toRow - move.fromRow == 2) {
        position->enPassantRow = move.fromRow + 1;
        position->enPassantColumn = move.fromColumn;
    } else if (movingPiece.type == PAWN &&
               move.fromRow - move.toRow == 2) {
        position->enPassantRow = move.fromRow - 1;
        position->enPassantColumn = move.fromColumn;
    }

    if (movingPiece.type == PAWN || isCapture) {
        position->halfmoveClock = 0;
    } else {
        position->halfmoveClock++;
    }

    position->turn = OtherColor(position->turn);
}

static int GenerateLegalMoves(const Position *position, int row, int column,
                              Move moves[MAX_MOVES])
{
    Piece movingPiece = position->board[row][column];

    if (movingPiece.type == EMPTY || movingPiece.color != position->turn) {
        return 0;
    }

    Move pseudoMoves[MAX_MOVES];
    int pseudoMoveCount = GeneratePseudoLegalMoves(position, row, column,
                                                    pseudoMoves);
    int legalMoveCount = 0;

    for (int i = 0; i < pseudoMoveCount; i++) {
        Position testPosition = *position;
        ApplyMoveToPosition(&testPosition, pseudoMoves[i]);

        if (!IsInCheck(&testPosition, movingPiece.color)) {
            moves[legalMoveCount++] = pseudoMoves[i];
        }
    }

    return legalMoveCount;
}

static bool HasAnyLegalMove(const Position *position)
{
    Move moves[MAX_MOVES];

    for (int row = 0; row < 8; row++) {
        for (int column = 0; column < 8; column++) {
            Piece piece = position->board[row][column];

            if (piece.color == position->turn &&
                GenerateLegalMoves(position, row, column, moves) > 0) {
                return true;
            }
        }
    }

    return false;
}

// -----------------------------------------------------------------------------
// Game-ending conditions and history
// -----------------------------------------------------------------------------

static bool IsInsufficientMaterial(const Position *position)
{
    int bishopCount = 0;
    int knightCount = 0;
    int firstBishopSquareColor = -1;
    bool bishopsShareSquareColor = true;

    for (int row = 0; row < 8; row++) {
        for (int column = 0; column < 8; column++) {
            Piece piece = position->board[row][column];

            if (piece.type == EMPTY || piece.type == KING) {
                continue;
            }

            if (piece.type == PAWN || piece.type == ROOK || piece.type == QUEEN) {
                return false;
            }

            if (piece.type == KNIGHT) {
                knightCount++;
            }

            if (piece.type == BISHOP) {
                bishopCount++;
                int squareColor = (row + column) % 2;

                if (firstBishopSquareColor == -1) {
                    firstBishopSquareColor = squareColor;
                } else if (firstBishopSquareColor != squareColor) {
                    bishopsShareSquareColor = false;
                }
            }
        }
    }

    int minorPieceCount = bishopCount + knightCount;

    if (minorPieceCount == 0) return true; // King against king
    if (minorPieceCount == 1) return true; // King+bishop/knight against king

    // Any number of bishops is insufficient when every bishop is restricted
    // to squares of the same color and there are no other pieces.
    return knightCount == 0 && bishopsShareSquareColor;
}

static int CountCurrentPosition(const Game *game)
{
    int count = 0;
    const char *currentKey = game->positionKeys[game->moveCount];

    for (int i = 0; i <= game->moveCount; i++) {
        if (strcmp(game->positionKeys[i], currentKey) == 0) {
            count++;
        }
    }

    return count;
}

static void UpdateGameResult(Game *game)
{
    game->result = GAME_PLAYING;

    if (!HasAnyLegalMove(&game->position)) {
        if (IsInCheck(&game->position, game->position.turn)) {
            game->result = game->position.turn == PIECE_WHITE
                ? PIECE_BLACK_WINS
                : PIECE_WHITE_WINS;
        } else {
            game->result = DRAW_STALEMATE;
        }
        return;
    }

    if (game->position.halfmoveClock >= 100) {
        game->result = DRAW_FIFTY_MOVE;
    } else if (CountCurrentPosition(game) >= 3) {
        game->result = DRAW_REPETITION;
    } else if (IsInsufficientMaterial(&game->position)) {
        game->result = DRAW_INSUFFICIENT_MATERIAL;
    }
}

static bool MakeGameMove(Game *game, Move move)
{
    if (game->moveCount >= MAX_HISTORY || game->result != GAME_PLAYING) {
        return false;
    }

    game->history[game->moveCount] = game->position;
    game->moveHistory[game->moveCount] = move;
    ApplyMoveToPosition(&game->position, move);
    game->moveCount++;

    CreatePositionKey(&game->position,
                      game->positionKeys[game->moveCount]);
    UpdateGameResult(game);
    return true;
}

static void UndoMove(Game *game)
{
    if (game->moveCount == 0) {
        return;
    }

    game->moveCount--;
    game->position = game->history[game->moveCount];
    game->result = GAME_PLAYING;
}

// -----------------------------------------------------------------------------
// Mouse input
// -----------------------------------------------------------------------------

static void SelectSquare(const Game *game, InterfaceState *interfaceState,
                         int row, int column)
{
    Piece piece = game->position.board[row][column];

    if (piece.type == EMPTY || piece.color != game->position.turn) {
        ClearSelection(interfaceState);
        return;
    }

    interfaceState->selectedRow = row;
    interfaceState->selectedColumn = column;
    interfaceState->legalMoveCount = GenerateLegalMoves(
        &game->position,
        row,
        column,
        interfaceState->legalMoves
    );
}

static bool IsMoveDestination(const InterfaceState *interfaceState,
                              int row, int column, Move *foundMove)
{
    for (int i = 0; i < interfaceState->legalMoveCount; i++) {
        Move move = interfaceState->legalMoves[i];

        if (move.toRow == row && move.toColumn == column) {
            if (foundMove != NULL) {
                *foundMove = move;
            }
            return true;
        }
    }

    return false;
}

static void HandleBoardClick(Game *game, InterfaceState *interfaceState,
                             Vector2 mouse)
{
    Rectangle boardRectangle = {
        BOARD_X, BOARD_Y, BOARD_SIZE, BOARD_SIZE
    };

    if (game->result != GAME_PLAYING ||
        !CheckCollisionPointRec(mouse, boardRectangle)) {
        return;
    }

    int column = (int) ((mouse.x - BOARD_X) / SQUARE_SIZE);
    int row = (int) ((mouse.y - BOARD_Y) / SQUARE_SIZE);
    Piece clickedPiece = game->position.board[row][column];

    if (interfaceState->selectedRow == -1) {
        SelectSquare(game, interfaceState, row, column);
        return;
    }

    if (row == interfaceState->selectedRow &&
        column == interfaceState->selectedColumn) {
        ClearSelection(interfaceState);
        return;
    }

    Move chosenMove;

    if (IsMoveDestination(interfaceState, row, column, &chosenMove)) {
        Piece movingPiece = game->position.board[chosenMove.fromRow]
                                              [chosenMove.fromColumn];

        if (movingPiece.type == PAWN &&
            (chosenMove.toRow == 0 || chosenMove.toRow == 7)) {
            interfaceState->choosingPromotion = true;
            interfaceState->pendingPromotionMove = chosenMove;
        } else {
            MakeGameMove(game, chosenMove);
            ClearSelection(interfaceState);
        }
        return;
    }

    if (clickedPiece.type != EMPTY &&
        clickedPiece.color == game->position.turn) {
        SelectSquare(game, interfaceState, row, column);
    } else {
        ClearSelection(interfaceState);
    }
}

static Rectangle UndoButtonRectangle(void)
{
    return (Rectangle) { 740, 570, 250, 50 };
}

static Rectangle NewGameButtonRectangle(void)
{
    return (Rectangle) { 740, 635, 250, 50 };
}

static Rectangle PromotionRectangle(int index)
{
    const float optionSize = 90.0f;
    const float gap = 12.0f;
    const float totalWidth = 4 * optionSize + 3 * gap;
    float startX = BOARD_X + (BOARD_SIZE - totalWidth) / 2.0f;

    return (Rectangle) {
        startX + index * (optionSize + gap),
        BOARD_Y + BOARD_SIZE / 2.0f - optionSize / 2.0f,
        optionSize,
        optionSize
    };
}

static void HandlePromotionClick(Game *game, InterfaceState *interfaceState,
                                 Vector2 mouse)
{
    PieceType choices[4] = { QUEEN, ROOK, BISHOP, KNIGHT };

    for (int i = 0; i < 4; i++) {
        if (CheckCollisionPointRec(mouse, PromotionRectangle(i))) {
            interfaceState->pendingPromotionMove.promotion = choices[i];
            MakeGameMove(game, interfaceState->pendingPromotionMove);
            interfaceState->choosingPromotion = false;
            ClearSelection(interfaceState);
            return;
        }
    }
}

// -----------------------------------------------------------------------------
// Drawing
// -----------------------------------------------------------------------------

static char PieceLetter(PieceType type)
{
    switch (type) {
        case PAWN:   return 'P';
        case KNIGHT: return 'N';
        case BISHOP: return 'B';
        case ROOK:   return 'R';
        case QUEEN:  return 'Q';
        case KING:   return 'K';
        case EMPTY:  return ' ';
    }

    return ' ';
}

static void DrawPiece(Piece piece, Rectangle square)
{
    if (piece.type == EMPTY) {
        return;
    }

    Vector2 center = {
        square.x + square.width / 2.0f,
        square.y + square.height / 2.0f
    };

    Color PIECE_WHITEPiece = { 247, 242, 224, 255 };
    Color PIECE_BLACKPiece = { 47, 43, 40, 255 };
    Color fill = piece.color == PIECE_WHITE ? PIECE_WHITEPiece : PIECE_BLACKPiece;
    Color outline = piece.color == PIECE_WHITE ? PIECE_BLACKPiece : PIECE_WHITEPiece;

    DrawCircle((int) center.x, (int) center.y, 28.0f, fill);
    DrawCircleLines((int) center.x, (int) center.y, 28.0f, outline);

    char text[2] = { PieceLetter(piece.type), '\0' };
    int fontSize = 34;
    int textWidth = MeasureText(text, fontSize);

    DrawText(text,
             (int) (center.x - textWidth / 2.0f),
             (int) (center.y - fontSize / 2.0f),
             fontSize,
             outline);
}

static void DrawBoard(const Game *game, const InterfaceState *interfaceState)
{
    Color lightSquare = { 240, 217, 181, 255 };
    Color darkSquare = { 181, 136, 99, 255 };
    Color lastMoveColor = { 246, 246, 105, 120 };
    Color selectedColor = { 246, 246, 105, 210 };
    Color legalMoveColor = { 30, 30, 30, 95 };
    Color checkColor = { 220, 45, 45, 180 };

    Move lastMove = { 0 };
    bool hasLastMove = game->moveCount > 0;

    if (hasLastMove) {
        lastMove = game->moveHistory[game->moveCount - 1];
    }

    for (int row = 0; row < 8; row++) {
        for (int column = 0; column < 8; column++) {
            Rectangle square = {
                BOARD_X + column * SQUARE_SIZE,
                BOARD_Y + row * SQUARE_SIZE,
                SQUARE_SIZE,
                SQUARE_SIZE
            };

            Color baseColor = (row + column) % 2 == 0
                ? lightSquare
                : darkSquare;
            DrawRectangleRec(square, baseColor);

            if (hasLastMove &&
                ((lastMove.fromRow == row && lastMove.fromColumn == column) ||
                 (lastMove.toRow == row && lastMove.toColumn == column))) {
                DrawRectangleRec(square, lastMoveColor);
            }

            if (interfaceState->selectedRow == row &&
                interfaceState->selectedColumn == column) {
                DrawRectangleRec(square, selectedColor);
            }
        }
    }

    // Mark the king in red when the current player is in check.
    if (IsInCheck(&game->position, game->position.turn)) {
        int kingRow;
        int kingColumn;

        if (FindKing(&game->position, game->position.turn,
                     &kingRow, &kingColumn)) {
            Rectangle kingSquare = {
                BOARD_X + kingColumn * SQUARE_SIZE,
                BOARD_Y + kingRow * SQUARE_SIZE,
                SQUARE_SIZE,
                SQUARE_SIZE
            };
            DrawRectangleRec(kingSquare, checkColor);
        }
    }

    // Legal-move dots and capture rings
    for (int i = 0; i < interfaceState->legalMoveCount; i++) {
        Move move = interfaceState->legalMoves[i];
        Piece target = game->position.board[move.toRow][move.toColumn];
        int centerX = BOARD_X + move.toColumn * SQUARE_SIZE + SQUARE_SIZE / 2;
        int centerY = BOARD_Y + move.toRow * SQUARE_SIZE + SQUARE_SIZE / 2;

        if (target.type == EMPTY && !move.enPassant) {
            DrawCircle(centerX, centerY, 11.0f, legalMoveColor);
        } else {
            DrawCircleLines(centerX, centerY, 31.0f, legalMoveColor);
            DrawCircleLines(centerX, centerY, 32.0f, legalMoveColor);
        }
    }

    // Pieces are drawn last so that they appear above square highlights.
    for (int row = 0; row < 8; row++) {
        for (int column = 0; column < 8; column++) {
            Rectangle square = {
                BOARD_X + column * SQUARE_SIZE,
                BOARD_Y + row * SQUARE_SIZE,
                SQUARE_SIZE,
                SQUARE_SIZE
            };
            DrawPiece(game->position.board[row][column], square);
        }
    }

    // Coordinates
    for (int i = 0; i < 8; i++) {
        char file[2] = { (char) ('a' + i), '\0' };
        char rank[2] = { (char) ('8' - i), '\0' };

        DrawText(file,
                 BOARD_X + i * SQUARE_SIZE + SQUARE_SIZE - 14,
                 BOARD_Y + BOARD_SIZE - 17,
                 14,
                 (i + 7) % 2 == 0 ? darkSquare : lightSquare);

        DrawText(rank,
                 BOARD_X + 4,
                 BOARD_Y + i * SQUARE_SIZE + 4,
                 14,
                 i % 2 == 0 ? darkSquare : lightSquare);
    }

    DrawRectangleLinesEx(
        (Rectangle) { BOARD_X - 4, BOARD_Y - 4, BOARD_SIZE + 8, BOARD_SIZE + 8 },
        4.0f,
        (Color) { 74, 52, 37, 255 }
    );
}

static const char *StatusText(const Game *game)
{
    switch (game->result) {
        case PIECE_WHITE_WINS: return "Checkmate - PIECE_WHITE wins!";
        case PIECE_BLACK_WINS: return "Checkmate - PIECE_BLACK wins!";
        case DRAW_STALEMATE: return "Draw by stalemate";
        case DRAW_REPETITION: return "Draw by repetition";
        case DRAW_FIFTY_MOVE: return "Draw by 50-move rule";
        case DRAW_INSUFFICIENT_MATERIAL:
            return "Draw - insufficient material";
        case GAME_PLAYING:
            if (IsInCheck(&game->position, game->position.turn)) {
                return game->position.turn == PIECE_WHITE
                    ? "PIECE_WHITE is in check"
                    : "PIECE_BLACK is in check";
            }
            return game->position.turn == PIECE_WHITE
                ? "PIECE_WHITE to move"
                : "PIECE_BLACK to move";
    }

    return "";
}

static void DrawButton(Rectangle rectangle, const char *text,
                       Vector2 mouse, bool enabled)
{
    Color normal = { 74, 52, 37, 255 };
    Color hover = { 104, 75, 54, 255 };
    Color disabled = { 145, 138, 132, 255 };
    Color color = disabled;

    if (enabled) {
        color = CheckCollisionPointRec(mouse, rectangle) ? hover : normal;
    }

    DrawRectangleRec(rectangle, color);
    DrawRectangleLinesEx(rectangle, 2.0f, (Color) { 48, 36, 28, 255 });

    int fontSize = 20;
    int textWidth = MeasureText(text, fontSize);
    DrawText(text,
             (int) (rectangle.x + (rectangle.width - textWidth) / 2.0f),
             (int) (rectangle.y + (rectangle.height - fontSize) / 2.0f),
             fontSize,
             RAYWHITE);
}

static void DrawSidePanel(const Game *game, Vector2 mouse)
{
    DrawText("RAYLIB CHESS", 740, 70, 30, (Color) { 47, 43, 40, 255 });
    DrawRectangle(740, 115, 250, 3, (Color) { 181, 136, 99, 255 });

    DrawText(StatusText(game), 740, 145, 22, (Color) { 47, 43, 40, 255 });
    DrawText(TextFormat("Moves played: %d", game->moveCount),
             740, 185, 18, DARKGRAY);

    DrawText("How to play", 740, 245, 24, (Color) { 47, 43, 40, 255 });
    DrawText("1. Click one of your pieces", 740, 285, 18, DARKGRAY);
    DrawText("2. Click a highlighted square", 740, 315, 18, DARKGRAY);
    DrawText("3. PIECE_WHITE and PIECE_BLACK alternate", 740, 345, 18, DARKGRAY);

    DrawText("Piece letters", 740, 405, 24, (Color) { 47, 43, 40, 255 });
    DrawText("K King    Q Queen    R Rook", 740, 445, 17, DARKGRAY);
    DrawText("B Bishop  N Knight   P Pawn", 740, 475, 17, DARKGRAY);

    DrawText("Esc cancels a selection", 740, 525, 17, GRAY);

    DrawButton(UndoButtonRectangle(), "Undo move", mouse,
               game->moveCount > 0);
    DrawButton(NewGameButtonRectangle(), "New game", mouse, true);
}

static void DrawPromotionWindow(const Game *game,
                                const InterfaceState *interfaceState,
                                Vector2 mouse)
{
    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
                  (Color) { 0, 0, 0, 145 });

    Rectangle panel = {
        BOARD_X + 75,
        BOARD_Y + BOARD_SIZE / 2.0f - 105,
        BOARD_SIZE - 150,
        210
    };

    DrawRectangleRec(panel, (Color) { 245, 242, 232, 255 });
    DrawRectangleLinesEx(panel, 4.0f, (Color) { 74, 52, 37, 255 });

    const char *title = "Choose a promotion piece";
    int titleWidth = MeasureText(title, 24);
    DrawText(title,
             (int) (panel.x + (panel.width - titleWidth) / 2.0f),
             (int) panel.y + 25,
             24,
             (Color) { 47, 43, 40, 255 });

    PieceType choices[4] = { QUEEN, ROOK, BISHOP, KNIGHT };
    PieceColor color = game->position.board[
        interfaceState->pendingPromotionMove.fromRow
    ][
        interfaceState->pendingPromotionMove.fromColumn
    ].color;

    for (int i = 0; i < 4; i++) {
        Rectangle option = PromotionRectangle(i);
        bool hovered = CheckCollisionPointRec(mouse, option);

        DrawRectangleRec(option,
                         hovered
                             ? (Color) { 246, 246, 105, 255 }
                             : (Color) { 240, 217, 181, 255 });
        DrawRectangleLinesEx(option, 2.0f, (Color) { 74, 52, 37, 255 });
        DrawPiece((Piece) { choices[i], color }, option);
    }
}

// -----------------------------------------------------------------------------
// Program entry point
// -----------------------------------------------------------------------------

int main(void)
{
    Game game;
    InterfaceState interfaceState = { 0 };

    ResetGame(&game);
    ClearSelection(&interfaceState);
    interfaceState.choosingPromotion = false;

    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Simple Raylib Chess");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        Vector2 mouse = GetMousePosition();

        if (IsKeyPressed(KEY_ESCAPE)) {
            interfaceState.choosingPromotion = false;
            ClearSelection(&interfaceState);
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (interfaceState.choosingPromotion) {
                HandlePromotionClick(&game, &interfaceState, mouse);
            } else if (CheckCollisionPointRec(mouse, UndoButtonRectangle())) {
                UndoMove(&game);
                ClearSelection(&interfaceState);
            } else if (CheckCollisionPointRec(mouse, NewGameButtonRectangle())) {
                ResetGame(&game);
                ClearSelection(&interfaceState);
            } else {
                HandleBoardClick(&game, &interfaceState, mouse);
            }
        }

        BeginDrawing();
        ClearBackground((Color) { 235, 235, 235, 255 });

        DrawBoard(&game, &interfaceState);
        DrawSidePanel(&game, mouse);

        if (interfaceState.choosingPromotion) {
            DrawPromotionWindow(&game, &interfaceState, mouse);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
