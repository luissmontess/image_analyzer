#ifndef BMP_H
#define BMP_H

#include <stddef.h>

typedef struct {
    unsigned char header[54];
    int width;
    int height;
    int row_stride;
    unsigned char *data;
} BMPImage;

int bmp_load(const char *path, BMPImage *image);
int bmp_save(const char *path, const BMPImage *image);
int bmp_create_like(const BMPImage *source, BMPImage *image);
void bmp_free(BMPImage *image);

#endif
