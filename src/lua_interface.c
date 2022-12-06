#include "lua_interface.h"

#include "lua-5.4.4/src/lua.h"
#include "lua-5.4.4/src/lauxlib.h"
#include "lua-5.4.4/src/lualib.h"
#include "heap.h"
#include "gpu.h"
#include "ecs.h"
#include "fs.h"
#include "wm.h"
#include "render.h"
#include "timer_object.h"
#include "transform.h"
#include "components.h"

#include <direct.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DIR_PATH_MAX_LENGTH MAX_PATH


typedef struct lua_project_t
{
    lua_State* L;

    heap_t* heap;
    fs_t* fs;
    wm_window_t* window;
    render_t* render;

    timer_object_t* timer;

    ecs_t* ecs;

    int camera_type;
    int player_type;
    int transform_type;
    int model_type;

    gpu_mesh_info_t cube_mesh_green;
    gpu_mesh_info_t cube_mesh_red;
    gpu_shader_info_t cube_shader;
    fs_work_t* vertex_shader_work;
    fs_work_t* fragment_shader_work;
} lua_project_t;


static void load_resources(lua_project_t* lp);
static void unload_resources(lua_project_t* lp);
static void draw_models(lua_project_t* lp);


lua_project_t* get_project_from_state(lua_State* L)
{
    lua_getglobal(L, "lua_project");
    return lua_touserdata(L, lua_gettop(L));
}


// Wrapper to print error messages when a Lua file encounters an error
bool handle_lua_error(lua_State* L, int result)
{
    if (result != LUA_OK)
    {
        const char* error_msg = luaL_tolstring(L, -1, NULL);
        printf("%s\n", error_msg);
        return false;
    }
    return true;
}


// Lua file search & start
void run_lua_file(lua_State* L, const char* path)
{
    if (handle_lua_error(L, luaL_dofile(L, path)))
    {
        lua_pop(L, lua_gettop(L));
    }
}

const char* get_ext(const char* path) {
    // Returns a pointer to the last occurrence of a period in the string (aka file extension)
    char* ext = strrchr(path, '.');
    if (ext == NULL)
    {
        ext = "";
    }
    return ext;
}

void search_dir_for_lua_files(lua_State* L, const char* sDir)
{
    char sPath[DIR_PATH_MAX_LENGTH];
    sprintf_s(sPath, DIR_PATH_MAX_LENGTH, "%s/*.*", sDir);

    WIN32_FIND_DATA fdFile;
    HANDLE hFind;

    wchar_t basePath[DIR_PATH_MAX_LENGTH];
    mbstowcs_s(NULL, basePath, strlen(sPath) + 1, sPath, strlen(sPath));

    hFind = FindFirstFile(basePath, &fdFile);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        printf("Path not found: [%s]\n", sPath);
        return;
    }

    do
    {
        if (strcmp((char*)fdFile.cFileName, ".") != 0 // First file found (current directory)
            && strcmp((char*)fdFile.cFileName, "..") != 0) // Second file found (last directory)
        {
            sprintf_s(sPath, DIR_PATH_MAX_LENGTH, "%s/%ls", sDir, fdFile.cFileName);
            
            // If the file is a directory, we'll recursively search it as well
            if (fdFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                search_dir_for_lua_files(L, sPath);
            }
            else
            {
                // If this file is a .lua file, we'll attempt to run it
                if (strcmp(get_ext(sPath), ".lua") == 0)
                {
                    run_lua_file(L, sPath);
                }
            }
        }
    } while (FindNextFile(hFind, &fdFile));

    FindClose(hFind);
}


// Entities and Components
static ecs_entity_ref_t* check_entity(lua_State* L) {
    void* ud = luaL_checkudata(L, 1, "Entity");
    luaL_argcheck(L, ud != NULL, 1, "`entity' expected");
    return (ecs_entity_ref_t*)ud;
}

int add_entity(lua_State* L)
{
    lua_project_t* lp = get_project_from_state(L);

    int k_ent_mask = (int)(luaL_checkinteger(L, 2));
    ecs_entity_ref_t entity = ecs_entity_add(lp->ecs, (uint64_t)k_ent_mask);

    ecs_entity_ref_t* new_entity = (ecs_entity_ref_t*)lua_newuserdata(L, sizeof(entity));
    memcpy(new_entity, &entity, sizeof(entity));
    luaL_getmetatable(L, "Entity");
    lua_setmetatable(L, -2);
    
    return 1;
}

int create_component(lua_State* L, const char* name, size_t size_per_component, size_t alignment)
{
    const lua_project_t* lp = get_project_from_state(L);

    int component_id = ecs_register_component_type(lp->ecs, name, size_per_component, alignment);
    if (component_id == -1)
    {
        printf("Failed to register component '%s'\n", name);
        return component_id;
    }

    lua_pushinteger(L, (lua_Integer)component_id);
    lua_setglobal(L, name);

    return component_id;
}

void create_base_components(lua_State* L)
{
    lua_project_t* lp = get_project_from_state(L);

    lp->camera_type = create_component(L, "CameraComponent", sizeof(camera_component_t), _Alignof(camera_component_t));
    lp->player_type = create_component(L, "PlayerComponent", sizeof(player_component_t), _Alignof(player_component_t));
    lp->transform_type = create_component(L, "TransformComponent", sizeof(transform_component_t), _Alignof(transform_component_t));
    lp->model_type = create_component(L, "ModelComponent", sizeof(model_component_t), _Alignof(model_component_t));
    create_component(L, "NameComponent", sizeof(name_component_t), _Alignof(name_component_t));
    create_component(L, "TrafficComponent", sizeof(traffic_component_t), _Alignof(traffic_component_t));
}

static int entity_get_component(lua_State* L)
{
    const lua_project_t* lp = get_project_from_state(L);
    ecs_entity_ref_t* entity = check_entity(L);

    char* comp_name = (char*)(luaL_checkstring(L, 2));

    if (!lua_getglobal(L, comp_name))
    {
        printf("Attempt to get invalid component '%s'\n", comp_name);
        lua_pushnil(L);
        return 1;
    }
    lua_Integer comp_type = luaL_checkinteger(L, lua_gettop(L));

    void* comp = ecs_entity_get_component(lp->ecs, *entity, (int)comp_type, true);
    if (comp != NULL)
    {
        void** new_component = lua_newuserdata(L, sizeof(void*));
        *new_component = comp;
        luaL_getmetatable(L, comp_name);
        lua_setmetatable(L, -2);
    }
    else
    {
        lua_pushnil(L);
    }

    return 1;
}


// Basic Input Library
int get_key_mask(lua_State* L)
{
    lua_project_t* lp = get_project_from_state(L);

    int key_mask = (int)wm_get_key_mask(lp->window);
    lua_pushinteger(L, key_mask);

    return 1;
}

int get_key_code(lua_State* L)
{
    lua_project_t* lp = get_project_from_state(L);

    const char* key = luaL_checkstring(L, 2);

    const char* up = "Up";
    const char* down = "Down";
    const char* left = "Left";
    const char* right = "Right";

    if (strcmp(key, up) == 0)
    {
        lua_pushinteger(L, k_key_up);
    }
    else if (strcmp(key, down) == 0)
    {
        lua_pushinteger(L, k_key_down);
    }
    else if (strcmp(key, left) == 0)
    {
        lua_pushinteger(L, k_key_left);
    }
    else if (strcmp(key, right) == 0)
    {
        lua_pushinteger(L, k_key_right);
    }

    return 1;
}


// Set up Lua ECS and other API
int lua_add_custom_api(lua_State* L)
{
    luaL_newmetatable(L, "Entity");
    lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, entity_get_component); lua_setfield(L, -2, "GetComponent");
    lua_pop(L, 1);

    lua_prepare_components(L);

    const struct luaL_Reg ECSLib[] = {
        { "AddEntity", add_entity },
        { NULL, NULL },
    };
    luaL_newlib(L, ECSLib);
    lua_setglobal(L, "ECS");

    const struct luaL_Reg InputLib[] = {
        { "GetKeyDown", get_key_mask },
        { "GetKeyCode", get_key_code },
        { NULL, NULL },
    };
    luaL_newlib(L, InputLib);
    lua_setglobal(L, "Input");

    return 1;
}


// Lua Project
lua_project_t* lua_project_create(const char* lua_src, heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render)
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    lua_add_custom_api(L);

    lua_project_t* lp = heap_alloc(heap, sizeof(lua_project_t), 8);
    lp->heap = heap;
    lp->fs = fs;
    lp->window = window;
    lp->render = render;
    lp->ecs = ecs_create(heap);
    lp->timer = timer_object_create(heap, NULL);
    lp->L = L;

    lua_pushlightuserdata(L, lp);
    lua_setglobal(L, "lua_project");

    load_resources(lp);
    create_base_components(L);

    char sDir[DIR_PATH_MAX_LENGTH];
    sprintf_s(sDir, DIR_PATH_MAX_LENGTH, "%s", lua_src);
    search_dir_for_lua_files(L, sDir);

	return lp;
}

void lua_project_update(lua_project_t* lp)
{
    timer_object_update(lp->timer);
    ecs_update(lp->ecs);

    float dt = (float)timer_object_get_delta_ms(lp->timer) * 0.001f;
    if (lua_getglobal(lp->L, "RenderStepped"))
    {
        lua_pushnumber(lp->L, dt);
        handle_lua_error(lp->L, lua_pcall(lp->L, 1, 0, 0));
    }

    draw_models(lp);
    render_push_done(lp->render);
}

void lua_project_destroy(lua_project_t* lp)
{
    lua_close(lp->L);
    ecs_destroy(lp->ecs);
    timer_object_destroy(lp->timer);
    unload_resources(lp);
    heap_free(lp->heap, lp);
}


// Rendering system
static void load_resources(lua_project_t* lp)
{
    lp->vertex_shader_work = fs_read(lp->fs, "shaders/triangle.vert.spv", lp->heap, false, false);
    lp->fragment_shader_work = fs_read(lp->fs, "shaders/triangle.frag.spv", lp->heap, false, false);
    lp->cube_shader = (gpu_shader_info_t)
    {
        .vertex_shader_data = fs_work_get_buffer(lp->vertex_shader_work),
        .vertex_shader_size = fs_work_get_size(lp->vertex_shader_work),
        .fragment_shader_data = fs_work_get_buffer(lp->fragment_shader_work),
        .fragment_shader_size = fs_work_get_size(lp->fragment_shader_work),
        .uniform_buffer_count = 1,
    };

    static vec3f_t cube_verts_green[] =
    {
        { -1.0f, -1.0f,  1.0f }, { 0.0f, 0.5f,  0.0f },
        {  1.0f, -1.0f,  1.0f }, { 0.0f, 0.5f,  0.0f },
        {  1.0f,  1.0f,  1.0f }, { 0.0f, 0.5f,  0.0f },
        { -1.0f,  1.0f,  1.0f }, { 0.0f, 0.5f,  0.0f },
        { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.5f,  0.0f },
        {  1.0f, -1.0f, -1.0f }, { 0.0f, 0.5f,  0.0f },
        {  1.0f,  1.0f, -1.0f }, { 0.0f, 0.5f,  0.0f },
        { -1.0f,  1.0f, -1.0f }, { 0.0f, 0.5f,  0.0f },
    };
    static uint16_t cube_indices[] =
    {
        0, 1, 2,
        2, 3, 0,
        1, 5, 6,
        6, 2, 1,
        7, 6, 5,
        5, 4, 7,
        4, 0, 3,
        3, 7, 4,
        4, 5, 1,
        1, 0, 4,
        3, 2, 6,
        6, 7, 3
    };
    lp->cube_mesh_green = (gpu_mesh_info_t)
    {
        .layout = k_gpu_mesh_layout_tri_p444_c444_i2,
        .vertex_data = cube_verts_green,
        .vertex_data_size = sizeof(cube_verts_green),
        .index_data = cube_indices,
        .index_data_size = sizeof(cube_indices),
    };

    static vec3f_t cube_verts_red[] =
    {
        { -1.0f, -1.0f,  1.0f }, { 0.5f, 0.0f,  0.0f },
        {  1.0f, -1.0f,  1.0f }, { 0.5f, 0.0f,  0.0f },
        {  1.0f,  1.0f,  1.0f }, { 0.5f, 0.0f,  0.0f },
        { -1.0f,  1.0f,  1.0f }, { 0.5f, 0.0f,  0.0f },
        { -1.0f, -1.0f, -1.0f }, { 0.5f, 0.0f,  0.0f },
        {  1.0f, -1.0f, -1.0f }, { 0.5f, 0.0f,  0.0f },
        {  1.0f,  1.0f, -1.0f }, { 0.5f, 0.0f,  0.0f },
        { -1.0f,  1.0f, -1.0f }, { 0.5f, 0.0f,  0.0f },
    };
    lp->cube_mesh_red = (gpu_mesh_info_t)
    {
        .layout = k_gpu_mesh_layout_tri_p444_c444_i2,
        .vertex_data = cube_verts_red,
        .vertex_data_size = sizeof(cube_verts_red),
        .index_data = cube_indices,
        .index_data_size = sizeof(cube_indices),
    };
}

static void unload_resources(lua_project_t* lp)
{
    fs_work_destroy(lp->fragment_shader_work);
    fs_work_destroy(lp->vertex_shader_work);
}

static void draw_models(lua_project_t* lp)
{
    uint64_t k_camera_query_mask = (1ULL << lp->camera_type);
    for (ecs_query_t camera_query = ecs_query_create(lp->ecs, k_camera_query_mask);
        ecs_query_is_valid(lp->ecs, &camera_query);
        ecs_query_next(lp->ecs, &camera_query))
    {
        camera_component_t* camera_comp = ecs_query_get_component(lp->ecs, &camera_query, lp->camera_type);

        uint64_t k_model_query_mask = (1ULL << lp->transform_type) | (1ULL << lp->model_type);
        for (ecs_query_t query = ecs_query_create(lp->ecs, k_model_query_mask);
            ecs_query_is_valid(lp->ecs, &query);
            ecs_query_next(lp->ecs, &query))
        {
            transform_component_t* transform_comp = ecs_query_get_component(lp->ecs, &query, lp->transform_type);
            model_component_t* model_comp = ecs_query_get_component(lp->ecs, &query, lp->model_type);
            ecs_entity_ref_t entity_ref = ecs_query_get_entity(lp->ecs, &query);

            struct
            {
                mat4f_t projection;
                mat4f_t model;
                mat4f_t view;
            } uniform_data;
            uniform_data.projection = camera_comp->projection;
            uniform_data.view = camera_comp->view;
            transform_to_matrix(&transform_comp->transform, &uniform_data.model);
            gpu_uniform_buffer_info_t uniform_info = { .data = &uniform_data, sizeof(uniform_data) };

            // Due to time constraints, we will force all models to render as pre-colored cubes
            player_component_t* player_comp = ecs_query_get_component(lp->ecs, &query, lp->player_type);
            gpu_mesh_info_t* mesh_info = (player_comp && player_comp->index > 0) ? &lp->cube_mesh_green : &lp->cube_mesh_red;

            render_push_model(lp->render, &entity_ref, mesh_info, &lp->cube_shader, &uniform_info);
        }
    }
}
