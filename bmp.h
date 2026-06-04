#ifndef BMP_H
#define BMP_H

#include <stddef.h>

typedef enum {
    BMP_STATUS_OK = 0,
    BMP_STATUS_READ_BMP_FAILED,
    BMP_STATUS_UNSUPPORTED_BMP_FORMAT,
    BMP_STATUS_UNSUPPORTED_BIT_DEPTH,
    BMP_STATUS_UNSUPPORTED_COMPRESSION,
    BMP_STATUS_ALLOCATION_FAILED,
    BMP_STATUS_WRITE_BMP_FAILED,
    BMP_STATUS_INVALID_DIMENSIONS,
    BMP_STATUS_INVALID_CHANNELS
} BMPStatus;

typedef struct {
    int width;
    int height;
    int bits_per_pixel;
    unsigned int compression;
    int row_stride;
    int channels;
    int top_down;
    unsigned int pixel_offset;
    unsigned int dib_header_size;
    unsigned int palette_entries;
} BMPInfo;

typedef struct {
    unsigned char header[54];
    int width;
    int height;
    int row_stride;
    int channels;
    int source_bits_per_pixel;
    unsigned int source_compression;
    int source_row_stride;
    int top_down;
    unsigned char *data;
} BMPImage;

int bmp_load(const char *path, BMPImage *image);
int bmp_load_detailed(const char *path, BMPImage *image, BMPInfo *info, BMPStatus *status);
int bmp_save(const char *path, const BMPImage *image);
int bmp_save_with_error(const char *path, const BMPImage *image, BMPStatus *status);
int bmp_create_like(const BMPImage *source, BMPImage *image);
int bmp_create_like_with_error(const BMPImage *source, BMPImage *image, BMPStatus *status);
const char *bmp_status_message(BMPStatus status);
void bmp_free(BMPImage *image);

#endif
