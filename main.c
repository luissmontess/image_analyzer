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

#define MAX_IMAGE_COUNT 10

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
    int blur_kernel_gray;
    int blur_kernel_color;
    int selected_transforms[TRANSFORM_COUNT];
    int selected_transform_count;
} ProgramOptions;

static void print_usage(const char *program_name) {
    fprintf(stderr,
            "Uso: %s --threads N [--input-dir input] [--output-dir output]\n"
            "          [--transforms 0,1,2,...]\n"
            "          [--blur-kernel-gray N] [--blur-kernel-color N]\n"
            "Opciones por defecto:\n"
            "  --input-dir  input\n"
            "  --output-dir output\n",
            program_name);
}

static int parse_kernel_size(const char *text) {
    char *endptr = NULL;
    long value = 0;

    if (text == NULL) {
        return -1;
    }

    value = strtol(text, &endptr, 10);
    while (endptr != NULL && *endptr == ' ') {
        ++endptr;
    }

    if (endptr == text || (endptr != NULL && *endptr != '\0')) {
        return -1;
    }

    if (value < 3 || (value % 2) == 0) {
        return -1;
    }

    return (int)value;
}

static void set_default_transforms(ProgramOptions *options) {
    int i = 0;

    options->selected_transform_count = TRANSFORM_COUNT;
    for (i = 0; i < TRANSFORM_COUNT; ++i) {
        options->selected_transforms[i] = i;
    }
}

static int parse_transform_list(const char *text, ProgramOptions *options) {
    char buffer[128];
    char *token = NULL;
    char *saveptr = NULL;
    int seen[TRANSFORM_COUNT];
    int count = 0;

    if (text == NULL || options == NULL) {
        return -1;
    }

    if (strcmp(text, "all") == 0 || strcmp(text, "ALL") == 0) {
        set_default_transforms(options);
        return 0;
    }

    if (strlen(text) >= sizeof(buffer)) {
        return -1;
    }

    memset(seen, 0, sizeof(seen));
    strcpy(buffer, text);

    token = strtok_r(buffer, ",", &saveptr);
    while (token != NULL) {
        char *endptr = NULL;
        long value = strtol(token, &endptr, 10);

        while (endptr != NULL && *endptr == ' ') {
            ++endptr;
        }

        if (endptr == token || (endptr != NULL && *endptr != '\0') || value < 0 || value >= TRANSFORM_COUNT) {
            return -1;
        }

        if (!seen[value]) {
            seen[value] = 1;
            options->selected_transforms[count++] = (int)value;
        }

        token = strtok_r(NULL, ",", &saveptr);
    }

    if (count <= 0) {
        return -1;
    }

    options->selected_transform_count = count;
    return 0;
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

static int collect_input_images(const char *input_dir, LoadedImage *images, int max_image_count) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    char **names = NULL;
    int name_count = 0;
    int total_names = 0;
    int loaded_count = 0;
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

    qsort(names, (size_t)name_count, sizeof(*names), compare_strings);

    for (name_count = 0; name_count < total_names && loaded_count < max_image_count; ++name_count) {
        BMPImage test_bmp;
        char test_path[PATH_MAX];

        snprintf(test_path, sizeof(test_path), "%s/%s", input_dir, names[name_count]);
        memset(&test_bmp, 0, sizeof(test_bmp));

        if (bmp_load(test_path, &test_bmp) != 0) {
            continue;
        }

        bmp_free(&test_bmp);
        snprintf(images[loaded_count].path, sizeof(images[loaded_count].path), "%s", test_path);
        strip_extension(names[name_count], images[loaded_count].base_name, sizeof(images[loaded_count].base_name));
        ++loaded_count;
    }

    if (loaded_count == 0) {
        fprintf(stderr, "No se encontraron imagenes BMP 24-bit validas en '%s'.\n", input_dir);
        goto cleanup;
    }

    status = loaded_count;

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
                       int blur_kernel_gray,
                       int blur_kernel_color,
                       const int *selected_transforms,
                       int selected_transform_count,
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
        for (transform_index = 0; transform_index < selected_transform_count; ++transform_index) {
            Task *task = NULL;
            TransformType transform = (TransformType)selected_transforms[transform_index];

            if (task_index >= task_count) {
                return -1;
            }

            task = &tasks[task_index++];
            task->input_path = images[image_index].path;
            task->image_name = images[image_index].base_name;
            task->transform = transform;
            task->input_image = &images[image_index].bmp;
            task->blur_kernel_size = 0;
            task->elapsed_seconds = 0.0;
            task->status = -1;

            if (transform == TRANSFORM_BLUR_GRAY) {
                task->blur_kernel_size = blur_kernel_gray;
            } else if (transform == TRANSFORM_BLUR_COLOR) {
                task->blur_kernel_size = blur_kernel_color;
            }

            if (strlen(run_dir) + strlen(images[image_index].base_name) + strlen(transform_slug(task->transform)) + 7
                >= sizeof(task->output_path)) {
                fprintf(stderr, "La ruta de salida excede PATH_MAX.\n");
                return -1;
            }

            snprintf(task->output_path,
                     sizeof(task->output_path),
                     "%s/%s_%s.bmp",
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
    options->blur_kernel_gray = 3;
    options->blur_kernel_color = 3;
    set_default_transforms(options);

    while (i < argc) {
        if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            options->threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--input-dir") == 0 && i + 1 < argc) {
            strncpy(options->input_dir, argv[++i], sizeof(options->input_dir) - 1);
            options->input_dir[sizeof(options->input_dir) - 1] = '\0';
        } else if (strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) {
            strncpy(options->output_dir, argv[++i], sizeof(options->output_dir) - 1);
            options->output_dir[sizeof(options->output_dir) - 1] = '\0';
        } else if (strcmp(argv[i], "--transforms") == 0 && i + 1 < argc) {
            if (parse_transform_list(argv[++i], options) != 0) {
                print_usage(argv[0]);
                return -1;
            }
        } else if (strcmp(argv[i], "--blur-kernel-gray") == 0 && i + 1 < argc) {
            options->blur_kernel_gray = parse_kernel_size(argv[++i]);
            if (options->blur_kernel_gray < 0) {
                print_usage(argv[0]);
                return -1;
            }
        } else if (strcmp(argv[i], "--blur-kernel-color") == 0 && i + 1 < argc) {
            options->blur_kernel_color = parse_kernel_size(argv[++i]);
            if (options->blur_kernel_color < 0) {
                print_usage(argv[0]);
                return -1;
            }
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
    LoadedImage images[MAX_IMAGE_COUNT];
    Task tasks[MAX_IMAGE_COUNT * TRANSFORM_COUNT];
    double start_time = 0.0;
    double total_seconds = 0.0;
    int success_count = 0;
    int image_count = 0;
    int task_count = 0;
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

    image_count = collect_input_images(options.input_dir, images, MAX_IMAGE_COUNT);
    if (image_count <= 0) {
        return EXIT_FAILURE;
    }

    if (load_images(images, image_count) != 0) {
        free_images(images, image_count);
        return EXIT_FAILURE;
    }

    task_count = image_count * options.selected_transform_count;

    if (build_tasks(images,
                    image_count,
                    options.threads,
                    options.output_dir,
                    options.blur_kernel_gray,
                    options.blur_kernel_color,
                    options.selected_transforms,
                    options.selected_transform_count,
                    tasks,
                    task_count) != 0) {
        free_images(images, image_count);
        return EXIT_FAILURE;
    }

    start_time = now_seconds();
    if (run_tasks(tasks, task_count, options.threads) != 0) {
        fprintf(stderr, "Fallo la ejecucion del pool de tareas.\n");
        free_images(images, image_count);
        return EXIT_FAILURE;
    }
    total_seconds = now_seconds() - start_time;

    for (i = 0; i < task_count; ++i) {
        if (tasks[i].status == 0) {
            ++success_count;
        }
    }

    print_run_summary(tasks, task_count, options.threads, total_seconds);

    if (append_summary_csv(options.summary_csv,
                           options.threads,
                           image_count,
                           task_count,
                           total_seconds,
                           success_count) != 0) {
        fprintf(stderr, "Advertencia: no se pudo actualizar '%s'.\n", options.summary_csv);
    }

    if (append_task_csv(options.task_csv,
                        tasks,
                        task_count,
                        options.threads) != 0) {
        fprintf(stderr, "Advertencia: no se pudo actualizar '%s'.\n", options.task_csv);
    }

    free_images(images, image_count);
    return success_count == task_count ? EXIT_SUCCESS : EXIT_FAILURE;
}
