#ifndef RUNFORLIFE64_BUILDING_MODELS_H
#define RUNFORLIFE64_BUILDING_MODELS_H

#include <stdint.h>

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} RflModelVertex;

typedef struct {
    uint16_t a;
    uint16_t b;
    uint16_t c;
    uint8_t r;
    uint8_t g;
    uint8_t blue;
} RflModelTriangle;

typedef struct {
    const RflModelVertex *vertices;
    const RflModelTriangle *triangles;
    uint16_t vertex_count;
    uint16_t triangle_count;
} RflStaticModel;

extern const RflStaticModel rfl_building_models[];
extern const uint16_t rfl_building_model_count;
extern const RflStaticModel rfl_player_model;

#endif
