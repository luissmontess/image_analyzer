#include "task_pool.h"

#include "timing.h"

#include <stddef.h>

static void execute_task(Task *task, int thread_count) {
    BMPImage output;
    double start_time = now_seconds();

    task->status = -1;
    task->elapsed_seconds = 0.0;

    if (bmp_create_like(task->input_image, &output) != 0) {
        return;
    }

    if (apply_transform_parallel(task->input_image,
                                 &output,
                                 task->transform,
                                 task->blur_kernel_size,
                                 thread_count) != 0) {
        bmp_free(&output);
        return;
    }

    if (bmp_save(task->output_path, &output) != 0) {
        bmp_free(&output);
        return;
    }

    task->elapsed_seconds = now_seconds() - start_time;
    task->status = 0;
    bmp_free(&output);
}

int run_tasks(Task *tasks, int task_count, int thread_count) {
    int i = 0;

    if (tasks == NULL || task_count <= 0 || thread_count <= 0) {
        return -1;
    }

    for (i = 0; i < task_count; ++i) {
        execute_task(&tasks[i], thread_count);
    }

    return 0;
}
