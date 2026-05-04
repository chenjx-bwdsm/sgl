/* source/core/sgl_anim.c
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
#include <sgl_anim.h>
#include <sgl_math.h>
#include <sgl_log.h>
#include <sgl_mm.h>


#if (CONFIG_SGL_ANIMATION)
/* animation context */
static SGL_LIST_HEAD(anim_head);

/**
 * @brief  Animation static initialization
 * @param  anim - Animation object
 * @return none
 */
void sgl_anim_init(sgl_anim_t *anim)
{
    sgl_list_init(&anim->node);
    anim->data = NULL;
    anim->act_delay = 0;
    anim->act_duration = 0;
    anim->start_value = 0;
    anim->end_value = 0;

    anim->path_cb = NULL;
    anim->path_algo = NULL;

    anim->finish_cb = NULL;
    anim->auto_free = 0;
    anim->finished = 1;
}


/**
 * @brief dynamic alloc animation object with initialization
 * @param  none
 * @return animation object
*/
sgl_anim_t* sgl_anim_create(void)
{
    sgl_anim_t *anim = sgl_malloc(sizeof(sgl_anim_t));
    if (anim == NULL) {
        SGL_LOG_ERROR("sgl_anim_create: malloc failed");
        return NULL;
    }

    sgl_anim_init(anim);
    return anim;
}


/**
 * @brief add animation object to animation list
 * @param  anim animation object
 * @return none
*/
static inline void sgl_anim_add(sgl_anim_t *anim)
{
    sgl_list_add_node_at_tail(&anim_head, &anim->node);
    anim->finished = 0;
}


/**
 * @brief remove animation object from animation list
 * @param  anim animation object
 * @return none
*/
static inline void sgl_anim_remove(sgl_anim_t *anim)
{
    SGL_ASSERT(anim != NULL);
    sgl_list_del_node(&anim->node);
}


/**
 * @brief start animation
 * @param  anim animation object
 * @para  repeat_cnt repeat count of animation
 * @return none
*/
void sgl_anim_start(sgl_anim_t *anim, uint32_t repeat_cnt)
{
    SGL_ASSERT(anim != NULL);
    if (anim->finished && repeat_cnt) {
        sgl_anim_add(anim);
        anim->finished = 0;
    }

    if (anim->act_duration == 0) {
        SGL_LOG_WARN("animation duration is 0, you must set animation duration larger than 0");
        return;
    }

    anim->act_time = sgl_tick_get() + anim->act_delay;
    anim->repeat_cnt = repeat_cnt & SGL_ANIM_REPEAT_LOOP;
    anim->last_value = anim->start_value;
}


/**
 * @brief stop animation
 * @param  anim animation object
 * @return none
*/
void sgl_anim_stop(sgl_anim_t *anim)
{
    SGL_ASSERT(anim != NULL);
    if (!anim->finished) {
        sgl_anim_remove(anim);
        anim->finished = 1;
    }
}


/**
 * @brief delete animation object
 * @param anim animation object
 * @return none
*/
void sgl_anim_delete(sgl_anim_t *anim)
{
    SGL_ASSERT(anim != NULL);
    if (!anim->finished) {
        sgl_anim_stop(anim);
    } 
    sgl_free(anim);
}


/**
 * @brief animation task, it will foreach all animation
 * @param  none
 * @return none
 * @note   this function should be called in sgl_task()
 */
void sgl_anim_task(void)
{
    int32_t value = 0;
    uint16_t elaps_time = 0;
    const uint32_t current_tick = sgl_tick_get();
    sgl_anim_t *anim = NULL, *next = NULL;

    sgl_list_for_each_entry_safe(anim, next, &anim_head, sgl_anim_t, node) {
        if(current_tick < anim->act_time) {
            continue;
        }
        elaps_time = current_tick - anim->act_time;

        /* check callback function for debug */
        SGL_ASSERT(anim->path_cb != NULL);
        SGL_ASSERT(anim->path_algo != NULL);
        value = anim->path_algo(sgl_min(elaps_time, anim->act_duration), anim->act_duration, anim->start_value, anim->end_value);
        if (value != anim->last_value) {
            anim->path_cb(anim, value);
        }

        if (elaps_time > anim->act_duration) {
            if (anim->repeat_cnt != SGL_ANIM_REPEAT_LOOP) {
                anim->repeat_cnt--;
            }

            if (anim->finish_cb) {
                anim->finish_cb(anim);
            }

            /* remove anim object if repeat count is 0 */
            if (anim->repeat_cnt == 0) {
                sgl_anim_stop(anim);

                /* if animation is auto free, free it */
                if (anim->auto_free) {
                    sgl_free(anim);
                    continue;
                }
            }

            anim->act_time += elaps_time + anim->act_delay;
        }
    }
}


/**
 * Linear animation path calculation function
 *
 * Calculates the current interpolated value based on elapsed time and total duration
 * using linear interpolation.
 *
 * @param elaps     Elapsed time in milliseconds
 * @param duration  Total animation duration in milliseconds
 * @param start     Start value
 * @param end       End value
 *
 * @return          The interpolated value for the current time
 *
 * @note            Returns 'end' if elaps >= duration (animation finished)
 *                  Returns 'start' if elaps == 0 (animation just started)
 *                  Uses 32-bit integer arithmetic to avoid floating-point operations
 *                  for better performance on embedded systems
 */
int32_t sgl_anim_path_linear(uint16_t elaps, uint16_t duration, int32_t start, int32_t end)
{
    int64_t progress_fixed, delta, result;

    // If duration is zero or elapsed time exceeds duration, return end value
    if (elaps >= duration) {
        return (int32_t)end;
    }

    // If no time has elapsed, return start value
    if (elaps == 0) {
        return (int32_t)start;
    }

    // Calculate progress (elaps / duration) as a fixed-point number with 16 fractional bits
    // Use 64-bit intermediate to prevent overflow during multiplication
    progress_fixed = ((int64_t)elaps << 16) / duration;

    // Calculate the difference between end and start
    delta = end - start;

    // Compute the interpolated result: start + delta * (elaps/duration)
    // Right-shift by 16 to scale back from fixed-point representation
    result = start + ((delta * progress_fixed) >> 16);

    return result;
}


/**
 * sgl_anim_path_ease_in_out - Cubic ease-in-out animation path
 *
 * This function creates a smooth animation curve that starts slow,
 * accelerates in the middle, and decelerates at the end.
 *
 * @param elaps     Elapsed time (ms)
 * @param duration  Total animation duration (ms)
 * @param start     Start value
 * @param end       End value
 * @return          Interpolated value at current time
 */
int32_t sgl_anim_path_ease_in_out(uint16_t elaps, uint16_t duration, int32_t start, int32_t end)
{
    int32_t t_180, cos_val, delta;
    if (elaps >= duration)
        return end;
    if (elaps == 0)
        return start;

    t_180 = (elaps * 180) / duration;

    if (t_180 <= 90) {
        cos_val = sgl_sin(90 - t_180);
    } else {
        cos_val = -sgl_sin(t_180 - 90);
    }

    // Now: progress = 0.5 * (1 - cos_val/1000) = (1000 - cos_val) / 2000
    // So: result = start + (end - start) * (1000 - cos_val) / 2000
    delta = end - start;
    return start + (delta * (32767 - cos_val)) / 65535;
}


/**
 * sgl_anim_path_ease_out - Cubic ease-in animation path
 *
 * This function creates a smooth animation curve that starts accelerates,
 * it will be slow in the after
 *
 * @param elaps     Elapsed time (ms)
 * @param duration  Total animation duration (ms)
 * @param start     Start value
 * @param end       End value
 * @return          Interpolated value at current time
 */
int32_t sgl_anim_path_ease_out(uint16_t elaps, uint16_t duration, int32_t start, int32_t end)
{
    int32_t angle, sin_val, delta;
    if (elaps >= duration)
        return end;
    if (elaps == 0)
        return start;

    // t ∈ [0, 90] degrees: sin(t * 90 / duration)
    angle = (elaps * 90) / duration;
    sin_val = sgl_sin(angle);  // Assume returns 0 ~ 65535 or Q15

    // If sgl_sin returns ×1000 (e.g., sin(90) = 32767)
    delta = end - start;
    return start + ((delta * sin_val) >> 15);
}


/**
 * sgl_anim_path_ease_in - Cubic ease-in animation path
 *
 * This function creates a smooth animation curve that starts accelerates,
 * accelerates in the after
 *
 * @param elaps     Elapsed time (ms)
 * @param duration  Total animation duration (ms)
 * @param start     Start value
 * @param end       End value
 * @return          Interpolated value at current time
 */
int32_t sgl_anim_path_ease_in(uint16_t elaps, uint16_t duration, int32_t start, int32_t end)
{
    int32_t angle, cos_val, delta;
    if (elaps >= duration)
        return end;
    if (elaps == 0)
        return start;

    // t ∈ [0, 90] degrees: sin(t * 90 / duration)
    angle = (elaps * 90) / duration;
    cos_val = sgl_cos(angle);  // Assume returns 0 ~ 65535 or Q15

    // If sgl_sin returns ×1000 (e.g., sin(90) = 32767)
    delta = end - start;
    return start + ((delta * (32767 - cos_val)) >> 15);
}


/**
 * sgl_anim_path_overshoot - Overshoot animation path
 *
 * This function creates an animation curve that overshoots the target end value
 * slightly before settling back to it, creating a natural "bounce" or "spring-like"
 * effect for a more dynamic and realistic animation.
 *
 * @param elaps     Elapsed time (ms) since the animation started
 * @param duration  Total animation duration (ms)
 * @param start     Initial value of the animated property at the start of the animation
 * @param end       Target end value of the animated property
 * @return          Interpolated value of the animated property at the current elapsed time
 */
int32_t sgl_anim_path_overshoot(uint16_t elaps, uint16_t duration, int32_t start, int32_t end)
{
    int64_t t, t1, t2, inv_t2, ease_back, diff, progress;
    if (elaps >= duration)
        return end;

    t = (int64_t)elaps * 16384 / duration;
    diff = end - start;

    if (t < 11468) {
        t1 = t * 16384 / 11468;
        progress = (t1 * (32768 - t1)) >> 14; 
        progress = (progress * 18841) >> 14; 
    }
    else {
        t2 = (t - 11468) * 16384 / (16384 - 11468);
        inv_t2 = 16384 - t2;
        ease_back = (inv_t2 * inv_t2) >> 14;
        progress = 16384 + (ease_back * (18841 - 16384) >> 14);
    }

    return start + (int32_t)((diff * progress) >> 14);
}

#endif // !CONFIG_SGL_ANIMATION
