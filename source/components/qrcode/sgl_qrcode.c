/* source/components/qrcode/sgl_qrcode.c
 *
 * MIT License
 *
 * Copyright(c) 2023-present All contributors of SGL  
 * Document reference link: https://sgl-docs.readthedocs.io
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <sgl_core.h>
#include <sgl_draw.h>
#include <sgl_math.h>
#include <sgl_log.h>
#include <sgl_mm.h>
#include <sgl_cfgfix.h>
#include <string.h>
#include "sgl.h"



#include "qrcodegen.h"
#include "sgl_qrcode.h"

#if (CONFIG_SGL_QRCODE)

/* ---- SGL custom draw callback ---- */

/*
 * qr_canvas_cb - called by SGL each render cycle for the QR canvas object.
 *
 * On SGL_EVENT_DRAW_MAIN:
 *   1. Fills the QR bounding box (including quiet zone) with white.
 *   2. For every dark module in the QR matrix, fills a QR_SCALE×QR_SCALE
 *      rectangle with black using sgl_draw_fill_rect().
 *
 * Coordinates:
 *   The QR is centered on the canvas region.  If the QR (with quiet zone) is wider
 *   than the canvas width, the offset goes negative and SGL clips the excess — the
 *   finder-pattern corners and all data modules remain within canvas bounds
 *   for the version/scale combination specified in ui_qr.h.
 */
static void qr_canvas_cb(sgl_surf_t *surf, sgl_obj_t *obj, sgl_event_t *evt)
{
    sgl_qrcode_t *qrcode = (sgl_qrcode_t *)obj->event_data;
    if (!qrcode) { return; }
    if (evt->type != SGL_EVENT_DRAW_MAIN) { return; }
    if (!qrcode->qr_buf || qrcode->qr_buf[0] == 0) { return; }


    int qr_size   = qrcodegen_getSize(qrcode->qr_buf);
    int quiet_px  = qrcode->qr_quiet_zone * qrcode->qr_scale;
    int canvas_w  = qr_size  * qrcode->qr_scale + 2 * quiet_px;
    int canvas_h  = canvas_w; /* QR codes are square */

    int off_x = (qrcode->width - canvas_w) / 2;
    int off_y = (qrcode->height - canvas_h) / 2;

    /* 1. White background (quiet zone + data area) */
    sgl_area_t bg = {
        .x1 = (int16_t)off_x,
        .y1 = (int16_t)off_y,
        .x2 = (int16_t)(off_x + canvas_w - 1),
        .y2 = (int16_t)(off_y + canvas_h - 1),
    };
    sgl_draw_fill_rect(surf, &obj->area, &bg, 0,
                       SGL_COLOR_WHITE, qrcode->alpha);

    /* 2. Dark modules */
    for (int my = 0; my < qr_size; my++) {
        for (int mx = 0; mx < qr_size; mx++) {
            if (!qrcodegen_getModule(qrcode->qr_buf, mx, my)) { continue; }

            sgl_area_t mod = {
                .x1 = (int16_t)(off_x + quiet_px + mx * qrcode->qr_scale),
                .y1 = (int16_t)(off_y + quiet_px + my * qrcode->qr_scale),
                .x2 = (int16_t)(off_x + quiet_px + mx * qrcode->qr_scale + qrcode->qr_scale - 1),
                .y2 = (int16_t)(off_y + quiet_px + my * qrcode->qr_scale + qrcode->qr_scale - 1),
            };
            sgl_draw_fill_rect(surf, &obj->area, &mod, 0,
                               SGL_COLOR_BLACK, qrcode->alpha);
        }
    }
}

static void sgl_qrcode_construct_cb(sgl_surf_t *surf, sgl_obj_t *obj, sgl_event_t *evt)
{
    SGL_UNUSED(surf);
    sgl_qrcode_t *qrcode = sgl_container_of(obj, sgl_qrcode_t, obj);
    if (!qrcode) { return; }

    switch(evt->type) {
    case SGL_EVENT_DRAW_MAIN:
        break;
    case SGL_EVENT_PRESSED:
        if(sgl_obj_is_flexible(obj)) {
            sgl_obj_size_zoom(obj, 2);
        }
        sgl_obj_set_dirty(obj);
        break;
    case SGL_EVENT_RELEASED:
        if(sgl_obj_is_flexible(obj)) {
            sgl_obj_size_zoom(obj, -2);
        }
        sgl_obj_set_dirty(obj);
        break;
    case SGL_EVENT_DESTROYED:
        /* Free dynamic members owned by qrcode object */
        if (qrcode->canvas) {
            sgl_obj_delete(qrcode->canvas);
            qrcode->canvas = NULL;
        }
        if (qrcode->qr_buf) {
            sgl_free(qrcode->qr_buf);
            qrcode->qr_buf = NULL;
        }
        break;
    default:
        break;
    }
}


/**
 * @brief create a qrcode object
 * @param parent parent object of the qrcode
 * @return qrcode object
 */
sgl_obj_t* sgl_qrcode_create(sgl_obj_t* parent)
{
    /* default QR buffer size for the maximum version which is configured by user expectedly*/
    const size_t qr_buf_size = (size_t)qrcodegen_BUFFER_LEN_FOR_VERSION(CONFIG_SGL_QRCODE_QR_VERSION_MAX);

    sgl_qrcode_t *qrcode = sgl_malloc(sizeof(sgl_qrcode_t));
    if(qrcode == NULL) {
        SGL_LOG_ERROR("sgl_qrcode_create: malloc failed");
        return NULL;
    }
    memset(qrcode, 0, sizeof(sgl_qrcode_t));

    qrcode->qr_version = CONFIG_SGL_QRCODE_QR_VERSION_MAX;
    qrcode->qr_quiet_zone = SGL_QRCODE_QR_QUIET_ZONE_DEFAULT;
    qrcode->qr_scale = SGL_QRCODE_QR_SCALE_DEFAULT;

    /* calcuate width and height by default parameters */
    qrcode->width = (4 * qrcode->qr_version + 17) * qrcode->qr_scale + 2 * qrcode->qr_quiet_zone * qrcode->qr_scale;
    qrcode->height = qrcode->width;

    qrcode->qr_buf = sgl_malloc(qr_buf_size);
    if(qrcode->qr_buf == NULL) {
        SGL_LOG_ERROR("sgl_qrcode_create: malloc failed");
        sgl_free(qrcode);
        return NULL;
    }
    memset(qrcode->qr_buf, 0, qr_buf_size);

    sgl_obj_t *obj = &qrcode->obj;
    sgl_obj_init(&qrcode->obj, parent);
    sgl_obj_set_clickable(obj);
    sgl_obj_set_movable(obj);
    obj->construct_fn = sgl_qrcode_construct_cb;
    sgl_obj_set_border_width(obj, 0);

    /* Create a opaque overlay to support QR code drawing */
    qrcode->canvas = sgl_rect_create(obj);
    if (!qrcode->canvas) {
        sgl_free(qrcode->qr_buf);
        qrcode->qr_buf = NULL;
        sgl_free(qrcode);
        return NULL;
    }

    sgl_obj_set_pos(qrcode->canvas, 0, 0);
    sgl_obj_set_size(qrcode->canvas, qrcode->width, qrcode->height);
    sgl_rect_set_color(qrcode->canvas, SGL_COLOR_BLACK);
    sgl_rect_set_alpha(qrcode->canvas, SGL_ALPHA_MAX);
    qrcode->canvas->event_data = qrcode;
    qrcode->canvas->construct_fn = qr_canvas_cb;

    return obj;
}

void sgl_qrcode_set_url(sgl_obj_t *obj, const char *url)
{
    sgl_qrcode_t *qrcode = sgl_container_of(obj, sgl_qrcode_t, obj);
    if (!qrcode || !url) { return; }

    const size_t qr_buf_size = (size_t)qrcodegen_BUFFER_LEN_FOR_VERSION(qrcode->qr_version);

    uint8_t* qr_tmp = sgl_malloc(qr_buf_size);
    if (!qr_tmp) {
        SGL_LOG_ERROR("sgl_qrcode_set_url: malloc failed");
        return;
    }

    bool qr_valid = qrcodegen_encodeText(
        url,
        qr_tmp,
        qrcode->qr_buf,
        qrcodegen_Ecc_LOW,
        qrcodegen_VERSION_MIN,
        qrcode->qr_version,
        qrcodegen_Mask_AUTO,
        true);

    if (!qr_valid) { 
        SGL_LOG_ERROR("URL cannot be encoded in current QR version/ECC");
        sgl_free(qr_tmp);
        return; 
    }  

    sgl_free(qr_tmp);

    sgl_obj_set_dirty(obj);
}


void sgl_qrcode_set_qr_version(sgl_obj_t *obj, uint8_t version)
{
    sgl_qrcode_t *qrcode = sgl_container_of(obj, sgl_qrcode_t, obj);

    if (version < qrcodegen_VERSION_MIN || version > qrcodegen_VERSION_MAX) {
        SGL_LOG_ERROR("Invalid QR version");
        return;
    }
    /* re-aclloating the qrcode buffer by newly sepcified version */
    const size_t qr_buf_size = (size_t)qrcodegen_BUFFER_LEN_FOR_VERSION(version);
    if(qrcode->qr_buf) {
        sgl_free(qrcode->qr_buf);
        qrcode->qr_buf = NULL;
    }
    qrcode->qr_buf = sgl_malloc(qr_buf_size);
    if (!qrcode->qr_buf) {
        SGL_LOG_ERROR("sgl_qrcode_set_qr_version: malloc failed");
        return;
    }
    memset(qrcode->qr_buf, 0, qr_buf_size);

    qrcode->qr_version = version;

    sgl_obj_set_dirty(obj);
}

void sgl_qrcode_set_qr_scale(sgl_obj_t *obj, uint8_t scale)
{
    sgl_qrcode_t *qrcode = sgl_container_of(obj, sgl_qrcode_t, obj);

    if (scale < 1 || scale > 15) {
        SGL_LOG_ERROR("Scale must be between 1 and 15");
        return;
    }

    qrcode->qr_scale = scale;

    sgl_obj_set_dirty(obj);
}

void sgl_qrcode_set_qr_quiet_zone(sgl_obj_t *obj, uint8_t quiet_zone)
{
    sgl_qrcode_t *qrcode = sgl_container_of(obj, sgl_qrcode_t, obj);

    if(quiet_zone > 15) {
        SGL_LOG_ERROR("Quiet zone must be between 0 and 15");
        return;
    }

    qrcode->qr_quiet_zone = quiet_zone;

    sgl_obj_set_dirty(obj);
}

void sgl_qrcode_set_alpha(sgl_obj_t *obj, uint8_t alpha)
{
    sgl_qrcode_t *qrcode = sgl_container_of(obj, sgl_qrcode_t, obj);

    qrcode->alpha = alpha;

    sgl_obj_set_dirty(obj);
}

void sgl_qrcode_set_size(sgl_obj_t *obj, uint16_t width, uint16_t height)
{
    sgl_qrcode_t *qrcode = sgl_container_of(obj, sgl_qrcode_t, obj);

    qrcode->width = width;
    qrcode->height = height;

    if (qrcode->canvas) {
        sgl_obj_set_size(qrcode->canvas, width, height);
        sgl_obj_set_dirty(qrcode->canvas);
    }

    sgl_obj_set_dirty(obj);
}


#endif // CONFIG_SGL_QRCODE