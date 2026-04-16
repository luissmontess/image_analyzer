#include "task_pool.h"

#include "timing.h"

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct {
    Task *tasks;
    int task_count;
    int next_index;
    pthread_mutex_t mutex;
} TaskQueue;

static void execute_task(Task *task) {
    BMPImage output;
    double start_time = now_seconds();

    task->status = -1;
    task->elapsed_seconds = 0.0;

    if (bmp_create_like(task->input_image, &output) != 0) {
        return;
    }

    if (apply_transform(task->input_image, &output, task->transform) != 0) {
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

static void *worker_main(void *arg) {
    TaskQueue *queue = (TaskQueue *)arg;

    for (;;) {
        int index = 0;

        pthread_mutex_lock(&queue->mutex);
        index = queue->next_index;
        queue->next_index += 1;
        pthread_mutex_unlock(&queue->mutex);

        if (index >= queue->task_count) {
            break;
        }

        execute_task(&queue->tasks[index]);
    }

    return NULL;
}

int run_tasks(Task *tasks, int task_count, int thread_count) {
    TaskQueue queue;
    pthread_t *threads = NULL;
    int i = 0;
    int status = 0;

    if (tasks == NULL || task_count <= 0 || thread_count <= 0) {
        return -1;
    }

    queue.tasks = tasks;
    queue.task_count = task_count;
    queue.next_index = 0;
    pthread_mutex_init(&queue.mutex, NULL);

    threads = (pthread_t *)malloc((size_t)thread_count * sizeof(*threads));
    if (threads == NULL) {
        pthread_mutex_destroy(&queue.mutex);
        return -1;
    }

    for (i = 0; i < thread_count; ++i) {
        if (pthread_create(&threads[i], NULL, worker_main, &queue) != 0) {
            status = -1;
            thread_count = i;
            break;
        }
    }

    for (i = 0; i < thread_count; ++i) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&queue.mutex);
    free(threads);
    return status;
}
