#pragma once

// Frogger Game
// An implementation of the game Frogger!

typedef struct frogger_game_t frogger_game_t;

typedef struct fs_t fs_t;
typedef struct heap_t heap_t;
typedef struct render_t render_t;
typedef struct wm_window_t wm_window_t;

// Create an instance of Frogger.
frogger_game_t* frogger_game_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render);

// Destroy an instance of Frogger.
void frogger_game_destroy(frogger_game_t* game);

// Per-frame update for Frogger.
void frogger_game_update(frogger_game_t* game);
