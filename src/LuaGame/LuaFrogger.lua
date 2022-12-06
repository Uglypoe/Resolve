--[[
	This is an implementation of Frogger in Lua!
--]]

-- {spawn_percent, row, speed, size, move_left}
local all_cars = {
	{0.25, 0.0, 2.5, 1.5, false},
	{1.0, 0.0, 2.5, 1.5, false},
	{0.0, 1.0, 1.5, 2.5, true},

	{0.0, 3.5, 2.0, 1.5, false},
	{0.45, 3.5, 2.0, 2.0, false},
	{0.0, 4.5, 3.0, 3.0, true},
	{0.3, 4.5, 3.0, 3.0, true},

	{0.1, 7.0, 2.5, 1.5, false},
	{0.5, 7.0, 2.5, 2.0, false},
	{0.8, 7.0, 2.5, 1.5, false},
	{0.15, 8.0, 3.25, 2.5, true},
	{0.45, 8.0, 3.25, 2.5, true},
	{0.25, 9.0, 2.0, 2.5, true},
	{0.65, 9.0, 2.0, 2.5, true}
}
for i, car_data in ipairs(all_cars) do
	all_cars[i] = {
		spawn_percent = car_data[1],
		row = car_data[2],
		speed = car_data[3],
		size = car_data[4],
		move_left = car_data[5],
	}
end

-- Since these may be expensive calls and we access
-- them constantly, we'll just cache their values
local k_key_up = Input:GetKeyCode("Up")
local k_key_down = Input:GetKeyCode("Down")
local k_key_left = Input:GetKeyCode("Left")
local k_key_right = Input:GetKeyCode("Right")


local function spawn_camera(game)
	local camera_mask = (1 << CameraComponent) | (1 << NameComponent)
	local new_camera = ECS:AddEntity(camera_mask)

	new_camera:GetComponent("NameComponent").name = "camera"

	new_camera:GetComponent("CameraComponent"):MakeOrthographic(
		game.bound_left, game.bound_right, game.bound_bottom, game.bound_top, 0.1, 10
	)
end

local function spawn_player(game, index)
	local player_mask = (1 << TransformComponent) | (1 << ModelComponent) | (1 << PlayerComponent) | (1 << NameComponent)
	local new_player = ECS:AddEntity(player_mask)
	
	new_player:GetComponent("NameComponent").name = "player"

	local transform_component = new_player:GetComponent("TransformComponent")
	transform_component.MakeIdentity = 0 -- Ugly hack, but not high priority to change
	transform_component.z = game.bound_bottom - 1.5

	local player_component = new_player:GetComponent("PlayerComponent")
	player_component.index = index
	player_component.speed = 1.5

	game.plr = new_player
end

local function spawn_traffic(game, index)
	local traffic_mask = (1 << TransformComponent) | (1 << ModelComponent) | (1 << TrafficComponent) | (1 << NameComponent)
	local new_traffic = ECS:AddEntity(traffic_mask)

	local car_data = all_cars[index];

	new_traffic:GetComponent("NameComponent").name = "traffic"

	local transform_component = new_traffic:GetComponent("TransformComponent")
	transform_component.MakeIdentity = 0 -- Ugly hack, but not high priority to change
	transform_component.z = game.bound_bottom - 4 - car_data.row * 2.1
	transform_component.sy = car_data.size

	local traffic_component = new_traffic:GetComponent("TrafficComponent")
	traffic_component.index = index
	traffic_component.moving_left = car_data.move_left
	traffic_component.speed = car_data.speed

	local left_end = game.bound_left - transform_component.sy;
	local right_end = game.bound_right + transform_component.sy;
	local total_distance = math.abs(left_end - right_end);
	if traffic_component.moving_left then
		transform_component.y = right_end - car_data.spawn_percent * total_distance
	else
		transform_component.y = left_end + car_data.spawn_percent * total_distance
	end

	table.insert(game.cars, new_traffic)
end

local function create_game()
	local game = {
		plr = nil,
		cars = {},

		bound_left = nil,
		bound_right = nil,
		bound_top = nil,
		bound_bottom = nil
	}
	
	local aspectRatio = 16.0 / 9.0
	local top = -13
	local left = top * aspectRatio
	game.bound_left = left
	game.bound_right = -left
	game.bound_top = top
	game.bound_bottom = -top
	
	spawn_player(game, 1)
	for i = 1, 14 do
		spawn_traffic(game, i)
	end
	spawn_camera(game)

	return game
end


local function did_player_collide_with_traffic(game, player_transform)
	for i = 1, #game.cars do
		local transform_comp = game.cars[i]:GetComponent("TransformComponent")

		local within_y = math.abs(transform_comp.y - player_transform.y) < (transform_comp.sy + player_transform.sy)
		local within_z = math.abs(transform_comp.z - player_transform.z) < (transform_comp.sz + player_transform.sz)

		if within_y and within_z then
			return true
		end
	end
	return false
end

local function update_players(game, dt)
	local plr = game.plr
	if not plr then
		return
	end

	local key_mask = Input:GetKeyDown()

	local player_transform = plr:GetComponent("TransformComponent")
	local speed = plr:GetComponent("PlayerComponent").speed

	local size_y = player_transform.sy;
	local size_z = player_transform.sz;
	if math.abs(game.bound_top + size_z - player_transform.z) < 0.01 then
		-- Player hit top of screen (win condition), teleport them back to the bottom
		player_transform.y = 0;
		player_transform.z = game.bound_bottom;
	end

	if did_player_collide_with_traffic(game, player_transform) then
		-- Player hit traffic (lose condition), teleport them back to the bottom
		player_transform.y = 0;
		player_transform.z = game.bound_bottom;
	end

	local dy = 0
	local dz = 0

	if key_mask & k_key_up ~= 0 then
		dz = dz - 1
	end
	if key_mask & k_key_down ~= 0 then
		dz = dz + 1
	end
	if key_mask & k_key_left ~= 0 then
		dy = dy - 1
	end
	if key_mask & k_key_right ~= 0 then
		dy = dy + 1
	end
	
	dy = dy * speed * dt
	dz = dz * speed * dt

	local new_y = player_transform.y + dy;
	local clamped_y = math.min(game.bound_right - size_y, math.max(game.bound_left + size_y, new_y));
	player_transform.y = clamped_y;

	local new_z = player_transform.z + dz;
	local clamped_z = math.min(game.bound_bottom - size_z, math.max(game.bound_top + size_z, new_z));
	player_transform.z = clamped_z;
end

local function update_traffic(game, dt)
	for _, car in ipairs(game.cars) do
		local transform_comp = car:GetComponent("TransformComponent")
		local traffic_comp = car:GetComponent("TrafficComponent")

		-- Check if traffic hit edge of screen, if so, then teleport back to opposite edge
		if traffic_comp.moving_left then
			if transform_comp.y <= game.bound_left - transform_comp.sy then
				transform_comp.y = game.bound_right + transform_comp.sy
			end
			transform_comp.y = transform_comp.y - (dt * traffic_comp.speed)
		else
			if transform_comp.y >= game.bound_right + transform_comp.sy then
				transform_comp.y = game.bound_left - transform_comp.sy
			end
			transform_comp.y = transform_comp.y + (dt * traffic_comp.speed)
		end
	end
end


local game = create_game()

-- RESERVED GLOBAL FUNCTION NAME. ONLY DEFINE THIS ONCE!!!
function RenderStepped(dt)
	update_traffic(game, dt)
	update_players(game, dt)
end