#include "game.h"

#include <string.h>

#define START_SPEED 4
#define MAX_SPEED 12
#define SPEEDUP_FRAMES 900
#define LEVEL_FRAMES 1800
#define LEVEL_PHASE_COUNT 3
#define MAX_LEVEL 9
#define LEVELUP_NOTICE_FRAMES 120
#define JUMP_FRAMES 40
#define JUMP_PEAK_HEIGHT 58
#define SLIDE_FRAMES 30
#define STUMBLE_FRAMES 18
#define JETPACK_FRAMES 300
#define JETPACK_HEIGHT 96
#define INVISIBLE_FRAMES 300
#define MAX_POWERUP_FRAMES 600
#define POWERUP_UNLOCK_LEVEL 2
#define POWERUP_CHANCE 7
#define MIN_SPAWN_FRAMES 42
#define MAX_SPAWN_FRAMES 98
#define COIN_TRAIL_COUNT 5
#define COIN_SPACING 42
#define COIN_Z_JITTER 14
#define COIN_LANE_SHIFT_CHANCE 4
#define COIN_HIGH_CHANCE 5
#define COIN_HIGH_MIN 24
#define COIN_HIGH_VARIANCE 22
#define GATE_NEAR_Z 20
#define BUS_CLEAR_HEIGHT 28
#define BUS_TOP_HEIGHT 34
#define BUS_RIDE_FRAMES 16
#define BUS_COIN_HEIGHT 42
#define BUS_COIN_COUNT 4
#define BUS_COIN_SPACING 32

static uint32_t rfl_rand(RflGame *game)
{
    game->rng = game->rng * 1664525u + 1013904223u;
    return game->rng;
}

static int rfl_rand_range(RflGame *game, int max_exclusive)
{
    return (int)(rfl_rand(game) % (uint32_t)max_exclusive);
}

static void rfl_clear_entities(RflGame *game)
{
    for (int i = 0; i < RFL_MAX_ENTITIES; i++) {
        game->entities[i].active = false;
        game->entities[i].kind = RFL_ENTITY_NONE;
        game->entities[i].height = 0;
        game->entities[i].collected = false;
    }
}

static RflEntity *rfl_alloc_entity(RflGame *game)
{
    for (int i = 0; i < RFL_MAX_ENTITIES; i++) {
        if (!game->entities[i].active) {
            return &game->entities[i];
        }
    }

    return 0;
}

static void rfl_spawn_entity_at_height(RflGame *game, RflEntityKind kind, int lane, int z, int height)
{
    RflEntity *entity = rfl_alloc_entity(game);
    if (!entity) {
        return;
    }

    entity->kind = kind;
    entity->lane = lane;
    entity->z = z;
    entity->height = height;
    entity->active = true;
    entity->collected = false;
}

static void rfl_spawn_entity(RflGame *game, RflEntityKind kind, int lane, int z)
{
    rfl_spawn_entity_at_height(game, kind, lane, z, 0);
}

static int rfl_random_coin_height(RflGame *game, int base_height)
{
    if (base_height > 0 || game->level < 2 || rfl_rand_range(game, COIN_HIGH_CHANCE) != 0) {
        return base_height;
    }

    return COIN_HIGH_MIN + rfl_rand_range(game, COIN_HIGH_VARIANCE);
}

static void rfl_spawn_coin_trail_at_height(RflGame *game, int lane, int start_z, int count, int height)
{
    int coin_lane = lane;
    int z = start_z;

    for (int i = 0; i < count; i++) {
        int coin_z = z + rfl_rand_range(game, COIN_Z_JITTER * 2 + 1) - COIN_Z_JITTER;
        int coin_height = rfl_random_coin_height(game, height);
        rfl_spawn_entity_at_height(game, RFL_ENTITY_COIN, coin_lane, coin_z, coin_height);

        z += COIN_SPACING - 8 + rfl_rand_range(game, 17);
        if (rfl_rand_range(game, COIN_LANE_SHIFT_CHANCE) == 0) {
            if (coin_lane == 0) {
                coin_lane = 1;
            } else if (coin_lane == RFL_LANE_COUNT - 1) {
                coin_lane = RFL_LANE_COUNT - 2;
            } else {
                coin_lane += rfl_rand_range(game, 2) == 0 ? -1 : 1;
            }
        }
    }
}

static void rfl_spawn_coin_trail(RflGame *game, int lane, int start_z, int count)
{
    rfl_spawn_coin_trail_at_height(game, lane, start_z, count, 0);
}

static void rfl_spawn_bus_with_coins(RflGame *game, int lane, int z)
{
    rfl_spawn_entity(game, RFL_ENTITY_BUS, lane, z);
    for (int i = 0; i < BUS_COIN_COUNT; i++) {
        rfl_spawn_entity_at_height(game, RFL_ENTITY_COIN, lane, z - 24 + i * BUS_COIN_SPACING, BUS_COIN_HEIGHT);
    }
}

static int rfl_other_lane(RflGame *game, int lane)
{
    return (lane + 1 + rfl_rand_range(game, RFL_LANE_COUNT - 1)) % RFL_LANE_COUNT;
}

static int rfl_level_from_frames(int frames_alive)
{
    int level = 1 + frames_alive / LEVEL_FRAMES;
    if (level > MAX_LEVEL) {
        return MAX_LEVEL;
    }

    return level;
}

static int rfl_level_phase(const RflGame *game)
{
    return (game->frames_alive % LEVEL_FRAMES) / (LEVEL_FRAMES / LEVEL_PHASE_COUNT);
}

static int rfl_speed_cap_for_level(int level)
{
    int cap = START_SPEED + level - 1;
    if (cap > MAX_SPEED) {
        return MAX_SPEED;
    }

    return cap;
}

static int rfl_difficulty_tier(const RflGame *game)
{
    if (game->level <= 1) {
        return 0;
    }
    if (game->level <= 3) {
        return 1;
    }
    if (game->level <= 6) {
        return 2;
    }

    return 3;
}

static RflEntityKind rfl_random_obstacle(RflGame *game, int tier)
{
    if (tier <= 0) {
        return RFL_ENTITY_BARRIER;
    }
    if (tier == 1) {
        return (rfl_rand_range(game, 2) == 0) ? RFL_ENTITY_BARRIER : RFL_ENTITY_GATE;
    }

    return (RflEntityKind)(RFL_ENTITY_BARRIER + rfl_rand_range(game, 3));
}

static RflEntityKind rfl_random_powerup(RflGame *game)
{
    return (rfl_rand_range(game, 2) == 0) ? RFL_ENTITY_JETPACK : RFL_ENTITY_INVISIBLE;
}

static int rfl_spawn_delay(const RflGame *game)
{
    int delay = MAX_SPAWN_FRAMES - (game->speed - START_SPEED) * 3 - rfl_difficulty_tier(game) * 6;
    if (delay < MIN_SPAWN_FRAMES) {
        delay = MIN_SPAWN_FRAMES;
    }

    return delay;
}

static void rfl_try_spawn_powerup(RflGame *game)
{
    if (game->level < POWERUP_UNLOCK_LEVEL) {
        return;
    }
    if (rfl_rand_range(game, POWERUP_CHANCE) != 0) {
        return;
    }

    int lane = rfl_rand_range(game, RFL_LANE_COUNT);
    int z = RFL_SPAWN_Z + 80 + rfl_rand_range(game, 130);
    rfl_spawn_entity(game, rfl_random_powerup(game), lane, z);
}

static void rfl_spawn_pattern(RflGame *game)
{
    int tier = rfl_difficulty_tier(game);
    int phase = rfl_level_phase(game);
    int pattern = rfl_rand_range(game, 10);
    int lane = rfl_rand_range(game, RFL_LANE_COUNT);
    int coin_lane = game->player.lane;

    if (phase == 0 && pattern > 5) {
        pattern = rfl_rand_range(game, 6);
    } else if (phase == 2 && tier >= 2 && pattern < 4) {
        pattern += 4;
    }

    if (tier == 0) {
        if (phase > 0 && pattern >= 6) {
            rfl_spawn_bus_with_coins(game, lane, RFL_SPAWN_Z);
            rfl_try_spawn_powerup(game);
            return;
        }

        rfl_spawn_coin_trail(game, coin_lane, RFL_SPAWN_Z - 120, COIN_TRAIL_COUNT);
        if (phase > 0 && rfl_rand_range(game, phase == 1 ? 4 : 2) == 0) {
            rfl_spawn_entity(game, RFL_ENTITY_BARRIER, rfl_other_lane(game, coin_lane), RFL_SPAWN_Z);
        }
        rfl_try_spawn_powerup(game);
        return;
    }

    if (pattern <= 2) {
        coin_lane = (rfl_rand_range(game, 2) == 0) ? game->player.lane : lane;
        rfl_spawn_coin_trail(game, coin_lane, RFL_SPAWN_Z - 60, COIN_TRAIL_COUNT);
    } else if (pattern <= 5 || tier == 1) {
        RflEntityKind kind = rfl_random_obstacle(game, tier);
        coin_lane = rfl_other_lane(game, lane);
        rfl_spawn_entity(game, kind, lane, RFL_SPAWN_Z);
        rfl_spawn_coin_trail(game, coin_lane, RFL_SPAWN_Z + 40, 3);
    } else if (tier >= 2 && (pattern == 7 || pattern == 8)) {
        rfl_spawn_bus_with_coins(game, lane, RFL_SPAWN_Z);
        if (tier >= 3 && rfl_rand_range(game, 2) == 0) {
            rfl_spawn_entity(game, rfl_random_obstacle(game, tier), rfl_other_lane(game, lane), RFL_SPAWN_Z + 35);
        }
    } else if (pattern <= 7 || tier == 2) {
        int open_lane = rfl_rand_range(game, RFL_LANE_COUNT);
        for (int i = 0; i < RFL_LANE_COUNT; i++) {
            if (i != open_lane) {
                RflEntityKind kind = rfl_random_obstacle(game, tier);
                rfl_spawn_entity(game, kind, i, RFL_SPAWN_Z + i * 12);
            }
        }
        rfl_spawn_coin_trail(game, open_lane, RFL_SPAWN_Z + 30, 4);
    } else {
        rfl_spawn_entity(game, RFL_ENTITY_GATE, lane, RFL_SPAWN_Z);
        coin_lane = rfl_other_lane(game, lane);
        if (tier >= 3 && rfl_rand_range(game, 2) == 0) {
            rfl_spawn_entity(game, RFL_ENTITY_BLOCK, rfl_other_lane(game, coin_lane), RFL_SPAWN_Z + 30);
        }
        rfl_spawn_coin_trail(game, coin_lane, RFL_SPAWN_Z + 55, 3);
    }

    rfl_try_spawn_powerup(game);
}

static bool rfl_obstacle_hits_player(const RflGame *game, const RflEntity *entity)
{
    if (rfl_player_is_invisible(game) || rfl_player_has_jetpack(game)) {
        return false;
    }

    if (entity->lane != game->player.lane) {
        return false;
    }

    if (entity->z < -RFL_NEAR_Z || entity->z > RFL_NEAR_Z) {
        return false;
    }

    switch (entity->kind) {
    case RFL_ENTITY_BARRIER:
        return !rfl_player_is_jumping(game);
    case RFL_ENTITY_GATE:
        if (entity->z < -GATE_NEAR_Z || entity->z > GATE_NEAR_Z) {
            return false;
        }
        return !rfl_player_is_low_profile(game);
    case RFL_ENTITY_BLOCK:
        return true;
    case RFL_ENTITY_BUS:
        return rfl_player_jump_height(game) < BUS_CLEAR_HEIGHT;
    default:
        return false;
    }
}

static bool rfl_entity_is_pickup(const RflEntity *entity)
{
    return entity->kind == RFL_ENTITY_COIN ||
        entity->kind == RFL_ENTITY_JETPACK ||
        entity->kind == RFL_ENTITY_INVISIBLE;
}

static bool rfl_pickup_reachable(const RflGame *game, const RflEntity *entity)
{
    if (entity->height <= 0 || rfl_player_has_jetpack(game)) {
        return true;
    }

    return rfl_player_jump_height(game) + 12 >= entity->height;
}

static int rfl_extend_powerup_timer(int current_frames, int added_frames)
{
    int frames = current_frames + added_frames;
    if (frames > MAX_POWERUP_FRAMES) {
        return MAX_POWERUP_FRAMES;
    }

    return frames;
}

static void rfl_collect_pickup(RflGame *game, RflEntity *entity)
{
    entity->active = false;
    entity->collected = true;

    switch (entity->kind) {
    case RFL_ENTITY_COIN:
        game->coins++;
        game->score += 250;
        break;
    case RFL_ENTITY_JETPACK:
        game->player.jetpack_frames = rfl_extend_powerup_timer(game->player.jetpack_frames, JETPACK_FRAMES);
        game->score += 500;
        break;
    case RFL_ENTITY_INVISIBLE:
        game->player.invisible_frames = rfl_extend_powerup_timer(game->player.invisible_frames, INVISIBLE_FRAMES);
        game->score += 500;
        break;
    default:
        break;
    }
}

static void rfl_update_running(RflGame *game, const RflInput *input)
{
    RflPlayer *player = &game->player;

    if (input->left && player->lane > 0) {
        player->lane--;
    }
    if (input->right && player->lane < RFL_LANE_COUNT - 1) {
        player->lane++;
    }

    if (input->jump && player->jump_frames == 0 && player->slide_frames == 0 && !player->squatting) {
        player->jump_frames = JUMP_FRAMES;
    }
    if (input->slide && player->slide_frames == 0 && player->jump_frames == 0 && !player->squatting) {
        player->slide_frames = SLIDE_FRAMES;
    }

    if (player->jump_frames > 0) {
        player->jump_frames--;
    }
    if (player->slide_frames > 0) {
        player->slide_frames--;
    }
    if (player->stumble_frames > 0) {
        player->stumble_frames--;
    }
    if (player->bus_ride_frames > 0) {
        player->bus_ride_frames--;
    }
    if (player->jetpack_frames > 0) {
        player->jetpack_frames--;
        player->jump_frames = 0;
        player->slide_frames = 0;
    }
    if (player->invisible_frames > 0) {
        player->invisible_frames--;
    }
    if (player->jetpack_frames > 0) {
        player->squatting = false;
    } else {
        player->squatting = input->squat && player->jump_frames == 0 && player->slide_frames == 0;
    }

    game->frames_alive++;
    int next_level = rfl_level_from_frames(game->frames_alive);
    if (next_level > game->level) {
        game->level = next_level;
        game->levelup_frames = LEVELUP_NOTICE_FRAMES;
    }
    if (game->levelup_frames > 0) {
        game->levelup_frames--;
    }

    game->world_offset = (game->world_offset + game->speed) % 48;
    game->score += game->speed;

    int speed_cap = rfl_speed_cap_for_level(game->level);
    if ((game->frames_alive % SPEEDUP_FRAMES) == 0 && game->speed < speed_cap) {
        game->speed++;
    }

    game->spawn_timer--;
    if (game->spawn_timer <= 0) {
        rfl_spawn_pattern(game);
        game->spawn_timer = rfl_spawn_delay(game) + rfl_rand_range(game, 20);
    }

    for (int i = 0; i < RFL_MAX_ENTITIES; i++) {
        RflEntity *entity = &game->entities[i];
        if (!entity->active) {
            continue;
        }

        entity->z -= game->speed;

        if (rfl_entity_is_pickup(entity) &&
            entity->lane == player->lane &&
            entity->z >= -RFL_NEAR_Z &&
            entity->z <= RFL_NEAR_Z + 12 &&
            rfl_pickup_reachable(game, entity)) {
            rfl_collect_pickup(game, entity);
            continue;
        }

        if (rfl_obstacle_hits_player(game, entity)) {
            player->stumble_frames = STUMBLE_FRAMES;
            game->mode = RFL_MODE_GAME_OVER;
            if (game->score > game->best_score) {
                game->best_score = game->score;
            }
            continue;
        }

        if (entity->kind == RFL_ENTITY_BUS &&
            entity->lane == player->lane &&
            entity->z >= -RFL_NEAR_Z &&
            entity->z <= RFL_NEAR_Z + 22 &&
            rfl_player_jump_height(game) >= BUS_CLEAR_HEIGHT) {
            player->bus_ride_frames = BUS_RIDE_FRAMES;
        }

        if (entity->z < -80) {
            entity->active = false;
        }
    }
}

void rfl_game_init(RflGame *game)
{
    memset(game, 0, sizeof(*game));
    game->mode = RFL_MODE_TITLE;
    game->rng = 0x64f0a11u;
    game->best_score = 0;
    game->level = 1;
    game->player.lane = 1;
}

void rfl_game_start(RflGame *game)
{
    int best_score = game->best_score;
    uint32_t rng = game->rng ^ 0x9e3779b9u;

    memset(game, 0, sizeof(*game));
    game->mode = RFL_MODE_RUNNING;
    game->rng = rng ? rng : 0x64f0a11u;
    game->speed = START_SPEED;
    game->spawn_timer = 90;
    game->best_score = best_score;
    game->level = 1;
    game->player.lane = 1;
    rfl_clear_entities(game);
    rfl_spawn_coin_trail(game, game->player.lane, RFL_SPAWN_Z - 240, COIN_TRAIL_COUNT);
}

void rfl_game_update(RflGame *game, const RflInput *input)
{
    switch (game->mode) {
    case RFL_MODE_TITLE:
        if (input->start || input->jump) {
            rfl_game_start(game);
        }
        break;
    case RFL_MODE_RUNNING:
        rfl_update_running(game, input);
        break;
    case RFL_MODE_GAME_OVER:
        if (input->start || input->jump) {
            rfl_game_start(game);
        }
        break;
    }
}

bool rfl_player_is_jumping(const RflGame *game)
{
    return rfl_player_has_jetpack(game) ||
        (game->player.jump_frames > 8 && game->player.jump_frames < JUMP_FRAMES - 4);
}

bool rfl_player_is_sliding(const RflGame *game)
{
    return game->player.slide_frames > 0;
}

bool rfl_player_is_squatting(const RflGame *game)
{
    return game->player.squatting;
}

bool rfl_player_is_low_profile(const RflGame *game)
{
    return rfl_player_is_sliding(game) || rfl_player_is_squatting(game);
}

bool rfl_player_has_jetpack(const RflGame *game)
{
    return game->player.jetpack_frames > 0;
}

bool rfl_player_is_invisible(const RflGame *game)
{
    return game->player.invisible_frames > 0;
}

int rfl_player_jump_height(const RflGame *game)
{
    int platform_height = game->player.bus_ride_frames > 0 ? BUS_TOP_HEIGHT : 0;

    if (rfl_player_has_jetpack(game)) {
        return JETPACK_HEIGHT + ((game->frames_alive / 5) & 1) * 5;
    }

    if (game->player.jump_frames <= 0) {
        return platform_height;
    }

    int t = JUMP_FRAMES - game->player.jump_frames;
    if (t > JUMP_FRAMES / 2) {
        t = JUMP_FRAMES - t;
    }

    int jump_height = (t * JUMP_PEAK_HEIGHT) / (JUMP_FRAMES / 2);
    return jump_height > platform_height ? jump_height : platform_height;
}
