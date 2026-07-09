/* source/core/sgl_task.c
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
#include <sgl_task.h>
#include <sgl_mm.h>

static SGL_LIST_HEAD(sgl_task_list);

/**
 * @brief Get the next run time of the task
 * 
 * @param task The task
 * @return uint32_t The next run time of the task
 */
static inline uint32_t sgl_task_next_run(const sgl_task_t *task)
{
    return task->last_run_tick + task->interval;
}

/**
 * @brief Add a task to the task list
 * @param task The task
 * @return none
 */
static void task_add(sgl_task_t *task)
{
    sgl_task_t *pos;

    sgl_list_for_each_entry(pos, &sgl_task_list, sgl_task_t, node) {
        if (sgl_task_next_run(pos) > sgl_task_next_run(task)) {
            sgl_list_add_node_at_before(&pos->node, &task->node);
            return;
        }
    }
    /* Append at tail (largest next_run or empty list) */
    sgl_list_add_node_at_before(&sgl_task_list, &task->node);
}

/**
 * @brief Create a task
 * @param task_func Task function
 * @param param Task parameter
 * @param interval Task interval, in ms
 * @return sgl_task_t* The task
 */
sgl_task_t *sgl_task_create(void (*task_func)(void *param), void *param, uint32_t interval)
{
    sgl_task_t *task = (sgl_task_t *)sgl_malloc(sizeof(sgl_task_t));
    if (task == NULL) {
        SGL_LOG_ERROR("Failed to allocate memory for task");
        return NULL;
    }

    task->task_func = task_func;
    task->param = param;
    task->interval = interval;
    task->last_run_tick = sgl_tick_get();
    task->paused = false;
    task->destroyed = false;

    task_add(task);
    return task;
}

/**
 * @brief Task worker function
 * @param none
 * @return none
 * @details This function is called by the task scheduler
 */
void sgl_task_worker(void)
{
    sgl_task_t *task, *tmp;
    uint32_t curr_tick = sgl_tick_get();

    sgl_list_for_each_entry_safe(task, tmp, &sgl_task_list, sgl_task_t, node) {
        if (task->destroyed) {
            sgl_list_del_node(&task->node);
            sgl_free(task);
            continue;
        }

        if (task->paused) {
            continue;
        }

        if ((curr_tick - task->last_run_tick) >= task->interval) {
            task->task_func(task->param);
            task->last_run_tick = curr_tick;
        }
        else {
            break;
        }
    }
}
