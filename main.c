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
#include <time.h>
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
            "Uso: %s [--threads N] [--input-dir dir] [--output-dir dir] [--logs-dir dir]\n"
            "          [--transforms all|0,1,2,...]\n"
            "          [--blur-kernel-gray N] [--blur-kernel-color N]\n",
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

static void format_timestamp(char *buffer, size_t buffer_size) {
    time_t now = time(NULL);
    struct tm local_tm;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (now == (time_t)-1 || localtime_r(&now, &local_tm) == NULL) {
        buffer[0] = '\0';
        return;
    }

    if (strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%S", &local_tm) == 0) {
        buffer[0] = '\0';
    }
}

static void csv_write_field(FILE *file, const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    int needs_quotes = 0;

    if (file == NULL) {
        return;
    }

    if (text == NULL) {
        return;
    }

    while (*cursor != '\0') {
        if (*cursor == '"' || *cursor == ',' || *cursor == '\n' || *cursor == '\r') {
            needs_quotes = 1;
            break;
        }
        ++cursor;
    }

    if (!needs_quotes) {
        fputs(text, file);
        return;
    }

    fputc('"', file);
    while (*text != '\0') {
        if (*text == '"') {
            fputc('"', file);
        }
        fputc(*text, file);
        ++text;
    }
    fputc('"', file);
}

static void to_logical_path(const char *absolute_path,
                            const char *base_dir,
                            const char *prefix,
                            char *buffer,
                            size_t buffer_size) {
    size_t base_length = 0;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    buffer[0] = '\0';
    if (absolute_path == NULL || prefix == NULL) {
        return;
    }

    if (base_dir != NULL) {
        base_length = strlen(base_dir);
        if (strncmp(absolute_path, base_dir, base_length) == 0 && absolute_path[base_length] == '/') {
            snprintf(buffer, buffer_size, "%s/%s", prefix, absolute_path + base_length + 1);
            return;
        }
    }

    snprintf(buffer, buffer_size, "%s", absolute_path);
}

static void write_csv_header(FILE *file) {
    if (file == NULL) {
        return;
    }

    fprintf(file,
            "timestamp,rank,size,host,arch,input_image,filter,output_file,status,pixels,width,height,"
            "elapsed_seconds,error_message\n");
}

static void write_csv_record(FILE *file,
                             int rank,
                             int world_size,
                             const char *host,
                             const char *arch,
                             const char *input_image,
                             const char *filter_name,
                             const char *output_file,
                             const char *status,
                             long long pixels,
                             int width,
                             int height,
                             double elapsed_seconds,
                             const char *error_message) {
    char timestamp[32];

    if (file == NULL) {
        return;
    }

    format_timestamp(timestamp, sizeof(timestamp));
    csv_write_field(file, timestamp);
    fprintf(file, ",%d,%d,", rank, world_size);
    csv_write_field(file, host);
    fputc(',', file);
    csv_write_field(file, arch);
    fputc(',', file);
    csv_write_field(file, input_image);
    fputc(',', file);
    csv_write_field(file, filter_name);
    fputc(',', file);
    csv_write_field(file, output_file);
    fputc(',', file);
    csv_write_field(file, status);
    fprintf(file, ",%lld,%d,%d,%.4f,", pixels, width, height, elapsed_seconds);
    csv_write_field(file, error_message);
    fputc('\n', file);
}

static int append_csv_without_header(FILE *destination, const char *source_path) {
    FILE *source = NULL;
    char line[8192];
    int first_line = 1;

    if (destination == NULL || source_path == NULL) {
        return -1;
    }

    source = fopen(source_path, "r");
    if (source == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), source) != NULL) {
        if (first_line) {
            first_line = 0;
            continue;
        }
        fputs(line, destination);
    }

    fclose(source);
    return ferror(destination) ? -1 : 0;
}

static void log_bmp_probe(FILE *rank_log,
                          int rank,
                          const char *host,
                          const char *arch,
                          const char *input_image,
                          const BMPInfo *info,
                          const char *stage,
                          const char *status_text,
                          const char *error_text) {
    int internal_row_stride = 0;

    if (rank_log == NULL || input_image == NULL || stage == NULL || status_text == NULL) {
        return;
    }

    if (info != NULL && info->width > 0) {
        internal_row_stride = (info->width * 3 + 3) & ~3;
    }

    fprintf(rank_log,
            "rank=%d host=%s arch=%s image=%s stage=%s status=%s width=%d height=%d bits_per_pixel=%d "
            "compression=%u row_stride=%d channels=%d internal_row_stride=%d internal_channels=3 "
            "orientation=%s conversion=normalized_to_rgb24 error=%s\n",
            rank,
            host != NULL ? host : "",
            arch != NULL ? arch : "",
            input_image,
            stage,
            status_text,
            info != NULL ? info->width : 0,
            info != NULL ? info->height : 0,
            info != NULL ? info->bits_per_pixel : 0,
            info != NULL ? info->compression : 0u,
            info != NULL ? info->row_stride : 0,
            info != NULL ? info->channels : 0,
            internal_row_stride,
            (info != NULL && info->top_down) ? "top-down" : "bottom-up",
            error_text != NULL ? error_text : "");
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
    char rank_csv_path[PATH_MAX];
    char global_csv_path[PATH_MAX];
    char summary_path[PATH_MAX];
    FILE *rank_log = NULL;
    FILE *rank_csv = NULL;
    double rank_start = 0.0;
    double rank_elapsed = 0.0;
    double total_elapsed = 0.0;
    int local_success = 0;
    int local_fail = 0;
    int total_success = 0;
    int total_fail = 0;
    long long local_pixels_processed = 0;
    long long total_pixels_processed = 0;
    char *all_hosts = NULL;
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
    snprintf(rank_csv_path, sizeof(rank_csv_path), "%s/processing_rank_%d_%s.csv", options.logs_dir, rank, hostname);
    snprintf(global_csv_path, sizeof(global_csv_path), "%s/processing_global.csv", options.logs_dir);
    snprintf(summary_path, sizeof(summary_path), "%s/summary.txt", options.logs_dir);
    rank_log = fopen(log_path, "w");
    if (rank_log == NULL) {
        fprintf(stderr, "Rank %d: no se pudo abrir log '%s'.\n", rank, log_path);
        MPI_Abort(MPI_COMM_WORLD, 7);
    }

    rank_csv = fopen(rank_csv_path, "w");
    if (rank_csv == NULL) {
        fprintf(stderr, "Rank %d: no se pudo abrir CSV temporal '%s'.\n", rank, rank_csv_path);
        fclose(rank_log);
        MPI_Abort(MPI_COMM_WORLD, 8);
    }
    write_csv_header(rank_csv);

    fprintf(rank_log, "rank=%d size=%d host=%s arch=%s assigned_images=%d\n",
            rank,
            world_size,
            hostname,
            uts.machine,
            local_count);
    fflush(rank_log);
    fflush(rank_csv);

    MPI_Barrier(MPI_COMM_WORLD);
    rank_start = now_seconds();

    for (i = local_start; i < local_start + local_count; ++i) {
        BMPImage image;
        BMPInfo bmp_info;
        BMPStatus bmp_status;
        Task tasks[TRANSFORM_COUNT];
        char input_logical[PATH_MAX];
        int t = 0;

        to_logical_path(images[i].path, options.input_dir, "input", input_logical, sizeof(input_logical));
        fprintf(rank_log,
                "rank=%d host=%s arch=%s assigned_image=%s\n",
                rank,
                hostname,
                uts.machine,
                input_logical);
        log_bmp_probe(rank_log, rank, hostname, uts.machine, input_logical, NULL, "before_read", "BEGIN", "");

        memset(&image, 0, sizeof(image));
        memset(&bmp_info, 0, sizeof(bmp_info));
        bmp_status = BMP_STATUS_OK;
        if (bmp_load_detailed(images[i].path, &image, &bmp_info, &bmp_status) != 0) {
            const char *bmp_error = bmp_status_message(bmp_status);

            log_bmp_probe(rank_log,
                          rank,
                          hostname,
                          uts.machine,
                          input_logical,
                          &bmp_info,
                          "after_read",
                          "FAIL",
                          bmp_error);
            for (t = 0; t < options.selected_transform_count; ++t) {
                TransformType transform = (TransformType)options.selected_transforms[t];
                char output_path[PATH_MAX];
                char output_logical[PATH_MAX];

                snprintf(output_path,
                         sizeof(output_path),
                         "%s/%s_%s.bmp",
                         run_dir,
                         images[i].base_name,
                         transform_slug(transform));
                to_logical_path(output_path, options.output_dir, "output", output_logical, sizeof(output_logical));

                fprintf(rank_log,
                        "rank=%d host=%s arch=%s image=%s filter=%s output=%s status=FAIL error=%s\n",
                        rank,
                        hostname,
                        uts.machine,
                        input_logical,
                        transform_name(transform),
                        output_logical,
                        bmp_error);
                write_csv_record(rank_csv,
                                 rank,
                                 world_size,
                                 hostname,
                                 uts.machine,
                                 input_logical,
                                 transform_name(transform),
                                 output_logical,
                                 "FAIL",
                                 0,
                                 bmp_info.width,
                                 bmp_info.height,
                                 0.0,
                                 bmp_error);
                ++local_fail;
            }
            continue;
        }
        log_bmp_probe(rank_log, rank, hostname, uts.machine, input_logical, &bmp_info, "after_read", "OK", "");

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
            task->error_message[0] = '\0';

            if (transform == TRANSFORM_BLUR_GRAY) {
                task->blur_kernel_size = options.blur_kernel_gray;
            } else if (transform == TRANSFORM_BLUR_COLOR) {
                task->blur_kernel_size = options.blur_kernel_color;
            }

            if (strlen(run_dir) + strlen(images[i].base_name) + strlen(transform_slug(transform)) + 7 >
                sizeof(task->output_path) - 1) {
                int failed_index = 0;

                for (failed_index = 0; failed_index <= t; ++failed_index) {
                    char output_logical[PATH_MAX];
                    Task *failed_task = &tasks[failed_index];

                    if (failed_task->output_path[0] == '\0') {
                        snprintf(failed_task->output_path,
                                 sizeof(failed_task->output_path),
                                 "%s/%s_%s.bmp",
                                 run_dir,
                                 images[i].base_name,
                                 transform_slug(failed_task->transform));
                    }

                    to_logical_path(failed_task->output_path,
                                    options.output_dir,
                                    "output",
                                    output_logical,
                                    sizeof(output_logical));
                    fprintf(rank_log,
                            "rank=%d host=%s arch=%s image=%s filter=%s output=%s status=FAIL error=output_path_failed\n",
                            rank,
                            hostname,
                            uts.machine,
                            input_logical,
                            transform_name(failed_task->transform),
                            output_logical);
                    write_csv_record(rank_csv,
                                     rank,
                                     world_size,
                                     hostname,
                                     uts.machine,
                                     input_logical,
                                     transform_name(failed_task->transform),
                                     output_logical,
                                     "FAIL",
                                     0,
                                     image.width,
                                     image.height,
                                     0.0,
                                     "output_path_failed");
                    ++local_fail;
                }
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
            long long pixels = 0;
            char output_logical[PATH_MAX];
            const char *status_text = tasks[t].status == 0 ? "OK" : "FAIL";
            const char *error_text = tasks[t].status == 0 ? "" :
                                     (tasks[t].error_message[0] != '\0' ? tasks[t].error_message : "filter_failed");

            to_logical_path(tasks[t].output_path, options.output_dir, "output", output_logical, sizeof(output_logical));
            if (tasks[t].status == 0) {
                pixels = (long long)image.width * (long long)image.height;
                local_pixels_processed += pixels;
            }

            fprintf(rank_log,
                    "rank=%d host=%s arch=%s image=%s filter=%s output=%s status=%s pixels=%lld "
                    "width=%d height=%d elapsed=%.6f error=%s\n",
                    rank,
                    hostname,
                    uts.machine,
                    input_logical,
                    transform_name(tasks[t].transform),
                    output_logical,
                    status_text,
                    pixels,
                    image.width,
                    image.height,
                    tasks[t].elapsed_seconds,
                    error_text);
            write_csv_record(rank_csv,
                             rank,
                             world_size,
                             hostname,
                             uts.machine,
                             input_logical,
                             transform_name(tasks[t].transform),
                             output_logical,
                             status_text,
                             pixels,
                             image.width,
                             image.height,
                             tasks[t].elapsed_seconds,
                             error_text);
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
            "summary rank=%d size=%d host=%s arch=%s assigned_images=%d success=%d fail=%d elapsed=%.6f\n",
            rank,
            world_size,
            hostname,
            uts.machine,
            local_count,
            local_success,
            local_fail,
            rank_elapsed);
    fflush(rank_csv);
    fclose(rank_csv);
    fclose(rank_log);

    printf("rank=%d size=%d host=%s arch=%s assigned_images=%d elapsed=%.6f\n",
           rank,
           world_size,
           hostname,
           uts.machine,
           local_count,
           rank_elapsed);

    MPI_Reduce(&local_success, &total_success, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_fail, &total_fail, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_pixels_processed,
               &total_pixels_processed,
               1,
               MPI_LONG_LONG,
               MPI_SUM,
               0,
               MPI_COMM_WORLD);
    MPI_Reduce(&rank_elapsed, &total_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        all_hosts = (char *)calloc((size_t)world_size, sizeof(hostname));
        if (all_hosts == NULL) {
            fprintf(stderr, "Rank 0: no se pudo reservar memoria para hosts.\n");
            MPI_Abort(MPI_COMM_WORLD, 9);
        }
    }
    MPI_Gather(hostname, (int)sizeof(hostname), MPI_CHAR, all_hosts, (int)sizeof(hostname), MPI_CHAR, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        FILE *global_csv = NULL;
        FILE *summary_file = NULL;
        char hosts_used[2048];
        int node_count = 0;
        double throughput = 0.0;

        hosts_used[0] = '\0';

        global_csv = fopen(global_csv_path, "w");
        if (global_csv == NULL) {
            fprintf(stderr, "Rank 0: no se pudo crear '%s'.\n", global_csv_path);
            MPI_Abort(MPI_COMM_WORLD, 10);
        }
        write_csv_header(global_csv);

        for (i = 0; i < world_size; ++i) {
            char temp_csv_path[PATH_MAX];
            char *host_ptr = all_hosts + ((size_t)i * sizeof(hostname));
            int known_host = 0;
            int j = 0;

            snprintf(temp_csv_path,
                     sizeof(temp_csv_path),
                     "%s/processing_rank_%d_%s.csv",
                     options.logs_dir,
                     i,
                     host_ptr);
            if (append_csv_without_header(global_csv, temp_csv_path) != 0) {
                fclose(global_csv);
                fprintf(stderr, "Rank 0: no se pudo combinar '%s'.\n", temp_csv_path);
                MPI_Abort(MPI_COMM_WORLD, 11);
            }

            for (j = 0; j < i; ++j) {
                char *seen_host = all_hosts + ((size_t)j * sizeof(hostname));
                if (strcmp(host_ptr, seen_host) == 0) {
                    known_host = 1;
                    break;
                }
            }

            if (!known_host) {
                size_t used_length = strlen(hosts_used);
                size_t host_length = strlen(host_ptr);
                if (used_length > 0 && used_length + 1 < sizeof(hosts_used)) {
                    hosts_used[used_length++] = ',';
                    hosts_used[used_length] = '\0';
                }
                if (used_length + host_length < sizeof(hosts_used)) {
                    memcpy(hosts_used + used_length, host_ptr, host_length + 1);
                }
                ++node_count;
            }
        }
        fclose(global_csv);

        throughput = total_elapsed > 0.0 ? (double)total_pixels_processed / total_elapsed : 0.0;

        summary_file = fopen(summary_path, "w");
        if (summary_file == NULL) {
            fprintf(stderr, "Rank 0: no se pudo crear '%s'.\n", summary_path);
            MPI_Abort(MPI_COMM_WORLD, 12);
        }
        fprintf(summary_file, "total_images=%d\n", image_count);
        fprintf(summary_file, "total_outputs_ok=%d\n", total_success);
        fprintf(summary_file, "total_outputs_fail=%d\n", total_fail);
        fprintf(summary_file, "total_pixels_processed=%lld\n", total_pixels_processed);
        fprintf(summary_file, "total_elapsed_seconds=%.6f\n", total_elapsed);
        fprintf(summary_file, "throughput_pixels_per_second=%.6e\n", throughput);
        fprintf(summary_file, "nodes_used=%d\n", node_count);
        fprintf(summary_file, "ranks_used=%d\n", world_size);
        fprintf(summary_file, "hosts_used=%s\n", hosts_used);
        fclose(summary_file);

        printf("total_images=%d total_outputs_ok=%d total_outputs_fail=%d total_pixels_processed=%lld "
               "total_elapsed_seconds=%.6f throughput_pixels_per_second=%.6e csv_global=%s\n",
               image_count,
               total_success,
               total_fail,
               total_pixels_processed,
               total_elapsed,
               throughput,
               global_csv_path);
    }

    free(all_hosts);
    free(paths_buf);
    free(bases_buf);
    free(images);
    MPI_Finalize();

    return (rank == 0 && total_fail > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
