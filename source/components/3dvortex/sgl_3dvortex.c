/* source/components/sgl_3dvortex.c */
#include <sgl_theme.h>
#include "sgl_3dvortex.h"

#define VORTEX_HEIGHT   25.0f
#define DISTRIBUTION_R  1024.0f
#define INIT_V          0.01f
#define SCALE_BASE      500.0f
#define CAM_Z_INIT      -14.0f
#define FLOOR_Y         26.5f

typedef struct {
    float x, y, z;
    float vy;
    float radius;
    float color;
    float dist;
    int   active;
} VtxParticle;

typedef struct { float x, y, d; int valid; } ProjPoint;

typedef struct {
    float camX, camY, camZ;
    float yaw, pitch;
    float cx, cy;
    int   frameNo;
    int   width, height;
    VtxParticle *particles;
    int   particleCount;
    int   max_particles;
} VortexState;

static ProjPoint project3D(float x, float y, float z, const VortexState *v)
{
    ProjPoint p = {0.0f, 0.0f, -1.0f, 0};
    x -= v->camX;
    y -= v->camY - 8.0f;
    z -= v->camZ;

    float ang = sgl_atan2f_rad(z, x);
    float d   = sgl_sqrtf(x * x + z * z);
    x = sgl_sinf_rad(ang - v->yaw) * d;
    z = sgl_cosf_rad(ang - v->yaw) * d;

    ang = sgl_atan2f_rad(z, y);
    d   = sgl_sqrtf(y * y + z * z);
    y = sgl_sinf_rad(ang - v->pitch) * d;
    z = sgl_cosf_rad(ang - v->pitch) * d;

    if (sgl_fabsf(z) < 0.0001f) z = (z >= 0.0f) ? 0.0001f : -0.0001f;

    if (z > 0.1f) {
        float scale = SCALE_BASE * (float)sgl_min(v->width, v->height) / 720.0f;
        p.x = v->cx + x / z * scale;
        p.y = v->cy + y / z * scale;
        p.d = x * x + y * y + z * z;
        p.valid = 1;
    }
    return p;
}

static sgl_color_t rgb_from_col(float col)
{
    col += 0.000001f;
    int r = (int)((0.5f + sgl_sinf_rad(col) * 0.5f) * 255.0f);
    int g = (int)((0.5f + sgl_cosf_rad(col) * 0.5f) * 255.0f);
    int b = (int)((0.5f - sgl_sinf_rad(col) * 0.5f) * 255.0f);
    r = sgl_clamp(r, 0, 255);
    g = sgl_clamp(g, 0, 255);
    b = sgl_clamp(b, 0, 255);
    return sgl_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

static void spawn_particle(VortexState *v)
{
    VtxParticle *pt = NULL;
    for (int i = 0; i < v->max_particles; i++) {
        if (!v->particles[i].active) { pt = &v->particles[i]; break; }
    }
    if (!pt) return;

    float p_angle = SGL_PI * 2 * sgl_randf();
    float ls = sgl_sqrtf(sgl_randf() * DISTRIBUTION_R);

    pt->x = sgl_sinf_rad(p_angle) * ls;
    pt->y = -VORTEX_HEIGHT / 2.0f;
    pt->vy = INIT_V / 20.0f + sgl_randf() * INIT_V;
    pt->z = sgl_cosf_rad(p_angle) * ls;
    pt->radius = 200.0f + 800.0f * sgl_randf();
    pt->color = pt->radius / 1000.0f + (float)v->frameNo / 250.0f;
    pt->dist = 0.0f;
    pt->active = 1;
    v->particleCount++;
}

static void process_particles(VortexState *v)
{
    while (v->particleCount < SGL_VORTEX_INIT_PARTICLES) {
        spawn_particle(v);
        if (v->particleCount >= SGL_VORTEX_INIT_PARTICLES) break;
    }

    float p = sgl_atan2f_rad(v->camZ, v->camX);
    float d = sgl_sqrtf(v->camX * v->camX + v->camZ * v->camZ);
    d -= sgl_sinf_rad((float)v->frameNo / 80.0f) / 25.0f;
    float t = sgl_cosf_rad((float)v->frameNo / 300.0f) / 165.0f;
    v->camX = sgl_sinf_rad(p + t) * d;
    v->camZ = sgl_cosf_rad(p + t) * d;
    v->camY = -sgl_sinf_rad((float)v->frameNo / 220.0f) * 15.0f;
    v->yaw = (float)SGL_PI + p + t;

    float elev_dist = sgl_sqrtf(v->camX*v->camX + v->camZ*v->camZ + v->camY*v->camY);
    float cos_val = (elev_dist > 0.0f) ? (v->camZ / elev_dist) : 0.0f;
    cos_val = sgl_clamp(cos_val, -1.0f, 1.0f);
    float sin_val = sgl_sqrtf(sgl_max(1.0f - cos_val * cos_val, 0.0f));
    float elev_val = sgl_atan2f_rad(cos_val, sin_val);
    v->pitch = elev_val - SGL_PI / 2.0f;

    for (int i = 0; i < v->max_particles; i++) {
        VtxParticle *pt = &v->particles[i];
        if (!pt->active) continue;

        float dx = pt->x, dz = pt->z;
        float dd = sgl_sqrtf(dx * dx + dz * dz) / 1.0075f;
        float tt = 0.1f / (1.0f + dd * dd / 5.0f);
        float pp = sgl_atan2f_rad(dz, dx) + tt;

        pt->x = sgl_sinf_rad(pp) * dd;
        pt->z = sgl_cosf_rad(pp) * dd;
        pt->y += pt->vy * tt * ((sgl_sqrtf(DISTRIBUTION_R) - dd) * 2.0f);

        if (pt->y > VORTEX_HEIGHT / 2.0f || dd < 0.25f) {
            pt->active = 0;
            v->particleCount--;
            spawn_particle(v);
        }
    }
}

static void apply_trail(sgl_surf_t *surf, const sgl_area_t *clip, uint8_t trail_alpha)
{
    sgl_draw_fill_rect(surf, (sgl_area_t *)clip, (sgl_area_t *)clip, 0, SGL_COLOR_BLACK, trail_alpha);
}

static void draw_floor(sgl_surf_t *surf, sgl_area_t *clip, const VortexState *v)
{
    for (int sign = 0; sign < 2; sign++) {
        float floor_y = sign ? -FLOOR_Y : FLOOR_Y;
        float y_dir   = sign ? 1.0f : -1.0f;

        sgl_color_t base_col = sign ? sgl_rgb(0x20, 0x00, 0x80) : sgl_rgb(0x00, 0x80, 0x20);

        for (int i = -25; i <= 25; i++) {
            for (int j = -25; j <= 25; j++) {
                float fx = i * 2.0f;
                float fz = j * 2.0f;
                float fd = sgl_sqrtf(fx * fx + fz * fz);
                float fy = floor_y + y_dir * fd * fd / 85.0f;

                ProjPoint proj = project3D(fx, fy, fz, v);
                if (!proj.valid) continue;

                float size = 1.0f + 15000.0f / (1.0f + proj.d);
                float fd_ratio = fd / 50.0f;
                float fd_ratio_sq = fd_ratio * fd_ratio;
                float alpha = 0.15f - fd_ratio_sq * fd_ratio_sq * 0.15f;
                if (alpha <= 0.001f) continue;

                float col_param = sign ? (-fd / 26.0f - (float)v->frameNo / 40.0f)
                                       : (fd / 26.0f - (float)v->frameNo / 40.0f);
                float blend = 0.5f + sgl_sinf_rad(sign ? (-fd/6.0f - (float)v->frameNo/8.0f)
                                                  : (fd/6.0f - (float)v->frameNo/8.0f)) / 2.0f;

                sgl_color_t dyn_col = rgb_from_col(col_param);
                sgl_color_t final_col = sgl_color_mixer(dyn_col, base_col, (uint8_t)(blend * 255.0f));

                int px = (int)(proj.x - size * 0.5f) + v->cx - (float)v->width / 2.0f;
                int py = (int)(proj.y - size * 0.5f) + v->cy - (float)v->height / 2.0f;
                int ps = (int)size;
                if (ps < 1) ps = 1;

                sgl_area_t rect = {
                    .x1 = (int16_t)px,
                    .y1 = (int16_t)py,
                    .x2 = (int16_t)(px + ps - 1),
                    .y2 = (int16_t)(py + ps - 1)
                };
                sgl_draw_fill_rect(surf, clip, &rect, 0, final_col, (uint8_t)(alpha * 255.0f));
            }
        }
    }
}

static void sgl_3dvortex_construct_cb(sgl_surf_t *surf, sgl_obj_t* obj, sgl_event_t *evt)
{
    sgl_3dvortex_t *vortex = sgl_container_of(obj, sgl_3dvortex_t, obj);
    VortexState *v = (VortexState *)vortex->state;

    if (evt->type != SGL_EVENT_DRAW_MAIN) return;
    if (!v || !vortex->running) return;

    sgl_area_t clip = SGL_AREA_MAX;
    sgl_surf_clip_area_return(surf, &obj->area, &clip);

    if (vortex->update) {
        v->width  = obj->coords.x2 - obj->coords.x1 + 1;
        v->height = obj->coords.y2 - obj->coords.y1 + 1;
        v->cx = (float)v->width / 2.0f;
        v->cy = (float)v->height / 2.0f;
        process_particles(v);
    }

    if (vortex->update) {
        apply_trail(surf, &clip, vortex->trail_alpha);
    }
    draw_floor(surf, &clip, v);

    typedef struct { int idx; float dist; } VisParticle_t;
    VisParticle_t visible[SGL_VORTEX_MAX_PARTICLES];
    int vis_count = 0;

    for (int i = 0; i < v->max_particles && vis_count < SGL_VORTEX_MAX_PARTICLES; i++) {
        if (!v->particles[i].active) continue;
        ProjPoint proj = project3D(v->particles[i].x, v->particles[i].y,
                                    v->particles[i].z, v);
        if (proj.valid) {
            v->particles[i].dist = proj.d;
            visible[vis_count].idx = i;
            visible[vis_count].dist = proj.d;
            vis_count++;
        }
    }

    for (int i = 0; i < vis_count - 1; i++) {
        for (int j = i + 1; j < vis_count; j++) {
            if (visible[j].dist > visible[i].dist) {
                VisParticle_t tmp = visible[i];
                visible[i] = visible[j];
                visible[j] = tmp;
            }
        }
    }

    int obj_x = obj->coords.x1;
    int obj_y = obj->coords.y1;
    for (int vi = 0; vi < vis_count; vi++) {
        VtxParticle *pt = &v->particles[visible[vi].idx];
        ProjPoint proj = project3D(pt->x, pt->y, pt->z, v);
        if (!proj.valid) continue;

        float size = 1.0f + pt->radius / (1.0f + proj.d);
        float dy = sgl_fabsf(pt->y);
        float half_h = VORTEX_HEIGHT / 2.0f;

        float ratio = dy / half_h;
        float alpha_f = 0.8f;
        if (ratio > 0.95f) alpha_f *= sgl_max(1.0f - (ratio - 0.95f) * 20.0f, 0.0f);
        uint8_t alpha = (uint8_t)(sgl_clamp(alpha_f, 0.0f, 1.0f) * 255.0f);

        sgl_color_t col = rgb_from_col(pt->color);

        int px = (int)(proj.x - size * 0.5f) + obj_x;
        int py = (int)(proj.y - size * 0.5f) + obj_y;
        int ps = (int)size;
        if (ps < 1) ps = 1;

        sgl_area_t rect = {
            .x1 = (int16_t)px,
            .y1 = (int16_t)py,
            .x2 = (int16_t)(px + ps - 1),
            .y2 = (int16_t)(py + ps - 1)
        };
        sgl_draw_fill_rect(surf, &clip, &rect, 0, col, alpha);
    }

    vortex->update = false;
    if (clip.y2 == surf->dirty->y2) {
        v->frameNo++;
        sgl_obj_set_dirty(obj);
        vortex->update = true;
    }
}

sgl_obj_t* sgl_3dvortex_create(sgl_obj_t* parent)
{
    sgl_3dvortex_t *vortex = sgl_malloc(sizeof(sgl_3dvortex_t));
    if (!vortex) {
        SGL_LOG_ERROR("sgl_3dvortex_create: malloc failed");
        return NULL;
    }
    memset(vortex, 0, sizeof(sgl_3dvortex_t));

    VortexState *v = sgl_malloc(sizeof(VortexState));
    if (!v) {
        sgl_free(vortex);
        SGL_LOG_ERROR("sgl_3dvortex_create: state malloc failed");
        return NULL;
    }
    memset(v, 0, sizeof(VortexState));

    v->max_particles = SGL_VORTEX_MAX_PARTICLES;
    v->particles = sgl_malloc(sizeof(VtxParticle) * SGL_VORTEX_MAX_PARTICLES);
    if (!v->particles) {
        sgl_free(v);
        sgl_free(vortex);
        SGL_LOG_ERROR("sgl_3dvortex_create: particles malloc failed");
        return NULL;
    }
    memset(v->particles, 0, sizeof(VtxParticle) * SGL_VORTEX_MAX_PARTICLES);
    v->camZ = CAM_Z_INIT;

    vortex->state = v;
    vortex->running = true;
    vortex->trail_alpha = SGL_VORTEX_TRAIL_ALPHA;
    vortex->update = true;

    sgl_obj_t *obj = &vortex->obj;
    sgl_obj_init(obj, parent);
    obj->construct_fn = sgl_3dvortex_construct_cb;

    return obj;
}

void sgl_3dvortex_set_running(sgl_obj_t *obj, bool running)
{
    sgl_3dvortex_t *vortex = sgl_container_of(obj, sgl_3dvortex_t, obj);
    vortex->running = running;
    sgl_obj_set_dirty(obj);
}

void sgl_3dvortex_set_trail_alpha(sgl_obj_t *obj, uint8_t alpha)
{
    sgl_3dvortex_t *vortex = sgl_container_of(obj, sgl_3dvortex_t, obj);
    vortex->trail_alpha = alpha;
}

void sgl_3dvortex_reset(sgl_obj_t *obj)
{
    sgl_3dvortex_t *vortex = sgl_container_of(obj, sgl_3dvortex_t, obj);
    VortexState *v = (VortexState *)vortex->state;
    if (v && v->particles) {
        memset(v->particles, 0, sizeof(VtxParticle) * v->max_particles);
        v->particleCount = 0;
        v->frameNo = 0;
        v->camZ = CAM_Z_INIT;
    }
    vortex->update = true;
    sgl_obj_set_dirty(obj);
}
