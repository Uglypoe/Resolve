#include "ecs.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "render.h"
#include "timer_object.h"
#include "transform.h"
#include "wm.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>

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

typedef struct frogger_game_t
{
	heap_t* heap;
	fs_t* fs;
	wm_window_t* window;
	render_t* render;

	timer_object_t* timer;

	ecs_t* ecs;
	int transform_type;
	int camera_type;
	int model_type;
	int player_type;
	int traffic_type;
	int name_type;
	ecs_entity_ref_t player_ent;
	ecs_entity_ref_t traffic_ent;
	ecs_entity_ref_t camera_ent;

	gpu_mesh_info_t car_mesh;
	gpu_mesh_info_t cube_mesh;
	gpu_shader_info_t cube_shader;
	fs_work_t* vertex_shader_work;
	fs_work_t* fragment_shader_work;

	float bound_left;
	float bound_right;
	float bound_top;
	float bound_bottom;
} frogger_game_t;

static void load_resources(frogger_game_t* game);
static void unload_resources(frogger_game_t* game);
static void spawn_player(frogger_game_t* game, int index);
static void spawn_traffic(frogger_game_t* game, int index);
static void spawn_camera(frogger_game_t* game);
static void update_players(frogger_game_t* game);
static void update_traffic(frogger_game_t* game);
static void draw_models(frogger_game_t* game);

typedef struct car_data {
	float spawn_percent;
	float row;
	float speed;
	float size;
	bool move_left;
} car_data;

struct car_data all_cars[] = {
	{0.25f, 0.0f, 2.5f, 1.5f, false},
	{1.0f, 0.0f, 2.5f, 1.5f, false},
	{0.0f, 1.0f, 1.5f, 2.5f, true},

	{0.0f, 3.5f, 2.0f, 1.5f, false},
	{0.45f, 3.5f, 2.0f, 2.0f, false},
	{0.0f, 4.5f, 3.0f, 3.0f, true},
	{0.3f, 4.5f, 3.0f, 3.0f, true},

	{0.1f, 7.0f, 2.5f, 1.5f, false},
	{0.5f, 7.0f, 2.5f, 2.0f, false},
	{0.8f, 7.0f, 2.5f, 1.5f, false},
	{0.15f, 8.0f, 3.25f, 2.5f, true},
	{0.45f, 8.0f, 3.25f, 2.5f, true},
	{0.25f, 9.0f, 2.0f, 2.5f, true},
	{0.65f, 9.0f, 2.0f, 2.5f, true},
};

frogger_game_t* frogger_game_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render)
{
	frogger_game_t* game = heap_alloc(heap, sizeof(frogger_game_t), 8);
	game->heap = heap;
	game->fs = fs;
	game->window = window;
	game->render = render;

	game->timer = timer_object_create(heap, NULL);
	
	game->ecs = ecs_create(heap);
	game->transform_type = ecs_register_component_type(game->ecs, "transform", sizeof(transform_component_t), _Alignof(transform_component_t));
	game->camera_type = ecs_register_component_type(game->ecs, "camera", sizeof(camera_component_t), _Alignof(camera_component_t));
	game->model_type = ecs_register_component_type(game->ecs, "model", sizeof(model_component_t), _Alignof(model_component_t));
	game->player_type = ecs_register_component_type(game->ecs, "player", sizeof(player_component_t), _Alignof(player_component_t));
	game->traffic_type = ecs_register_component_type(game->ecs, "traffic", sizeof(traffic_component_t), _Alignof(traffic_component_t));
	game->name_type = ecs_register_component_type(game->ecs, "name", sizeof(name_component_t), _Alignof(name_component_t));

	float aspectRatio = 16.0f / 9.0f;
	float top = -13;
	float left = top * aspectRatio;
	game->bound_left = left;
	game->bound_right = -left;
	game->bound_top = top;
	game->bound_bottom = -top;

	load_resources(game);
	spawn_player(game, 0);
	for (int i = 0; i < 14; i++)
	{
		spawn_traffic(game, i);
	}
	spawn_camera(game);

	return game;
}

void frogger_game_destroy(frogger_game_t* game)
{
	ecs_destroy(game->ecs);
	timer_object_destroy(game->timer);
	unload_resources(game);
	heap_free(game->heap, game);
}

void frogger_game_update(frogger_game_t* game)
{
	timer_object_update(game->timer);
	ecs_update(game->ecs);
	update_players(game);
	update_traffic(game);
	draw_models(game);
	render_push_done(game->render);
}

static void load_resources(frogger_game_t* game)
{
	game->vertex_shader_work = fs_read(game->fs, "shaders/triangle.vert.spv", game->heap, false, false);
	game->fragment_shader_work = fs_read(game->fs, "shaders/triangle.frag.spv", game->heap, false, false);
	game->cube_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(game->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(game->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(game->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(game->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	static vec3f_t cube_verts[] =
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
	game->cube_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = cube_verts,
		.vertex_data_size = sizeof(cube_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};

	static vec3f_t car_verts[] =
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
	game->car_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = car_verts,
		.vertex_data_size = sizeof(car_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
}

static void unload_resources(frogger_game_t* game)
{
	fs_work_destroy(game->fragment_shader_work);
	fs_work_destroy(game->vertex_shader_work);
}

static void spawn_player(frogger_game_t* game, int index)
{
	uint64_t k_player_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->player_type) |
		(1ULL << game->name_type);
	game->player_ent = ecs_entity_add(game->ecs, k_player_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation.z = game->bound_bottom - 1.5f;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "player");

	player_component_t* player_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->player_type, true);
	player_comp->index = index;
	player_comp->speed = 1.5f;

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->model_type, true);
	model_comp->mesh_info = &game->cube_mesh;
	model_comp->shader_info = &game->cube_shader;
}

static void spawn_traffic(frogger_game_t* game, int index)
{
	uint64_t k_traffic_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->traffic_type) |
		(1ULL << game->name_type);
	game->traffic_ent = ecs_entity_add(game->ecs, k_traffic_ent_mask);

	car_data init_data = all_cars[index];

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->traffic_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation.z = game->bound_bottom - 4 - init_data.row * 2.1f;
	transform_comp->transform.scale.y = init_data.size;

	traffic_component_t* traffic_comp = ecs_entity_get_component(game->ecs, game->traffic_ent, game->traffic_type, true);
	traffic_comp->index = index;
	traffic_comp->moving_left = init_data.move_left;
	traffic_comp->speed = init_data.speed;

	float left_end = game->bound_left - transform_comp->transform.scale.y;
	float right_end = game->bound_right + transform_comp->transform.scale.y;
	float total_distance = fabsf(left_end - right_end);
	if (traffic_comp->moving_left)
	{
		transform_comp->transform.translation.y = right_end - init_data.spawn_percent * total_distance;
	}
	else
	{
		transform_comp->transform.translation.y = left_end + init_data.spawn_percent * total_distance;
	}

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->traffic_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "traffic");
	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->traffic_ent, game->model_type, true);
	model_comp->mesh_info = &game->car_mesh;
	model_comp->shader_info = &game->cube_shader;
}

static void spawn_camera(frogger_game_t* game)
{
	uint64_t k_camera_ent_mask =
		(1ULL << game->camera_type) |
		(1ULL << game->name_type);
	game->camera_ent = ecs_entity_add(game->ecs, k_camera_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "camera");

	camera_component_t* camera_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->camera_type, true);
	mat4f_make_orthographic(&camera_comp->projection, game->bound_left, game->bound_right, game->bound_bottom, game->bound_top, 0.1f, 10.f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);
}

bool did_player_collide_with_traffic(frogger_game_t* game, transform_t player_transform)
{
	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->traffic_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);

		bool within_y = fabs(transform_comp->transform.translation.y - player_transform.translation.y)
			< (transform_comp->transform.scale.y + player_transform.scale.y);

		bool within_z = fabs(transform_comp->transform.translation.z - player_transform.translation.z)
			< (transform_comp->transform.scale.z + player_transform.scale.z);

		if (within_y && within_z)
		{
			return true;
		}
	}
	return false;
}

static void update_players(frogger_game_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	uint32_t key_mask = wm_get_key_mask(game->window);

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->player_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		player_component_t* player_comp = ecs_query_get_component(game->ecs, &query, game->player_type);

		float size_y = transform_comp->transform.scale.y; // Technically wrong (scale doesn't describe bounding box), but it works for now
		float size_z = transform_comp->transform.scale.z;
		if (fabs(game->bound_top + size_z - transform_comp->transform.translation.z) < 0.01f)
		{
			// Player hit top of screen (win condition), teleport them back to the bottom
			transform_comp->transform.translation.y = 0;
			transform_comp->transform.translation.z = game->bound_bottom;
		}

		if (did_player_collide_with_traffic(game, transform_comp->transform))
		{
			// Player hit traffic (lose condition), teleport them back to the bottom
			transform_comp->transform.translation.y = 0;
			transform_comp->transform.translation.z = game->bound_bottom;
		}

		transform_t move;
		transform_identity(&move);
		if (key_mask & k_key_up)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), -dt));
		}
		if (key_mask & k_key_down)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), dt));
		}
		if (key_mask & k_key_left)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), -dt));
		}
		if (key_mask & k_key_right)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt));
		}

		move.translation = vec3f_scale(move.translation, player_comp->speed);

		float new_y = transform_comp->transform.translation.y + move.translation.y;
		float clamped_y = min(game->bound_right - size_y, max(game->bound_left + size_y, new_y));
		move.translation.y = clamped_y - transform_comp->transform.translation.y;

		float new_z = transform_comp->transform.translation.z + move.translation.z;
		float clamped_z = min(game->bound_bottom - size_z, max(game->bound_top + size_z, new_z));
		move.translation.z = clamped_z - transform_comp->transform.translation.z;

		transform_multiply(&transform_comp->transform, &move);
	}
}

static void update_traffic(frogger_game_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->traffic_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		traffic_component_t* traffic_comp = ecs_query_get_component(game->ecs, &query, game->traffic_type);

		transform_t move;
		transform_identity(&move);

		// Check if traffic hit edge of screen, if so, then teleport back to opposite edge
		if (traffic_comp->moving_left)
		{
			if (transform_comp->transform.translation.y <= game->bound_left - transform_comp->transform.scale.y)
			{
				transform_comp->transform.translation.y = game->bound_right + transform_comp->transform.scale.y;
			}
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), -dt));
		}
		else
		{
			if (transform_comp->transform.translation.y >= game->bound_right + transform_comp->transform.scale.y)
			{
				transform_comp->transform.translation.y = game->bound_left - transform_comp->transform.scale.y;
			}
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt));
		}

		move.translation = vec3f_scale(move.translation, traffic_comp->speed);
		transform_multiply(&transform_comp->transform, &move);
	}
}

static void draw_models(frogger_game_t* game)
{
	uint64_t k_camera_query_mask = (1ULL << game->camera_type);
	for (ecs_query_t camera_query = ecs_query_create(game->ecs, k_camera_query_mask);
		ecs_query_is_valid(game->ecs, &camera_query);
		ecs_query_next(game->ecs, &camera_query))
	{
		camera_component_t* camera_comp = ecs_query_get_component(game->ecs, &camera_query, game->camera_type);

		uint64_t k_model_query_mask = (1ULL << game->transform_type) | (1ULL << game->model_type);
		for (ecs_query_t query = ecs_query_create(game->ecs, k_model_query_mask);
			ecs_query_is_valid(game->ecs, &query);
			ecs_query_next(game->ecs, &query))
		{
			transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
			model_component_t* model_comp = ecs_query_get_component(game->ecs, &query, game->model_type);
			ecs_entity_ref_t entity_ref = ecs_query_get_entity(game->ecs, &query);

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

			render_push_model(game->render, &entity_ref, model_comp->mesh_info, model_comp->shader_info, &uniform_info);
		}
	}
}
