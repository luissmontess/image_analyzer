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

static unsigned int read_le_uint32(const unsigned char *buffer) {
    return (unsigned int)buffer[0] |
           ((unsigned int)buffer[1] << 8) |
           ((unsigned int)buffer[2] << 16) |
           ((unsigned int)buffer[3] << 24);
}

static unsigned short read_le_uint16(const unsigned char *buffer) {
    return (unsigned short)((unsigned short)buffer[0] | ((unsigned short)buffer[1] << 8));
}

static void write_le_uint32(unsigned char *buffer, unsigned int value) {
    buffer[0] = (unsigned char)(value & 0xFFu);
    buffer[1] = (unsigned char)((value >> 8) & 0xFFu);
    buffer[2] = (unsigned char)((value >> 16) & 0xFFu);
    buffer[3] = (unsigned char)((value >> 24) & 0xFFu);
}

static void write_le_uint16(unsigned char *buffer, unsigned short value) {
    buffer[0] = (unsigned char)(value & 0xFFu);
    buffer[1] = (unsigned char)((value >> 8) & 0xFFu);
}

static void normalize_header_to_24bit(unsigned char *header, size_t data_size) {
    write_le_uint16(&header[28], 24);
    write_le_uint32(&header[30], 0);
    write_le_uint32(&header[34], (unsigned int)data_size);
    write_le_uint32(&header[2], (unsigned int)(54u + data_size));
    write_le_uint32(&header[10], 54u);
}

int bmp_load(const char *path, BMPImage *image) {
    FILE *file = NULL;
    int height = 0;
    int abs_height = 0;
    int source_stride = 0;
    int source_bpp = 0;
    unsigned int dib_header_size = 0;
    unsigned int colors_used = 0;
    unsigned int pixel_offset = 0;
    unsigned int compression = 0;
    unsigned char *source_data = NULL;
    unsigned char *palette_data = NULL;
    size_t data_size = 0;
    size_t source_data_size = 0;
    int palette_start = 0;
    int palette_entries = 0;
    int row = 0;

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
    dib_header_size = read_le_uint32(&image->header[14]);
    pixel_offset = read_le_uint32(&image->header[10]);
    source_bpp = (int)read_le_uint16(&image->header[28]);
    compression = read_le_uint32(&image->header[30]);
    colors_used = read_le_uint32(&image->header[46]);

    if (image->width <= 0 || height == 0) {
        fclose(file);
        return -1;
    }

    if (source_bpp == 24) {
        if (compression != 0) {
            fclose(file);
            return -1;
        }
    } else if (source_bpp == 8) {
        if (compression != 0) {
            fclose(file);
            return -1;
        }
    } else if (source_bpp == 32) {
        if (compression != 0 && compression != 3 && compression != 6) {
            fclose(file);
            return -1;
        }
    } else {
        fclose(file);
        return -1;
    }

    if (pixel_offset < sizeof(image->header)) {
        fclose(file);
        return -1;
    }

    image->height = height < 0 ? -height : height;
    abs_height = image->height;
    image->row_stride = (image->width * 3 + 3) & ~3;
    data_size = (size_t)image->row_stride * (size_t)image->height;
    image->data = (unsigned char *)malloc(data_size);
    if (image->data == NULL) {
        fclose(file);
        return -1;
    }

    source_stride = (image->width * source_bpp / 8 + 3) & ~3;
    source_data_size = (size_t)source_stride * (size_t)abs_height;
    source_data = (unsigned char *)malloc(source_data_size);
    if (source_data == NULL) {
        bmp_free(image);
        fclose(file);
        return -1;
    }

    if (fseek(file, (long)pixel_offset, SEEK_SET) != 0 ||
        fread(source_data, 1, source_data_size, file) != source_data_size) {
        free(source_data);
        bmp_free(image);
        fclose(file);
        return -1;
    }

    if (source_bpp == 24) {
        memcpy(image->data, source_data, data_size);
    } else if (source_bpp == 8) {
        palette_start = (int)(14 + dib_header_size);
        if (dib_header_size < 40 || palette_start < (int)sizeof(image->header) || palette_start > (int)pixel_offset) {
            free(source_data);
            bmp_free(image);
            fclose(file);
            return -1;
        }

        palette_entries = (int)((pixel_offset - (unsigned int)palette_start) / 4u);
        if (colors_used > 0 && (int)colors_used < palette_entries) {
            palette_entries = (int)colors_used;
        }

        if (palette_entries <= 0 || palette_entries > 256) {
            free(source_data);
            bmp_free(image);
            fclose(file);
            return -1;
        }

        palette_data = (unsigned char *)malloc((size_t)palette_entries * 4u);
        if (palette_data == NULL) {
            free(source_data);
            bmp_free(image);
            fclose(file);
            return -1;
        }

        if (fseek(file, palette_start, SEEK_SET) != 0 ||
            fread(palette_data, 4u, (size_t)palette_entries, file) != (size_t)palette_entries) {
            free(palette_data);
            free(source_data);
            bmp_free(image);
            fclose(file);
            return -1;
        }

        for (row = 0; row < abs_height; ++row) {
            int col = 0;
            const unsigned char *src_row = source_data + (size_t)row * (size_t)source_stride;
            unsigned char *dst_row = image->data + (size_t)row * (size_t)image->row_stride;

            for (col = 0; col < image->width; ++col) {
                unsigned int index = src_row[col];
                if ((int)index >= palette_entries) {
                    index = 0;
                }
                dst_row[col * 3 + 0] = palette_data[index * 4 + 0];
                dst_row[col * 3 + 1] = palette_data[index * 4 + 1];
                dst_row[col * 3 + 2] = palette_data[index * 4 + 2];
            }
            memset(dst_row + image->width * 3, 0, (size_t)(image->row_stride - image->width * 3));
        }

        free(palette_data);
        normalize_header_to_24bit(image->header, data_size);
    } else {
        for (row = 0; row < abs_height; ++row) {
            int col = 0;
            const unsigned char *src_row = source_data + (size_t)row * (size_t)source_stride;
            unsigned char *dst_row = image->data + (size_t)row * (size_t)image->row_stride;
            for (col = 0; col < image->width; ++col) {
                dst_row[col * 3 + 0] = src_row[col * 4 + 0];
                dst_row[col * 3 + 1] = src_row[col * 4 + 1];
                dst_row[col * 3 + 2] = src_row[col * 4 + 2];
            }
            memset(dst_row + image->width * 3, 0, (size_t)(image->row_stride - image->width * 3));
        }

        normalize_header_to_24bit(image->header, data_size);
    }

    free(source_data);
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
