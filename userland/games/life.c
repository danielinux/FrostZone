#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define X 60
#define Y 25
// Conway's Game of Life
// make conway && ./conway
// change X and Y for display area
typedef struct {
  int* cur;
  int* nxt;
} game_t;
void fmt_tick (const int* board) {
  const int* bd = board;
  int i, j;
  printf("\033[2J\033[1;1H");
  for (j = 1; j < Y; j++) {
    printf("\n");
    for (i = 0; i < X; i++) {
      printf("%s", bd[j*X+i]? "#": " ");
    }
  }
}
void clearboard (int* board) {
  int* bd = board;
  int i, j;
  for (j = 0; j < Y; j++) {
    for (i = 0; i < X; i++) {
      bd[j*X+i] = 0;
    }
  }
}
void tick (game_t* game) {
  int* cur = game->cur;
  int* nxt;
  int i, j, s;
  clearboard(game->nxt);
  nxt = game->nxt;
  s = 0;
  for (j = 1; j < (Y-1); j++) {
    for (i = 1; i < (X-1); i++) {
      s = 0;
      s += cur[(j-1)*(X) + (i-1)];
      s += cur[(j-1)*(X) + (i)];
      s += cur[(j-1)*(X) + (i+1)];
      s += cur[j*(X) + (i-1)];
      s += cur[j*(X) + (i+1)];
      s += cur[(j+1)*(X) + (i+1)];
      s += cur[(j+1)*(X) + (i)];
      s += cur[(j+1)*(X) + (i-1)];
      if (cur[j*X+i]) {
        if (s == 2 || s == 3) {
          nxt[j*X+i] = 1;
        }
      } else {
        if (s == 3) {
          nxt[j*X+i] = 1;
        }
      }
    }
  }
  fmt_tick(nxt);
  game->cur = nxt;
  game->nxt = cur;
}
void seed (int* board) {
  int* bd = board;
  int i, j, v;
  for (j = 1; j < Y; j++) {
    for (i = 1; i < X; i++) {
      v = rand() % 2;
      bd[j*X+i] = v? 0: 1;
    }
  }
}
int main () {
  game_t gm;
  game_t* game = &gm;
  size_t boardsz = X * Y * sizeof(int);
  int* seedboard = malloc(boardsz);
  int* gameboard = malloc(boardsz);
  printf("\033[2J\033[1;1H");
  if (seedboard == NULL || gameboard == NULL) {
    printf("life: failed to allocate %lu bytes for boards\n",
           (unsigned long)(boardsz * 2));
    free(seedboard);
    free(gameboard);
    return 1;
  }
  seed(seedboard);
  clearboard(gameboard);
  game->cur = seedboard;
  game->nxt = gameboard;
  while (1) {
    tick(game);
    printf("\n");
    usleep(300000);
  }
  free(seedboard);
  free(gameboard);
  return 0;
}
