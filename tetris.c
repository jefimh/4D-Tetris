/**
* @brief   4-directional Tetris implementation for DE10-Lite FPGA in RISC-V
*
*
* A unique implementation of Tetris featuring 4-directional gravity and movement.
* This version runs on DE10-Lite FPGA hardware using RISC-V architecture and
* includes several distinctive features:
* 
* Key Features:
* - 4-directional piece movement (up, down, left, right)
* - Gravity effects relative to board center
* - Both horizontal and vertical line clearing
* - Dynamic difficulty scaling
* - VGA display output with 3D block effects
* - Hardware-specific optimizations for DE10-Lite
*
* Technical Specifications:
* - Display: VGA 320x240 pixels
* - Game board: 20x20 grid
* - Block size: 8x8 pixels
* - Controls: Hardware switches and buttons
* - Memory-mapped I/O for hardware interface
* 
* Dependencies:
* - External printing functions (print, print_dec)
* - Timer and display control registers
* - VGA output buffer
*/

#include <stdint.h>

/* External functions */
extern void print(const char *);
extern void print_dec(unsigned int);
extern void display_string(char *);
extern void time2string(char *, int);
extern void tick(int *);
extern void delay(int);
extern int nextprime(int);

/* Hardware interface definitions */
#define VGA_PIXELS ((volatile char *)0x08000000)
#define VGA_CTRL ((volatile uint32_t *)0x04000100)
#define SWITCH_ADDRESS ((volatile int *)0x04000010)
#define BUTTON_ADDRESS ((volatile int *)0x040000d0)
#define TIMER_STATUS ((volatile int *)0x04000020)
#define TIMER_CONTROL ((volatile int *)0x04000024)
#define TIMER_PERIODL ((volatile int *)0x04000028)
#define TIMER_PERIODH ((volatile int *)0x0400002C)

#define LARGE_CHAR_WIDTH 12
#define LARGE_CHAR_HEIGHT 12
#define GAME_OVER_X ((SCREEN_WIDTH - (9 * LARGE_CHAR_WIDTH)) / 2)
#define GAME_OVER_Y ((SCREEN_HEIGHT - LARGE_CHAR_HEIGHT) / 2)

/* Screen and game dimensions */
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define BLOCK_SIZE 8                                                    
#define BOARD_WIDTH 20                                                
#define BOARD_HEIGHT 20                                                 
#define BOARD_START_X ((SCREEN_WIDTH - BOARD_WIDTH * BLOCK_SIZE) / 2)   // Center horizontally
#define BOARD_START_Y ((SCREEN_HEIGHT - BOARD_HEIGHT * BLOCK_SIZE) / 2) // Center vertically

/* Colors */
#define BLACK 0     // Background
#define CYAN 1      // I piece
#define BLUE 2      // J piece
#define ORANGE 3    // L piece
#define YELLOW 4    // O piece
#define GREEN 5     // S piece
#define PURPLE 6    // T piece
#define RED 7       // Z piece
#define WHITE 0xBBB // Border

/* Game scoring */
#define SCORE_SINGLE 100
#define SCORE_DOUBLE 300
#define SCORE_TRIPLE 500
#define SCORE_TETRIS 800

/* direction definitions */
#define DIR_DOWN 0
#define DIR_UP 1
#define DIR_LEFT 2
#define DIR_RIGHT 3

/* Input configuration */
#define SWITCH_LEFT 0x2
#define SWITCH_RIGHT 0x1
#define SWITCH_DOWN 0x4 // Using switch 3
#define SWITCH_UP 0x8   // Using switch 4

/* Tetromino definitions */
const uint16_t TETROMINOS[7][4] = {
    {0x0F00, 0x2222, 0x0F00, 0x2222}, // I
    {0x8E00, 0x6440, 0x0E20, 0x44C0}, // J
    {0x2E00, 0x4460, 0x0E80, 0xC440}, // L
    {0x6600, 0x6600, 0x6600, 0x6600}, // O
    {0x6C00, 0x4620, 0x6C00, 0x4620}, // S
    {0x4E00, 0x4640, 0x0E40, 0x4C40}, // T
    {0xC600, 0x2640, 0xC600, 0x2640}  // Z
};

#define DIGIT_WIDTH 5
#define DIGIT_HEIGHT 7
#define SCORE_Y 2
#define SCORE_X 5

// 5x7 digit patterns (0-9 and ':')
const unsigned long DIGIT_PATTERNS[11] = {
    0b01110100011000110001100011000101110, // 0
    0b00100011000010000100001000010001110, // 1
    0b01110100010000100110010001000111111, // 2
    0b01110100010000100110000011000101110, // 3
    0b00011001010010010001111110001000010, // 4
    0b11111100001111000001000011000101110, // 5
    0b01110100001000011110100011000101110, // 6
    0b11111000010001000100010001000010000, // 7
    0b01110100011000101110100011000101110, // 8
    0b01110100011000101111000011000101110, // 9
    0b00000001000000000000001000000000000  // :
};

// Letter patterns for "SCORE" - fixed 5x7 pixel patterns
const uint32_t LETTER_PATTERNS[5] = {
    0b01110100011000001110000011000101110, // S
    0b01110100001000010000100001000101110, // C
    0b01110100011000110001100011000101110, // O
    0b11110100011000111110100101001010001, // R
    0b11111100001111010000100001000011111  // E
};

/* Game structures */
typedef struct
{
    int x, y;
    int type;
    int rotation;
    int direction;
} Piece;

typedef struct
{
    char cells[BOARD_HEIGHT][BOARD_WIDTH];
} Board;

/* Global variables */
Board board;
Piece currentPiece;
int gameOver = 0;
int timeoutcount = 0;
int lastButtonState = 0;
int lastSwitchState = 0;
int score = 0;
static unsigned int randState = 1;
int startSpeed = 899999;
int speed = 0;

/**
* @brief Generates a pseudo-random number using linear congruential generator
* 
* @return Integer in range [0, 32767] (15 bits)
* 
* Linear Congruential Generator implementation that:
* 1. Uses standard LCG parameters:
*    - Multiplier: 1103515245 (prime number)
*    - Increment: 12345
*    - No explicit modulus (relies on integer overflow)
* 
* 2. State management:
*    - Updates global randState with each call
*    - Initial seed set from timer value at game start 
* 
* 3. Output processing:
*    - Right shifts by 16 bits to discard lower bits
*    - Masks with 0x7fff to ensure 15-bit positive output
*    - Returns values in range [0, 32767]
* 
* Used for:
* - Selecting random tetromino types
* - Setting initial piece directions
* - Any other game randomization needs
* 
*/
int my_rand(void)
{
    randState = randState * 1103515245 + 12345;
    return (randState >> 16) & 0x7fff;
}

/**
* @brief Converts internal color indices to VGA-compatible color codes
* 
* @param piece_color Game color index (0-8)
* @return 8-bit VGA color code
* 
* Color mapping function that:
* 1. Maps game color constants to VGA color codes:
*    - BLACK (0): 0x92 (Light gray background)
*    - CYAN (1): 0x3F (Light cyan for I piece)
*    - BLUE (2): 0x03 (Blue for J piece)
*    - ORANGE (3): 0xE0 (Orange for L piece)
*    - YELLOW (4): 0xFC (Yellow for O piece)
*    - GREEN (5): 0x1C (Green for S piece)
*    - PURPLE (6): 0x43 (Purple for T piece)
*    - RED (7): 0xE0 (Red for Z piece)
*    - WHITE (0xBBB): 0xFF (White for borders)
* 
* 2. VGA color format:
*    - Uses 8-bit color codes
*    - Chosen for optimal visibility on VGA display
*    - Default to light gray (0x92) for unknown colors
* 
* Used by all drawing functions to ensure consistent
* color representation across the game interface
*/
char get_vga_color(char piece_color)
{
    switch (piece_color)
    {
    case BLACK:
        return 0x92; // Light gray for background
    case CYAN:
        return 0x3F; // Light cyan
    case BLUE:
        return 0x03; // Blue
    case ORANGE:
        return 0xF4; // Orange
    case YELLOW:
        return 0xFC; // Yellow
    case GREEN:
        return 0x1C; // Green
    case PURPLE:
        return 0x43; // Purple
    case RED:
        return 0xE0; // Red
    case WHITE:
        return 0xFF; // White
    default:
        return 0x92; // Light gray
    }
}

/**
* @brief Renders a single game block with 3D lighting effects
* 
* @param x Board grid X-coordinate
* @param y Board grid Y-coordinate
* @param color Game color index for the block
* 
* Block rendering function that:
* 1. Coordinate conversion:
*    - Converts grid coordinates to screen pixels
*    - Uses BOARD_START_X/Y for offset from screen edges
*    - Multiplies by BLOCK_SIZE for pixel dimensions
* 
* 2. Color processing:
*    - Converts game color to VGA color code
*    - Special handling for background blocks:
*      * Light edge: 0xB6 (lighter gray)
*      * Dark edge: 0x6D (darker gray)
*    - Non-background blocks:
*      * Light edge: White (0xFF)
*      * Dark edge: Black (0x00)
* 
* 3. 3D effect rendering:
*    - Main block body in primary color
*    - Top/left edges in lighter shade (2 pixels)
*    - Bottom/right edges in darker shade (2 pixels)
*    - Creates raised appearance for pieces
*    - Creates recessed appearance for background
* 
* 4. Safety features:
*    - Screen boundary checking
*    - Prevents buffer overflow
*    - Handles off-screen blocks
* 
* Used for:
* - Drawing tetris pieces
* - Drawing board grid
* - Drawing border blocks
* 
* Core rendering function called frequently during gameplay
*/
void draw_block(int x, int y, char color)
{
    // Convert grid coordinates to screen pixels
    int screenX = BOARD_START_X + x * BLOCK_SIZE;
    int screenY = BOARD_START_Y + y * BLOCK_SIZE;

    // Get VGA-compatible color
    char vgaColor = get_vga_color(color);

    // For background blocks, use slightly different shades of gray
    char lightEdge = (color == BLACK) ? 0xB6 : get_vga_color(WHITE); // Lighter gray for background
    char darkEdge = (color == BLACK) ? 0x6D : 0x00;                  // Darker gray for background

    // Draw block pixel by pixel
    for (int dy = 0; dy < BLOCK_SIZE; dy++)
    {
        for (int dx = 0; dx < BLOCK_SIZE; dx++)
        {
            if ((screenX + dx) >= 0 && (screenX + dx) < SCREEN_WIDTH &&
                (screenY + dy) >= 0 && (screenY + dy) < SCREEN_HEIGHT)
            {

                // Default to main color
                char pixelColor = vgaColor;

                // Top and left edges (lighter)
                if (dx <= 1 || dy <= 1)
                {
                    pixelColor = lightEdge;
                }

                // Bottom and right edges (darker)
                if (dx >= BLOCK_SIZE - 2 || dy >= BLOCK_SIZE - 2)
                {
                    pixelColor = darkEdge;
                }

                // Write pixel to VGA buffer
                VGA_PIXELS[(screenY + dy) * SCREEN_WIDTH + (screenX + dx)] = pixelColor;
            }
        }
    }
}

/**
* @brief Renders the current game score with label on the VGA display
* 
* 
* Score display function that:
* 1. Display area management:
*    - Positions score at BOARD_START_X horizontally
*    - Uses SCORE_Y constant for vertical position
*    - Clears entire score area before drawing
*    - Area height = DIGIT_HEIGHT + 2 pixels padding
* 
* 2. "SCORE" label rendering:
*    - Uses 5x7 LETTER_PATTERNS for "SCORE" text
*    - Draws each letter using bit patterns
*    - White color for high contrast
*    - 1-pixel spacing between letters
* 
* 3. Colon separator:
*    - Adds colon after "SCORE" text
*    - Uses DIGIT_PATTERNS[10] for colon pattern
*    - Extra pixel spacing around colon
* 
* 4. Score conversion and display:
*    - Handles scores 0-99999 (5 digits max)
*    - Special case for zero score
*    - Converts score to individual digits
*    - Right-aligns score digits
*    - Uses 5x7 DIGIT_PATTERNS for numbers
*    - Consistent 1-pixel digit spacing
* 
* 5. Hardware updates:
*    - Updates VGA control registers
*    - Triggers display refresh
* 
* Called after:
* - Line clears
* - Score changes
* - Game initialization
* 
* Core UI element for player feedback
*/
void draw_score(void)
{
    int xPosition = BOARD_START_X;

    // First, clear the entire score area
    for (int y = SCORE_Y; y < SCORE_Y + DIGIT_HEIGHT + 2; y++)
    {
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            VGA_PIXELS[y * SCREEN_WIDTH + x] = BLACK;
        }
    }

    // Draw "SCORE"
    for (int i = 0; i < 5; i++)
    {
        unsigned long pattern = LETTER_PATTERNS[i];
        for (int y = 0; y < DIGIT_HEIGHT; y++)
        {
            for (int x = 0; x < DIGIT_WIDTH; x++)
            {
                if (pattern & (1UL << (34 - (y * DIGIT_WIDTH + x))))
                {
                    VGA_PIXELS[(SCORE_Y + y) * SCREEN_WIDTH + xPosition + x] = WHITE;
                }
            }
        }
        xPosition += DIGIT_WIDTH + 1;
    }

    // Draw colon
    xPosition += 1;
    unsigned long colon = DIGIT_PATTERNS[10];
    for (int y = 0; y < DIGIT_HEIGHT; y++)
    {
        for (int x = 0; x < DIGIT_WIDTH; x++)
        {
            if (colon & (1UL << (34 - (y * DIGIT_WIDTH + x))))
            {
                VGA_PIXELS[(SCORE_Y + y) * SCREEN_WIDTH + xPosition + x] = WHITE;
            }
        }
    }
    xPosition += DIGIT_WIDTH + 1;

    // Convert score to digits
    char digits[6]; // Max 5 digits + null terminator
    int tempScore = score;
    int digitCount = 0;

    // Handle zero score case
    if (tempScore == 0)
    {
        digits[digitCount++] = '0';
    }

    // Convert score to digits
    while (tempScore > 0 && digitCount < 5)
    {
        digits[digitCount++] = '0' + (tempScore % 10);
        tempScore /= 10;
    }

    // Draw score digits from left to right
    for (int i = digitCount - 1; i >= 0; i--)
    {
        int digit = digits[i] - '0';
        unsigned long pattern = DIGIT_PATTERNS[digit];

        for (int y = 0; y < DIGIT_HEIGHT; y++)
        {
            for (int x = 0; x < DIGIT_WIDTH; x++)
            {
                if (pattern & (1UL << (34 - (y * DIGIT_WIDTH + x))))
                {
                    VGA_PIXELS[(SCORE_Y + y) * SCREEN_WIDTH + xPosition + x] = WHITE;
                }
            }
        }
        xPosition += DIGIT_WIDTH + 1;
    }

    // Update the display
    *(VGA_CTRL + 1) = (uint32_t)(uintptr_t)VGA_PIXELS;
    *(VGA_CTRL + 0) = 0;
}

/**
* @brief Renders the complete game board including border and all pieces
* 
* 
* Comprehensive board rendering function that:
* 1. Background preparation:
*    - Clears entire board area including border space
*    - Uses BLACK color for empty cells
*    - Includes one cell padding on all sides
*    - Range: (-1,-1) to (BOARD_WIDTH, BOARD_HEIGHT)
* 
* 2. Border rendering:
*    - Draws WHITE border blocks around play area
*    - Top border: spans full width plus corners
*    - Bottom border: spans full width plus corners 
*    - Left border: spans full height plus corners
*    - Right border: spans full height plus corners
*    - Creates clear game area boundary
* 
* 3. Piece rendering:
*    - Iterates through entire board.cells array
*    - Draws only non-BLACK cells
*    - Uses piece-specific colors from cell values
*    - Maintains consistent block appearance
* 
* Called during:
* - Initial board setup
* - After piece locking
* - After line clears
* - After gravity effects
* 
* Core display function for game state visualization
* Uses draw_block() for consistent block appearance
*/
void draw_board(void)
{
    // Fill the entire board area with black first
    for (int y = -1; y <= BOARD_HEIGHT; y++)
    {
        for (int x = -1; x <= BOARD_WIDTH; x++)
        {
            draw_block(x, y, BLACK);
        }
    }

    // Draw the border blocks
    for (int i = -1; i <= BOARD_WIDTH; i++)
    {
        draw_block(i, -1, WHITE);           // Top border
        draw_block(i, BOARD_HEIGHT, WHITE); // Bottom border
    }

    for (int i = -1; i <= BOARD_HEIGHT; i++)
    {
        draw_block(-1, i, WHITE);          // Left border
        draw_block(BOARD_WIDTH, i, WHITE); // Right border
    }

    // Draw the game pieces
    for (int y = 0; y < BOARD_HEIGHT; y++)
    {
        for (int x = 0; x < BOARD_WIDTH; x++)
        {
            if (board.cells[y][x] != BLACK)
            {
                draw_block(x, y, board.cells[y][x]);
            }
        }
    }
}

/**
* @brief Renders the currently active tetromino 
* 
* 
* Active piece rendering function that:
* 1. Shape retrieval and color mapping:
*    - Gets 16-bit shape pattern from TETROMINOS array using:
*      * currentPiece.type (0-6 for piece type)
*      * currentPiece.rotation (0-3 for rotation state)
*    - Maps piece type to color (type + 1):
*      * 1: CYAN (I piece)
*      * 2: BLUE (J piece)
*      * 3: ORANGE (L piece)
*      * 4: YELLOW (O piece)
*      * 5: GREEN (S piece)
*      * 6: PURPLE (T piece)
*      * 7: RED (Z piece)
* 
* 2. Shape rendering:
*    - Processes 4x4 shape grid
*    - Right shifts 16-bit pattern to check each bit
*    - Only draws blocks where bits are set (1)
*    - Offsets blocks by currentPiece position
* 
* Called during:
* - Every game tick
* - After piece movement
* - After piece rotation
* 
* Critical for real-time piece visualization
* Uses draw_block() for consistent appearance
*/
void draw_current_piece(void)
{
    uint16_t shape = TETROMINOS[currentPiece.type][currentPiece.rotation];
    char pieceColor = currentPiece.type + 1; // Maps to CYAN through RED based on piece type

    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            if ((shape >> (15 - (y * 4 + x))) & 1)
            {
                draw_block(currentPiece.x + x, currentPiece.y + y, pieceColor);
            }
        }
    }
}

/**
* @brief Checks if a piece collides with board boundaries or other pieces
* 
* @param p Pointer to Piece structure to check for collisions
* @return 1 if collision detected, 0 if no collision
* 
* Collision detection function that:
* 1. Shape processing:
*    - Gets 16-bit shape pattern from TETROMINOS array
*    - Uses piece type and current rotation state
*    - Processes each bit in 4x4 shape grid
* 
* 2. Collision checks for each filled block:
*    - Board boundaries:
*      * Left wall (boardX < 0)
*      * Right wall (boardX >= BOARD_WIDTH)
*      * Top edge (boardY < 0)
*      * Bottom edge (boardY >= BOARD_HEIGHT)
*    - Other pieces:
*      * Checks if board cell is non-BLACK
* 
* 3. Coordinate translation:
*    - Converts piece-relative coordinates to board coordinates
*    - Accounts for piece position (p->x, p->y)
* 
* Called during:
* - Piece movement
* - Piece rotation
* - New piece spawn
* 
* Critical for:
* - Movement validation
* - Game over detection
* - Wall kick handling
*/
int check_collision(Piece *p)
{
    uint16_t shape = TETROMINOS[p->type][p->rotation];
    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            if ((shape >> (15 - (y * 4 + x))) & 1)
            {
                //Convert piece coordinates to board coordinates
                int boardX = p->x + x;
                int boardY = p->y + y;

                if (boardX < 0 || boardX >= BOARD_WIDTH ||
                    boardY < 0 || boardY >= BOARD_HEIGHT ||
                    board.cells[boardY][boardX] != BLACK)
                {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/**
* @brief Initializes the game board and screen display
* 
* 
* Comprehensive initialization function that:
* 1. Board state initialization:
*    - Clears game board array (board.cells)
*    - Sets all cells to BLACK
*    - Covers full BOARD_WIDTH x BOARD_HEIGHT area
* 
* 2. Screen clearing:
*    - Cleans entire VGA display buffer
*    - Sets all pixels to BLACK
*    - Covers full SCREEN_WIDTH x SCREEN_HEIGHT
*    - Includes border areas
* 
* 3. Border rendering:
*    - Draws solid WHITE borders
*    - Consistent BLOCK_SIZE thickness
*    - Horizontal borders:
*      * Top: (BOARD_START_Y - BLOCK_SIZE)
*      * Bottom: (BOARD_START_Y + BOARD_HEIGHT * BLOCK_SIZE)
*      * Spans: -1 to BOARD_WIDTH inclusive
*    - Vertical borders:
*      * Left: (BOARD_START_X - BLOCK_SIZE)
*      * Right: (BOARD_START_X + BOARD_WIDTH * BLOCK_SIZE)
*      * Spans: -1 to BOARD_HEIGHT inclusive
* 
* Called during:
* - Game start
* - Game restart
* 
* Sets up clean initial state for gameplay
* Direct pixel manipulation for efficiency
*/
void init_board(void)
{
    // First clear the entire board
    for (int y = 0; y < BOARD_HEIGHT; y++)
    {
        for (int x = 0; x < BOARD_WIDTH; x++)
        {
            board.cells[y][x] = BLACK;
        }
    }

    // Initial screen clear including border area
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            VGA_PIXELS[y * SCREEN_WIDTH + x] = BLACK;
        }
    }

    // Horizontal borders
    for (int i = -1; i <= BOARD_WIDTH; i++)
    {
        for (int thickness = 0; thickness < BLOCK_SIZE; thickness++)
        {
            // Top border
            VGA_PIXELS[((BOARD_START_Y - BLOCK_SIZE) + thickness) * SCREEN_WIDTH +
                       (BOARD_START_X + i * BLOCK_SIZE)] = WHITE;
            // Bottom border
            VGA_PIXELS[(BOARD_START_Y + BOARD_HEIGHT * BLOCK_SIZE + thickness) * SCREEN_WIDTH +
                       (BOARD_START_X + i * BLOCK_SIZE)] = WHITE;
        }
    }

    //Vertical borders
    for (int i = -1; i <= BOARD_HEIGHT; i++)
    {
        for (int thickness = 0; thickness < BLOCK_SIZE; thickness++)
        {
            // Left border
            VGA_PIXELS[(BOARD_START_Y + i * BLOCK_SIZE) * SCREEN_WIDTH +
                       (BOARD_START_X - BLOCK_SIZE + thickness)] = WHITE;
            // Right border
            VGA_PIXELS[(BOARD_START_Y + i * BLOCK_SIZE) * SCREEN_WIDTH +
                       (BOARD_START_X + BOARD_WIDTH * BLOCK_SIZE + thickness)] = WHITE;
        }
    }
}

/**
* @brief Creates and positions a new tetromino piece
* 
* 
* Piece generation function that:
* 1. Piece initialization:
*    - Randomly selects piece type (0-6)
*    - Sets initial rotation to 0
*    - Generates random movement direction (0-3)
* 
* 2. Position setup:
*    - Places piece at board center
*    - X offset: (BOARD_WIDTH / 2) - 2
*    - Y offset: (BOARD_HEIGHT / 2) - 2
*    - Accounts for 4x4 piece grid
* 
* 3. Direction assignment:
*    - Random direction from:
*      * DIR_DOWN (0)
*      * DIR_UP (1)
*      * DIR_LEFT (2)
*      * DIR_RIGHT (3)
* 
* 4. Game over detection:
*    - Checks for collision at spawn position
*    - Sets gameOver flag if collision occurs
* 
* Called:
* - At game start
* - After piece lock
* - After line clear
* 
* Critical for game progression and difficulty
*/
void spawn_piece(void)
{
    currentPiece.type = my_rand() % 7;
    currentPiece.rotation = 0;

    // Spawn in center of square board
    currentPiece.x = (BOARD_WIDTH / 2) - 2;
    currentPiece.y = (BOARD_HEIGHT / 2) - 2;

    // Random direction
    currentPiece.direction = my_rand() % 4;

    if (check_collision(&currentPiece))
    {
        gameOver = 1;
    }
}

/**
* @brief Fixes the current tetromino piece to the game board
* 
* 
* Piece locking function that:
* 1. Shape processing:
*    - Gets 16-bit shape pattern from TETROMINOS array
*    - Uses currentPiece type and rotation
*    - Processes each bit in 4x4 shape grid
* 
* 2. Board integration:
*    - Converts active piece to static board cells
*    - Maps piece type to color value (type + 1):
*      * I piece → CYAN (1)
*      * J piece → BLUE (2)
*      * L piece → ORANGE (3)
*      * O piece → YELLOW (4)
*      * S piece → GREEN (5)
*      * T piece → PURPLE (6)
*      * Z piece → RED (7)
* 
* 3. Position translation:
*    - Converts piece-relative coordinates to board coordinates
*    - Uses currentPiece.x and currentPiece.y as offsets
* 
* Called when:
* - Piece hits bottom/other pieces
* - Collision detected in current direction
* 
* Triggers:
* - Line clear checks
* - New piece spawn
*/
void lock_piece(void)
{
    uint16_t shape = TETROMINOS[currentPiece.type][currentPiece.rotation];
    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            if ((shape >> (15 - (y * 4 + x))) & 1)
            {
                board.cells[currentPiece.y + y][currentPiece.x + x] = currentPiece.type + 1;
            }
        }
    }
}

/**
* @brief Attempts to rotate the current piece 90 degrees clockwise
* 
* 
* Piece rotation function that:
* 1. Rotation state management:
*    - Stores current rotation state
*    - Advances rotation by 1 position
*    - Uses modulo 4 to cycle through states (0->1->2->3->0)
* 
* 2. Collision validation:
*    - Checks if rotated position is valid
*    - Uses check_collision() for validation
* 
* 3. Rotation recovery:
*    - Restores previous rotation if collision detected
*    - Implements simple wall kick system
* 
* Called when:
* - Button press detected
* - User requests rotation
* 
* Core gameplay mechanic for piece manipulation
* Simple but effective rotation system
*/
void rotate_piece(void)
{
    int oldRotation = currentPiece.rotation;
    currentPiece.rotation = (currentPiece.rotation + 1) % 4;
    if (check_collision(&currentPiece))
    {
        currentPiece.rotation = oldRotation;
    }
}

/**
* @brief Applies quad-directional gravity effects after line clears
* 
* @param clearedRow Index of cleared horizontal line (-1 if none)
* @param clearedCol Index of cleared vertical line (-1 if none)
* 
* Complex gravity simulation function that:
* 1. Coordinate system:
*    - Uses board center as gravity pivot point
*    - centerX = BOARD_WIDTH / 2
*    - centerY = BOARD_HEIGHT / 2
*    - Divides board into four quadrants
* 
* 2. Horizontal line clear handling:
*    - Above center:
*      * Blocks fall upward
*      * Scans from y=1 to centerY
*      * Moves blocks into empty spaces above
*    - Below center:
*      * Blocks fall downward
*      * Scans from bottom up to centerY
*      * Moves blocks into empty spaces below
* 
* 3. Vertical line clear handling:
*    - Top-right quadrant:
*      * Blocks fall rightward
*      * Scans from right edge to centerX
*      * Y range: 0 to centerY
*    - Top-left quadrant:
*      * Blocks fall leftward
*      * Scans from left edge to centerX
*      * Y range: 0 to centerY
*    - Bottom-right quadrant:
*      * Blocks fall rightward
*      * Scans from right edge to centerX
*      * Y range: centerY to BOARD_HEIGHT
*    - Bottom-left quadrant:
*      * Blocks fall leftward
*      * Scans from left edge to centerX
*      * Y range: centerY to BOARD_HEIGHT
* 
* 4. Animation handling:
*    - Tracks number of block movements
*    - Redraws board after each iteration
*    - 50ms delay between frames
*    - Continues until no more movements possible
* 
* Called after:
* - Line clear detection
* - Before spawning new piece
* 
* Creates unique gameplay mechanic with
* quad-directional gravity based on board position
*/
void apply_gravity(int clearedRow, int clearedCol)
{
    int changes;
    int centerX = BOARD_WIDTH / 2;
    int centerY = BOARD_HEIGHT / 2;

    do
    {
        changes = 0;

        // If a row was cleared (horizontal line clear)
        if (clearedRow != -1)
        {
            // For blocks above center, fall upward
            if (clearedRow < centerY)
            {
                for (int y = 1; y < centerY; y++)
                {
                    for (int x = 0; x < BOARD_WIDTH; x++)
                    {
                        if (board.cells[y][x] != BLACK && board.cells[y - 1][x] == BLACK)
                        {
                            board.cells[y - 1][x] = board.cells[y][x];
                            board.cells[y][x] = BLACK;
                            changes++;
                        }
                    }
                }
            }
            // For blocks below center, fall downward
            else
            {
                for (int y = BOARD_HEIGHT - 2; y >= centerY; y--)
                {
                    for (int x = 0; x < BOARD_WIDTH; x++)
                    {
                        if (board.cells[y][x] != BLACK && board.cells[y + 1][x] == BLACK)
                        {
                            board.cells[y + 1][x] = board.cells[y][x];
                            board.cells[y][x] = BLACK;
                            changes++;
                        }
                    }
                }
            }
        }

        // If a column was cleared (vertical line clear)
        if (clearedCol != -1)
        {
            // Top-right quadrant: fall right
            if (clearedCol >= centerX)
            {
                for (int x = BOARD_WIDTH - 2; x >= centerX; x--)
                {
                    for (int y = 0; y < centerY; y++)
                    {
                        if (board.cells[y][x] != BLACK && board.cells[y][x + 1] == BLACK)
                        {
                            board.cells[y][x + 1] = board.cells[y][x];
                            board.cells[y][x] = BLACK;
                            changes++;
                        }
                    }
                }
            }
            // Top-left quadrant: fall left
            else
            {
                for (int x = 1; x < centerX; x++)
                {
                    for (int y = 0; y < centerY; y++)
                    {
                        if (board.cells[y][x] != BLACK && board.cells[y][x - 1] == BLACK)
                        {
                            board.cells[y][x - 1] = board.cells[y][x];
                            board.cells[y][x] = BLACK;
                            changes++;
                        }
                    }
                }
            }

            // Bottom-right quadrant: fall right
            if (clearedCol >= centerX)
            {
                for (int x = BOARD_WIDTH - 2; x >= centerX; x--)
                {
                    for (int y = centerY; y < BOARD_HEIGHT; y++)
                    {
                        if (board.cells[y][x] != BLACK && board.cells[y][x + 1] == BLACK)
                        {
                            board.cells[y][x + 1] = board.cells[y][x];
                            board.cells[y][x] = BLACK;
                            changes++;
                        }
                    }
                }
            }
            // Bottom-left quadrant: fall left
            else
            {
                for (int x = 1; x < centerX; x++)
                {
                    for (int y = centerY; y < BOARD_HEIGHT; y++)
                    {
                        if (board.cells[y][x] != BLACK && board.cells[y][x - 1] == BLACK)
                        {
                            board.cells[y][x - 1] = board.cells[y][x];
                            board.cells[y][x] = BLACK;
                            changes++;
                        }
                    }
                }
            }
        }

        // Redraw after each iteration to show the falling animation
        if (changes > 0)
        {
            draw_board();
            delay(50); // Add a small delay to make the falling animation visible
        }

    } while (changes > 0); // Keep applying gravity until no more changes occur
}

/**
* @brief Checks for and processes completed lines, updates score and speed
* 
* 
* Comprehensive line clearing function that:
* 1. Line detection:
*    - Checks horizontal rows and vertical columns
*    - Tracks number of lines cleared simultaneously
*    - Records positions of last cleared row/column
*    - Marks complete lines by setting cells to BLACK
* 
* 2. Scoring system:
*    - SCORE_SINGLE (100): 1 line
*    - SCORE_DOUBLE (300): 2 lines
*    - SCORE_TRIPLE (500): 3 lines
*    - SCORE_TETRIS (800): 4 lines
* 
* 3. Speed adjustment:
*    - Decreases timer period based on:
*      * Current score
*      * Number of lines cleared
*      * Base multiplier of 400
*    - Has minimum speed threshold of 1000
*    - Updates timer hardware registers:
*      * TIMER_PERIODL: Lower 16 bits
*      * TIMER_PERIODH: Upper 16 bits
* 
* 4. Follow-up actions:
*    - Triggers gravity effects if lines cleared
*    - Updates score display
*    - Redraws game board
* 
* Called after:
* - Piece locking
* - Before spawning new piece
* 
* Core scoring and difficulty progression mechanic
* Unique vertical line clear feature
*/
void check_lines(void)
{
    int linesCleared = 0;
    int lastClearedRow = -1;
    int lastClearedCol = -1;

    // Check horizontal lines
    for (int y = 0; y < BOARD_HEIGHT; y++)
    {
        int complete = 1;
        for (int x = 0; x < BOARD_WIDTH; x++)
        {
            if (board.cells[y][x] == BLACK)
            {
                complete = 0;
                break;
            }
        }

        if (complete)
        {
            linesCleared++;
            lastClearedRow = y;
            // Clear the completed row
            for (int x = 0; x < BOARD_WIDTH; x++)
            {
                board.cells[y][x] = BLACK;
            }
        }
    }

    // Check vertical lines
    for (int x = 0; x < BOARD_WIDTH; x++)
    {
        int complete = 1;
        for (int y = 0; y < BOARD_HEIGHT; y++)
        {
            if (board.cells[y][x] == BLACK)
            {
                complete = 0;
                break;
            }
        }

        if (complete)
        {
            linesCleared++;
            lastClearedCol = x;
            // Clear the completed column
            for (int y = 0; y < BOARD_HEIGHT; y++)
            {
                board.cells[y][x] = BLACK;
            }
        }
    }

    // Apply gravity effects if any lines were cleared
    if (lastClearedRow != -1 || lastClearedCol != -1)
    {
        apply_gravity(lastClearedRow, lastClearedCol);
    }

    // Update score
    switch (linesCleared)
    {
    case 1:
        score += SCORE_SINGLE;
        if (speed > 1000)
        {
            speed -= score * 400;
        }

        break;
    case 2:
        score += SCORE_DOUBLE;
        if (speed > 1000)
        {
            speed -= score * 400 * 2;
        }
        break;
    case 3:
        score += SCORE_TRIPLE;
        if (speed > 1000)
        {
            speed -= score * 400 * 3;
        }
        break;
    case 4:
        score += SCORE_TETRIS;
        if (speed > 1000)
        {
            speed -= score * 400 * 4;
        }
        break;
    }

    int periodLow = (speed) & 0xFFFF;
    int periodHigh = (speed >> 16) & 0xFFFF;
    *TIMER_PERIODL = periodLow;
    *TIMER_PERIODH = periodHigh;

    if (linesCleared > 0)
    {
        draw_score();
        draw_board();
    }
}

/**
* @brief Processes state changes in hardware switches for piece direction
* 
* 
* Complex input handling function that:
* 1. Reads current state of all switches from hardware address
* 2. Uses XOR operation to detect changed switches since last check
* 3. Handles four directional switches (RIGHT, LEFT, UP, DOWN)
* 4. Changes piece direction only if different from current direction
* 5. Implements priority order: RIGHT > LEFT > UP > DOWN
* 6. Updates lastSwitchState for next comparison
* 
* Hardware specific details:
* - Uses SWITCH_ADDRESS memory-mapped IO
* - Masks with 0x3FF to read 10 switch bits
* - Uses predefined direction constants (DIR_RIGHT, DIR_LEFT, etc.)
* 
* Switch mappings:
* - SWITCH_RIGHT (0x1): Change direction to right
* - SWITCH_LEFT (0x2): Change direction to left 
* - SWITCH_UP (0x8): Change direction to up
* - SWITCH_DOWN (0x4): Change direction to down
* 
* Called every game tick to check for direction changes
* Critical for implementing unique quad-directional gameplay mechanic
*/
void handle_switch_changes(void)
{
    int currentSwitches = *SWITCH_ADDRESS & 0x3FF;
    int switchChanges = currentSwitches ^ lastSwitchState;

    if (switchChanges)
    {
        // Check if the right switch changed state (either on->off or off->on)
        if (switchChanges & SWITCH_RIGHT)
        {
            if (currentPiece.direction != DIR_RIGHT)
            {
                currentPiece.direction = DIR_RIGHT;
            }
        }
        // Check if the left switch changed state (either on->off or off->on)
        else if (switchChanges & SWITCH_LEFT)
        {
            if (currentPiece.direction != DIR_LEFT)
            {
                currentPiece.direction = DIR_LEFT;
            }
        }
        // Check if the up switch changed state (either on->off or off->on)
        else if (switchChanges & SWITCH_UP)
        {
            if (currentPiece.direction != DIR_UP)
            {
                currentPiece.direction = DIR_UP;
            }
        }
        // Check if the down switch changed state (either on->off or off->on)
        else if (switchChanges & SWITCH_DOWN)
        {
            if (currentPiece.direction != DIR_DOWN)
            {
                currentPiece.direction = DIR_DOWN;
            }
        }
    }

    lastSwitchState = currentSwitches;
}

/**
* @brief Handles timer interrupt by setting the interrupt flag and updating timeout counter
* @param cause The interrupt cause value (unused but required by hardware interface)
*
* Timer interrupt handler that:
* 1. Sets the first bit of the interrupt flag at memory address 0x04000024
* 2. Increments the global timeoutcount used for game timing/piece movement
*/
void handle_interrupt(unsigned cause)
{
    volatile int *interruptFlag = (volatile int *)0x04000024;
    *interruptFlag |= 0x1;
    timeoutcount++;
}

/**
* @brief Processes button input for piece rotation with debouncing
*
* Input handler that:
* 1. Reads button state from hardware address
* 2. Detects rising edge (button press) using lastButtonState
* 3. Triggers piece rotation on button press
* 4. Updates lastButtonState for next check
*/
void handle_input(void)
{
    int button = *BUTTON_ADDRESS & 0x1;

    if (button && !lastButtonState)
    {
        rotate_piece();
    }
    lastButtonState = button;
}

/**
* @brief Handles automatic piece movement based on current direction
* 
* 
* Core movement function that:
* 1. Direction-based movement:
*    - DOWN: Increments Y position
*    - UP: Decrements Y position
*    - LEFT: Decrements X position
*    - RIGHT: Increments X position
* 
* 2. Collision handling:
*    - Checks for collisions after movement
*    - If collision detected:
*      * Reverts movement in opposite direction
*      * Locks piece in last valid position
*      * Checks for completed lines
*      * Updates board display
*      * Spawns new piece
* 
* Called:
* - On timer tick
* - When timeoutcount reaches threshold (20)
* 
* Ensures consistent game pace
* Handles end of piece lifecycle
* Core game loop mechanic
*/
void handle_tick_movement(void)
{
    switch (currentPiece.direction)
    {
    case DIR_DOWN:
        currentPiece.y++;
        break;
    case DIR_UP:
        currentPiece.y--;
        break;
    case DIR_LEFT:
        currentPiece.x--;
        break;
    case DIR_RIGHT:
        currentPiece.x++;
        break;
    }

    if (check_collision(&currentPiece))
    {
        // Undo movement
        switch (currentPiece.direction)
        {
        case DIR_DOWN:
            currentPiece.y--;
            break;
        case DIR_UP:
            currentPiece.y++;
            break;
        case DIR_LEFT:
            currentPiece.x++;
            break;
        case DIR_RIGHT:
            currentPiece.x--;
            break;
        }
        lock_piece();
        check_lines();
        draw_board();
        spawn_piece();
    }
}

/**
* @brief Initializes game timer hardware with configured speed
*
* Timer initialization sequence:
* 1. Disables timer by clearing control register
* 2. Splits 32-bit speed value into two 16-bit periods
* 3. Sets low and high period registers
* 4. Enables timer with control value 0x7 (enables timer, interrupts, and continuous mode)
*/
void init_timer(void)
{
    *TIMER_CONTROL = 0;

    int periodLow = speed & 0xFFFF;
    int periodHigh = (speed >> 16) & 0xFFFF;

    *TIMER_PERIODL = periodLow;
    *TIMER_PERIODH = periodHigh;
    *TIMER_CONTROL = 0x7;
}

/**
* @brief Renders a large 12x12 character on the VGA display
* 
* @param x X-coordinate for character placement
* @param y Y-coordinate for character placement
* @param c Character to draw ('G','A','M','E','O','V','R',' ')
* @param color Color index for the character
* 
* Sophisticated bitmap character rendering function that:
* 1. Uses predefined 12x12 bitmap patterns for each supported character
*    - Each character defined by 12 rows of 16-bit patterns
*    - Supports "GAME OVER" text characters plus space
*    - Patterns optimized for readability at large size
* 
* 2. Character drawing process:
*    - Converts game color to VGA-compatible color
*    - Maps input character to correct pattern index
*    - Renders pattern bit-by-bit to screen buffer
*    - Handles screen boundary checking
*
* 3. Pattern details:
*    - Each character uses 12x12 pixel grid
*    - Patterns stored as 16-bit integers (12 per character)
*    - MSB defines leftmost pixel
*    - Bit 1 = pixel on, Bit 0 = pixel off
* 
* 4. Supported characters:
*    - G: Index 0 (Game)
*    - A: Index 1 (Game)
*    - M: Index 2 (Game)
*    - E: Index 3 (Game/Over)
*    - O: Index 5 (Over)
*    - V: Index 6 (Over)
*    - R: Index 8 (Over)
*    - Space: Index 4
*
* Used primarily for game over screen display
* Ensures consistent large text rendering across supported characters
*/
void draw_large_char(int x, int y, char c, char color)
{
    // Simple, reliable patterns for each character (12x12 bitmap patterns)
    const uint16_t charPatterns[][12] = {
        {// G
         0x0F80, 0x1FC0, 0x3060, 0x2020, 0x2000, 0x2380,
         0x2380, 0x2020, 0x2020, 0x3060, 0x1FC0, 0x0F80},
        {// A
         0x0F00, 0x1F80, 0x3180, 0x2080, 0x2080, 0x3F80,
         0x3F80, 0x2080, 0x2080, 0x2080, 0x2080, 0x2080},
        {// M
         0x2020, 0x3060, 0x3FE0, 0x3FE0, 0x2920, 0x2120,
         0x2020, 0x2020, 0x2020, 0x2020, 0x2020, 0x2020},
        {// E
         0x3FE0, 0x3FE0, 0x2000, 0x2000, 0x2000, 0x3F80,
         0x3F80, 0x2000, 0x2000, 0x2000, 0x3FE0, 0x3FE0},
        {// space
         0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
         0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
        {// O
         0x0F80, 0x1FC0, 0x3060, 0x2020, 0x2020, 0x2020,
         0x2020, 0x2020, 0x2020, 0x3060, 0x1FC0, 0x0F80},
        {// V
         0x2020, 0x2020, 0x2020, 0x2020, 0x2020, 0x1140,
         0x1140, 0x0A80, 0x0A80, 0x0500, 0x0500, 0x0200},
        {// E (reuse)
         0x3FE0, 0x3FE0, 0x2000, 0x2000, 0x2000, 0x3F80,
         0x3F80, 0x2000, 0x2000, 0x2000, 0x3FE0, 0x3FE0},
        {// R
         0x3F80, 0x3FC0, 0x2060, 0x2060, 0x2060, 0x3FC0,
         0x3F80, 0x3060, 0x2860, 0x2460, 0x2260, 0x2130}};

    // Use the get_vga_color function to convert the game color to VGA color
    char vgaColor = get_vga_color(color);

    // Map character to pattern index
    int patternIndex;
    switch (c)
    {
    case 'G':
        patternIndex = 0;
        break;
    case 'A':
        patternIndex = 1;
        break;
    case 'M':
        patternIndex = 2;
        break;
    case 'E':
        patternIndex = 3;
        break;
    case ' ':
        patternIndex = 4;
        break;
    case 'O':
        patternIndex = 5;
        break;
    case 'V':
        patternIndex = 6;
        break;
    case 'R':
        patternIndex = 8;
        break;
    default:
        return;
    }

    // Draw the character line by line
    for (int row = 0; row < LARGE_CHAR_HEIGHT; row++)
    {
        uint16_t pattern = charPatterns[patternIndex][row];
        for (int col = 0; col < LARGE_CHAR_WIDTH; col++)
        {
            //Using the leftmost 12 of 16 bits. 
            if (pattern & (0x8000 >> col))
            {
                //Calulates screen position
                int pixelX = x + col;
                int pixelY = y + row;

                //Checks screen boundaries
                if (pixelX >= 0 && pixelX < SCREEN_WIDTH &&
                    pixelY >= 0 && pixelY < SCREEN_HEIGHT)
                {
                    VGA_PIXELS[pixelY * SCREEN_WIDTH + pixelX] = vgaColor; // Use the converted VGA color
                }
            }
        }
    }
}

/**
* @brief Displays the game over screen with final score
* * 
* 
* Comprehensive end-game display function that:
* 1. Screen preparation:
*    - Clears entire screen to black background
*    - Calculates centered positions for text and score
* 
* 2. "GAME OVER" text rendering:
*    - Uses large 12x12 characters in RED color
*    - Centers text horizontally using GAME_OVER_X constant
*    - Places text vertically using GAME_OVER_Y constant
*    - Adds 2-pixel spacing between characters
* 
* 3. Score processing:
*    - Handles scores from 0 to 99999 (5 digits)
*    - Converts numeric score to individual digits
*    - Special handling for zero score case
*    - Practical limit ~1000 rows/columns for 6-digit score
* 
* 4. Score display:
*    - Uses 5x7 pixel digit patterns from DIGIT_PATTERNS
*    - Centers score below "GAME OVER" text
*    - Renders in WHITE color
*    - Places 20 pixels below main text
*    - Maintains consistent digit spacing
* 
* Called once when game ends (collision detection fails)
* Remains displayed until game restart triggered
*/
void draw_game_over(void)
{
    const char *text = "GAME OVER";
    int x = GAME_OVER_X;

    // Clear screen first
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            VGA_PIXELS[y * SCREEN_WIDTH + x] = BLACK;
        }
    }

    // Draw each character in "GAME OVER"
    for (int i = 0; text[i] != '\0'; i++)
    {
        draw_large_char(x, GAME_OVER_Y, text[i], RED);
        x += LARGE_CHAR_WIDTH + 2; // Move right for next char with 2-pixel spacing
    }

    // Convert score to digits
    char digits[6]; // Max 5 digits + null terminator
    int tempScore = score;
    int digitCount = 0;

    // Handle zero score case
    if (tempScore == 0)
    {
        digits[digitCount++] = '0';
    }

    // Convert score to digits, the player would need to break approximately 1000 rows/columns to reach 6 digit count score.
    while (tempScore > 0 && digitCount < 5)
    {
        digits[digitCount++] = '0' + (tempScore % 10);
        tempScore /= 10;
    }

    // Calculate center position for the score
    int scoreWidth = digitCount * (DIGIT_WIDTH + 1);
    int scoreX = (SCREEN_WIDTH - scoreWidth) / 2;
    int scoreY = GAME_OVER_Y + LARGE_CHAR_HEIGHT + 20;

    // Draw score digits using same pattern as draw_score function
    for (int i = digitCount - 1; i >= 0; i--)
    {
        int digit = digits[i] - '0'; // Convert char to number
        unsigned long pattern = DIGIT_PATTERNS[digit];

        for (int y = 0; y < DIGIT_HEIGHT; y++)
        {
            for (int x = 0; x < DIGIT_WIDTH; x++)
            {
                // Using the same bit pattern calculation as draw_score()
                if (pattern & (1UL << (34 - (y * 5 + x))))
                { 
                    //Absolute screen position coordinates
                    int pixelX = scoreX + x;
                    int pixelY = scoreY + y;

                    if (pixelX >= 0 && pixelX < SCREEN_WIDTH &&
                        pixelY >= 0 && pixelY < SCREEN_HEIGHT)
                    {
                        VGA_PIXELS[pixelY * SCREEN_WIDTH + pixelX] = WHITE;
                    }
                }
            }
        }
        scoreX += DIGIT_WIDTH + 1;
    }
}

/* Main game loop */
int main(void)
{
game_start: // Label for restarting the game
    print("Starting Tetris...\n");

    randState = *TIMER_STATUS; // Use whatever value is in the timer as our seed
    init_timer();

    // Reset game variables
    speed = startSpeed;
    gameOver = 0;
    score = 0;
    timeoutcount = 0;
    lastButtonState = 0;
    lastSwitchState = *SWITCH_ADDRESS & 0x3FF;

    // Reset update frequency (game speed)
    int periodLow = (speed) & 0xFFFF;
    int periodHigh = (speed >> 16) & 0xFFFF;
    *TIMER_PERIODL = periodLow;
    *TIMER_PERIODH = periodHigh;

    // Initial screen clear
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++)
    {
        VGA_PIXELS[i] = BLACK;
    }

    init_board();
    spawn_piece();
    draw_board();
    draw_score();

    while (!gameOver)
    {
        // Only clear the previous piece position
        uint16_t oldShape = TETROMINOS[currentPiece.type][currentPiece.rotation];
        for (int y = 0; y < 4; y++)
        {
            for (int x = 0; x < 4; x++)
            {
                if ((oldShape >> (15 - (y * 4 + x))) & 1)
                {
                    draw_block(currentPiece.x + x, currentPiece.y + y, BLACK);
                }
            }
        }

        handle_input();
        handle_switch_changes();

        if (*TIMER_STATUS & 0x1)
        {
            timeoutcount++;
            *TIMER_STATUS &= ~0x1;

            if (timeoutcount >= 20)
            {
                timeoutcount = 0;
                handle_tick_movement();
            }
        }

        draw_current_piece();

        *(VGA_CTRL + 1) = (uint32_t)(uintptr_t)VGA_PIXELS;
        *(VGA_CTRL + 0) = 0;

        delay(10);
    }

    // Stop timer interrupts
    *TIMER_CONTROL = 0;

    // Clear the screen with a fade effect
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            VGA_PIXELS[y * SCREEN_WIDTH + x] = BLACK;
        }
        delay(10); // Slow fade effect
    }

    // Draw the game over screen
    draw_game_over();

    // Display game over message
    print("Game Over! Final Score: ");
    print_dec(score);
    print("\n");
    print("Press button to restart\n");

    // Wait for button press, with debounce
    int restart_button_state = 0;
    while (1)
    {
        int current_button = *BUTTON_ADDRESS & 0x1;

        // Check for button press (transition from 0 to 1)
        if (current_button && !restart_button_state)
        {
            delay(50); // Debounce delay
            if (*BUTTON_ADDRESS & 0x1)
            {                    // Check if button is still pressed
                goto game_start; // Restart the game
            }
        }
        restart_button_state = current_button;
        delay(10);
    }

    return 0; // This line will never be reached
}