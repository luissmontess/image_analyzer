#ifndef TASK_POOL_H
#define TASK_POOL_H

#include "bmp.h"
#include "filters.h"

#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    const char *input_path;
    const char *image_name;
    TransformType transform;
    const BMPImage *input_image;
    char output_path[PATH_MAX];
    double elapsed_seconds;
    int status;
} Task;

int run_tasks(Task *tasks, int task_count, int thread_count);

#endif
