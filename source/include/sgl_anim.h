/* source/include/sgl_anim.h
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

#ifndef __SGL_ANIM_H__
#define __SGL_ANIM_H__

#ifdef __cplusplus
extern "C" {
#endif


#include <sgl_cfgfix.h>
#include <stddef.h>
#include <sgl_list.h>
#include <sgl_types.h>
#include <sgl_mm.h>

#if (CONFIG_SGL_ANIMATION)

/* Forward declaration of sgl_pos sgl_anim structures */
struct sgl_pos;
struct sgl_anim;


/* Anim path callback */
typedef void (*sgl_anim_path_cb_t)(struct sgl_anim *anim, int32_t value);
typedef int32_t (*sgl_anim_path_algo_t)(uint16_t elaps, uint16_t duration, int32_t start, int32_t end);


/**
 * @brief Animation object structure used to manage a single animation instance.
 *
 * This structure holds all the necessary state and configuration for an animation,
 * including timing parameters, value interpolation, callbacks, and linkage in a list.
 * All time values (act_delay, act_duration) are in milliseconds.
 *
 * @data:      Pointer to user-defined private data associated with this animation.
 *             Not used internally by the animation engine; intended for application use.
 * 
 * @next:      Pointer to the next animation in a singly-linked list.
 *             Used internally by the animation scheduler to chain active animations.
 * 
 * @act_time:  Current time (in ms) of the animation.
 * 
 * @act_delay: Delay time (in ms) before the animation starts after being added to the system.
 *             The animation will not progress until this delay has elapsed.
 * 
 * @act_duration: Total duration (in ms) of the animation from start_value to end_value.
 * 
 * @last_value: The last computed value of the animation.
 * 
 * @start_value: The initial value at the beginning of the animation (after delay).
 * 
 * @end_value: The target value at the end of the animation.
 * 
 * @path_cb: Optional custom callback function to compute intermediate animation values.
 *           If set, it overrides the built-in path algorithm (`path_algo`).
 *
 * @path_algo: Predefined interpolation algorithm (e.g., linear, ease-in, ease-out).
 *             Used only if `path_cb` is NULL.
 *
 * @finish_cb: Callback function invoked when the animation completes (including repeats).
 *             May be NULL if no cleanup or notification is needed.
 *
 * @repeat_cnt: Number of times the animation should repeat.
 *              - -1: play indefinitely, you can use SGL_ANIM_REPEAT_LOOP
 *              - 1: play once (no repeat), you can use SGL_ANIM_REPEAT_ONCE
 *              - n: repeat n times (total plays = n)
 *              @note Only 30 bits are allocated; max value is 0x3FFF.
 *
 * @finished: Flag indicating whether the animation has completed (including all repeats).
 *            Set to 1 when the animation ends naturally or is stopped.
 *
 * @auto_free: If set to 1, the animation object will be automatically freed after completion.
 *             Useful for fire-and-forget animations; ensure memory was allocated dynamically.
 */
typedef struct sgl_anim {
    void                  *data;
    sgl_list_node_t       node;
    uint16_t              act_time;
    uint16_t              act_delay;
    uint16_t              act_duration;
    uint16_t              repeat_cnt : 14;
    uint16_t              finished : 1;
    uint16_t              auto_free : 1;
    int32_t               last_value;
    int32_t               start_value;
    int32_t               end_value;
    sgl_anim_path_cb_t    path_cb;
    sgl_anim_path_algo_t  path_algo;
    void                  (*finish_cb)(struct sgl_anim *anim);
} sgl_anim_t;


#define  SGL_ANIM_REPEAT_LOOP                          (0x3FFF)
#define  SGL_ANIM_REPEAT_ONCE                          (1)


/**
 * @brief  Animation static initialization
 * @param  anim - Animation object
 * @return none
 */
void sgl_anim_init(sgl_anim_t *anim);


/**
 * @brief dynamic alloc animation object with initialization
 * @param  none
 * @return animation object
*/
sgl_anim_t* sgl_anim_create(void);


/**
 * @brief start animation
 * @param  anim animation object
 * @para  repeat_cnt repeat count of animation
 * @return none
*/
void sgl_anim_start(sgl_anim_t *anim, uint32_t repeat_cnt);


/**
 * @brief stop animation
 * @param  anim animation object
 * @return none
*/
void sgl_anim_stop(sgl_anim_t *anim);


/**
 * @brief delete animation object
 * @param anim animation object
 * @return none
*/
void sgl_anim_delete(sgl_anim_t *anim);


/**
 * @brief set animation private data
 * @param  anim animation object
 * @param  data pointer to private data
 * @return none
 */
static inline void sgl_anim_set_data(sgl_anim_t *anim, void *data)
{
    SGL_ASSERT(anim != NULL);
    anim->data = data;
}


/**
 * @brief set animation path callback function
 * @param  anim animation object
 * @param  path_cb path callback function
 * @param  path_algo path algo callback function
 * @return none
 */
static inline void sgl_anim_set_path(sgl_anim_t *anim, sgl_anim_path_cb_t path_cb, sgl_anim_path_algo_t path_algo)
{
    SGL_ASSERT(anim != NULL && path_cb != NULL && path_algo != NULL);
    anim->path_cb = path_cb;
    anim->path_algo = path_algo;
}


/**
 * @brief set animation start value
 * @param  anim animation object
 * @param  value start value
 * @return none
 */
static inline void sgl_anim_set_start_value(sgl_anim_t *anim, int32_t value)
{
    SGL_ASSERT(anim != NULL);
    anim->start_value = value;
}


/**
 * @brief set animation end value
 * @param  anim animation object
 * @param  value end value
 * @return none
 */
static inline void sgl_anim_set_end_value(sgl_anim_t *anim, int32_t value)
{
    SGL_ASSERT(anim != NULL);
    anim->end_value = value;
}


/**
 * @brief set animation active delay time, ms
 * @param  anim animation object
 * @param  delay active delay time, ms
 * @return none
 */
static inline void sgl_anim_set_act_delay(sgl_anim_t *anim, uint32_t delay_ms)
{
    SGL_ASSERT(anim != NULL);
    anim->act_delay = delay_ms;
}


/**
 * @brief set animation active duration time, ms
 * @param  anim animation object
 * @param  duration active duration time, ms
 * @return none
 */
static inline void sgl_anim_set_act_duration(sgl_anim_t *anim, uint32_t duration_ms)
{
    SGL_ASSERT(anim != NULL);
    anim->act_duration = duration_ms;
}


/**
 * @brief set finish callback for animation
 * @param  anim animation object
 * @param  finish_cb finish callback
 * @return none
 */
static inline void sgl_anim_set_finish_cb(sgl_anim_t *anim, void (*finish_cb)(sgl_anim_t *anim))
{
    SGL_ASSERT(anim != NULL);
    anim->finish_cb = finish_cb;
}


/**
 * @brief check animation is finished or not
 * @param  anim animation object
 * @return true or false
 */
static inline bool sgl_anim_is_finished(sgl_anim_t *anim)
{
    SGL_ASSERT(anim != NULL);
    return (bool)anim->finished;
}


/**
 * @brief set auto free flag for animation
 * @param  anim animation
 * @return none
 */
static inline void sgl_anim_set_auto_free(sgl_anim_t *anim)
{
    SGL_ASSERT(anim != NULL);
    anim->auto_free = 1;
}


/**
 * @brief animation task, it will foreach all animation
 * @param  none
 * @return none
 * @note   this function should be called in sgl_task()
 */
void sgl_anim_task(void);


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
int32_t sgl_anim_path_linear(uint16_t elaps, uint16_t duration, int32_t start, int32_t end);
#define SGL_ANIM_PATH_LINEAR  sgl_anim_path_linear


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
int32_t sgl_anim_path_ease_in_out(uint16_t elaps, uint16_t duration, int32_t start, int32_t end);
#define SGL_ANIM_PATH_EASE_IN_OUT  sgl_anim_path_ease_in_out


/**
 * sgl_anim_path_ease_in - Cubic ease-in animation path
 *
 * This function creates a smooth animation curve that starts slow,
 * accelerates in the after
 *
 * @param elaps     Elapsed time (ms)
 * @param duration  Total animation duration (ms)
 * @param start     Start value
 * @param end       End value
 * @return          Interpolated value at current time
 */
int32_t sgl_anim_path_ease_out(uint16_t elaps, uint16_t duration, int32_t start, int32_t end);
#define SGL_ANIM_PATH_EASE_OUT  sgl_anim_path_ease_out


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
int32_t sgl_anim_path_ease_in(uint16_t elaps, uint16_t duration, int32_t start, int32_t end);
#define SGL_ANIM_PATH_EASE_IN  sgl_anim_path_ease_in


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
int32_t sgl_anim_path_overshoot(uint16_t elaps, uint16_t duration, int32_t start, int32_t end);
#define SGL_ANIM_PATH_OVERSHOOT  sgl_anim_path_overshoot


#endif // ! CONFIG_SGL_ANIMATION

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // ! __SGL_ANIM_H__
