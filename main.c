#include "bmp.h"
#include "filters.h"
#include "task_pool.h"
#include "timing.h"

#include <dirent.h>
#include <errno.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char path[PATH_MAX];
    char base_name[PATH_MAX];
} ImageMeta;

typedef struct {
    int threads;
    char input_dir[PATH_MAX];
    char output_dir[PATH_MAX];
    char logs_dir[PATH_MAX];
    int blur_kernel_gray;
    int blur_kernel_color;
    int selected_transforms[TRANSFORM_COUNT];
    int selected_transform_count;
} ProgramOptions;

static void print_usage(const char *program_name) {
    fprintf(stderr,
            "Uso: %s [--threads N] [--input-dir dir] [--output-dir dir] [--logs-dir dir]\\n"
            "          [--transforms all|0,1,2,...]\\n"
            "          [--blur-kernel-gray N] [--blur-kernel-color N]\\n",
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

static int collect_input_images(const char *input_dir, ImageMeta **images_out, int *count_out) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    char **names = NULL;
    int count = 0;
    int i = 0;
    ImageMeta *images = NULL;

    *images_out = NULL;
    *count_out = 0;

    dir = opendir(input_dir);
    if (dir == NULL) {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char **resized = NULL;
        if (!has_bmp_extension(entry->d_name)) {
            continue;
        }

        resized = (char **)realloc(names, (size_t)(count + 1) * sizeof(*names));
        if (resized == NULL) {
            goto fail;
        }
        names = resized;
        names[count] = duplicate_string(entry->d_name);
        if (names[count] == NULL) {
            goto fail;
        }
        ++count;
    }

    qsort(names, (size_t)count, sizeof(*names), compare_strings);

    images = (ImageMeta *)calloc((size_t)count, sizeof(*images));
    if (images == NULL && count > 0) {
        goto fail;
    }

    for (i = 0; i < count; ++i) {
        snprintf(images[i].path, sizeof(images[i].path), "%s/%s", input_dir, names[i]);
        strip_extension(names[i], images[i].base_name, sizeof(images[i].base_name));
    }

    for (i = 0; i < count; ++i) {
        free(names[i]);
    }
    free(names);
    closedir(dir);

    *images_out = images;
    *count_out = count;
    return 0;

fail:
    if (names != NULL) {
        for (i = 0; i < count; ++i) {
            free(names[i]);
        }
        free(names);
    }
    free(images);
    closedir(dir);
    return -1;
}

static void compute_assignment(int rank, int world_size, int total_items, int *start, int *count) {
    int base = total_items / world_size;
    int rem = total_items % world_size;

    *count = base + (rank < rem ? 1 : 0);
    *start = rank * base + (rank < rem ? rank : rem);
}

static int parse_arguments(int argc, char **argv, ProgramOptions *options) {
    int i = 1;

    options->threads = 1;
    strcpy(options->input_dir, "/mirror/image_parallel/input");
    strcpy(options->output_dir, "/mirror/image_parallel/output");
    strcpy(options->logs_dir, "/mirror/image_parallel/logs");
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
        } else if (strcmp(argv[i], "--logs-dir") == 0 && i + 1 < argc) {
            strncpy(options->logs_dir, argv[++i], sizeof(options->logs_dir) - 1);
            options->logs_dir[sizeof(options->logs_dir) - 1] = '\0';
        } else if (strcmp(argv[i], "--transforms") == 0 && i + 1 < argc) {
            if (parse_transform_list(argv[++i], options) != 0) {
                return -1;
            }
        } else if (strcmp(argv[i], "--blur-kernel-gray") == 0 && i + 1 < argc) {
            options->blur_kernel_gray = parse_kernel_size(argv[++i]);
            if (options->blur_kernel_gray < 0) {
                return -1;
            }
        } else if (strcmp(argv[i], "--blur-kernel-color") == 0 && i + 1 < argc) {
            options->blur_kernel_color = parse_kernel_size(argv[++i]);
            if (options->blur_kernel_color < 0) {
                return -1;
            }
        } else {
            return -1;
        }
        ++i;
    }

    if (options->threads <= 0) {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    ProgramOptions options;
    int rank = 0;
    int world_size = 1;
    int image_count = 0;
    int local_start = 0;
    int local_count = 0;
    ImageMeta *images = NULL;
    char *paths_buf = NULL;
    char *bases_buf = NULL;
    struct utsname uts;
    char hostname[256];
    char run_dir[PATH_MAX];
    char log_path[PATH_MAX];
    FILE *rank_log = NULL;
    double rank_start = 0.0;
    double rank_elapsed = 0.0;
    int local_success = 0;
    int local_fail = 0;
    int total_success = 0;
    int total_fail = 0;
    int i = 0;

    if (MPI_Init(&argc, &argv) != MPI_SUCCESS) {
        fprintf(stderr, "No se pudo inicializar MPI.\n");
        return EXIT_FAILURE;
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (parse_arguments(argc, argv, &options) != 0) {
        if (rank == 0) {
            print_usage(argv[0]);
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if (rank == 0) {
        if (ensure_directory(options.input_dir) != 0 || ensure_directory(options.output_dir) != 0 ||
            ensure_directory(options.logs_dir) != 0) {
            fprintf(stderr, "No se pudieron crear directorios en /mirror/image_parallel.\n");
            MPI_Abort(MPI_COMM_WORLD, 2);
        }

        if (collect_input_images(options.input_dir, &images, &image_count) != 0) {
            fprintf(stderr, "No se pudo leer input o no hay BMPs validos en '%s'.\n", options.input_dir);
            MPI_Abort(MPI_COMM_WORLD, 3);
        }
    }

    MPI_Bcast(&image_count, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (image_count <= 0) {
        if (rank == 0) {
            fprintf(stderr, "No hay imagenes para procesar.\n");
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if (rank != 0) {
        images = (ImageMeta *)calloc((size_t)image_count, sizeof(*images));
        if (images == NULL) {
            MPI_Abort(MPI_COMM_WORLD, 4);
        }
    }

    paths_buf = (char *)calloc((size_t)image_count, PATH_MAX);
    bases_buf = (char *)calloc((size_t)image_count, PATH_MAX);
    if (paths_buf == NULL || bases_buf == NULL) {
        MPI_Abort(MPI_COMM_WORLD, 5);
    }

    if (rank == 0) {
        for (i = 0; i < image_count; ++i) {
            memcpy(paths_buf + ((size_t)i * PATH_MAX), images[i].path, PATH_MAX);
            memcpy(bases_buf + ((size_t)i * PATH_MAX), images[i].base_name, PATH_MAX);
        }
    }

    MPI_Bcast(paths_buf, image_count * PATH_MAX, MPI_CHAR, 0, MPI_COMM_WORLD);
    MPI_Bcast(bases_buf, image_count * PATH_MAX, MPI_CHAR, 0, MPI_COMM_WORLD);

    if (rank != 0) {
        for (i = 0; i < image_count; ++i) {
            memcpy(images[i].path, paths_buf + ((size_t)i * PATH_MAX), PATH_MAX);
            memcpy(images[i].base_name, bases_buf + ((size_t)i * PATH_MAX), PATH_MAX);
        }
    }

    compute_assignment(rank, world_size, image_count, &local_start, &local_count);

    gethostname(hostname, sizeof(hostname));
    hostname[sizeof(hostname) - 1] = '\0';
    if (uname(&uts) != 0) {
        strcpy(uts.machine, "unknown");
    }

    snprintf(run_dir, sizeof(run_dir), "%s/%d_threads", options.output_dir, options.threads);
    if (ensure_directory(run_dir) != 0 || ensure_directory(options.logs_dir) != 0) {
        fprintf(stderr, "Rank %d: no se pudo crear output/logs.\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 6);
    }

    snprintf(log_path, sizeof(log_path), "%s/rank_%d_%s.log", options.logs_dir, rank, hostname);
    rank_log = fopen(log_path, "w");
    if (rank_log == NULL) {
        fprintf(stderr, "Rank %d: no se pudo abrir log '%s'.\n", rank, log_path);
        MPI_Abort(MPI_COMM_WORLD, 7);
    }

    fprintf(rank_log, "rank=%d size=%d host=%s arch=%s assigned_images=%d\\n",
            rank,
            world_size,
            hostname,
            uts.machine,
            local_count);
    fflush(rank_log);

    MPI_Barrier(MPI_COMM_WORLD);
    rank_start = now_seconds();

    for (i = local_start; i < local_start + local_count; ++i) {
        BMPImage image;
        Task tasks[TRANSFORM_COUNT];
        int t = 0;

        memset(&image, 0, sizeof(image));
        if (bmp_load(images[i].path, &image) != 0) {
            fprintf(rank_log, "image=%s status=error reason=load_failed\\n", images[i].path);
            ++local_fail;
            continue;
        }

        for (t = 0; t < options.selected_transform_count; ++t) {
            TransformType transform = (TransformType)options.selected_transforms[t];
            Task *task = &tasks[t];

            memset(task, 0, sizeof(*task));
            task->input_path = images[i].path;
            task->image_name = images[i].base_name;
            task->transform = transform;
            task->input_image = &image;
            task->blur_kernel_size = 0;
            task->status = -1;

            if (transform == TRANSFORM_BLUR_GRAY) {
                task->blur_kernel_size = options.blur_kernel_gray;
            } else if (transform == TRANSFORM_BLUR_COLOR) {
                task->blur_kernel_size = options.blur_kernel_color;
            }

            if (strlen(run_dir) + strlen(images[i].base_name) + strlen(transform_slug(transform)) + 7 >
                sizeof(task->output_path) - 1) {
                fprintf(rank_log, "image=%s status=error reason=output_path_too_long\n", images[i].path);
                bmp_free(&image);
                goto next_image;
            }

            snprintf(task->output_path,
                     sizeof(task->output_path),
                     "%s/%s_%s.bmp",
                     run_dir,
                     images[i].base_name,
                     transform_slug(transform));
        }

        run_tasks(tasks, options.selected_transform_count, options.threads);

        for (t = 0; t < options.selected_transform_count; ++t) {
            fprintf(rank_log,
                    "image=%s transform=%s status=%s out=%s time=%.6f\\n",
                    images[i].path,
                    transform_slug(tasks[t].transform),
                    tasks[t].status == 0 ? "ok" : "error",
                    tasks[t].output_path,
                    tasks[t].elapsed_seconds);
            if (tasks[t].status == 0) {
                ++local_success;
            } else {
                ++local_fail;
            }
        }

        bmp_free(&image);
next_image:
        ;
    }

    rank_elapsed = now_seconds() - rank_start;

    fprintf(rank_log,
            "summary rank=%d size=%d host=%s arch=%s assigned_images=%d success=%d fail=%d elapsed=%.6f\\n",
            rank,
            world_size,
            hostname,
            uts.machine,
            local_count,
            local_success,
            local_fail,
            rank_elapsed);
    fclose(rank_log);

    printf("rank=%d size=%d host=%s arch=%s assigned_images=%d elapsed=%.6f\\n",
           rank,
           world_size,
           hostname,
           uts.machine,
           local_count,
           rank_elapsed);

    MPI_Reduce(&local_success, &total_success, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_fail, &total_fail, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("total_images=%d total_ok=%d total_fail=%d\\n", image_count, total_success, total_fail);
    }

    free(paths_buf);
    free(bases_buf);
    free(images);
    MPI_Finalize();

    return (rank == 0 && total_fail > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
