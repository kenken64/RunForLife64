#ifndef RUNFORLIFE64_TINY_SCENE_H
#define RUNFORLIFE64_TINY_SCENE_H

#include <stdbool.h>

#include "game.h"

#define RFL_TINY_MAX_BUILDINGS 4

typedef struct {
    float x;
    float z;
    float w;
    float h;
    float d;
    bool mirror_x;
} RflTinyBuilding;

void rfl_tiny_scene_init(void);
bool rfl_tiny_scene_ready(void);
void rfl_tiny_scene_render(const RflGame *game, const RflTinyBuilding *buildings, int building_count);

#endif
