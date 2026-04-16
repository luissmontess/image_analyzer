#include "bmp.h"
#include "filters.h"
#include "task_pool.h"
#include "timing.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define REQUIRED_IMAGE_COUNT 3

typedef struct {
    char path[PATH_MAX];
    char base_name[PATH_MAX];
    BMPImage bmp;
} LoadedImage;

typedef struct {
    int threads;
    char input_dir[PATH_MAX];
    char output_dir[PATH_MAX];
    char summary_csv[PATH_MAX];
    char task_csv[PATH_MAX];
} ProgramOptions;

static void print_usage(const char *program_name) {
    fprintf(stderr,
            "Uso: %s --threads N [--input-dir input] [--output-dir output]\n"
            "Opciones por defecto:\n"
            "  --input-dir  input\n"
            "  --output-dir output\n",
            program_name);
}

static int has_bmp_extension(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot != NULL && strcasecmp(dot, ".bmp") == 0;
}

static char *duplicate_string(const char *text) {
    size_t length = strlen(text) + 1;
    char *copy = (char *)malloc(length);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, length);
    return copy;
}

static void strip_extension(const char *name, char *buffer, size_t buffer_size) {
    const char *dot = strrchr(name, '.');
    size_t length = dot == NULL ? strlen(name) : (size_t)(dot - name);

    if (length >= buffer_size) {
        length = buffer_size - 1;
    }

    memcpy(buffer, name, length);
    buffer[length] = '\0';
}

static int compare_strings(const void *lhs, const void *rhs) {
    const char *const *left = (const char *const *)lhs;
    const char *const *right = (const char *const *)rhs;
    return strcmp(*left, *right);
}

static int ensure_directory(const char *path) {
    char partial[PATH_MAX];
    size_t length = strlen(path);
    size_t i = 0;

    if (length == 0 || length >= sizeof(partial)) {
        return -1;
    }

    strcpy(partial, path);

    for (i = 1; i < length; ++i) {
        if (partial[i] == '/') {
            partial[i] = '\0';
            if (strlen(partial) > 0 && mkdir(partial, 0777) != 0 && errno != EEXIST) {
                return -1;
            }
            partial[i] = '/';
        }
    }

    if (mkdir(partial, 0777) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

static int collect_input_images(const char *input_dir, LoadedImage *images, int image_count) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    char **names = NULL;
    int name_count = 0;
    int total_names = 0;
    int status = -1;

    dir = opendir(input_dir);
    if (dir == NULL) {
        fprintf(stderr, "No se pudo abrir el directorio de entrada '%s'.\n", input_dir);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char **resized = NULL;
        if (!has_bmp_extension(entry->d_name)) {
            continue;
        }

        resized = (char **)realloc(names, (size_t)(name_count + 1) * sizeof(*names));
        if (resized == NULL) {
            goto cleanup;
        }

        names = resized;
        names[name_count] = duplicate_string(entry->d_name);
        if (names[name_count] == NULL) {
            goto cleanup;
        }
        ++name_count;
        total_names = name_count;
    }

    if (name_count < image_count) {
        fprintf(stderr,
                "Se esperaban al menos %d imagenes BMP en '%s' y solo se encontraron %d.\n",
                image_count,
                input_dir,
                name_count);
        goto cleanup;
    }

    qsort(names, (size_t)name_count, sizeof(*names), compare_strings);

    for (name_count = 0; name_count < image_count; ++name_count) {
        snprintf(images[name_count].path, sizeof(images[name_count].path), "%s/%s", input_dir, names[name_count]);
        strip_extension(names[name_count], images[name_count].base_name, sizeof(images[name_count].base_name));
    }

    status = 0;

cleanup:
    if (names != NULL) {
        int i = 0;
        for (i = 0; i < total_names; ++i) {
            free(names[i]);
        }
        free(names);
    }

    closedir(dir);
    return status;
}

static int load_images(LoadedImage *images, int image_count) {
    int i = 0;
    for (i = 0; i < image_count; ++i) {
        if (bmp_load(images[i].path, &images[i].bmp) != 0) {
            fprintf(stderr, "No se pudo cargar la imagen BMP '%s'.\n", images[i].path);
            return -1;
        }
    }
    return 0;
}

static void free_images(LoadedImage *images, int image_count) {
    int i = 0;
    for (i = 0; i < image_count; ++i) {
        bmp_free(&images[i].bmp);
    }
}

static int build_tasks(const LoadedImage *images,
                       int image_count,
                       int threads,
                       const char *output_dir,
                       Task *tasks,
                       int task_count) {
    int image_index = 0;
    int task_index = 0;
    char run_dir[PATH_MAX];

    snprintf(run_dir, sizeof(run_dir), "%s/%d_threads", output_dir, threads);
    if (ensure_directory(run_dir) != 0) {
        fprintf(stderr, "No se pudo crear el directorio de salida '%s'.\n", run_dir);
        return -1;
    }

    for (image_index = 0; image_index < image_count; ++image_index) {
        int transform_index = 0;
        for (transform_index = 0; transform_index < TRANSFORM_COUNT; ++transform_index) {
            Task *task = NULL;
            if (task_index >= task_count) {
                return -1;
            }

            task = &tasks[task_index++];
            task->input_path = images[image_index].path;
            task->image_name = images[image_index].base_name;
            task->transform = (TransformType)transform_index;
            task->input_image = &images[image_index].bmp;
            task->elapsed_seconds = 0.0;
            task->status = -1;

            if (strlen(run_dir) + strlen(images[image_index].base_name) + strlen(transform_slug(task->transform)) + 8
                >= sizeof(task->output_path)) {
                fprintf(stderr, "La ruta de salida excede PATH_MAX.\n");
                return -1;
            }

            snprintf(task->output_path,
                     sizeof(task->output_path),
                     "%s/%s__%s.bmp",
                     run_dir,
                     images[image_index].base_name,
                     transform_slug(task->transform));
        }
    }

    return 0;
}

static int file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

static int append_summary_csv(const char *path,
                              int threads,
                              int image_count,
                              int task_count,
                              double total_seconds,
                              int success_count) {
    int new_file = !file_exists(path);
    FILE *file = fopen(path, "a");
    if (file == NULL) {
        return -1;
    }

    if (new_file) {
        fprintf(file, "threads,image_count,task_count,successful_tasks,total_time_seconds\n");
    }

    fprintf(file, "%d,%d,%d,%d,%.6f\n", threads, image_count, task_count, success_count, total_seconds);
    fclose(file);
    return 0;
}

static int append_task_csv(const char *path, const Task *tasks, int task_count, int threads) {
    int new_file = !file_exists(path);
    FILE *file = fopen(path, "a");
    int i = 0;

    if (file == NULL) {
        return -1;
    }

    if (new_file) {
        fprintf(file, "threads,image,transform,input_path,output_path,status,task_time_seconds\n");
    }

    for (i = 0; i < task_count; ++i) {
        fprintf(file,
                "%d,%s,%s,%s,%s,%s,%.6f\n",
                threads,
                tasks[i].image_name,
                transform_name(tasks[i].transform),
                tasks[i].input_path,
                tasks[i].output_path,
                tasks[i].status == 0 ? "ok" : "error",
                tasks[i].elapsed_seconds);
    }

    fclose(file);
    return 0;
}

static void print_run_summary(const Task *tasks, int task_count, int threads, double total_seconds) {
    int i = 0;
    int ok = 0;

    printf("=============================================\n");
    printf("Resumen de corrida\n");
    printf("Threads: %d\n", threads);
    printf("Tareas totales: %d\n", task_count);
    printf("Tiempo total (s): %.6f\n", total_seconds);
    printf("---------------------------------------------\n");

    for (i = 0; i < task_count; ++i) {
        if (tasks[i].status == 0) {
            ++ok;
        }

        printf("%-22s | %-28s | %-5s | %.6f s\n",
               tasks[i].image_name,
               transform_slug(tasks[i].transform),
               tasks[i].status == 0 ? "OK" : "FAIL",
               tasks[i].elapsed_seconds);
    }

    printf("---------------------------------------------\n");
    printf("Tareas exitosas: %d/%d\n", ok, task_count);
    printf("=============================================\n");
}

static int parse_arguments(int argc, char **argv, ProgramOptions *options) {
    int i = 1;

    options->threads = 0;
    strcpy(options->input_dir, "input");
    strcpy(options->output_dir, "output");
    options->summary_csv[0] = '\0';
    options->task_csv[0] = '\0';

    while (i < argc) {
        if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            options->threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--input-dir") == 0 && i + 1 < argc) {
            strncpy(options->input_dir, argv[++i], sizeof(options->input_dir) - 1);
            options->input_dir[sizeof(options->input_dir) - 1] = '\0';
        } else if (strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) {
            strncpy(options->output_dir, argv[++i], sizeof(options->output_dir) - 1);
            options->output_dir[sizeof(options->output_dir) - 1] = '\0';
        } else {
            print_usage(argv[0]);
            return -1;
        }
        ++i;
    }

    if (options->threads <= 0) {
        print_usage(argv[0]);
        return -1;
    }

    snprintf(options->summary_csv, sizeof(options->summary_csv), "%s/summary_runs.csv", options->output_dir);
    snprintf(options->task_csv, sizeof(options->task_csv), "%s/task_runs.csv", options->output_dir);
    return 0;
}

int main(int argc, char **argv) {
    ProgramOptions options;
    LoadedImage images[REQUIRED_IMAGE_COUNT];
    Task tasks[REQUIRED_IMAGE_COUNT * TRANSFORM_COUNT];
    double start_time = 0.0;
    double total_seconds = 0.0;
    int success_count = 0;
    int i = 0;

    memset(images, 0, sizeof(images));
    memset(tasks, 0, sizeof(tasks));

    if (parse_arguments(argc, argv, &options) != 0) {
        return EXIT_FAILURE;
    }

    if (ensure_directory(options.output_dir) != 0) {
        fprintf(stderr, "No se pudo crear el directorio '%s'.\n", options.output_dir);
        return EXIT_FAILURE;
    }

    if (collect_input_images(options.input_dir, images, REQUIRED_IMAGE_COUNT) != 0) {
        return EXIT_FAILURE;
    }

    if (load_images(images, REQUIRED_IMAGE_COUNT) != 0) {
        free_images(images, REQUIRED_IMAGE_COUNT);
        return EXIT_FAILURE;
    }

    if (build_tasks(images,
                    REQUIRED_IMAGE_COUNT,
                    options.threads,
                    options.output_dir,
                    tasks,
                    REQUIRED_IMAGE_COUNT * TRANSFORM_COUNT) != 0) {
        free_images(images, REQUIRED_IMAGE_COUNT);
        return EXIT_FAILURE;
    }

    start_time = now_seconds();
    if (run_tasks(tasks, REQUIRED_IMAGE_COUNT * TRANSFORM_COUNT, options.threads) != 0) {
        fprintf(stderr, "Fallo la ejecucion del pool de tareas.\n");
        free_images(images, REQUIRED_IMAGE_COUNT);
        return EXIT_FAILURE;
    }
    total_seconds = now_seconds() - start_time;

    for (i = 0; i < REQUIRED_IMAGE_COUNT * TRANSFORM_COUNT; ++i) {
        if (tasks[i].status == 0) {
            ++success_count;
        }
    }

    print_run_summary(tasks, REQUIRED_IMAGE_COUNT * TRANSFORM_COUNT, options.threads, total_seconds);

    if (append_summary_csv(options.summary_csv,
                           options.threads,
                           REQUIRED_IMAGE_COUNT,
                           REQUIRED_IMAGE_COUNT * TRANSFORM_COUNT,
                           total_seconds,
                           success_count) != 0) {
        fprintf(stderr, "Advertencia: no se pudo actualizar '%s'.\n", options.summary_csv);
    }

    if (append_task_csv(options.task_csv,
                        tasks,
                        REQUIRED_IMAGE_COUNT * TRANSFORM_COUNT,
                        options.threads) != 0) {
        fprintf(stderr, "Advertencia: no se pudo actualizar '%s'.\n", options.task_csv);
    }

    free_images(images, REQUIRED_IMAGE_COUNT);
    return success_count == REQUIRED_IMAGE_COUNT * TRANSFORM_COUNT ? EXIT_SUCCESS : EXIT_FAILURE;
}
