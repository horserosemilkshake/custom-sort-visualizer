#ifndef SORT_ENGINE_H
#define SORT_ENGINE_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>

G_BEGIN_DECLS

typedef struct {
    int **frames;
    size_t frame_count;
    size_t frame_capacity;
    size_t n;
} SortFrames;

typedef struct {
    const char *id;
    const char *label;
    const char *best_case;
    const char *avg_case;
    const char *worst_case;
    gboolean stable;
    gboolean in_place;
    const char *notes;
    gboolean (*run)(int *arr, size_t n, SortFrames *frames, GError **error);
} SortAlgorithm;

typedef struct {
    gchar *build_dir;
    gchar *source_path;
    gchar *library_path;
    void *dl_handle;
    int (*custom_sort_fn)(int *arr, size_t n, void (*swap_cb)(size_t, size_t, void *), void *user_data);
} CustomSortHandle;

void sort_frames_init(SortFrames *frames);
void sort_frames_clear(SortFrames *frames);
gboolean sort_frames_capture(SortFrames *frames, const int *arr, size_t n, GError **error);

const SortAlgorithm *sort_get_algorithms(size_t *count);
const SortAlgorithm *sort_find_algorithm(const char *id);

gboolean sort_run_algorithm(const SortAlgorithm *algorithm, const int *input, size_t n, SortFrames *frames, GError **error);

void custom_sort_handle_init(CustomSortHandle *handle);
void custom_sort_handle_clear(CustomSortHandle *handle);
gboolean custom_sort_compile(CustomSortHandle *handle, const char *user_code, GError **error);
gboolean custom_sort_run(CustomSortHandle *handle, const int *input, size_t n, SortFrames *frames, GError **error);
const char *custom_sort_template(void);

G_END_DECLS

#endif
