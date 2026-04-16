#include "bmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_le_int32(const unsigned char *buffer) {
    return (int)buffer[0] |
           ((int)buffer[1] << 8) |
           ((int)buffer[2] << 16) |
           ((int)buffer[3] << 24);
}

int bmp_load(const char *path, BMPImage *image) {
    FILE *file = NULL;
    int height = 0;
    size_t data_size = 0;

    if (image == NULL || path == NULL) {
        return -1;
    }

    memset(image, 0, sizeof(*image));
    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    if (fread(image->header, 1, sizeof(image->header), file) != sizeof(image->header)) {
        fclose(file);
        return -1;
    }

    if (image->header[0] != 'B' || image->header[1] != 'M') {
        fclose(file);
        return -1;
    }

    image->width = read_le_int32(&image->header[18]);
    height = read_le_int32(&image->header[22]);

    if (image->width <= 0 || height == 0) {
        fclose(file);
        return -1;
    }

    if (image->header[28] != 24 || image->header[30] != 0) {
        fclose(file);
        return -1;
    }

    image->height = height < 0 ? -height : height;
    image->row_stride = (image->width * 3 + 3) & ~3;
    data_size = (size_t)image->row_stride * (size_t)image->height;
    image->data = (unsigned char *)malloc(data_size);
    if (image->data == NULL) {
        fclose(file);
        return -1;
    }

    if (fread(image->data, 1, data_size, file) != data_size) {
        bmp_free(image);
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;
}

int bmp_save(const char *path, const BMPImage *image) {
    FILE *file = NULL;
    size_t data_size = 0;

    if (path == NULL || image == NULL || image->data == NULL) {
        return -1;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }

    data_size = (size_t)image->row_stride * (size_t)image->height;
    if (fwrite(image->header, 1, sizeof(image->header), file) != sizeof(image->header)) {
        fclose(file);
        return -1;
    }

    if (fwrite(image->data, 1, data_size, file) != data_size) {
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;
}

int bmp_create_like(const BMPImage *source, BMPImage *image) {
    size_t data_size = 0;

    if (source == NULL || image == NULL || source->data == NULL) {
        return -1;
    }

    memset(image, 0, sizeof(*image));
    memcpy(image->header, source->header, sizeof(image->header));
    image->width = source->width;
    image->height = source->height;
    image->row_stride = source->row_stride;
    data_size = (size_t)image->row_stride * (size_t)image->height;
    image->data = (unsigned char *)calloc(data_size, 1);
    if (image->data == NULL) {
        return -1;
    }

    return 0;
}

void bmp_free(BMPImage *image) {
    if (image == NULL) {
        return;
    }

    free(image->data);
    image->data = NULL;
    image->width = 0;
    image->height = 0;
    image->row_stride = 0;
}
