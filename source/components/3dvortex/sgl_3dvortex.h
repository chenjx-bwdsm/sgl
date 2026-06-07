/* source/components/sgl_3dvortex.h */
#ifndef __SGL_3DVORTEX_H__
#define __SGL_3DVORTEX_H__

#include <sgl_core.h>
#include <sgl_draw.h>
#include <sgl_math.h>
#include <sgl_log.h>
#include <sgl_mm.h>
#include <sgl_cfgfix.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SGL_VORTEX_MAX_PARTICLES    150
#define SGL_VORTEX_INIT_PARTICLES   150
#define SGL_VORTEX_TRAIL_ALPHA      38

typedef struct sgl_3dvortex {
    sgl_obj_t       obj;
    void           *state;
    bool            running;
    uint8_t         trail_alpha;
} sgl_3dvortex_t;

sgl_obj_t* sgl_3dvortex_create(sgl_obj_t* parent);
void sgl_3dvortex_set_running(sgl_obj_t *obj, bool running);
void sgl_3dvortex_set_trail_alpha(sgl_obj_t *obj, uint8_t alpha);
void sgl_3dvortex_reset(sgl_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* __SGL_3DVORTEX_H__ */
