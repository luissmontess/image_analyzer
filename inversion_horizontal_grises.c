#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void inversion_horizontal_escala_grises(const char* input_path, const char* name_output) {
    FILE *image, *outputImage;
    char output_path[100] = "./img/";
    strcat(output_path, name_output);
    strcat(output_path, ".bmp");

    image = fopen(input_path, "rb");
    outputImage = fopen(output_path, "wb");

    if (!image || !outputImage) {
        printf("Error abriendo archivos.\n");
        if (image) fclose(image);
        if (outputImage) fclose(outputImage);
        return;
    }

    unsigned char header[54];
    fread(header, sizeof(unsigned char), 54, image);
    fwrite(header, sizeof(unsigned char), 54, outputImage);

    int width = *(int*)&header[18];
    int height = *(int*)&header[22];
    int row_padded = (width * 3 + 3) & (~3);

    unsigned char** input_rows = (unsigned char**)malloc(height * sizeof(unsigned char*));
    unsigned char** output_rows = (unsigned char**)malloc(height * sizeof(unsigned char*));

    if (!input_rows || !output_rows) {
        printf("Error reservando memoria.\n");
        fclose(image);
        fclose(outputImage);
        free(input_rows);
        free(output_rows);
        return;
    }

    for (int i = 0; i < height; i++) {
        input_rows[i] = (unsigned char*)malloc(row_padded);
        output_rows[i] = (unsigned char*)malloc(row_padded);

        if (!input_rows[i] || !output_rows[i]) {
            printf("Error reservando memoria para filas.\n");

            for (int j = 0; j <= i; j++) {
                free(input_rows[j]);
                free(output_rows[j]);
            }

            free(input_rows);
            free(output_rows);
            fclose(image);
            fclose(outputImage);
            return;
        }

        fread(input_rows[i], sizeof(unsigned char), row_padded, image);
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src_x = width - 1 - x;
            int src_index = src_x * 3;
            int dst_index = x * 3;

            unsigned char blue = input_rows[y][src_index + 0];
            unsigned char green = input_rows[y][src_index + 1];
            unsigned char red = input_rows[y][src_index + 2];

            unsigned char gray = (unsigned char)((red + green + blue) / 3);

            output_rows[y][dst_index + 0] = gray;
            output_rows[y][dst_index + 1] = gray;
            output_rows[y][dst_index + 2] = gray;
        }

        for (int p = width * 3; p < row_padded; p++) {
            output_rows[y][p] = input_rows[y][p];
        }
    }

    for (int i = 0; i < height; i++) {
        fwrite(output_rows[i], sizeof(unsigned char), row_padded, outputImage);
        free(input_rows[i]);
        free(output_rows[i]);
    }

    FILE *outputLog = fopen("output_log.txt", "a");
    if (outputLog != NULL) {
        fprintf(outputLog, "Función: inversion_horizontal_escala_grises, con %s\n", input_path);
        fprintf(outputLog, "Localidades totales leídas: %d\n", width * height);
        fprintf(outputLog, "Localidades totales escritas: %d\n", width * height);
        fprintf(outputLog, "-------------------------------------\n");
        fclose(outputLog);
    }

    free(input_rows);
    free(output_rows);
    fclose(image);
    fclose(outputImage);
}
