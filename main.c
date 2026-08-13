#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

#define FRAME_MS 120
#define BOX_X0 60
#define BOX_Y0 10

#define BOX_H "─"
#define BOX_V "│"
#define BOX_S " "
#define BOX_TL "┌"
#define BOX_TR "┐"
#define BOX_BL "└"
#define BOX_BR "┘"

#define C_RESET "\033[0m"
#define C_GREEN "\033[32m"
#define C_RED   "\033[31m"
#define C_CYAN  "\033[36m"

#define SNAKE C_GREEN "$" C_RESET
#define FOOD  C_RED   "o" C_RESET

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static struct termios orig;

enum { ROWS = 12, COLS = 20, BODY_MAX = ROWS * COLS };

typedef struct { int x, y; } Pos;

typedef struct {
  Pos body[BODY_MAX];
  Pos food;
  int length;
  int high_score;
  int dirX, dirY;
  bool running;
  bool title;
} Game;

enum input {
  INPUT_NONE = 0,
  INPUT_QUIT,
  INPUT_UP, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT,
};

#define TITLE_W 38   /* widest line in title_screen, in columns */
#define SUBTITLE_W 35   /* widest line in subtitle, in columns */
#define TITLE_GAP 1   /* blank rows between the title and the subtitle */

static const char *const title_screen[] = {
"  _________              __           ",
"/   _____/ ____ _____  |  | __ ____  ",
" \\_____  \\ /    \\\\__  \\ |  |/ // __ \\ ",
" /        \\   |  \\/ __ \\|    <\\  ___/ ",
"/_______  /___|  (____  /__|_ \\\\___  >",
"        \\/     \\/     \\/     \\/    \\/ ",
};

static const char *const subtitle[] = {
"                          ▌        ",
"▛▀▖▙▀▖▞▀▖▞▀▘▞▀▘ ▝▀▖▛▀▖▌ ▌ ▌▗▘▞▀▖▌ ▌",
"▙▄▘▌  ▛▀ ▝▀▖▝▀▖ ▞▀▌▌ ▌▚▄▌ ▛▚ ▛▀ ▚▄▌",
"▌  ▘  ▝▀▘▀▀ ▀▀  ▝▀▘▘ ▘▗▄▘ ▘ ▘▝▀▘▗▄▘",
};

static void draw_block(int row, int col, const char *const *lines, int count)
{
  for (int i = 0; i < count; i++)
    printf("\033[%d;%dH%s", row + i, col, lines[i]);
}

static void draw_title(void)
{
  int title_h = (int)ARRAY_SIZE(title_screen);
  int sub_h   = (int)ARRAY_SIZE(subtitle);
  int row     = BOX_Y0 + 1 + (ROWS - (title_h + TITLE_GAP + sub_h)) / 2;

  draw_block(row, BOX_X0 + 1 + (COLS - TITLE_W) / 2, title_screen, title_h);
  draw_block(row + title_h + TITLE_GAP,
             BOX_X0 + 1 + (COLS - SUBTITLE_W) / 2, subtitle, sub_h);
}

void restoreTerminal(void)
{
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
  printf("\033[?25h");
  fflush(stdout);
}

static enum input read_input(void)
{
  enum input last = INPUT_NONE;
  unsigned char c;

  while (read(STDIN_FILENO, &c, 1) > 0) {
    switch (c) {
      case 'q':           last = INPUT_QUIT;  break;
      case 'w': case 'k': last = INPUT_UP;    break;
      case 's': case 'j': last = INPUT_DOWN;  break;
      case 'a': case 'h': last = INPUT_LEFT;  break;
      case 'd': case 'l': last = INPUT_RIGHT; break;
      default:            break;
    }
  }
  return last;
}

void applyInput(Game *g, enum input in)
{
  switch (in) {
    case INPUT_QUIT:
      g->running = false;
      break;
    case INPUT_UP:
      g->dirX = 0;
      g->dirY = -1;
      g->title = false;
      break;
    case INPUT_DOWN:
      g->dirX = 0;
      g->dirY = 1;
      g->title = false;
      break;
    case INPUT_LEFT:
      g->dirX = -1;
      g->dirY = 0;
      g->title = false;
      break;
    case INPUT_RIGHT:
      g->dirX = 1;
      g->dirY = 0;
      g->title = false;
      break;
    case INPUT_NONE:
      break;
    default:
      break;
  }
}

int load_high_score(void)
{
  FILE *f = fopen("highscore.txt", "r");
  if (!f)
    return 0;

  char buf[32];
  if (!fgets(buf, sizeof buf, f)) {
    fclose(f);
    return 0;
  }
  char *end;
  long v = strtol(buf, &end, 10);
  if (end == buf) {
    fclose(f);
    return 0;
  }
  fclose(f);

  if (v < 0)        v = 0;
  if (v > BODY_MAX) v = BODY_MAX;

  return v;
}

void save_high_score(int score)
{
  FILE *f = fopen("highscore.txt", "w");
  if (!f)
    return;
  fprintf(f, "%d\n", score);
  fclose(f);
}

void resetSnake(Game *g)
{
  memset(g->body, 0, sizeof g->body);
  g->body[0].x = COLS/2;
  g->body[0].y = ROWS/2;

  g->dirX = 0;
  g->dirY = 0;

  if (g->length > g->high_score) {
    g->high_score = g->length;
    save_high_score(g->high_score);
  }

  g->length = 1;
}

void resetFood(Game *g)
{
  g->food = (Pos){ .x = rand() % COLS, .y = rand() % ROWS };
}

void growSnake(Game *g)
{
  if (g->length >= BODY_MAX) {
    resetSnake(g);
    resetFood(g);
    return;
  }
  g->body[g->length] = g->body[g->length-1];
  g->length += 1;
}

void updateSnake(Game *g)
{
  for (int i = g->length - 1; i > 0; i--) {
    g->body[i].x = g->body[i-1].x;
    g->body[i].y = g->body[i-1].y;
  }
  g->body[0].x += g-> dirX;
  g->body[0].y += g-> dirY;

  for (int i = g->length - 1; i > 0; i--) {
    if (g->body[i].x == g->body[0].x && g->body[i].y == g->body[0].y) {
      resetSnake(g);
      resetFood(g);
      break;
    }
  }

  if (g->body[0].x >= COLS || g->body[0].x < 0 || g->body[0].y >= ROWS || g->body[0].y < 0) {
    resetSnake(g);
    resetFood(g);
  }

  if (g->body[0].x == g->food.x && g->body[0].y == g->food.y) {
    resetFood(g);
    growSnake(g);
  }
}

static void draw_border(int row, const char *left, const char *mid, const char *right)
{ 
  printf("\033[2K\033[%d;%dH", row, BOX_X0);
  fputs(left, stdout);
  for (int i = 0; i < COLS; i++)
    fputs(mid, stdout);
  fputs(right, stdout);
  fputs("\033[K\n", stdout);
}

int main(void)
{
  printf("\033[2J\033[?25l"); // clear screen and hide cursor
  atexit(restoreTerminal);

  struct timespec frame = { .tv_sec = 0, .tv_nsec = FRAME_MS * 1000000L };

  tcgetattr(STDIN_FILENO, &orig);
  struct termios raw = orig;
  raw.c_lflag &= ~(ICANON | ECHO);
  raw.c_cc[VMIN]   = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

  srand(time(NULL));

  Game game = {
    .body[0] = { .x = COLS - 3, .y = ROWS - 2 },
    .food = { .x = 3, .y = 1 },
    .length = 1,
    .high_score = load_high_score(),
    .dirX = 0,
    .dirY = 0,
    .running = true,
    .title = true,
  };

  while (game.running) {
    applyInput(&game, read_input());

    printf("\033[H"); // moves cursor to top left corner


    int row = BOX_Y0;
    draw_border(row++, BOX_TL, BOX_H, BOX_TR);
    for (int i = 0; i < ROWS; i++)
      draw_border(row++, BOX_V, BOX_S, BOX_V);
    draw_border(row++, BOX_BL, BOX_H, BOX_BR);
    printf("\033[%d;%dHScore: %d ", row++, BOX_X0, game.length);
    printf("\033[%d;%dHHigh Score: %d\n", row, BOX_X0, game.high_score);

    printf("\033[%d;%dH%s", game.food.y + 1 + BOX_Y0, game.food.x + 1 + BOX_X0, FOOD);

    for (int i = 0; i < game.length; i++) {
      printf("\033[%d;%dH%s", game.body[i].y + 1 + BOX_Y0, game.body[i].x + 1 + BOX_X0, SNAKE);
    }

    if (game.title)
      draw_title();

    fflush(stdout);

    updateSnake(&game);

    nanosleep(&frame, NULL);
  }
}
