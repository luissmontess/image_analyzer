#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void desenfoque_escala_grises(const char* input_path, const char* name_output, int kernel_size) {
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

    unsigned char** gray_rows = (unsigned char**)malloc(height * sizeof(unsigned char*));
    unsigned char** temp_rows = (unsigned char**)malloc(height * sizeof(unsigned char*));
    unsigned char** output_rows = (unsigned char**)malloc(height * sizeof(unsigned char*));

    if (!gray_rows || !temp_rows || !output_rows) {
        printf("Error reservando memoria.\n");
        fclose(image);
        fclose(outputImage);
        free(gray_rows);
        free(temp_rows);
        free(output_rows);
        return;
    }

    for (int i = 0; i < height; i++) {
        unsigned char* input_row = (unsigned char*)malloc(row_padded);
        gray_rows[i] = (unsigned char*)malloc(row_padded);
        temp_rows[i] = (unsigned char*)malloc(row_padded);
        output_rows[i] = (unsigned char*)malloc(row_padded);

        if (!input_row || !gray_rows[i] || !temp_rows[i] || !output_rows[i]) {
            printf("Error reservando memoria para filas.\n");
            free(input_row);

            for (int j = 0; j <= i; j++) {
                free(gray_rows[j]);
                free(temp_rows[j]);
                free(output_rows[j]);
            }

            free(gray_rows);
            free(temp_rows);
            free(output_rows);
            fclose(image);
            fclose(outputImage);
            return;
        }

        fread(input_row, sizeof(unsigned char), row_padded, image);

        for (int x = 0; x < width; x++) {
            int index = x * 3;
            unsigned char blue = input_row[index + 0];
            unsigned char green = input_row[index + 1];
            unsigned char red = input_row[index + 2];
            unsigned char gray = (unsigned char)((red + green + blue) / 3);

            gray_rows[i][index + 0] = gray;
            gray_rows[i][index + 1] = gray;
            gray_rows[i][index + 2] = gray;
        }

        for (int p = width * 3; p < row_padded; p++) {
            gray_rows[i][p] = input_row[p];
        }

        free(input_row);
    }

    int k = kernel_size / 2;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sum = 0;
            int count = 0;

            for (int dx = -k; dx <= k; dx++) {
                int nx = x + dx;
                if (nx >= 0 && nx < width) {
                    int idx = nx * 3;
                    sum += gray_rows[y][idx];
                    count++;
                }
            }

            unsigned char blurred = (unsigned char)(sum / count);
            int index = x * 3;
            temp_rows[y][index + 0] = blurred;
            temp_rows[y][index + 1] = blurred;
            temp_rows[y][index + 2] = blurred;
        }

        for (int p = width * 3; p < row_padded; p++) {
            temp_rows[y][p] = gray_rows[y][p];
        }
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sum = 0;
            int count = 0;

            for (int dy = -k; dy <= k; dy++) {
                int ny = y + dy;
                if (ny >= 0 && ny < height) {
                    int idx = x * 3;
                    sum += temp_rows[ny][idx];
                    count++;
                }
            }

            unsigned char blurred = (unsigned char)(sum / count);
            int index = x * 3;
            output_rows[y][index + 0] = blurred;
            output_rows[y][index + 1] = blurred;
            output_rows[y][index + 2] = blurred;
        }

        for (int p = width * 3; p < row_padded; p++) {
            output_rows[y][p] = temp_rows[y][p];
        }
    }

    for (int i = 0; i < height; i++) {
        fwrite(output_rows[i], sizeof(unsigned char), row_padded, outputImage);
        free(gray_rows[i]);
        free(temp_rows[i]);
        free(output_rows[i]);
    }

    FILE *outputLog = fopen("output_log.txt", "a");
    if (outputLog != NULL) {
        fprintf(outputLog, "Función: desenfoque_escala_grises, con %s\n", input_path);
        fprintf(outputLog, "Kernel usado: %d\n", kernel_size);
        fprintf(outputLog, "Localidades totales leídas: %d\n", width * height);
        fprintf(outputLog, "Localidades totales escritas: %d\n", width * height);
        fprintf(outputLog, "-------------------------------------\n");
        fclose(outputLog);
    }

    free(gray_rows);
    free(temp_rows);
    free(output_rows);
    fclose(image);
    fclose(outputImage);
}
