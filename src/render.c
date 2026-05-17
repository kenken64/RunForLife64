#include "render.h"

#include <display.h>
#include <graphics.h>
#include <rdpq_attach.h>
#include <rdpq_mode.h>
#include <rdpq_rect.h>
#include <rdpq_tri.h>
#include <stdio.h>

#include "building_models.h"
#include "tiny_scene.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define HORIZON_Y 70
#define PLAYER_Y 187

#define CAMERA_DEPTH 118.0f
#define FOCAL_X 155.0f
#define FOCAL_Y 142.0f
#define LANE_SPACING 54.0f
#define ROAD_HALF_W 96.0f
#define ROAD_SIDE_W 52.0f
#define FAST_PROJECTED_3D 1
#define STATIC_MODEL_SCALE 1024.0f
#define CITY_MESH_LOD_Z 250.0f

typedef enum {
    RENDER_MODE_NONE,
    RENDER_MODE_FILL,
    RENDER_MODE_TRI
} RenderMode;

static RenderMode g_render_mode = RENDER_MODE_NONE;
static uint32_t g_render_color = 0;
static RflTinyBuilding g_tiny_buildings[RFL_TINY_MAX_BUILDINGS];
static int g_tiny_building_count = 0;

static int clampi(int value, int min_value, int max_value);
void rdpq_triangle_cpu(const rdpq_trifmt_t *fmt, const float *v1, const float *v2, const float *v3);

typedef struct {
    float x;
    float y;
    float z;
} Vec3;

typedef struct {
    int x;
    int y;
    float depth;
    bool visible;
} Proj;

typedef struct {
    float x;
    float y;
    float z;
    float w;
    float h;
    float d;
    float cos_yaw;
    float sin_yaw;
    bool mirror_x;
} ModelPlacement;

static uint32_t col(int r, int g, int b)
{
    return graphics_make_color(r, g, b, 255);
}

static uint32_t rgba(int r, int g, int b, int a)
{
    return graphics_make_color(r, g, b, a);
}

void rfl_render_init(void)
{
    rfl_tiny_scene_init();
}

static color_t hw_col_from_packed(uint32_t color)
{
    if (display_get_bitdepth() == 2) {
        return color_from_packed16((uint16_t)(color & 0xffff));
    }

    return color_from_packed32(color);
}

static void use_fill_color(uint32_t color)
{
    if (g_render_mode != RENDER_MODE_FILL || g_render_color != color) {
        rdpq_set_mode_fill(hw_col_from_packed(color));
        g_render_mode = RENDER_MODE_FILL;
        g_render_color = color;
    }
}

static void use_triangle_color(uint32_t color)
{
    if (g_render_mode != RENDER_MODE_TRI) {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_dithering(DITHER_BAYER_BAYER);
        g_render_mode = RENDER_MODE_TRI;
        g_render_color = 0;
    }
    if (g_render_color != color) {
        rdpq_set_prim_color(hw_col_from_packed(color));
        g_render_color = color;
    }
}

static void draw_screen_rect(int x0, int y0, int x1, int y1, uint32_t color)
{
    x0 = clampi(x0, 0, SCREEN_W);
    y0 = clampi(y0, 0, SCREEN_H);
    x1 = clampi(x1, 0, SCREEN_W);
    y1 = clampi(y1, 0, SCREEN_H);
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    use_fill_color(color);
    rdpq_fill_rectangle(x0, y0, x1, y1);
}

static int clampi(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static float lane_world_x(int lane)
{
    return ((float)lane - 1.0f) * LANE_SPACING;
}

static Proj project_point(Vec3 p)
{
    Proj out;
    out.depth = p.z + CAMERA_DEPTH;
    out.visible = out.depth > 24.0f;

    if (!out.visible) {
        out.x = SCREEN_W / 2;
        out.y = SCREEN_H;
        return out;
    }

    float ground_y = (float)HORIZON_Y +
        ((float)(PLAYER_Y - HORIZON_Y) * CAMERA_DEPTH) / out.depth;
    out.x = SCREEN_W / 2 + (int)((p.x * FOCAL_X) / out.depth);
    out.y = (int)(ground_y - (p.y * FOCAL_Y) / out.depth);
    return out;
}

static void draw_centered_text(surface_t *surface, int y, const char *text)
{
    int len = 0;
    while (text[len]) {
        len++;
    }

    graphics_draw_text(surface, (SCREEN_W - len * 8) / 2, y, text);
}

static void draw_triangle(surface_t *surface, Proj a, Proj b, Proj c, uint32_t color)
{
    (void)surface;

    if (!a.visible || !b.visible || !c.visible) {
        return;
    }

    int area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (area > -2 && area < 2) {
        return;
    }

    if (area > 0) {
        Proj temp = b;
        b = c;
        c = temp;
    }

    use_triangle_color(color);
    float va[2] = { (float)a.x, (float)a.y };
    float vb[2] = { (float)b.x, (float)b.y };
    float vc[2] = { (float)c.x, (float)c.y };
    rdpq_triangle_cpu(&TRIFMT_FILL, va, vb, vc);
}

static void draw_projected_line(surface_t *surface, Proj a, Proj b, uint32_t color)
{
    (void)surface;

    if (!a.visible || !b.visible) {
        return;
    }

    float dx = (float)(b.x - a.x);
    float dy = (float)(b.y - a.y);
    float len2 = dx * dx + dy * dy;
    if (len2 < 1.0f) {
        draw_screen_rect(a.x, a.y, a.x + 1, a.y + 1, color);
        return;
    }

    float inv_len = 1.0f;
    if (len2 > 4.0f) {
        inv_len = 1.0f / __builtin_sqrtf(len2);
    }
    float nx = -dy * inv_len;
    float ny = dx * inv_len;
    float half = 0.75f;

    Proj p0 = { (int)((float)a.x + nx * half), (int)((float)a.y + ny * half), a.depth, true };
    Proj p1 = { (int)((float)b.x + nx * half), (int)((float)b.y + ny * half), b.depth, true };
    Proj p2 = { (int)((float)b.x - nx * half), (int)((float)b.y - ny * half), b.depth, true };
    Proj p3 = { (int)((float)a.x - nx * half), (int)((float)a.y - ny * half), a.depth, true };

    draw_triangle(surface, p0, p1, p2, color);
    draw_triangle(surface, p0, p2, p3, color);
}

static void draw_quad3d(surface_t *surface, Vec3 a, Vec3 b, Vec3 c, Vec3 d, uint32_t fill, uint32_t edge)
{
    Proj pa = project_point(a);
    Proj pb = project_point(b);
    Proj pc = project_point(c);
    Proj pd = project_point(d);

    draw_triangle(surface, pa, pb, pc, fill);
    draw_triangle(surface, pa, pc, pd, fill);

    if (edge) {
        draw_projected_line(surface, pa, pb, edge);
        draw_projected_line(surface, pb, pc, edge);
        draw_projected_line(surface, pc, pd, edge);
        draw_projected_line(surface, pd, pa, edge);
    }
}

static void draw_box3d(
    surface_t *surface,
    float x,
    float y,
    float z,
    float w,
    float h,
    float d,
    uint32_t front,
    uint32_t side,
    uint32_t top,
    uint32_t edge
)
{
    float lx = x - w * 0.5f;
    float rx = x + w * 0.5f;
    float nz = z - d * 0.5f;
    float fz = z + d * 0.5f;
    float by = y;
    float ty = y + h;

    Vec3 nlb = { lx, by, nz };
    Vec3 nrb = { rx, by, nz };
    Vec3 nrt = { rx, ty, nz };
    Vec3 nlt = { lx, ty, nz };
    Vec3 flb = { lx, by, fz };
    Vec3 frb = { rx, by, fz };
    Vec3 frt = { rx, ty, fz };
    Vec3 flt = { lx, ty, fz };

    draw_quad3d(surface, flb, frb, frt, flt, side, 0);
    draw_quad3d(surface, nlb, flb, flt, nlt, side, edge);
    draw_quad3d(surface, nrb, frb, frt, nrt, side, edge);
    draw_quad3d(surface, nlt, nrt, frt, flt, top, edge);
    draw_quad3d(surface, nlb, nrb, nrt, nlt, front, edge);
}

static Vec3 model_vertex_world(const RflModelVertex *vertex, const ModelPlacement *placement)
{
    float lx = ((float)vertex->x / STATIC_MODEL_SCALE) * placement->w * 0.5f;
    float ly = ((float)vertex->y / STATIC_MODEL_SCALE) * placement->h;
    float lz = ((float)vertex->z / STATIC_MODEL_SCALE) * placement->d * 0.5f;

    if (placement->mirror_x) {
        lx = -lx;
    }

    float rx = lx * placement->cos_yaw - lz * placement->sin_yaw;
    float rz = lx * placement->sin_yaw + lz * placement->cos_yaw;

    return (Vec3){
        placement->x + rx,
        placement->y + ly,
        placement->z + rz,
    };
}

static void draw_static_model(surface_t *surface, const RflStaticModel *model, const ModelPlacement *placement)
{
    for (uint16_t i = 0; i < model->triangle_count; i++) {
        const RflModelTriangle *triangle = &model->triangles[i];
        Vec3 a = model_vertex_world(&model->vertices[triangle->a], placement);
        Vec3 b = model_vertex_world(&model->vertices[triangle->b], placement);
        Vec3 c = model_vertex_world(&model->vertices[triangle->c], placement);

        draw_triangle(surface,
            project_point(a),
            project_point(b),
            project_point(c),
            col(triangle->r, triangle->g, triangle->blue));
    }
}

static uint32_t city_hash(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static void draw_city_model(
    surface_t *surface,
    float x,
    float z,
    float w,
    float h,
    float d,
    uint32_t seed,
    bool mirror_x
)
{
    if (z > CITY_MESH_LOD_Z) {
        uint32_t front = (seed & 1u) ? col(38, 52, 80) : col(43, 57, 87);
        uint32_t side = (seed & 2u) ? col(24, 36, 58) : col(29, 42, 66);
        uint32_t top = (seed & 4u) ? col(66, 80, 113) : col(56, 72, 105);
        draw_box3d(surface, x, 0.0f, z, w, h, d, front, side, top, col(11, 19, 35));
        return;
    }

    if (rfl_building_model_count == 0) {
        draw_box3d(surface, x, 0.0f, z, w, h, d,
            col(36, 49, 76), col(25, 35, 56), col(58, 70, 101), col(12, 20, 36));
        return;
    }

    const RflStaticModel *model = &rfl_building_models[seed % rfl_building_model_count];
    float yaw = mirror_x ? -0.18f : 0.18f;
    ModelPlacement placement = {
        .x = x,
        .y = 0.0f,
        .z = z,
        .w = w,
        .h = h,
        .d = d,
        .cos_yaw = 0.984f,
        .sin_yaw = yaw,
        .mirror_x = mirror_x,
    };
    draw_static_model(surface, model, &placement);
}

static void draw_vertical_rect3d(surface_t *surface, float x0, float x1, float y0, float y1, float z, uint32_t color)
{
    Vec3 a = { x0, y0, z };
    Vec3 b = { x1, y0, z };
    Vec3 c = { x1, y1, z };
    Vec3 d = { x0, y1, z };
    draw_quad3d(surface, a, b, c, d, color, 0);
}

static void draw_background(surface_t *surface, const RflGame *game)
{
    (void)surface;

    draw_screen_rect(0, 0, SCREEN_W, SCREEN_H, col(7, 11, 24));
    draw_screen_rect(0, 0, SCREEN_W, HORIZON_Y - 20, col(10, 16, 31));
    draw_screen_rect(0, HORIZON_Y - 20, SCREEN_W, HORIZON_Y + 46, col(18, 29, 54));
    draw_screen_rect(0, HORIZON_Y + 8, SCREEN_W, HORIZON_Y + 43, col(20, 66, 85));
    draw_screen_rect(0, HORIZON_Y, SCREEN_W, HORIZON_Y + 2, col(58, 216, 222));

    for (int i = 0; i < 10; i++) {
        int x = 4 + i * 34;
        int h = 18 + ((i * 17 + game->frames_alive / 8) % 38);
        draw_screen_rect(x, HORIZON_Y - h, x + 23, HORIZON_Y, (i & 1) ? col(27, 39, 61) : col(20, 30, 50));
        if ((i + game->frames_alive / 20) & 1) {
            draw_screen_rect(x + 6, HORIZON_Y - h + 7, x + 9, HORIZON_Y - h + 10, col(255, 214, 94));
        }
        draw_screen_rect(x + 15, HORIZON_Y - h + 13, x + 18, HORIZON_Y - h + 16, col(67, 228, 226));
    }
}

static void draw_road_segment(surface_t *surface, float near_z, float far_z, uint32_t road)
{
    Vec3 a = { -ROAD_HALF_W, 0.0f, near_z };
    Vec3 b = { ROAD_HALF_W, 0.0f, near_z };
    Vec3 c = { ROAD_HALF_W, 0.0f, far_z };
    Vec3 d = { -ROAD_HALF_W, 0.0f, far_z };
    draw_quad3d(surface, a, b, c, d, road, 0);

    Vec3 la = { -ROAD_HALF_W - ROAD_SIDE_W, 0.0f, near_z };
    Vec3 lb = { -ROAD_HALF_W, 0.0f, near_z };
    Vec3 lc = { -ROAD_HALF_W, 0.0f, far_z };
    Vec3 ld = { -ROAD_HALF_W - ROAD_SIDE_W, 0.0f, far_z };
    draw_quad3d(surface, la, lb, lc, ld, col(28, 36, 47), 0);

    Vec3 ra = { ROAD_HALF_W, 0.0f, near_z };
    Vec3 rb = { ROAD_HALF_W + ROAD_SIDE_W, 0.0f, near_z };
    Vec3 rc = { ROAD_HALF_W + ROAD_SIDE_W, 0.0f, far_z };
    Vec3 rd = { ROAD_HALF_W, 0.0f, far_z };
    draw_quad3d(surface, ra, rb, rc, rd, col(28, 36, 47), 0);

    float marks[2] = { -LANE_SPACING * 0.5f, LANE_SPACING * 0.5f };
    for (int i = 0; i < 2; i++) {
        float x = marks[i];
        Vec3 ma = { x - 1.3f, 0.3f, near_z };
        Vec3 mb = { x + 1.3f, 0.3f, near_z };
        Vec3 mc = { x + 1.3f, 0.3f, far_z };
        Vec3 md = { x - 1.3f, 0.3f, far_z };
        draw_quad3d(surface, ma, mb, mc, md, col(184, 190, 203), 0);
    }
}

static void draw_road(surface_t *surface, const RflGame *game)
{
    draw_screen_rect(0, PLAYER_Y + 30, SCREEN_W, SCREEN_H, col(12, 17, 25));

    for (int z = RFL_SPAWN_Z + 140; z > -90; z -= 72) {
        uint32_t road = ((z / 72) & 1) ? col(67, 57, 57) : col(81, 64, 55);
        draw_road_segment(surface, (float)(z - 72), (float)z, road);
    }

    int offset = 48 - (game->world_offset % 48);
    for (int z = offset; z < RFL_SPAWN_Z + 60; z += 48) {
        Vec3 a = { -ROAD_HALF_W + 10.0f, 1.0f, (float)z };
        Vec3 b = { ROAD_HALF_W - 10.0f, 1.0f, (float)z };
        Proj pa = project_point(a);
        Proj pb = project_point(b);
        draw_projected_line(surface, pa, pb, col(112, 76, 49));
    }

    Vec3 edge_l0 = { -ROAD_HALF_W, 1.0f, -80.0f };
    Vec3 edge_l1 = { -ROAD_HALF_W, 1.0f, (float)RFL_SPAWN_Z + 150.0f };
    Vec3 edge_r0 = { ROAD_HALF_W, 1.0f, -80.0f };
    Vec3 edge_r1 = { ROAD_HALF_W, 1.0f, (float)RFL_SPAWN_Z + 150.0f };
    draw_projected_line(surface, project_point(edge_l0), project_point(edge_l1), col(236, 86, 130));
    draw_projected_line(surface, project_point(edge_r0), project_point(edge_r1), col(53, 237, 202));
}

static void draw_side_city(surface_t *surface, const RflGame *game)
{
    const int spacing = 118;
    int travel = game->world_offset * 2;
    int scroll = travel % spacing;
    int base_segment = travel / spacing;

    for (int index = 0, z = 125 - scroll; z < RFL_SPAWN_Z + 220; z += spacing, index++) {
        uint32_t left_seed = city_hash((uint32_t)(base_segment + index) * 2u + 3u);
        uint32_t right_seed = city_hash((uint32_t)(base_segment + index) * 2u + 19u);

        float left_h = (float)(118 + (int)(left_seed & 87u));
        float left_w = (float)(66 + (int)((left_seed >> 8) & 39u));
        float left_d = (float)(88 + (int)((left_seed >> 16) & 47u));
        float right_h = (float)(132 + (int)(right_seed & 95u));
        float right_w = (float)(66 + (int)((right_seed >> 8) & 39u));
        float right_d = (float)(88 + (int)((right_seed >> 16) & 47u));

        float left_x = -142.0f - left_w * 0.5f;
        float right_x = 142.0f + right_w * 0.5f;
        float right_z = (float)z + 45.0f;
        bool tiny_left = rfl_tiny_scene_ready() && z > 70 && z < 390 && ((left_seed >> 3) & 3u) == 0u;
        bool tiny_right = rfl_tiny_scene_ready() && right_z > 70.0f && right_z < 390.0f && ((right_seed >> 4) & 3u) == 1u;

        if (tiny_left && g_tiny_building_count < RFL_TINY_MAX_BUILDINGS) {
            g_tiny_buildings[g_tiny_building_count++] = (RflTinyBuilding){
                .x = left_x,
                .z = (float)z,
                .w = left_w,
                .h = left_h,
                .d = left_d,
                .mirror_x = false,
            };
        } else {
            draw_city_model(surface, left_x, (float)z,
                left_w, left_h, left_d, left_seed, false);
        }

        if (tiny_right && g_tiny_building_count < RFL_TINY_MAX_BUILDINGS) {
            g_tiny_buildings[g_tiny_building_count++] = (RflTinyBuilding){
                .x = right_x,
                .z = right_z,
                .w = right_w,
                .h = right_h,
                .d = right_d,
                .mirror_x = true,
            };
        } else {
            draw_city_model(surface, right_x, right_z,
                right_w, right_h, right_d, right_seed, true);
        }
    }
}

static void draw_player_humanoid(
    surface_t *surface,
    const RflGame *game,
    float x,
    float y,
    float z,
    bool sliding,
    bool squatting,
    bool invisible
)
{
    uint32_t body = invisible ? col(80, 215, 235) : col(220, 94, 72);
    uint32_t body_side = invisible ? col(30, 116, 150) : col(119, 48, 50);
    uint32_t highlight = invisible ? col(170, 247, 255) : col(255, 174, 103);
    uint32_t skin = invisible ? col(132, 231, 246) : col(231, 174, 124);
    uint32_t dark = invisible ? col(17, 69, 91) : col(33, 28, 34);
    uint32_t boot = invisible ? col(21, 86, 111) : col(28, 34, 46);
    float stride = ((game->frames_alive / 7) & 1) ? 4.0f : -4.0f;
    float shadow_scale = 1.0f - y * 0.0065f;
    if (shadow_scale < 0.42f) {
        shadow_scale = 0.42f;
    }

    draw_box3d(surface, x, 0.4f, z + 5.0f, 48.0f * shadow_scale, 1.2f, 34.0f * shadow_scale,
        col(5, 8, 13), col(5, 8, 13), col(5, 8, 13), 0);

    if (sliding) {
        draw_box3d(surface, x + 4.0f, y + 8.0f, z, 48.0f, 15.0f, 38.0f,
            body, body_side, highlight, dark);
        draw_box3d(surface, x - 28.0f, y + 13.0f, z + 1.0f, 14.0f, 12.0f, 14.0f,
            skin, body_side, highlight, dark);
        draw_box3d(surface, x + 27.0f, y + 5.0f, z + 2.0f, 18.0f, 8.0f, 12.0f,
            boot, dark, body_side, 0);
        draw_box3d(surface, x - 4.0f, y + 3.0f, z - 12.0f, 36.0f, 6.0f, 8.0f,
            body_side, dark, body, 0);
        return;
    }

    if (squatting) {
        draw_box3d(surface, x - 11.0f, y + 1.0f, z + stride, 9.0f, 14.0f, 9.0f,
            boot, dark, body_side, 0);
        draw_box3d(surface, x + 11.0f, y + 1.0f, z - stride, 9.0f, 14.0f, 9.0f,
            boot, dark, body_side, 0);
        draw_box3d(surface, x, y + 13.0f, z, 27.0f, 22.0f, 17.0f,
            body, body_side, highlight, dark);
        draw_box3d(surface, x - 22.0f, y + 15.0f, z + 2.0f, 7.0f, 15.0f, 7.0f,
            skin, body_side, highlight, 0);
        draw_box3d(surface, x + 22.0f, y + 15.0f, z - 2.0f, 7.0f, 15.0f, 7.0f,
            skin, body_side, highlight, 0);
        draw_box3d(surface, x, y + 35.0f, z + 1.0f, 16.0f, 13.0f, 14.0f,
            skin, body_side, highlight, dark);
        return;
    }

    draw_box3d(surface, x - 9.0f, y + 1.0f, z + stride, 8.0f, 21.0f, 9.0f,
        boot, dark, body_side, 0);
    draw_box3d(surface, x + 9.0f, y + 1.0f, z - stride, 8.0f, 21.0f, 9.0f,
        boot, dark, body_side, 0);
    draw_box3d(surface, x, y + 20.0f, z, 24.0f, 24.0f, 17.0f,
        body, body_side, highlight, dark);
    draw_box3d(surface, x - 20.0f, y + 22.0f, z - stride, 7.0f, 20.0f, 7.0f,
        skin, body_side, highlight, 0);
    draw_box3d(surface, x + 20.0f, y + 22.0f, z + stride, 7.0f, 20.0f, 7.0f,
        skin, body_side, highlight, 0);
    draw_box3d(surface, x, y + 44.0f, z + 1.0f, 16.0f, 14.0f, 14.0f,
        skin, body_side, highlight, dark);
    draw_box3d(surface, x, y + 55.0f, z - 1.0f, 18.0f, 5.0f, 15.0f,
        dark, dark, body_side, 0);
}

static void draw_coin(surface_t *surface, float x, float y, float z)
{
    float r = 8.0f;
    Vec3 top = { x, y + r, z };
    Vec3 right = { x + r * 0.7f, y, z };
    Vec3 bottom = { x, y - r, z };
    Vec3 left = { x - r * 0.7f, y, z };
    draw_quad3d(surface, top, right, bottom, left, col(255, 220, 72), col(218, 148, 39));
    draw_projected_line(surface, project_point((Vec3){ x, y + r - 2.0f, z }),
        project_point((Vec3){ x, y - r + 2.0f, z }), col(255, 247, 158));
}

static void draw_gate(surface_t *surface, float x, float z)
{
    uint32_t front = col(255, 75, 124);
    uint32_t side = col(117, 31, 72);
    uint32_t top = col(255, 124, 160);
    uint32_t edge = col(255, 184, 201);

    draw_box3d(surface, x - 23.0f, 0.0f, z, 7.0f, 58.0f, 14.0f, front, side, top, edge);
    draw_box3d(surface, x + 23.0f, 0.0f, z, 7.0f, 58.0f, 14.0f, front, side, top, edge);
    draw_box3d(surface, x, 49.0f, z, 53.0f, 9.0f, 14.0f, front, side, top, edge);
}

static void draw_bus(surface_t *surface, float x, float z)
{
    float near_z = z - 37.0f;

    draw_box3d(surface, x, 0.0f, z, 58.0f, 34.0f, 74.0f,
        col(244, 178, 46), col(183, 113, 35), col(255, 226, 89), col(49, 40, 44));
    draw_vertical_rect3d(surface, x - 20.0f, x + 20.0f, 20.0f, 29.0f, near_z - 1.0f, col(78, 219, 231));
    draw_box3d(surface, x - 19.0f, 0.0f, near_z + 6.0f, 12.0f, 8.0f, 8.0f,
        col(17, 22, 30), col(9, 12, 18), col(43, 47, 54), 0);
    draw_box3d(surface, x + 19.0f, 0.0f, near_z + 6.0f, 12.0f, 8.0f, 8.0f,
        col(17, 22, 30), col(9, 12, 18), col(43, 47, 54), 0);
}

static void draw_entity(surface_t *surface, const RflEntity *entity)
{
    float x = lane_world_x(entity->lane);
    float z = (float)entity->z;
    float y = (float)entity->height;

    switch (entity->kind) {
    case RFL_ENTITY_BARRIER:
        draw_box3d(surface, x, 0.0f, z, 40.0f, 25.0f, 19.0f,
            col(236, 198, 57), col(126, 82, 38), col(255, 236, 112), col(70, 43, 40));
        break;
    case RFL_ENTITY_GATE:
        draw_gate(surface, x, z);
        break;
    case RFL_ENTITY_BLOCK:
        draw_box3d(surface, x, 0.0f, z, 38.0f, 50.0f, 38.0f,
            col(92, 116, 246), col(38, 54, 161), col(163, 183, 255), col(25, 34, 83));
        break;
    case RFL_ENTITY_BUS:
        draw_bus(surface, x, z);
        break;
    case RFL_ENTITY_COIN:
        draw_coin(surface, x, y + 18.0f, z);
        break;
    case RFL_ENTITY_JETPACK:
        draw_box3d(surface, x, 12.0f, z, 22.0f, 27.0f, 18.0f,
            col(50, 67, 94), col(30, 42, 66), col(161, 190, 216), col(255, 221, 79));
        draw_box3d(surface, x - 6.0f, 4.0f, z - 3.0f, 4.0f, 10.0f, 5.0f,
            col(255, 121, 52), col(150, 61, 24), col(255, 221, 79), 0);
        draw_box3d(surface, x + 6.0f, 4.0f, z - 3.0f, 4.0f, 10.0f, 5.0f,
            col(255, 121, 52), col(150, 61, 24), col(255, 221, 79), 0);
        break;
    case RFL_ENTITY_INVISIBLE:
        draw_coin(surface, x, y + 22.0f, z);
        draw_projected_line(surface, project_point((Vec3){ x - 13.0f, y + 22.0f, z }),
            project_point((Vec3){ x + 13.0f, y + 22.0f, z }), col(145, 235, 255));
        break;
    default:
        break;
    }
}

static void draw_entities(surface_t *surface, const RflGame *game)
{
    int order[RFL_MAX_ENTITIES];
    int count = 0;

    for (int i = 0; i < RFL_MAX_ENTITIES; i++) {
        if (game->entities[i].active) {
            int j = count;
            while (j > 0 && game->entities[order[j - 1]].z < game->entities[i].z) {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = i;
            count++;
        }
    }

    for (int i = 0; i < count; i++) {
        draw_entity(surface, &game->entities[order[i]]);
    }
}

static void draw_player(surface_t *surface, const RflGame *game)
{
    float x = lane_world_x(game->player.lane);
    float y = (float)rfl_player_jump_height(game);
    float z = -10.0f;
    bool sliding = rfl_player_is_sliding(game);
    bool squatting = rfl_player_is_squatting(game);
    bool jetpack = rfl_player_has_jetpack(game);
    bool invisible = rfl_player_is_invisible(game);

    float player_w = sliding ? 58.0f : 52.0f;
    float player_h = sliding ? 28.0f : (squatting ? 36.0f : 44.0f);

    if (game->player.stumble_frames > 0) {
        draw_box3d(surface, x, y + 2.0f, z + 1.0f, player_w + 8.0f, player_h + 8.0f, 8.0f,
            col(255, 80, 80), col(120, 35, 35), col(255, 147, 104), 0);
    }

    if (invisible) {
        draw_box3d(surface, x, y + 3.0f, z + 2.0f, player_w + 4.0f, player_h + 4.0f, 6.0f,
            col(80, 215, 235), col(32, 118, 155), col(169, 247, 255), 0);
    }

    if (jetpack) {
        draw_box3d(surface, x - 20.0f, y + 10.0f, z + 10.0f, 5.0f, 26.0f, 7.0f,
            col(49, 64, 88), col(25, 34, 55), col(161, 190, 216), 0);
        draw_box3d(surface, x + 20.0f, y + 10.0f, z + 10.0f, 5.0f, 26.0f, 7.0f,
            col(49, 64, 88), col(25, 34, 55), col(161, 190, 216), 0);
        draw_box3d(surface, x - 20.0f, y - 12.0f, z + 10.0f, 4.0f, 12.0f, 5.0f,
            col(255, 122, 52), col(150, 61, 24), col(255, 221, 79), 0);
        draw_box3d(surface, x + 20.0f, y - 12.0f, z + 10.0f, 4.0f, 12.0f, 5.0f,
            col(255, 122, 52), col(150, 61, 24), col(255, 221, 79), 0);
    }

    draw_player_humanoid(surface, game, x, y, z, sliding, squatting, invisible);
}

static void draw_hud(surface_t *surface, const RflGame *game)
{
    char text[48];
    graphics_set_color(col(237, 245, 255), rgba(0, 0, 0, 0));

    snprintf(text, sizeof(text), "SCORE %06d", game->score / 10);
    graphics_draw_text(surface, 10, 10, text);

    snprintf(text, sizeof(text), "COINS %03d", game->coins);
    graphics_draw_text(surface, 220, 10, text);

    snprintf(text, sizeof(text), "LVL %02d", game->level);
    graphics_draw_text(surface, 128, 10, text);

    snprintf(text, sizeof(text), "SPD %02d", game->speed);
    graphics_draw_text(surface, 10, 222, text);

    snprintf(text, sizeof(text), "FPS %02d", (int)(display_get_fps() + 0.5f));
    graphics_draw_text(surface, 250, 222, text);

    if (game->mode == RFL_MODE_RUNNING && game->levelup_frames > 0) {
        graphics_set_color(col(255, 221, 79), rgba(0, 0, 0, 0));
        snprintf(text, sizeof(text), "LEVEL %02d", game->level);
        draw_centered_text(surface, 34, text);
        graphics_set_color(col(237, 245, 255), rgba(0, 0, 0, 0));
    }

    if (rfl_player_has_jetpack(game)) {
        snprintf(text, sizeof(text), "JET %02d", (game->player.jetpack_frames + 59) / 60);
        graphics_set_color(col(255, 221, 79), rgba(0, 0, 0, 0));
        graphics_draw_text(surface, 101, 222, text);
    }

    if (rfl_player_is_invisible(game)) {
        snprintf(text, sizeof(text), "INV %02d", (game->player.invisible_frames + 59) / 60);
        graphics_set_color(col(145, 235, 255), rgba(0, 0, 0, 0));
        graphics_draw_text(surface, 200, 222, text);
    }
}

static void draw_title(surface_t *surface)
{
    graphics_set_color(col(237, 245, 255), rgba(0, 0, 0, 0));
    draw_centered_text(surface, 58, "RUN FOR LIFE 64");
    graphics_set_color(col(53, 237, 202), rgba(0, 0, 0, 0));
    draw_centered_text(surface, 82, "A / START TO RUN");
    graphics_set_color(col(182, 196, 218), rgba(0, 0, 0, 0));
    draw_centered_text(surface, 112, "LEFT RIGHT: CHANGE LANE");
    draw_centered_text(surface, 126, "A:JUMP B/Z:SLIDE DOWN:SQUAT");
    draw_centered_text(surface, 140, "BUS COINS NEED A JUMP");
}

static void draw_game_over(surface_t *surface, const RflGame *game)
{
    char text[48];

    graphics_draw_box_trans(surface, 36, 63, 248, 105, rgba(0, 0, 0, 190));
    graphics_set_color(col(255, 106, 106), rgba(0, 0, 0, 0));
    draw_centered_text(surface, 74, "RUN ENDED");

    graphics_set_color(col(237, 245, 255), rgba(0, 0, 0, 0));
    snprintf(text, sizeof(text), "SCORE %06d", game->score / 10);
    draw_centered_text(surface, 99, text);
    snprintf(text, sizeof(text), "LEVEL %02d", game->level);
    draw_centered_text(surface, 114, text);
    snprintf(text, sizeof(text), "BEST  %06d", game->best_score / 10);
    draw_centered_text(surface, 129, text);

    graphics_set_color(col(53, 237, 202), rgba(0, 0, 0, 0));
    draw_centered_text(surface, 150, "A / START TO RETRY");
}

static void draw_pause(surface_t *surface)
{
    graphics_draw_box_trans(surface, 46, 79, 228, 63, rgba(0, 0, 0, 190));
    graphics_set_color(col(255, 221, 79), rgba(0, 0, 0, 0));
    draw_centered_text(surface, 96, "PAUSED");
    graphics_set_color(col(237, 245, 255), rgba(0, 0, 0, 0));
    draw_centered_text(surface, 119, "START TO RESUME");
}

void rfl_render(surface_t *surface, surface_t *zbuffer, const RflGame *game)
{
    g_render_mode = RENDER_MODE_NONE;
    g_render_color = 0;
    g_tiny_building_count = 0;

    rdpq_attach(surface, zbuffer);
    draw_background(surface, game);
    draw_road(surface, game);
    draw_side_city(surface, game);
    draw_entities(surface, game);
    draw_player(surface, game);
    rfl_tiny_scene_render(game, g_tiny_buildings, g_tiny_building_count);
    rdpq_detach_wait();

    draw_hud(surface, game);

    if (game->mode == RFL_MODE_TITLE) {
        draw_title(surface);
    } else if (game->mode == RFL_MODE_PAUSED) {
        draw_pause(surface);
    } else if (game->mode == RFL_MODE_GAME_OVER) {
        draw_game_over(surface, game);
    }
}
