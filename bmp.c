#include "bmp.h"

#include <limits.h>
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

static void write_le_int32(unsigned char *buffer, int value) {
    buffer[0] = (unsigned char)(value & 0xFF);
    buffer[1] = (unsigned char)((value >> 8) & 0xFF);
    buffer[2] = (unsigned char)((value >> 16) & 0xFF);
    buffer[3] = (unsigned char)((value >> 24) & 0xFF);
}

static void write_le_uint16(unsigned char *buffer, unsigned short value) {
    buffer[0] = (unsigned char)(value & 0xFFu);
    buffer[1] = (unsigned char)((value >> 8) & 0xFFu);
}

static void bmp_set_status(BMPStatus *status, BMPStatus value) {
    if (status != NULL) {
        *status = value;
    }
}

static void normalize_header_to_24bit_topdown(BMPImage *image) {
    size_t data_size = (size_t)image->row_stride * (size_t)image->height;

    write_le_uint16(&image->header[28], 24);
    write_le_uint32(&image->header[30], 0);
    write_le_uint32(&image->header[34], (unsigned int)data_size);
    write_le_uint32(&image->header[2], (unsigned int)(54u + data_size));
    write_le_uint32(&image->header[10], 54u);
    write_le_uint32(&image->header[46], 0);
    write_le_int32(&image->header[22], -image->height);
}

const char *bmp_status_message(BMPStatus status) {
    switch (status) {
        case BMP_STATUS_OK:
            return "";
        case BMP_STATUS_READ_BMP_FAILED:
            return "read_bmp_failed";
        case BMP_STATUS_UNSUPPORTED_BMP_FORMAT:
            return "unsupported_bmp_format";
        case BMP_STATUS_UNSUPPORTED_BIT_DEPTH:
            return "unsupported_bit_depth";
        case BMP_STATUS_UNSUPPORTED_COMPRESSION:
            return "unsupported_compression";
        case BMP_STATUS_ALLOCATION_FAILED:
            return "allocation_failed";
        case BMP_STATUS_WRITE_BMP_FAILED:
            return "write_bmp_failed";
        case BMP_STATUS_INVALID_DIMENSIONS:
            return "invalid_dimensions";
        case BMP_STATUS_INVALID_CHANNELS:
            return "invalid_channels";
    }

    return "read_bmp_failed";
}

static int bmp_validate_and_fill_info(const unsigned char *header, BMPInfo *info, BMPStatus *status) {
    int raw_height = 0;
    int bits_per_pixel = 0;
    unsigned int dib_header_size = 0;
    unsigned int pixel_offset = 0;
    unsigned int compression = 0;
    unsigned int colors_used = 0;
    unsigned int palette_entries = 0;
    int channels = 0;
    long long raw_stride = 0;

    if (header[0] != 'B' || header[1] != 'M') {
        bmp_set_status(status, BMP_STATUS_UNSUPPORTED_BMP_FORMAT);
        return -1;
    }

    dib_header_size = read_le_uint32(&header[14]);
    if (dib_header_size < 40) {
        bmp_set_status(status, BMP_STATUS_UNSUPPORTED_BMP_FORMAT);
        return -1;
    }

    info->width = read_le_int32(&header[18]);
    raw_height = read_le_int32(&header[22]);
    info->height = raw_height < 0 ? -raw_height : raw_height;
    info->top_down = raw_height < 0 ? 1 : 0;
    bits_per_pixel = (int)read_le_uint16(&header[28]);
    compression = read_le_uint32(&header[30]);
    pixel_offset = read_le_uint32(&header[10]);
    colors_used = read_le_uint32(&header[46]);

    info->bits_per_pixel = bits_per_pixel;
    info->compression = compression;
    info->pixel_offset = pixel_offset;
    info->dib_header_size = dib_header_size;
    info->palette_entries = 0;

    if (info->width <= 0 || info->height <= 0) {
        bmp_set_status(status, BMP_STATUS_INVALID_DIMENSIONS);
        return -1;
    }

    if (bits_per_pixel == 8) {
        channels = 1;
        if (compression != 0) {
            bmp_set_status(status, BMP_STATUS_UNSUPPORTED_COMPRESSION);
            return -1;
        }
        if (pixel_offset <= 14u + dib_header_size) {
            bmp_set_status(status, BMP_STATUS_UNSUPPORTED_BMP_FORMAT);
            return -1;
        }
        palette_entries = (pixel_offset - (14u + dib_header_size)) / 4u;
        if (colors_used > 0 && colors_used < palette_entries) {
            palette_entries = colors_used;
        }
        if (palette_entries == 0 || palette_entries > 256u) {
            bmp_set_status(status, BMP_STATUS_UNSUPPORTED_BMP_FORMAT);
            return -1;
        }
        info->palette_entries = palette_entries;
    } else if (bits_per_pixel == 24) {
        channels = 3;
        if (compression != 0) {
            bmp_set_status(status, BMP_STATUS_UNSUPPORTED_COMPRESSION);
            return -1;
        }
    } else if (bits_per_pixel == 32) {
        channels = 4;
        if (compression != 0 && compression != 3 && compression != 6) {
            bmp_set_status(status, BMP_STATUS_UNSUPPORTED_COMPRESSION);
            return -1;
        }
    } else {
        bmp_set_status(status, BMP_STATUS_UNSUPPORTED_BIT_DEPTH);
        return -1;
    }

    raw_stride = ((long long)info->width * bits_per_pixel + 31LL) / 32LL;
    raw_stride *= 4LL;
    if (raw_stride <= 0 || raw_stride > INT_MAX) {
        bmp_set_status(status, BMP_STATUS_INVALID_DIMENSIONS);
        return -1;
    }

    info->channels = channels;
    info->row_stride = (int)raw_stride;
    bmp_set_status(status, BMP_STATUS_OK);
    return 0;
}

int bmp_load_detailed(const char *path, BMPImage *image, BMPInfo *info, BMPStatus *status) {
    FILE *file = NULL;
    unsigned char *source_data = NULL;
    unsigned char *palette_data = NULL;
    BMPInfo local_info;
    size_t source_data_size = 0;
    size_t data_size = 0;
    int row = 0;

    if (image == NULL || path == NULL) {
        bmp_set_status(status, BMP_STATUS_READ_BMP_FAILED);
        return -1;
    }

    memset(image, 0, sizeof(*image));
    memset(&local_info, 0, sizeof(local_info));
    if (info != NULL) {
        memset(info, 0, sizeof(*info));
    }
    bmp_set_status(status, BMP_STATUS_OK);

    file = fopen(path, "rb");
    if (file == NULL) {
        bmp_set_status(status, BMP_STATUS_READ_BMP_FAILED);
        return -1;
    }

    if (fread(image->header, 1, sizeof(image->header), file) != sizeof(image->header)) {
        fclose(file);
        bmp_set_status(status, BMP_STATUS_READ_BMP_FAILED);
        return -1;
    }

    if (bmp_validate_and_fill_info(image->header, &local_info, status) != 0) {
        if (info != NULL) {
            *info = local_info;
        }
        fclose(file);
        return -1;
    }

    image->width = local_info.width;
    image->height = local_info.height;
    image->row_stride = (image->width * 3 + 3) & ~3;
    image->channels = 3;
    image->source_bits_per_pixel = local_info.bits_per_pixel;
    image->source_compression = local_info.compression;
    image->source_row_stride = local_info.row_stride;
    image->top_down = 1;

    data_size = (size_t)image->row_stride * (size_t)image->height;
    image->data = (unsigned char *)calloc(data_size, 1);
    if (image->data == NULL) {
        fclose(file);
        bmp_set_status(status, BMP_STATUS_ALLOCATION_FAILED);
        return -1;
    }

    source_data_size = (size_t)local_info.row_stride * (size_t)local_info.height;
    source_data = (unsigned char *)malloc(source_data_size);
    if (source_data == NULL) {
        bmp_free(image);
        fclose(file);
        bmp_set_status(status, BMP_STATUS_ALLOCATION_FAILED);
        return -1;
    }

    if (fseek(file, (long)local_info.pixel_offset, SEEK_SET) != 0 ||
        fread(source_data, 1, source_data_size, file) != source_data_size) {
        free(source_data);
        bmp_free(image);
        fclose(file);
        bmp_set_status(status, BMP_STATUS_READ_BMP_FAILED);
        return -1;
    }

    if (local_info.bits_per_pixel == 8) {
        size_t palette_size = (size_t)local_info.palette_entries * 4u;

        palette_data = (unsigned char *)malloc(palette_size);
        if (palette_data == NULL) {
            free(source_data);
            bmp_free(image);
            fclose(file);
            bmp_set_status(status, BMP_STATUS_ALLOCATION_FAILED);
            return -1;
        }

        if (fseek(file, (long)(14u + local_info.dib_header_size), SEEK_SET) != 0 ||
            fread(palette_data, 1, palette_size, file) != palette_size) {
            free(palette_data);
            free(source_data);
            bmp_free(image);
            fclose(file);
            bmp_set_status(status, BMP_STATUS_READ_BMP_FAILED);
            return -1;
        }
    }

    for (row = 0; row < local_info.height; ++row) {
        int source_row_index = local_info.top_down ? row : (local_info.height - 1 - row);
        const unsigned char *src_row = source_data + (size_t)source_row_index * (size_t)local_info.row_stride;
        unsigned char *dst_row = image->data + (size_t)row * (size_t)image->row_stride;
        int col = 0;

        if (local_info.bits_per_pixel == 24) {
            memcpy(dst_row, src_row, (size_t)local_info.width * 3u);
        } else if (local_info.bits_per_pixel == 32) {
            for (col = 0; col < local_info.width; ++col) {
                dst_row[col * 3 + 0] = src_row[col * 4 + 0];
                dst_row[col * 3 + 1] = src_row[col * 4 + 1];
                dst_row[col * 3 + 2] = src_row[col * 4 + 2];
            }
        } else if (local_info.bits_per_pixel == 8) {
            for (col = 0; col < local_info.width; ++col) {
                unsigned int index = src_row[col];
                if (index >= local_info.palette_entries) {
                    index = 0;
                }
                dst_row[col * 3 + 0] = palette_data[index * 4 + 0];
                dst_row[col * 3 + 1] = palette_data[index * 4 + 1];
                dst_row[col * 3 + 2] = palette_data[index * 4 + 2];
            }
        }
    }

    normalize_header_to_24bit_topdown(image);
    if (info != NULL) {
        *info = local_info;
    }

    free(palette_data);
    free(source_data);
    fclose(file);
    bmp_set_status(status, BMP_STATUS_OK);
    return 0;
}

int bmp_load(const char *path, BMPImage *image) {
    return bmp_load_detailed(path, image, NULL, NULL);
}

int bmp_save_with_error(const char *path, const BMPImage *image, BMPStatus *status) {
    FILE *file = NULL;
    size_t data_size = 0;

    if (path == NULL || image == NULL || image->data == NULL) {
        bmp_set_status(status, BMP_STATUS_WRITE_BMP_FAILED);
        return -1;
    }

    if (image->width <= 0 || image->height <= 0 || image->row_stride <= 0) {
        bmp_set_status(status, BMP_STATUS_INVALID_DIMENSIONS);
        return -1;
    }

    if (image->channels != 3) {
        bmp_set_status(status, BMP_STATUS_INVALID_CHANNELS);
        return -1;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        bmp_set_status(status, BMP_STATUS_WRITE_BMP_FAILED);
        return -1;
    }

    data_size = (size_t)image->row_stride * (size_t)image->height;
    if (fwrite(image->header, 1, sizeof(image->header), file) != sizeof(image->header) ||
        fwrite(image->data, 1, data_size, file) != data_size) {
        fclose(file);
        bmp_set_status(status, BMP_STATUS_WRITE_BMP_FAILED);
        return -1;
    }

    fclose(file);
    bmp_set_status(status, BMP_STATUS_OK);
    return 0;
}

int bmp_save(const char *path, const BMPImage *image) {
    return bmp_save_with_error(path, image, NULL);
}

int bmp_create_like_with_error(const BMPImage *source, BMPImage *image, BMPStatus *status) {
    size_t data_size = 0;

    if (source == NULL || image == NULL || source->data == NULL) {
        bmp_set_status(status, BMP_STATUS_ALLOCATION_FAILED);
        return -1;
    }

    if (source->width <= 0 || source->height <= 0 || source->row_stride <= 0) {
        bmp_set_status(status, BMP_STATUS_INVALID_DIMENSIONS);
        return -1;
    }

    if (source->channels != 3) {
        bmp_set_status(status, BMP_STATUS_INVALID_CHANNELS);
        return -1;
    }

    memset(image, 0, sizeof(*image));
    memcpy(image->header, source->header, sizeof(image->header));
    image->width = source->width;
    image->height = source->height;
    image->row_stride = source->row_stride;
    image->channels = source->channels;
    image->source_bits_per_pixel = source->source_bits_per_pixel;
    image->source_compression = source->source_compression;
    image->source_row_stride = source->source_row_stride;
    image->top_down = source->top_down;

    data_size = (size_t)image->row_stride * (size_t)image->height;
    image->data = (unsigned char *)calloc(data_size, 1);
    if (image->data == NULL) {
        bmp_set_status(status, BMP_STATUS_ALLOCATION_FAILED);
        return -1;
    }

    bmp_set_status(status, BMP_STATUS_OK);
    return 0;
}

int bmp_create_like(const BMPImage *source, BMPImage *image) {
    return bmp_create_like_with_error(source, image, NULL);
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
    image->channels = 0;
    image->source_bits_per_pixel = 0;
    image->source_compression = 0;
    image->source_row_stride = 0;
    image->top_down = 0;
}
