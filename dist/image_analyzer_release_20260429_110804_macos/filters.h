#ifndef FILTERS_H
#define FILTERS_H

#include "bmp.h"

typedef enum {
    TRANSFORM_FLIP_HORIZONTAL_GRAY = 0,
    TRANSFORM_FLIP_VERTICAL_GRAY,
    TRANSFORM_BLUR_GRAY,
    TRANSFORM_FLIP_HORIZONTAL_COLOR,
    TRANSFORM_FLIP_VERTICAL_COLOR,
    TRANSFORM_BLUR_COLOR,
    TRANSFORM_COUNT
} TransformType;

const char *transform_name(TransformType type);
const char *transform_slug(TransformType type);
int apply_transform(const BMPImage *input, BMPImage *output, TransformType type);
int apply_transform_parallel(const BMPImage *input,
                             BMPImage *output,
                             TransformType type,
                             int blur_kernel_size,
                             int thread_count);

#endif
