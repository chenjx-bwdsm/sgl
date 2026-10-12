/* sgl/source/components/bench/sgl_bench.h
 *
 * SGL performance benchmark (LVGL "benchmark demo" style) - public API.
 *
 * Self contained: the benchmark uses SGL only. It writes nothing to the file system and
 * prints nothing while it runs -- the on-screen HUD shows the live numbers. Once every
 * scene has been sampled it leaves a summary table on screen and logs the same table.
 *
 * Usage (already wired up in demo/main.c):
 *      sgl_bench_start();                 // start, call once
 *      while (!quit) {
 *          ...
 *          sgl_bench_loop();              // call once per main-loop iteration
 *          sgl_bench_frame_begin();       // wrap "render + present"
 *          sgl_task_handler();
 *          platform_present();            // whatever the port uses to push a frame
 *          sgl_bench_frame_end();
 *      }
 *
 * Each scene runs SGL_BENCH_WARMUP_MS of warm-up + SGL_BENCH_SCENE_MS of sampling.
 * During sampling the benchmark measures the full-screen redraw frame rate, the real
 * per-frame cost and the pixel throughput.
 */

#ifndef __SGL_BENCH_H__
#define __SGL_BENCH_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the benchmark (creates the first scene and the HUD)
 * @note  Clears every object currently on the screen first
 */
void sgl_bench_start(void);

/**
 * @brief Benchmark main-loop hook, must be called once per main-loop iteration
 * @return true  benchmark is running
 * @return false benchmark has finished (or was never started)
 */
bool sgl_bench_loop(void);

/**
 * @brief Mark the start of a render round (call before sgl_task_handler)
 * @note  Pair it with sgl_bench_frame_end. Only rounds that actually present a
 *        frame are counted as busy time, which bypasses the systick rate limit
 *        and yields the real per-frame draw cost.
 */
void sgl_bench_frame_begin(void);

/**
 * @brief Mark the end of a render round (call after present)
 */
void sgl_bench_frame_end(void);

#ifdef __cplusplus
}
#endif

#endif /* __SGL_BENCH_H__ */
