#pragma once

// Lua Interface
// Prepares a C / Lua interface to allow us to develop games in Lua

typedef struct lua_project_t lua_project_t;

typedef struct fs_t fs_t;
typedef struct heap_t heap_t;
typedef struct render_t render_t;
typedef struct wm_window_t wm_window_t;

// Create a Lua project using descendant files found at path lua_src
lua_project_t* lua_project_create(const char* lua_src, heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render);

// Per-frame update for a Lua project.
void lua_project_update(lua_project_t* lp);

// Destroy an instance of a Lua project.
void lua_project_destroy(lua_project_t* lp);
