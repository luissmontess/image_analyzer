#include "task_pool.h"

#include "timing.h"

#include <stdio.h>
#include <stddef.h>
#include <string.h>

static void execute_task(Task *task, int thread_count) {
    BMPImage output;
    BMPStatus bmp_status = BMP_STATUS_OK;
    double start_time = now_seconds();

    task->status = -1;
    task->elapsed_seconds = 0.0;
    task->error_message[0] = '\0';

    if (task->input_image == NULL || task->input_image->channels != 3) {
        snprintf(task->error_message, sizeof(task->error_message), "invalid_channels");
        return;
    }

    if (bmp_create_like_with_error(task->input_image, &output, &bmp_status) != 0) {
        snprintf(task->error_message, sizeof(task->error_message), "%s", bmp_status_message(bmp_status));
        return;
    }

    if (apply_transform_parallel(task->input_image,
                                 &output,
                                 task->transform,
                                 task->blur_kernel_size,
                                 thread_count) != 0) {
        bmp_free(&output);
        snprintf(task->error_message, sizeof(task->error_message), "filter_failed");
        return;
    }

    if (bmp_save_with_error(task->output_path, &output, &bmp_status) != 0) {
        bmp_free(&output);
        snprintf(task->error_message, sizeof(task->error_message), "%s", bmp_status_message(bmp_status));
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

int run_tasks_distributed(Task *tasks, int task_count, int thread_count, int rank, int world_size) {
    int i = 0;

    if (tasks == NULL || task_count <= 0 || thread_count <= 0 || rank < 0 || world_size <= 0) {
        return -1;
    }

    for (i = rank; i < task_count; i += world_size) {
        execute_task(&tasks[i], thread_count);
    }

    return 0;
}
