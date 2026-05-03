/* source/core/sgl_event.c
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

#include <sgl_event.h>
#include <sgl_math.h>
#include <sgl_cfgfix.h>
#include <sgl_log.h>
#include <string.h>
#include <sgl_mm.h>


/* define event queue size */
#define SGL_EVENT_QUEUE_SIZE          (CONFIG_SGL_EVENT_QUEUE_SIZE)
#define SGL_EVENT_QUEUE_SIZE_MASK     (SGL_EVENT_QUEUE_SIZE - 1)

/**
 * @brief event queue struct
 * @buffer: event buffer to save all event data
 * @head: event queue head which is used to push event
 * @tail: event queue tail which is used to pop event
 * @count: event queue count which have been pushed
 */
typedef struct event_queue {
    sgl_event_t buffer[SGL_EVENT_QUEUE_SIZE];
    uint16_t    head;
    uint16_t    tail;
} event_queue_t;


/**
 * @brief event context struct
 * @last_click: last click object which may be lost event
 * @last_touch: last touch position
 * @evtq: event queue
 */
static struct event_context {
    struct sgl_obj *last_click;
    struct sgl_obj *last_motion;
    sgl_event_pos_t last_touch;
    event_queue_t   evtq;
} evt_ctx;


/**
 * @brief Initialize the event queue
 * @param none
 * @return 0 on success, -1 on failure
 * @note !!!!!! the SGL_EVENT_QUEUE_SIZE must be power of 2 !!!!!!
 *       You must check the return value of this function.
 */
int sgl_event_queue_init(void)
{
    if (!sgl_is_pow2(SGL_EVENT_QUEUE_SIZE)) {
        SGL_LOG_ERROR("The capacity must be power of 2");
        return -1;
    }

    evt_ctx.evtq.head = evt_ctx.evtq.tail = 0;
    return 0;
}


/**
 * @brief Check whether the event queue is empty
 * @param none
 * @return true if the event queue is empty, false otherwise
 */
static inline bool sgl_event_queue_is_empty(void)
{
    return evt_ctx.evtq.head == evt_ctx.evtq.tail;
}


/**
 * @brief Check whether the event queue is full
 * @param none
 * @return true if the event queue is full, false otherwise
 */
static inline bool sgl_event_queue_is_full(void)
{
    uint32_t next_head = (evt_ctx.evtq.head + 1) & SGL_EVENT_QUEUE_SIZE_MASK;
    return next_head == evt_ctx.evtq.tail;
}


/**
 * @brief Push an event into the event queue
 * @param event The event to be pushed
 * @return 0 on success, -1 on failure
 */
void sgl_event_queue_push(sgl_event_t event)
{
    if (unlikely(sgl_event_queue_is_full())) {
        SGL_LOG_ERROR("Event queue is full, maybe system is too slow");
        return;
    }

    evt_ctx.evtq.buffer[evt_ctx.evtq.head] = event;
    evt_ctx.evtq.head = ((evt_ctx.evtq.head + 1) & SGL_EVENT_QUEUE_SIZE_MASK);
}


/**
 * @brief Pop an event from the event queue
 * @param out_event The event to be popped
 * @return 0 on success, -1 on failure
 */
static inline int sgl_event_queue_pop(sgl_event_t* out_event)
{
    if (sgl_event_queue_is_empty()) {
        return -1;
    }

    *out_event = evt_ctx.evtq.buffer[evt_ctx.evtq.tail];
    evt_ctx.evtq.tail = ((evt_ctx.evtq.tail + 1) & SGL_EVENT_QUEUE_SIZE_MASK);
    return 0;
}


/**
 * @brief Check whether the position is focus on the object
 * @param pos The position to be checked
 * @param rect The rectangle of the object
 * @param radius The radius of the object
 * @return true if the position is focus on the object, false otherwise
 */
static bool pos_is_focus_on_obj(sgl_event_pos_t *pos, sgl_rect_t *rect, int16_t radius)
{
    if (pos->x < rect->x1 || pos->x > rect->x2 || pos->y < rect->y1 || pos->y > rect->y2) {
        return false;
    }
    else if(radius == 0) {
        return true;
    }

    if ((pos->x >= rect->x1 + radius) && (pos->x <= rect->x2 - radius)) {
        return true;
    }
    else if ((pos->y >= rect->y1 + radius) && (pos->y <= rect->y2 - radius)) {
        return true;
    }

    if (pos->x <= rect->x1 + radius && pos->y <= rect->y1 + radius) {
        int16_t dx = pos->x - (rect->x1 + radius);
        int16_t dy = pos->y - (rect->y1 + radius);
        return sgl_pow2(dx) + sgl_pow2(dy) <= sgl_pow2(radius);
    }
    else if (pos->x >= rect->x2 - radius && pos->y <= rect->y1 + radius) {
        int16_t dx = pos->x - (rect->x2 - radius);
        int16_t dy = pos->y - (rect->y1 + radius);
        return sgl_pow2(dx) + sgl_pow2(dy) <= sgl_pow2(radius);
    }
    else if (pos->x <= rect->x1 + radius && pos->y >= rect->y2 - radius) {
        int16_t dx = pos->x - (rect->x1 + radius);
        int16_t dy = pos->y - (rect->y2 - radius);
        return sgl_pow2(dx) + sgl_pow2(dy) <= sgl_pow2(radius);
    }
    else if (pos->x >= rect->x2 - radius && pos->y >= rect->y2 - radius) {
        int16_t dx = pos->x - (rect->x2 - radius);
        int16_t dy = pos->y - (rect->y2 - radius);
        return sgl_pow2(dx) + sgl_pow2(dy) <= sgl_pow2(radius);
    }

    return false;
}


/**
 * @brief check whether the position is clicked on the object
 * @param pos The position to be clicked
 * @return The object that is clicked on, NULL if no object is clicked
 */
static struct sgl_obj* click_detect_object(sgl_event_pos_t *pos)
{
    struct sgl_obj *stack[SGL_OBJ_DEPTH_MAX], *obj = sgl_screen_act()->child, *find = NULL;
    int top = 0;

    if (unlikely(obj == NULL)) {
        return NULL;
    }
    stack[top++] = obj;

    while (top > 0) {
        SGL_ASSERT(top < SGL_OBJ_DEPTH_MAX);
        obj = stack[--top];
        if (sgl_obj_has_sibling(obj)) {
            stack[top++] = obj->sibling;
        }

        if (unlikely(sgl_obj_is_hidden(obj))) {
            continue;
        }

        if (pos_is_focus_on_obj(pos, &obj->coords, obj->radius)) {
            find = obj;
            if (sgl_obj_has_child(obj)) {
                stack[top++] = obj->child;
            }
        }
    }

    /**
     * if the object is clickable, return it, otherwise return its parent 
     * because the object may be a label attached to the object
    */
    if (find != NULL) {
        return sgl_obj_is_clickable(find) ? find : find->parent;
    }

    return find;
}


/**
 * @brief Handle the position event
 * @param pos The position to be handled
 * @param type The type of the event
 * @return none
 */
void sgl_event_send_pos(sgl_event_pos_t pos, sgl_event_type_t type)
{
    sgl_event_t event = {
        .obj = NULL,
        .type = type,
        .pos = pos,
    };

    if (type == SGL_EVENT_PRESSED) {
        evt_ctx.last_touch = pos;
    }

    sgl_event_queue_push(event);
}


/**
 * @brief get information of motion event type
 * @param evt [in][out] event to be handled
 * @return none
 */
static void sgl_get_move_info(sgl_event_t *evt)
{
    int16_t dx = evt->pos.x - evt_ctx.last_touch.x;
    int16_t dy = evt->pos.y - evt_ctx.last_touch.y;

    if (sgl_abs(dx) > sgl_abs(dy)) {
        if (dx > 0) {
            evt->type = SGL_EVENT_MOVE_RIGHT;
            evt->distance = dx;
        }
        else {
            evt->type = SGL_EVENT_MOVE_LEFT;
            evt->distance = -dx;
        }
    }
    else {
        if (dy > 0) {
            evt->type = SGL_EVENT_MOVE_DOWN;
            evt->distance = dy;
        }
        else {
            evt->type = SGL_EVENT_MOVE_UP;
            evt->distance = -dy;
        }
    }

    evt_ctx.last_touch = evt->pos;
}


/**
 * @brief Callback function for event
 * @param obj The object that triggered the event
 * @param evt The event that triggered the callback
 * @return none
 */
static inline void event_callback(sgl_obj_t *obj, sgl_event_t *evt)
{
    SGL_ASSERT(obj->construct_fn);
    evt->param = obj->event_data;
    evt->obj = obj;
    obj->construct_fn(NULL, obj, evt);
}


/**
 * @brief Inject motion event to the object
 * @param obj The object that triggered the event
 * @param evt The event that triggered the callback
 * @return none
 */
static inline void event_inject_motion(sgl_obj_t *obj, sgl_event_t *evt)
{
    while (!sgl_obj_is_movable(obj) && obj != sgl_screen_act()) {
        obj = obj->parent;
    }
    evt_ctx.last_motion = obj;
    event_callback(obj, evt);
}


/**
 * @brief All event task in SGL, this function will traverse all elements in the event queue, 
 *        respond to each element with an event, so that all events will trigger and point to the 
 *        corresponding callback function
 * @param none
 * @return none
*/
void sgl_event_task(void)
{
    sgl_event_t evt;
    struct sgl_obj *obj = NULL;

    /* Get event from event queue */
    while (sgl_event_queue_pop(&evt) == 0) {
        obj = evt.obj;

        /* if obj is NULL, it means the event from the input device */
        if (obj == NULL) {
            if (evt.type != SGL_EVENT_MOTION) {
                obj = click_detect_object(&evt.pos);
            } else {
                obj = evt_ctx.last_click;
                if (obj) {
                    sgl_get_move_info(&evt);
                    event_inject_motion(obj, &evt);
                }
                continue;
            }
        }

        if (obj) {
            evt.pos.x = sgl_clamp(evt.pos.x, obj->coords.x1, obj->coords.x2);
            evt.pos.y = sgl_clamp(evt.pos.y, obj->coords.y1, obj->coords.y2);

            if (evt.type == SGL_EVENT_PRESSED) {
                if (obj->pressed) {
                    continue;
                }
                obj->pressed = true;
                evt_ctx.last_click = obj;
            }
            else if (evt.type == SGL_EVENT_RELEASED) {
                if (!obj->pressed) {
                    if (evt_ctx.last_click && evt_ctx.last_click != obj) {
                        evt.obj = evt_ctx.last_click;
                        sgl_event_queue_push(evt);
                    }
                    continue;
                }
                if (evt_ctx.last_motion == obj) {
                    evt_ctx.last_motion = NULL;
                }
                obj->pressed = false;
                evt_ctx.last_click = NULL;
            }

            event_callback(obj, &evt);
            if (evt_ctx.last_motion && evt_ctx.last_motion != obj) {
                event_callback(evt_ctx.last_motion, &evt);
                evt_ctx.last_motion = NULL;
            }

            /* call user event function */
            if(obj->event_fn) {
                obj->event_fn(&evt);
            }
        }
        else {
            SGL_LOG_TRACE("pos is out of object or no event_lost, skip event");
            if (evt.type == SGL_EVENT_RELEASED && evt_ctx.last_click != NULL) {
                evt.obj = evt_ctx.last_click;
                sgl_event_queue_push(evt);
            }
        }
    }
}


/**
 * @brief Touch event read, this function will be called by user
 * @param x: touch x position
 * @param y: touch y position
 * @param flag: touch flag, it means touch event down or up:
 *              true : touch down
 *              false: touch up
 * @return none
 * @note: for example, you can call it in 30ms tick handler function
 *        void example_30ms_tick_handler(void)
 *        {
 *            int pos_x, pos_y;
 *            bool button_status;
 * 
 *            bsp_touch_read_pos(&pos_x, &pos_y);
 *            button_status = bsp_touch_read_status();
 *            
 *            sgl_event_pos_input(pos_x, pos_y, button_status);
 *        }
 */
void sgl_event_pos_input(int16_t x, int16_t y, bool flag)
{
    static sgl_event_pos_t first_pos, curr_pos;
    static uint32_t start_ms;
    static bool touch;
    const uint32_t tick = sgl_tick_get();
    int16_t dx, dy;

    /* rotate touch position */
#if (CONFIG_SGL_FBDEV_ROTATION != 0)
#if (CONFIG_SGL_FBDEV_ROTATION == 90)
    pos.x = sgl_min(SGL_SCREEN_WIDTH - y, SGL_SCREEN_WIDTH - 1);
    pos.y = sgl_min(x, SGL_SCREEN_HEIGHT - 1);
#elif (CONFIG_SGL_FBDEV_ROTATION == 180)
    pos.x = SGL_SCREEN_WIDTH - x - 1;
    pos.y = SGL_SCREEN_HEIGHT - y - 1;
#elif (CONFIG_SGL_FBDEV_ROTATION == 270)
    pos.x = sgl_min(y, SGL_SCREEN_WIDTH - 1);
    pos.y = sgl_min(SGL_SCREEN_HEIGHT - x, SGL_SCREEN_HEIGHT - 1);
#else
    #error "CONFIG_SGL_FBDEV_ROTATION is invalid rotation value (only 0/90/180/270 supported)"
#endif
#endif //!CONFIG_SGL_FBDEV_ROTATION

    if (flag) {
        if (!touch) {
            touch = true;
            first_pos.x = x; first_pos.y = y;
            curr_pos = first_pos;
            start_ms = tick;
            sgl_event_send_pos(first_pos, SGL_EVENT_PRESSED);
            SGL_LOG_INFO("Touch PRESSED: pos: %d, %d", x, y);
        }
        else {
            dx = x - curr_pos.x;
            dy = y - curr_pos.y;
            if (sgl_square_sum(dx, dy) > sgl_pow2(SGL_EVENT_MOVE_THRESHOLD)) {
                curr_pos.x = x;
                curr_pos.y = y;
                sgl_event_send_pos(curr_pos, SGL_EVENT_MOTION);
                SGL_LOG_INFO("Touch MOTION: pos: %d, %d", x, y);
            }
        }
    }
    else {
        if (touch) {
            touch = false;
            dx = first_pos.x - curr_pos.x;
            dy = first_pos.y - curr_pos.y;
            if (sgl_square_sum(dx, dy) <= sgl_pow2(SGL_EVENT_MOVE_THRESHOLD)) {
                if (tick - start_ms < SGL_EVENT_CLICK_INTERVAL) {
                    sgl_event_send_pos(first_pos, SGL_EVENT_CLICKED);
                    SGL_LOG_INFO("Touch CLICKED: pos: %d, %d", first_pos.x, first_pos.y);
                } else {
                    sgl_event_send_pos(first_pos, SGL_EVENT_LONG_CLICKED);
                    SGL_LOG_INFO("Touch LONG_CLICKED: pos: %d, %d", first_pos.x, first_pos.y);
                }
            }
            sgl_event_send_pos(curr_pos, SGL_EVENT_RELEASED);
            SGL_LOG_INFO("Touch RELEASED: pos: %d, %d", curr_pos.x, curr_pos.y);
        }
    }
}
