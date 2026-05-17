#ifndef RUNFORLIFE64_GAME_H
#define RUNFORLIFE64_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define RFL_LANE_COUNT 3
#define RFL_MAX_ENTITIES 40
#define RFL_SPAWN_Z 660
#define RFL_NEAR_Z 30

typedef enum {
    RFL_MODE_TITLE,
    RFL_MODE_RUNNING,
    RFL_MODE_GAME_OVER
} RflMode;

typedef enum {
    RFL_ENTITY_NONE,
    RFL_ENTITY_BARRIER,
    RFL_ENTITY_GATE,
    RFL_ENTITY_BLOCK,
    RFL_ENTITY_BUS,
    RFL_ENTITY_COIN,
    RFL_ENTITY_JETPACK,
    RFL_ENTITY_INVISIBLE
} RflEntityKind;

typedef struct {
    bool left;
    bool right;
    bool jump;
    bool slide;
    bool squat;
    bool start;
} RflInput;

typedef struct {
    RflEntityKind kind;
    int lane;
    int z;
    int height;
    bool active;
    bool collected;
} RflEntity;

typedef struct {
    int lane;
    int jump_frames;
    int slide_frames;
    int stumble_frames;
    int bus_ride_frames;
    int jetpack_frames;
    int invisible_frames;
    bool squatting;
} RflPlayer;

typedef struct {
    RflMode mode;
    RflPlayer player;
    RflEntity entities[RFL_MAX_ENTITIES];
    uint32_t rng;
    int speed;
    int spawn_timer;
    int score;
    int coins;
    int best_score;
    int frames_alive;
    int world_offset;
    int level;
    int levelup_frames;
} RflGame;

void rfl_game_init(RflGame *game);
void rfl_game_start(RflGame *game);
void rfl_game_update(RflGame *game, const RflInput *input);

bool rfl_player_is_jumping(const RflGame *game);
bool rfl_player_is_sliding(const RflGame *game);
bool rfl_player_is_squatting(const RflGame *game);
bool rfl_player_is_low_profile(const RflGame *game);
bool rfl_player_has_jetpack(const RflGame *game);
bool rfl_player_is_invisible(const RflGame *game);
int rfl_player_jump_height(const RflGame *game);

#endif
