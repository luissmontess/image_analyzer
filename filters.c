#include "filters.h"

#include <stddef.h>

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

static void apply_flip_horizontal_gray(const BMPImage *input, BMPImage *output) {
    int y = 0;

    for (y = 0; y < input->height; ++y) {
        int x = 0;
        for (x = 0; x < input->width; ++x) {
            const unsigned char *src = pixel_at_const(input, input->width - 1 - x, y);
            unsigned char *dst = pixel_at(output, x, y);
            fill_grayscale_pixel(dst, grayscale_of(src));
        }
    }
}

static void apply_flip_vertical_gray(const BMPImage *input, BMPImage *output) {
    int y = 0;

    for (y = 0; y < input->height; ++y) {
        int x = 0;
        for (x = 0; x < input->width; ++x) {
            const unsigned char *src = pixel_at_const(input, x, input->height - 1 - y);
            unsigned char *dst = pixel_at(output, x, y);
            fill_grayscale_pixel(dst, grayscale_of(src));
        }
    }
}

static void apply_blur_gray(const BMPImage *input, BMPImage *output) {
    int y = 0;

    /* Se ignoran vecinos fuera de rango para no leer memoria invalida.
       Eso hace que el promedio de bordes use menos de 9 muestras. */
    for (y = 0; y < input->height; ++y) {
        int x = 0;
        for (x = 0; x < input->width; ++x) {
            int sum = 0;
            int count = 0;
            int dy = 0;

            for (dy = -1; dy <= 1; ++dy) {
                int dx = 0;
                int ny = y + dy;
                if (ny < 0 || ny >= input->height) {
                    continue;
                }

                for (dx = -1; dx <= 1; ++dx) {
                    int nx = x + dx;
                    if (nx < 0 || nx >= input->width) {
                        continue;
                    }

                    sum += (int)grayscale_of(pixel_at_const(input, nx, ny));
                    ++count;
                }
            }

            fill_grayscale_pixel(pixel_at(output, x, y), clamp_u8(sum / count));
        }
    }
}

static void apply_flip_horizontal_color(const BMPImage *input, BMPImage *output) {
    int y = 0;

    for (y = 0; y < input->height; ++y) {
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

static void apply_flip_vertical_color(const BMPImage *input, BMPImage *output) {
    int y = 0;

    for (y = 0; y < input->height; ++y) {
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

static void apply_blur_color(const BMPImage *input, BMPImage *output) {
    int y = 0;

    /* Se usa un kernel promedio 3x3 uniforme con normalizacion dinamica
       en bordes: solo se promedian los vecinos realmente disponibles. */
    for (y = 0; y < input->height; ++y) {
        int x = 0;
        for (x = 0; x < input->width; ++x) {
            int sum_b = 0;
            int sum_g = 0;
            int sum_r = 0;
            int count = 0;
            int dy = 0;

            for (dy = -1; dy <= 1; ++dy) {
                int dx = 0;
                int ny = y + dy;
                if (ny < 0 || ny >= input->height) {
                    continue;
                }

                for (dx = -1; dx <= 1; ++dx) {
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

            pixel_at(output, x, y)[0] = clamp_u8(sum_b / count);
            pixel_at(output, x, y)[1] = clamp_u8(sum_g / count);
            pixel_at(output, x, y)[2] = clamp_u8(sum_r / count);
        }
    }
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
            return "flip_h_gray";
        case TRANSFORM_FLIP_VERTICAL_GRAY:
            return "flip_v_gray";
        case TRANSFORM_BLUR_GRAY:
            return "blur_gray";
        case TRANSFORM_FLIP_HORIZONTAL_COLOR:
            return "flip_h_color";
        case TRANSFORM_FLIP_VERTICAL_COLOR:
            return "flip_v_color";
        case TRANSFORM_BLUR_COLOR:
            return "blur_color";
        case TRANSFORM_COUNT:
            break;
    }

    return "unknown";
}

int apply_transform(const BMPImage *input, BMPImage *output, TransformType type) {
    if (input == NULL || output == NULL || input->data == NULL || output->data == NULL) {
        return -1;
    }

    copy_padding(input, output);

    switch (type) {
        case TRANSFORM_FLIP_HORIZONTAL_GRAY:
            apply_flip_horizontal_gray(input, output);
            return 0;
        case TRANSFORM_FLIP_VERTICAL_GRAY:
            apply_flip_vertical_gray(input, output);
            return 0;
        case TRANSFORM_BLUR_GRAY:
            apply_blur_gray(input, output);
            return 0;
        case TRANSFORM_FLIP_HORIZONTAL_COLOR:
            apply_flip_horizontal_color(input, output);
            return 0;
        case TRANSFORM_FLIP_VERTICAL_COLOR:
            apply_flip_vertical_color(input, output);
            return 0;
        case TRANSFORM_BLUR_COLOR:
            apply_blur_color(input, output);
            return 0;
        case TRANSFORM_COUNT:
            break;
    }

    return -1;
}
