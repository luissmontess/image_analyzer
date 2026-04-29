#include "filters.h"

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct {
    const BMPImage *input;
    BMPImage *output;
    TransformType type;
    int blur_kernel_size;
    int y_start;
    int y_end;
} TransformChunk;

static unsigned char clamp_u8(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return (unsigned char)value;
}

static unsigned char *pixel_at(BMPImage *image, int x, int y) {
    return image->data + (size_t)y * (size_t)image->row_stride + (size_t)x * 3U;
}

static const unsigned char *pixel_at_const(const BMPImage *image, int x, int y) {
    return image->data + (size_t)y * (size_t)image->row_stride + (size_t)x * 3U;
}

static unsigned char grayscale_of(const unsigned char *pixel) {
    return (unsigned char)(((int)pixel[0] + (int)pixel[1] + (int)pixel[2]) / 3);
}

static void fill_grayscale_pixel(unsigned char *pixel, unsigned char gray) {
    pixel[0] = gray;
    pixel[1] = gray;
    pixel[2] = gray;
}

static int validate_blur_kernel_size(int blur_kernel_size) {
    if (blur_kernel_size < 3 || (blur_kernel_size % 2) == 0) {
        return -1;
    }

    return blur_kernel_size;
}

static unsigned char blur_pixel_gray(const BMPImage *input, int x, int y, int blur_kernel_size) {
    int radius = blur_kernel_size / 2;
    int sum = 0;
    int count = 0;
    int dy = 0;

    for (dy = -radius; dy <= radius; ++dy) {
        int ny = y + dy;
        int dx = 0;
        if (ny < 0 || ny >= input->height) {
            continue;
        }

        for (dx = -radius; dx <= radius; ++dx) {
            int nx = x + dx;
            if (nx < 0 || nx >= input->width) {
                continue;
            }

            sum += (int)grayscale_of(pixel_at_const(input, nx, ny));
            ++count;
        }
    }

    if (count <= 0) {
        return 0;
    }

    return clamp_u8(sum / count);
}

static void blur_pixel_color(const BMPImage *input,
                             int x,
                             int y,
                             int blur_kernel_size,
                             unsigned char *dst) {
    int radius = blur_kernel_size / 2;
    int sum_b = 0;
    int sum_g = 0;
    int sum_r = 0;
    int count = 0;
    int dy = 0;

    for (dy = -radius; dy <= radius; ++dy) {
        int ny = y + dy;
        int dx = 0;
        if (ny < 0 || ny >= input->height) {
            continue;
        }

        for (dx = -radius; dx <= radius; ++dx) {
            int nx = x + dx;
            const unsigned char *src = NULL;
            if (nx < 0 || nx >= input->width) {
                continue;
            }

            src = pixel_at_const(input, nx, ny);
            sum_b += (int)src[0];
            sum_g += (int)src[1];
            sum_r += (int)src[2];
            ++count;
        }
    }

    if (count <= 0) {
        dst[0] = 0;
        dst[1] = 0;
        dst[2] = 0;
        return;
    }

    dst[0] = clamp_u8(sum_b / count);
    dst[1] = clamp_u8(sum_g / count);
    dst[2] = clamp_u8(sum_r / count);
}

static void copy_padding(const BMPImage *input, BMPImage *output) {
    int y = 0;
    int padding_offset = input->width * 3;
    int padding_bytes = input->row_stride - padding_offset;

    if (padding_bytes <= 0) {
        return;
    }

    for (y = 0; y < input->height; ++y) {
        const unsigned char *src = input->data + (size_t)y * (size_t)input->row_stride + (size_t)padding_offset;
        unsigned char *dst = output->data + (size_t)y * (size_t)output->row_stride + (size_t)padding_offset;
        int i = 0;
        for (i = 0; i < padding_bytes; ++i) {
            dst[i] = src[i];
        }
    }
}

static void apply_flip_horizontal_gray_range(const BMPImage *input, BMPImage *output, int y_start, int y_end) {
    int y = y_start;

    for (y = y_start; y < y_end; ++y) {
        int x = 0;
        for (x = 0; x < input->width; ++x) {
            const unsigned char *src = pixel_at_const(input, input->width - 1 - x, y);
            unsigned char *dst = pixel_at(output, x, y);
            fill_grayscale_pixel(dst, grayscale_of(src));
        }
    }
}

static void apply_flip_vertical_gray_range(const BMPImage *input, BMPImage *output, int y_start, int y_end) {
    int y = y_start;

    for (y = y_start; y < y_end; ++y) {
        int x = 0;
        for (x = 0; x < input->width; ++x) {
            const unsigned char *src = pixel_at_const(input, x, input->height - 1 - y);
            unsigned char *dst = pixel_at(output, x, y);
            fill_grayscale_pixel(dst, grayscale_of(src));
        }
    }
}

static void apply_blur_gray_range(const BMPImage *input,
                                  BMPImage *output,
                                  int blur_kernel_size,
                                  int y_start,
                                  int y_end) {
    int y = y_start;

    for (y = y_start; y < y_end; ++y) {
        int x = 0;
        for (x = 0; x < input->width; ++x) {
            fill_grayscale_pixel(pixel_at(output, x, y), blur_pixel_gray(input, x, y, blur_kernel_size));
        }
    }
}

static void apply_flip_horizontal_color_range(const BMPImage *input, BMPImage *output, int y_start, int y_end) {
    int y = y_start;

    for (y = y_start; y < y_end; ++y) {
        int x = 0;
        for (x = 0; x < input->width; ++x) {
            const unsigned char *src = pixel_at_const(input, input->width - 1 - x, y);
            unsigned char *dst = pixel_at(output, x, y);
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }
    }
}

static void apply_flip_vertical_color_range(const BMPImage *input, BMPImage *output, int y_start, int y_end) {
    int y = y_start;

    for (y = y_start; y < y_end; ++y) {
        int x = 0;
        for (x = 0; x < input->width; ++x) {
            const unsigned char *src = pixel_at_const(input, x, input->height - 1 - y);
            unsigned char *dst = pixel_at(output, x, y);
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }
    }
}

static void apply_blur_color_range(const BMPImage *input,
                                   BMPImage *output,
                                   int blur_kernel_size,
                                   int y_start,
                                   int y_end) {
    int y = y_start;

    for (y = y_start; y < y_end; ++y) {
        int x = 0;
        for (x = 0; x < input->width; ++x) {
            unsigned char *dst = pixel_at(output, x, y);
            blur_pixel_color(input, x, y, blur_kernel_size, dst);
        }
    }
}

static int apply_transform_range(const BMPImage *input,
                                 BMPImage *output,
                                 TransformType type,
                                 int blur_kernel_size,
                                 int y_start,
                                 int y_end) {
    switch (type) {
        case TRANSFORM_FLIP_HORIZONTAL_GRAY:
            apply_flip_horizontal_gray_range(input, output, y_start, y_end);
            return 0;
        case TRANSFORM_FLIP_VERTICAL_GRAY:
            apply_flip_vertical_gray_range(input, output, y_start, y_end);
            return 0;
        case TRANSFORM_BLUR_GRAY:
            blur_kernel_size = validate_blur_kernel_size(blur_kernel_size);
            if (blur_kernel_size < 0) {
                return -1;
            }
            apply_blur_gray_range(input, output, blur_kernel_size, y_start, y_end);
            return 0;
        case TRANSFORM_FLIP_HORIZONTAL_COLOR:
            apply_flip_horizontal_color_range(input, output, y_start, y_end);
            return 0;
        case TRANSFORM_FLIP_VERTICAL_COLOR:
            apply_flip_vertical_color_range(input, output, y_start, y_end);
            return 0;
        case TRANSFORM_BLUR_COLOR:
            blur_kernel_size = validate_blur_kernel_size(blur_kernel_size);
            if (blur_kernel_size < 0) {
                return -1;
            }
            apply_blur_color_range(input, output, blur_kernel_size, y_start, y_end);
            return 0;
        case TRANSFORM_COUNT:
            break;
    }

    return -1;
}

static void *transform_worker_main(void *arg) {
    TransformChunk *chunk = (TransformChunk *)arg;
    apply_transform_range(chunk->input,
                          chunk->output,
                          chunk->type,
                          chunk->blur_kernel_size,
                          chunk->y_start,
                          chunk->y_end);
    return NULL;
}

const char *transform_name(TransformType type) {
    switch (type) {
        case TRANSFORM_FLIP_HORIZONTAL_GRAY:
            return "inversion_horizontal_grises";
        case TRANSFORM_FLIP_VERTICAL_GRAY:
            return "inversion_vertical_grises";
        case TRANSFORM_BLUR_GRAY:
            return "desenfoque_grises";
        case TRANSFORM_FLIP_HORIZONTAL_COLOR:
            return "inversion_horizontal_color";
        case TRANSFORM_FLIP_VERTICAL_COLOR:
            return "inversion_vertical_color";
        case TRANSFORM_BLUR_COLOR:
            return "desenfoque_color";
        case TRANSFORM_COUNT:
            break;
    }

    return "transformacion_desconocida";
}

const char *transform_slug(TransformType type) {
    switch (type) {
        case TRANSFORM_FLIP_HORIZONTAL_GRAY:
            return "hg";
        case TRANSFORM_FLIP_VERTICAL_GRAY:
            return "vg";
        case TRANSFORM_BLUR_GRAY:
            return "dg";
        case TRANSFORM_FLIP_HORIZONTAL_COLOR:
            return "hc";
        case TRANSFORM_FLIP_VERTICAL_COLOR:
            return "vc";
        case TRANSFORM_BLUR_COLOR:
            return "dc";
        case TRANSFORM_COUNT:
            break;
    }

    return "unknown";
}

int apply_transform(const BMPImage *input, BMPImage *output, TransformType type) {
    return apply_transform_parallel(input, output, type, 3, 1);
}

int apply_transform_parallel(const BMPImage *input,
                             BMPImage *output,
                             TransformType type,
                             int blur_kernel_size,
                             int thread_count) {
    pthread_t *threads = NULL;
    TransformChunk *chunks = NULL;
    int created_threads = 0;
    int effective_threads = 0;
    int i = 0;
    int next_y = 0;

    if (input == NULL || output == NULL || input->data == NULL || output->data == NULL) {
        return -1;
    }

    copy_padding(input, output);

    if (thread_count <= 1) {
        return apply_transform_range(input, output, type, blur_kernel_size, 0, input->height);
    }

    if (input->height <= 0) {
        return -1;
    }

    if ((type == TRANSFORM_BLUR_GRAY || type == TRANSFORM_BLUR_COLOR) && validate_blur_kernel_size(blur_kernel_size) < 0) {
        return -1;
    }

    effective_threads = thread_count;
    if (effective_threads > input->height) {
        effective_threads = input->height;
    }

    copy_padding(input, output);

    threads = (pthread_t *)malloc((size_t)effective_threads * sizeof(*threads));
    chunks = (TransformChunk *)malloc((size_t)effective_threads * sizeof(*chunks));
    if (threads == NULL || chunks == NULL) {
        free(threads);
        free(chunks);
        return -1;
    }

    for (i = 0; i < effective_threads; ++i) {
        int remaining_rows = input->height - next_y;
        int remaining_threads = effective_threads - i;
        int block_rows = remaining_rows / remaining_threads;

        chunks[i].input = input;
        chunks[i].output = output;
        chunks[i].type = type;
        chunks[i].blur_kernel_size = blur_kernel_size;
        chunks[i].y_start = next_y;
        chunks[i].y_end = next_y + block_rows;
        next_y = chunks[i].y_end;

        if (pthread_create(&threads[i], NULL, transform_worker_main, &chunks[i]) != 0) {
            free(threads);
            free(chunks);
            return -1;
        }
        ++created_threads;
    }

    for (i = 0; i < created_threads; ++i) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
    free(chunks);
    return 0;
}
