#include "tiny_scene.h"

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

#define TINY_FB_COUNT 3
#define TINY_MAX_DRAWS RFL_TINY_MAX_BUILDINGS

static T3DViewport g_viewport;
static T3DMat4FP *g_matrices;
static T3DModel *g_building_model;
static int g_frame_index;
static bool g_ready;

static float model_span_x(const T3DModel *model)
{
    int span = model->aabbMax[0] - model->aabbMin[0];
    return span > 1 ? (float)span : 1.0f;
}

static float model_span_y(const T3DModel *model)
{
    int span = model->aabbMax[1] - model->aabbMin[1];
    return span > 1 ? (float)span : 1.0f;
}

static float model_span_z(const T3DModel *model)
{
    int span = model->aabbMax[2] - model->aabbMin[2];
    return span > 1 ? (float)span : 1.0f;
}

static void make_grounded_matrix(
    T3DMat4FP *out,
    const T3DModel *model,
    float x,
    float y,
    float z,
    float sx,
    float sy,
    float sz,
    float yaw
)
{
    float center_x = ((float)model->aabbMin[0] + (float)model->aabbMax[0]) * 0.5f;
    float center_z = ((float)model->aabbMin[2] + (float)model->aabbMax[2]) * 0.5f;

    t3d_mat4fp_from_srt_euler(out,
        (float[3]){ sx, sy, sz },
        (float[3]){ 0.0f, yaw, 0.0f },
        (float[3]){
            x - center_x * sx,
            y - (float)model->aabbMin[1] * sy,
            z - center_z * sz,
        });
}

void rfl_tiny_scene_init(void)
{
    t3d_init((T3DInitParams){ .matrixStackSize = 12 });
    g_viewport = t3d_viewport_create_buffered(TINY_FB_COUNT);
    g_matrices = malloc_uncached(sizeof(T3DMat4FP) * TINY_FB_COUNT * TINY_MAX_DRAWS);

    g_building_model = t3d_model_load("rom:/models/building2.t3dm");
    g_ready = g_matrices && g_building_model;
}

bool rfl_tiny_scene_ready(void)
{
    return g_ready;
}

void rfl_tiny_scene_render(const RflGame *game, const RflTinyBuilding *buildings, int building_count)
{
    (void)game;

    if (!g_ready) {
        return;
    }

    g_frame_index = (g_frame_index + 1) % TINY_FB_COUNT;
    T3DMat4FP *matrices = &g_matrices[g_frame_index * TINY_MAX_DRAWS];

    const T3DVec3 cam_pos = {{ 0.0f, 68.0f, -128.0f }};
    const T3DVec3 cam_target = {{ 0.0f, 26.0f, 250.0f }};
    const T3DVec3 cam_up = {{ 0.0f, 1.0f, 0.0f }};

    t3d_viewport_set_projection(&g_viewport, T3D_DEG_TO_RAD(74.0f), 12.0f, 960.0f);
    t3d_viewport_look_at(&g_viewport, &cam_pos, &cam_target, &cam_up);

    t3d_frame_start();
    t3d_viewport_attach(&g_viewport);
    t3d_screen_clear_depth();

    uint8_t ambient[4] = { 92, 100, 118, 255 };
    uint8_t sunlight[4] = { 210, 190, 170, 255 };
    T3DVec3 light_dir = {{ -0.45f, 0.8f, 0.35f }};
    t3d_vec3_norm(&light_dir);
    t3d_light_set_ambient(ambient);
    t3d_light_set_directional(0, sunlight, &light_dir);
    t3d_light_set_count(1);

    int draw_index = 0;
    int limit = building_count < RFL_TINY_MAX_BUILDINGS ? building_count : RFL_TINY_MAX_BUILDINGS;
    for (int i = 0; i < limit; i++) {
        const RflTinyBuilding *building = &buildings[i];
        float sx = building->w / model_span_x(g_building_model);
        float sy = building->h / model_span_y(g_building_model);
        float sz = building->d / model_span_z(g_building_model);
        make_grounded_matrix(&matrices[draw_index], g_building_model,
            building->x, 0.0f, building->z,
            sx, sy, sz,
            building->mirror_x ? -0.18f : 0.18f);

        rdpq_set_prim_color(RGBA32(80, 101, 130, 255));
        t3d_matrix_push(&matrices[draw_index]);
        t3d_model_draw(g_building_model);
        t3d_matrix_pop(1);
        draw_index++;
    }
}
