#pragma once

#include "lua-5.4.4/src/lua.h"
#include "lua-5.4.4/src/lauxlib.h"
#include "lua-5.4.4/src/lualib.h"
#include "gpu.h"
#include "transform.h"
#include "mat4f.h"

// Base Components
typedef struct transform_component_t
{
    transform_t transform;
} transform_component_t;

typedef struct camera_component_t
{
    mat4f_t projection;
    mat4f_t view;
} camera_component_t;

typedef struct model_component_t
{
    gpu_mesh_info_t* mesh_info;
    gpu_shader_info_t* shader_info;
} model_component_t;

typedef struct player_component_t
{
    int index;
    float speed;
} player_component_t;

typedef struct traffic_component_t
{
    int index;
    bool moving_left;
    float speed;
} traffic_component_t;

typedef struct name_component_t
{
    char name[32];
} name_component_t;

// Sets up components with their metamethods (__index and __newindex)
int lua_prepare_components(lua_State* L);