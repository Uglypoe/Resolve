#include "components.h"

#include "lua-5.4.4/src/lua.h"
#include "lua-5.4.4/src/lauxlib.h"
#include "lua-5.4.4/src/lualib.h"
#include "gpu.h"
#include "transform.h"
#include "mat4f.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>



// Utility function to return component* from userdata
void* check_component(lua_State* L) {
    return *(void**)lua_touserdata(L, 1);
}



// Transform Component methods
enum { f_transform_x, f_transform_y, f_transform_z, f_transform_sx, f_transform_sy, f_transform_sz, f_transform_make_identity };
static const char* f_transform_map[] = { "x", "y", "z", "sx", "sy", "sz", "MakeIdentity"};

static int transform_comp_index(lua_State* L)
{
    transform_component_t* comp = check_component(L);

    switch (luaL_checkoption(L, 2, NULL, f_transform_map))
    {
    case f_transform_x: lua_pushnumber(L, comp->transform.translation.x); break;
    case f_transform_y: lua_pushnumber(L, comp->transform.translation.y); break;
    case f_transform_z: lua_pushnumber(L, comp->transform.translation.z); break;
    case f_transform_sx: lua_pushnumber(L, comp->transform.scale.x); break;
    case f_transform_sy: lua_pushnumber(L, comp->transform.scale.y); break;
    case f_transform_sz: lua_pushnumber(L, comp->transform.scale.z); break;
    case f_transform_make_identity: transform_identity(&comp->transform);  break;
    }

    return 1;
}

static int transform_comp_newindex(lua_State* L)
{
    transform_component_t* comp = check_component(L);
    float new_value = (float)luaL_checknumber(L, 3);

    switch (luaL_checkoption(L, 2, NULL, f_transform_map))
    {
    case f_transform_x: comp->transform.translation.x = new_value; break;
    case f_transform_y: comp->transform.translation.y = new_value; break;
    case f_transform_z: comp->transform.translation.z = new_value; break;
    case f_transform_sx: comp->transform.scale.x = new_value; break;
    case f_transform_sy: comp->transform.scale.y = new_value; break;
    case f_transform_sz: comp->transform.scale.z = new_value; break;
    case f_transform_make_identity: transform_identity(&comp->transform);  break;
    }
    return 1;
}



// Camera Component methods
static int camera_comp_make_ortho(lua_State* L)
{
    camera_component_t* comp = check_component(L);

    float left = (float)luaL_checknumber(L, 2);
    float right = (float)luaL_checknumber(L, 3);
    float bottom = (float)luaL_checknumber(L, 4);
    float top = (float)luaL_checknumber(L, 5);
    float n = (float)luaL_checknumber(L, 6);
    float f = (float)luaL_checknumber(L, 7);
    mat4f_make_orthographic(&comp->projection, left, right, bottom, top, n, f);

    vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
    vec3f_t forward = vec3f_forward();
    vec3f_t up = vec3f_up();
    mat4f_make_lookat(&comp->view, &eye_pos, &forward, &up);

    return 0;
}



// Player Component methods
enum { f_player_index, f_player_speed };
static const char* f_player_map[] = { "index", "speed" };

static int player_comp_index(lua_State* L)
{
    player_component_t* comp = check_component(L);

    switch (luaL_checkoption(L, 2, NULL, f_player_map))
    {
    case f_player_index: lua_pushinteger(L, comp->index); break;
    case f_player_speed: lua_pushnumber(L, comp->speed); break;
    }
    return 1;
}

static int player_comp_newindex(lua_State* L)
{
    player_component_t* comp = check_component(L);

    switch (luaL_checkoption(L, 2, NULL, f_player_map))
    {
    case f_player_index: comp->index = (int)luaL_checkinteger(L, 3); break;
    case f_player_speed: comp->speed = (float)luaL_checknumber(L, 3); break;
    }
    return 1;
}



// Traffic Component methods
enum { f_traffic_index, f_traffic_moving_left, f_traffic_speed };
static const char* f_traffic_map[] = { "index", "moving_left", "speed" };

static int traffic_comp_index(lua_State* L)
{
    traffic_component_t* comp = check_component(L);

    switch (luaL_checkoption(L, 2, NULL, f_traffic_map))
    {
    case f_traffic_index: lua_pushinteger(L, comp->index); break;
    case f_traffic_moving_left: lua_pushboolean(L, comp->moving_left); break;
    case f_traffic_speed: lua_pushnumber(L, comp->speed); break;
    }
    return 1;
}

static int traffic_comp_newindex(lua_State* L)
{
    traffic_component_t* comp = check_component(L);

    switch (luaL_checkoption(L, 2, NULL, f_traffic_map))
    {
    case f_traffic_index: comp->index = (int)luaL_checkinteger(L, 3); break;
    case f_traffic_moving_left: comp->moving_left = lua_toboolean(L, 3); break;
    case f_traffic_speed: comp->speed = (float)luaL_checknumber(L, 3); break;
    }
    return 1;
}



// Name Component methods
enum { f_namecomp_name };
static const char* f_namecomp_map[] = { "name" };

static int name_comp_index(lua_State* L)
{
    name_component_t* comp = check_component(L);

    switch (luaL_checkoption(L, 2, NULL, f_namecomp_map))
    {
    case f_namecomp_name: lua_pushstring(L, comp->name); break;
    }
    return 1;
}

static int name_comp_newindex(lua_State* L)
{
    name_component_t* comp = check_component(L);
    char* new_value = (char*)luaL_checkstring(L, 3);

    switch (luaL_checkoption(L, 2, NULL, f_namecomp_map))
    {
    case f_namecomp_name:
        memcpy(comp->name, new_value, sizeof(new_value));
        break;
    }
    return 1;
}




int lua_prepare_components(lua_State* L)
{
    // Inheritance would make a lot of sense here, if the __index and
    // __newindex methods could somehow be combined into just 2 methods
    // luaL_newmetatable(L, "Component");

    luaL_newmetatable(L, "TransformComponent");
    // Since the metatable is being replaced with a function, I'm not sure how to add a new method on top of it.
    // For the sake of time, I'll be using a different (and hacky) method to access MakeIdentity instead.
    //lua_pushcfunction(L, transform_comp_make_identity); lua_setfield(L, -2, "MakeIdentity");
    lua_pushcfunction(L, transform_comp_index); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, transform_comp_newindex); lua_setfield(L, -2, "__newindex");
    lua_pop(L, 1);

    luaL_newmetatable(L, "CameraComponent");
    lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, camera_comp_make_ortho); lua_setfield(L, -2, "MakeOrthographic");
    lua_pop(L, 1);

    luaL_newmetatable(L, "ModelComponent");
    lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_newmetatable(L, "PlayerComponent");
    lua_pushcfunction(L, player_comp_index); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, player_comp_newindex); lua_setfield(L, -2, "__newindex");
    lua_pop(L, 1);

    luaL_newmetatable(L, "TrafficComponent");
    lua_pushcfunction(L, traffic_comp_index); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, traffic_comp_newindex); lua_setfield(L, -2, "__newindex");
    lua_pop(L, 1);

    luaL_newmetatable(L, "NameComponent");
    lua_pushcfunction(L, name_comp_index); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, name_comp_newindex); lua_setfield(L, -2, "__newindex");
    lua_pop(L, 1);

    return 1;
}