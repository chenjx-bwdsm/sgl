/* source/include/sgl_task.h
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
#ifndef SGL_TASK_H
#define SGL_TASK_H

#include <stdint.h>
#include <sgl_list.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task structure
 * @task_func Task function
 * @param Task parameter
 * @interval Task interval
 * @last_run_tick Last run tick
 * @paused Task paused flag
 * @destroyed Task destroyed flag
 * @node List node
 */
typedef struct sgl_task {
    void (*task_func)(void *param);
    void *param;
    uint32_t interval;
    uint32_t last_run_tick;
    bool paused;
    bool destroyed;
    sgl_list_node_t node;
} sgl_task_t;

/**
 * @brief Create a task
 * @param task_func Task function
 * @param param Task parameter
 * @param interval Task interval, in ms
 * @return sgl_task_t* The task
 */
sgl_task_t *sgl_task_create(void (*task_func)(void *param), void *param, uint32_t interval);

/**
 * @brief Delete a task
 * @param task The task
 * @return none
 */
void sgl_task_delete(sgl_task_t *task)
{
    task->destroyed = true;    
}

/**
 * @brief Pause a task
 * @param task The task
 * @return none
 */
void sgl_task_pause(sgl_task_t *task)
{
    task->paused = true;
}

/**
 * @brief Resume a task
 * @param task The task
 * @return none
 */
void sgl_task_resume(sgl_task_t *task)
{
    task->paused = false;
}

/**
 * @brief Task worker function
 * @param none
 * @return none
 * @details This function is called by the task scheduler
 */
void sgl_task_worker(void);

#ifdef __cplusplus
}
#endif

#endif // SGL_TASK_H
