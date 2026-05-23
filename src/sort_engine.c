#include "sort_engine.h"

#include <gmodule.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef G_OS_WIN32
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <glib/gstdio.h>

#define BOGO_MAX_SHUFFLES 50000
#define STOOGE_MAX_ELEMENTS 200
#define CUSTOM_WORKER_TIMEOUT_MS 5000

typedef struct {
    int *arr;
    size_t n;
    SortFrames *frames;
    GError **error;
    gboolean ok;
} SortContext;

static GQuark sort_error_quark(void) {
    return g_quark_from_static_string("sort-visualizer-error");
}

void sort_frames_init(SortFrames *frames) {
    frames->frames = NULL;
    frames->frame_count = 0;
    frames->frame_capacity = 0;
    frames->n = 0;
}

void sort_frames_clear(SortFrames *frames) {
    if (!frames) {
        return;
    }

    for (size_t i = 0; i < frames->frame_count; ++i) {
        g_free(frames->frames[i]);
    }
    g_free(frames->frames);

    frames->frames = NULL;
    frames->frame_count = 0;
    frames->frame_capacity = 0;
    frames->n = 0;
}

gboolean sort_frames_capture(SortFrames *frames, const int *arr, size_t n, GError **error) {
    if (frames->n == 0) {
        frames->n = n;
    }
    if (frames->n != n) {
        g_set_error(error, sort_error_quark(), 1, "Frame size mismatch");
        return FALSE;
    }

    if (frames->frame_count == frames->frame_capacity) {
        size_t new_capacity = frames->frame_capacity == 0 ? 64 : frames->frame_capacity * 2;
        int **new_frames = g_realloc_n(frames->frames, new_capacity, sizeof(int *));
        if (!new_frames) {
            g_set_error(error, sort_error_quark(), 1, "Out of memory while growing frame buffer");
            return FALSE;
        }
        frames->frames = new_frames;
        frames->frame_capacity = new_capacity;
    }

    int *snapshot = g_new(int, n);
    if (!snapshot) {
        g_set_error(error, sort_error_quark(), 1, "Out of memory while creating frame snapshot");
        return FALSE;
    }

    memcpy(snapshot, arr, sizeof(int) * n);
    frames->frames[frames->frame_count++] = snapshot;
    return TRUE;
}

static gboolean ctx_capture(SortContext *ctx) {
    if (!ctx->ok) {
        return FALSE;
    }
    ctx->ok = sort_frames_capture(ctx->frames, ctx->arr, ctx->n, ctx->error);
    return ctx->ok;
}

static void ctx_swap(SortContext *ctx, size_t i, size_t j) {
    if (!ctx->ok || i >= ctx->n || j >= ctx->n) {
        return;
    }

    int tmp = ctx->arr[i];
    ctx->arr[i] = ctx->arr[j];
    ctx->arr[j] = tmp;
    ctx_capture(ctx);
}

static void ctx_set(SortContext *ctx, size_t i, int value) {
    if (!ctx->ok || i >= ctx->n) {
        return;
    }

    ctx->arr[i] = value;
    ctx_capture(ctx);
}

static gboolean is_sorted(const int *arr, size_t n) {
    for (size_t i = 1; i < n; ++i) {
        if (arr[i - 1] > arr[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean run_bubble(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j + 1 < n - i; ++j) {
            if (ctx.arr[j] > ctx.arr[j + 1]) {
                ctx_swap(&ctx, j, j + 1);
            }
        }
    }
    return ctx.ok;
}

static gboolean run_selection(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};
    for (size_t i = 0; i < n; ++i) {
        size_t min_idx = i;
        for (size_t j = i + 1; j < n; ++j) {
            if (ctx.arr[j] < ctx.arr[min_idx]) {
                min_idx = j;
            }
        }
        if (min_idx != i) {
            ctx_swap(&ctx, i, min_idx);
        }
    }
    return ctx.ok;
}

static gboolean run_insertion(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};
    for (size_t i = 1; i < n && ctx.ok; ++i) {
        int key = ctx.arr[i];
        size_t j = i;
        while (j > 0 && ctx.arr[j - 1] > key) {
            ctx_set(&ctx, j, ctx.arr[j - 1]);
            --j;
        }
        ctx_set(&ctx, j, key);
    }
    return ctx.ok;
}

static gboolean run_gnome(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};
    size_t idx = 1;
    while (idx < n && ctx.ok) {
        if (idx == 0 || ctx.arr[idx - 1] <= ctx.arr[idx]) {
            ++idx;
        } else {
            ctx_swap(&ctx, idx, idx - 1);
            --idx;
        }
    }
    return ctx.ok;
}

static gboolean run_shaker(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};
    if (n < 2) {
        return TRUE;
    }

    size_t start = 0;
    size_t end = n - 1;
    gboolean swapped = TRUE;

    while (swapped && ctx.ok) {
        swapped = FALSE;
        for (size_t i = start; i < end; ++i) {
            if (ctx.arr[i] > ctx.arr[i + 1]) {
                ctx_swap(&ctx, i, i + 1);
                swapped = TRUE;
            }
        }

        if (!swapped) {
            break;
        }

        swapped = FALSE;
        if (end > 0) {
            --end;
        }

        for (size_t i = end; i > start; --i) {
            if (ctx.arr[i - 1] > ctx.arr[i]) {
                ctx_swap(&ctx, i - 1, i);
                swapped = TRUE;
            }
        }
        ++start;
    }

    return ctx.ok;
}

static gboolean run_odd_even(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};
    gboolean sorted = FALSE;

    while (!sorted && ctx.ok) {
        sorted = TRUE;

        for (size_t i = 1; i + 1 < n; i += 2) {
            if (ctx.arr[i] > ctx.arr[i + 1]) {
                ctx_swap(&ctx, i, i + 1);
                sorted = FALSE;
            }
        }

        for (size_t i = 0; i + 1 < n; i += 2) {
            if (ctx.arr[i] > ctx.arr[i + 1]) {
                ctx_swap(&ctx, i, i + 1);
                sorted = FALSE;
            }
        }
    }

    return ctx.ok;
}

static void pancake_flip(SortContext *ctx, size_t end) {
    for (size_t i = 0, j = end; i < j; ++i, --j) {
        ctx_swap(ctx, i, j);
    }
}

static size_t pancake_max_index(const int *arr, size_t n) {
    size_t max_idx = 0;
    for (size_t i = 1; i < n; ++i) {
        if (arr[i] > arr[max_idx]) {
            max_idx = i;
        }
    }
    return max_idx;
}

static gboolean run_pancake(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};
    for (size_t curr_size = n; curr_size > 1 && ctx.ok; --curr_size) {
        size_t max_idx = pancake_max_index(ctx.arr, curr_size);
        if (max_idx == curr_size - 1) {
            continue;
        }
        if (max_idx > 0) {
            pancake_flip(&ctx, max_idx);
        }
        pancake_flip(&ctx, curr_size - 1);
    }
    return ctx.ok;
}

static void merge_sort_impl(SortContext *ctx, size_t left, size_t right, int *tmp) {
    if (!ctx->ok || left >= right) {
        return;
    }

    size_t mid = left + (right - left) / 2;
    merge_sort_impl(ctx, left, mid, tmp);
    merge_sort_impl(ctx, mid + 1, right, tmp);

    size_t i = left;
    size_t j = mid + 1;
    size_t k = left;

    while (i <= mid && j <= right) {
        if (ctx->arr[i] <= ctx->arr[j]) {
            tmp[k++] = ctx->arr[i++];
        } else {
            tmp[k++] = ctx->arr[j++];
        }
    }

    while (i <= mid) {
        tmp[k++] = ctx->arr[i++];
    }
    while (j <= right) {
        tmp[k++] = ctx->arr[j++];
    }

    for (k = left; k <= right && ctx->ok; ++k) {
        ctx_set(ctx, k, tmp[k]);
    }
}

static gboolean run_merge(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};
    if (n < 2) {
        return TRUE;
    }

    int *tmp = g_new(int, n);
    if (!tmp) {
        g_set_error(error, sort_error_quark(), 1, "Out of memory in merge sort");
        return FALSE;
    }

    merge_sort_impl(&ctx, 0, n - 1, tmp);
    g_free(tmp);
    return ctx.ok;
}

static ssize_t partition_quick(SortContext *ctx, ssize_t low, ssize_t high) {
    int pivot = ctx->arr[high];
    ssize_t i = low - 1;

    for (ssize_t j = low; j < high; ++j) {
        if (ctx->arr[j] <= pivot) {
            ++i;
            ctx_swap(ctx, (size_t)i, (size_t)j);
        }
    }
    ctx_swap(ctx, (size_t)(i + 1), (size_t)high);
    return i + 1;
}

static void quick_sort_impl(SortContext *ctx, ssize_t low, ssize_t high) {
    if (!ctx->ok || low >= high) {
        return;
    }

    ssize_t pi = partition_quick(ctx, low, high);
    quick_sort_impl(ctx, low, pi - 1);
    quick_sort_impl(ctx, pi + 1, high);
}

static gboolean run_quick(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};
    if (n > 1) {
        quick_sort_impl(&ctx, 0, (ssize_t)n - 1);
    }
    return ctx.ok;
}

static void heapify(SortContext *ctx, size_t n, size_t root) {
    if (!ctx->ok) {
        return;
    }

    size_t largest = root;
    size_t left = 2 * root + 1;
    size_t right = 2 * root + 2;

    if (left < n && ctx->arr[left] > ctx->arr[largest]) {
        largest = left;
    }
    if (right < n && ctx->arr[right] > ctx->arr[largest]) {
        largest = right;
    }

    if (largest != root) {
        ctx_swap(ctx, root, largest);
        heapify(ctx, n, largest);
    }
}

static gboolean run_heap(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};
    if (n < 2) {
        return TRUE;
    }

    for (ssize_t i = (ssize_t)n / 2 - 1; i >= 0; --i) {
        heapify(&ctx, n, (size_t)i);
    }

    for (size_t i = n - 1; i > 0 && ctx.ok; --i) {
        ctx_swap(&ctx, 0, i);
        heapify(&ctx, i, 0);
    }

    return ctx.ok;
}

static gboolean run_shell(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};

    for (size_t gap = n / 2; gap > 0 && ctx.ok; gap /= 2) {
        for (size_t i = gap; i < n && ctx.ok; ++i) {
            int temp = ctx.arr[i];
            size_t j = i;
            while (j >= gap && ctx.arr[j - gap] > temp) {
                ctx_set(&ctx, j, ctx.arr[j - gap]);
                j -= gap;
            }
            ctx_set(&ctx, j, temp);
        }
    }

    return ctx.ok;
}

static gboolean run_comb(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};

    size_t gap = n;
    gboolean swapped = TRUE;

    while ((gap > 1 || swapped) && ctx.ok) {
        if (gap > 1) {
            gap = (size_t)((double)gap / 1.3);
            if (gap < 1) {
                gap = 1;
            }
        }

        swapped = FALSE;
        for (size_t i = 0; i + gap < n; ++i) {
            if (ctx.arr[i] > ctx.arr[i + gap]) {
                ctx_swap(&ctx, i, i + gap);
                swapped = TRUE;
            }
        }
    }

    return ctx.ok;
}

static size_t next_power_of_two(size_t n) {
    size_t p = 1;
    while (p < n) {
        p <<= 1;
    }
    return p;
}

static void bitonic_merge(SortContext *ctx, size_t low, size_t cnt, gboolean asc) {
    if (!ctx->ok || cnt <= 1) {
        return;
    }

    size_t k = cnt / 2;
    for (size_t i = low; i < low + k; ++i) {
        size_t j = i + k;
        if (i >= ctx->n || j >= ctx->n) {
            continue;
        }
        if ((asc && ctx->arr[i] > ctx->arr[j]) || (!asc && ctx->arr[i] < ctx->arr[j])) {
            ctx_swap(ctx, i, j);
        }
    }

    bitonic_merge(ctx, low, k, asc);
    bitonic_merge(ctx, low + k, k, asc);
}

static void bitonic_sort_impl(SortContext *ctx, size_t low, size_t cnt, gboolean asc) {
    if (!ctx->ok || cnt <= 1) {
        return;
    }

    size_t k = cnt / 2;
    bitonic_sort_impl(ctx, low, k, TRUE);
    bitonic_sort_impl(ctx, low + k, k, FALSE);
    bitonic_merge(ctx, low, cnt, asc);
}

static gboolean run_bitonic(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};
    if (n < 2) {
        return TRUE;
    }

    size_t m = next_power_of_two(n);
    bitonic_sort_impl(&ctx, 0, m, TRUE);
    return ctx.ok;
}

static gboolean run_radix(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};
    if (n < 2) {
        return TRUE;
    }

    int min = ctx.arr[0];
    int max = ctx.arr[0];
    for (size_t i = 1; i < n; ++i) {
        if (ctx.arr[i] < min) {
            min = ctx.arr[i];
        }
        if (ctx.arr[i] > max) {
            max = ctx.arr[i];
        }
    }

    int offset = min < 0 ? -min : 0;
    unsigned int *work = g_new(unsigned int, n);
    unsigned int *output = g_new(unsigned int, n);
    if (!work || !output) {
        g_free(work);
        g_free(output);
        g_set_error(error, sort_error_quark(), 1, "Out of memory in radix sort");
        return FALSE;
    }

    for (size_t i = 0; i < n; ++i) {
        work[i] = (unsigned int)(ctx.arr[i] + offset);
    }

    unsigned int max_u = (unsigned int)(max + offset);
    for (unsigned int exp = 1; max_u / exp > 0 && ctx.ok; exp *= 10) {
        unsigned int count[10] = {0};

        for (size_t i = 0; i < n; ++i) {
            count[(work[i] / exp) % 10]++;
        }

        for (size_t i = 1; i < 10; ++i) {
            count[i] += count[i - 1];
        }

        for (ssize_t i = (ssize_t)n - 1; i >= 0; --i) {
            unsigned int digit = (work[i] / exp) % 10;
            output[--count[digit]] = work[i];
        }

        for (size_t i = 0; i < n; ++i) {
            work[i] = output[i];
            ctx_set(&ctx, i, (int)work[i] - offset);
        }
    }

    g_free(work);
    g_free(output);
    return ctx.ok;
}

static void bogo_shuffle(SortContext *ctx, GRand *rand_gen) {
    if (!ctx->ok || ctx->n < 2) {
        return;
    }

    for (size_t i = ctx->n - 1; i > 0; --i) {
        size_t j = (size_t)g_rand_int_range(rand_gen, 0, (gint)i + 1);
        ctx_swap(ctx, i, j);
    }
}

static gboolean run_bogo(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};
    if (n > 10) {
        g_set_error(error, sort_error_quark(), 1, "Bogo sort is limited to arrays of 10 elements or fewer");
        return FALSE;
    }

    GRand *rand_gen = g_rand_new();
    if (!rand_gen) {
        g_set_error(error, sort_error_quark(), 1, "Could not initialize random generator for bogo sort");
        return FALSE;
    }

    size_t attempts = 0;
    while (!is_sorted(ctx.arr, n) && attempts < BOGO_MAX_SHUFFLES && ctx.ok) {
        bogo_shuffle(&ctx, rand_gen);
        ++attempts;
    }

    g_rand_free(rand_gen);

    if (!ctx.ok) {
        return FALSE;
    }

    if (!is_sorted(ctx.arr, n)) {
        g_set_error(error, sort_error_quark(), 1, "Bogo sort exceeded shuffle limit (%d)", BOGO_MAX_SHUFFLES);
        return FALSE;
    }

    return TRUE;
}

static void stooge_impl(SortContext *ctx, size_t l, size_t h) {
    if (!ctx->ok || l >= h || h >= ctx->n) {
        return;
    }

    if (ctx->arr[l] > ctx->arr[h]) {
        ctx_swap(ctx, l, h);
    }

    if (h - l + 1 > 2) {
        size_t t = (h - l + 1) / 3;
        stooge_impl(ctx, l, h - t);
        stooge_impl(ctx, l + t, h);
        stooge_impl(ctx, l, h - t);
    }
}

static gboolean run_stooge(int *arr, size_t n, SortFrames *frames, GError **error) {
    SortContext ctx = {.arr = arr, .n = n, .frames = frames, .error = error, .ok = TRUE};

    if (n > STOOGE_MAX_ELEMENTS) {
        g_set_error(error, sort_error_quark(), 1, "Stooge sort is limited to arrays of %d elements or fewer", STOOGE_MAX_ELEMENTS);
        return FALSE;
    }

    if (n > 1) {
        stooge_impl(&ctx, 0, n - 1);
    }

    return ctx.ok;
}

static const SortAlgorithm BUILTIN_ALGORITHMS[] = {
    {.id = "quick", .label = "Quick Sort", .best_case = "O(n log n)", .avg_case = "O(n log n)", .worst_case = "O(n^2)", .stable = FALSE, .in_place = TRUE, .notes = "Pick a pivot, partition into smaller and larger sides, then recurse. Great default sorter but pivot choice controls worst-case behavior.", .run = run_quick},
    {.id = "merge", .label = "Merge Sort", .best_case = "O(n log n)", .avg_case = "O(n log n)", .worst_case = "O(n log n)", .stable = TRUE, .in_place = FALSE, .notes = "Split array in half, sort each half, then merge sorted halves. Stable and predictable, ideal when consistent performance matters.", .run = run_merge},
    {.id = "heap", .label = "Heap Sort", .best_case = "O(n log n)", .avg_case = "O(n log n)", .worst_case = "O(n log n)", .stable = FALSE, .in_place = TRUE, .notes = "Build a max-heap, repeatedly extract the largest element to the end. In-place with strong worst-case guarantee.", .run = run_heap},
    {.id = "bubble", .label = "Bubble Sort", .best_case = "O(n)", .avg_case = "O(n^2)", .worst_case = "O(n^2)", .stable = TRUE, .in_place = TRUE, .notes = "Adjacent elements swap when out of order; large values bubble to the right. Simple to teach but inefficient on large inputs.", .run = run_bubble},
    {.id = "selection", .label = "Selection Sort", .best_case = "O(n^2)", .avg_case = "O(n^2)", .worst_case = "O(n^2)", .stable = FALSE, .in_place = TRUE, .notes = "Find the minimum in the unsorted suffix and place it next. Minimizes swaps, so it is useful when writes are expensive.", .run = run_selection},
    {.id = "insertion", .label = "Insertion Sort", .best_case = "O(n)", .avg_case = "O(n^2)", .worst_case = "O(n^2)", .stable = TRUE, .in_place = TRUE, .notes = "Grow a sorted prefix by inserting each new element into position. Excellent for nearly sorted or very small arrays.", .run = run_insertion},
    {.id = "gnome", .label = "Gnome Sort", .best_case = "O(n)", .avg_case = "O(n^2)", .worst_case = "O(n^2)", .stable = TRUE, .in_place = TRUE, .notes = "Walk forward when neighbors are ordered; step backward after swaps. Conceptually close to insertion sort using local swaps.", .run = run_gnome},
    {.id = "shaker", .label = "Shaker Sort", .best_case = "O(n)", .avg_case = "O(n^2)", .worst_case = "O(n^2)", .stable = TRUE, .in_place = TRUE, .notes = "Bubble sort in both directions each pass. Helps move both small and large out-of-place items faster than one-way bubbling.", .run = run_shaker},
    {.id = "odd_even", .label = "Odd Even Sort", .best_case = "O(n)", .avg_case = "O(n^2)", .worst_case = "O(n^2)", .stable = TRUE, .in_place = TRUE, .notes = "Alternate comparisons on odd/even pairs, then even/odd pairs. Easy to parallelize because each phase compares disjoint pairs.", .run = run_odd_even},
    {.id = "pancake", .label = "Pancake Sort", .best_case = "O(n^2)", .avg_case = "O(n^2)", .worst_case = "O(n^2)", .stable = FALSE, .in_place = TRUE, .notes = "Bring the largest remaining value to front, then flip it to final position. Educational for prefix-reversal operations.", .run = run_pancake},
    {.id = "bitonic", .label = "Bitonic Sort", .best_case = "O(n log^2 n)", .avg_case = "O(n log^2 n)", .worst_case = "O(n log^2 n)", .stable = FALSE, .in_place = TRUE, .notes = "Transforms data into bitonic sequences then merges with a fixed comparison network. Common in GPU and hardware-friendly sorting.", .run = run_bitonic},
    {.id = "radix", .label = "Radix Sort", .best_case = "O(d*(n+b))", .avg_case = "O(d*(n+b))", .worst_case = "O(d*(n+b))", .stable = TRUE, .in_place = FALSE, .notes = "Sort by digits from least significant to most significant using counting buckets. Non-comparison sort for integer-like keys.", .run = run_radix},
    {.id = "shell", .label = "Shell Sort", .best_case = "O(n log n)", .avg_case = "~O(n^1.5)", .worst_case = "O(n^2)", .stable = FALSE, .in_place = TRUE, .notes = "Perform insertion sort on spaced gaps and shrink the gap over time. Reduces long-distance disorder before final insertion pass.", .run = run_shell},
    {.id = "comb", .label = "Comb Sort", .best_case = "O(n log n)", .avg_case = "O(n^2 / 2^p)", .worst_case = "O(n^2)", .stable = FALSE, .in_place = TRUE, .notes = "Like bubble sort but starts with large comparison gaps that shrink. Quickly removes turtles (small values near the end).", .run = run_comb},
    {.id = "bogo", .label = "Bogo Sort", .best_case = "O(n)", .avg_case = "O((n+1)!)", .worst_case = "Unbounded", .stable = FALSE, .in_place = TRUE, .notes = "Shuffle randomly until sorted. Useful only as a cautionary teaching example of impractical algorithm design.", .run = run_bogo},
    {.id = "stooge", .label = "Stooge Sort", .best_case = "O(n^2.709)", .avg_case = "O(n^2.709)", .worst_case = "O(n^2.709)", .stable = FALSE, .in_place = TRUE, .notes = "Recursively sort first 2/3, last 2/3, then first 2/3 again. Demonstrates recursion overhead and poor practical performance.", .run = run_stooge},
};

const SortAlgorithm *sort_get_algorithms(size_t *count) {
    if (count) {
        *count = G_N_ELEMENTS(BUILTIN_ALGORITHMS);
    }
    return BUILTIN_ALGORITHMS;
}

const SortAlgorithm *sort_find_algorithm(const char *id) {
    if (!id) {
        return NULL;
    }

    for (size_t i = 0; i < G_N_ELEMENTS(BUILTIN_ALGORITHMS); ++i) {
        if (g_strcmp0(BUILTIN_ALGORITHMS[i].id, id) == 0) {
            return &BUILTIN_ALGORITHMS[i];
        }
    }

    return NULL;
}

gboolean sort_run_algorithm(const SortAlgorithm *algorithm, const int *input, size_t n, SortFrames *frames, GError **error) {
    if (!algorithm || !algorithm->run) {
        g_set_error(error, sort_error_quark(), 1, "Invalid algorithm");
        return FALSE;
    }

    sort_frames_clear(frames);
    sort_frames_init(frames);

    int *work = g_new(int, n);
    if (!work) {
        g_set_error(error, sort_error_quark(), 1, "Out of memory while copying input array");
        return FALSE;
    }

    memcpy(work, input, sizeof(int) * n);

    if (!sort_frames_capture(frames, work, n, error)) {
        g_free(work);
        return FALSE;
    }

    gboolean ok = algorithm->run(work, n, frames, error);
    if (ok && (frames->frame_count == 0 || !is_sorted(frames->frames[frames->frame_count - 1], n))) {
        ok = sort_frames_capture(frames, work, n, error);
    }

    g_free(work);
    return ok;
}

void custom_sort_handle_init(CustomSortHandle *handle) {
    memset(handle, 0, sizeof(*handle));
}

void custom_sort_handle_clear(CustomSortHandle *handle) {
    if (!handle) {
        return;
    }

    if (handle->dl_handle) {
        g_module_close((GModule *)handle->dl_handle);
    }

    if (handle->library_path) {
        g_remove(handle->library_path);
    }
    if (handle->source_path) {
        g_remove(handle->source_path);
    }
    if (handle->build_dir) {
        g_rmdir(handle->build_dir);
    }

    g_free(handle->library_path);
    g_free(handle->source_path);
    g_free(handle->build_dir);

    custom_sort_handle_init(handle);
}

const char *custom_sort_template(void) {
    return "/* Implement: int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) */\n"
           "#include <stddef.h>\n\n"
           "int custom_sort(int *arr, size_t n, void (*swap_cb)(size_t,size_t,void*), void *user_data) {\n"
           "    for (size_t i = 0; i < n; ++i) {\n"
           "        for (size_t j = 0; j + 1 < n - i; ++j) {\n"
           "            if (arr[j] > arr[j + 1]) {\n"
           "                if (swap_cb) {\n"
           "                    swap_cb(j, j + 1, user_data);\n"
           "                } else {\n"
           "                    int t = arr[j]; arr[j] = arr[j + 1]; arr[j + 1] = t;\n"
           "                }\n"
           "            }\n"
           "        }\n"
           "    }\n"
           "    return 0;\n"
           "}\n";
}

gboolean custom_sort_compile(CustomSortHandle *handle, const char *user_code, GError **error) {
    if (!handle || !user_code || strlen(user_code) == 0) {
        g_set_error(error, sort_error_quark(), 1, "Custom code cannot be empty");
        return FALSE;
    }

    custom_sort_handle_clear(handle);

    gchar *build_dir = g_dir_make_tmp("sortviz-custom-XXXXXX", error);
    if (!build_dir) {
        return FALSE;
    }

    gchar *source_path = g_build_filename(build_dir, "custom_sort.c", NULL);
    gchar *library_path = NULL;
#ifdef G_OS_WIN32
    library_path = g_build_filename(build_dir, "custom_sort.dll", NULL);
#else
    library_path = g_build_filename(build_dir, "custom_sort.so", NULL);
#endif

    if (!g_file_set_contents(source_path, user_code, -1, error)) {
        g_free(build_dir);
        g_free(source_path);
        g_free(library_path);
        return FALSE;
    }

    gchar *cmd = NULL;
#ifdef G_OS_WIN32
    cmd = g_strdup_printf("gcc -shared -O2 -std=c11 -o \"%s\" \"%s\"", library_path, source_path);
#else
    cmd = g_strdup_printf("gcc -shared -fPIC -O2 -std=c11 -o '%s' '%s'", library_path, source_path);
#endif
    gchar *stdout_str = NULL;
    gchar *stderr_str = NULL;
    gint exit_status = 0;

    gboolean spawned = g_spawn_command_line_sync(cmd, &stdout_str, &stderr_str, &exit_status, error);
    g_free(cmd);

    if (!spawned || exit_status != 0) {
        gchar *msg = g_strdup_printf("Custom sort compilation failed:\n%s\n%s", stdout_str ? stdout_str : "", stderr_str ? stderr_str : "");
        g_set_error(error, sort_error_quark(), 1, "%s", msg);
        g_free(msg);
        g_free(stdout_str);
        g_free(stderr_str);
        g_free(build_dir);
        g_free(source_path);
        g_free(library_path);
        return FALSE;
    }

    g_free(stdout_str);
    g_free(stderr_str);

    GModule *dl_handle = g_module_open(library_path, G_MODULE_BIND_LAZY);
    if (!dl_handle) {
        g_set_error(error, sort_error_quark(), 1, "Failed to load shared object: %s", g_module_error());
        g_free(build_dir);
        g_free(source_path);
        g_free(library_path);
        return FALSE;
    }

    int (*sort_fn)(int *, size_t, void (*)(size_t, size_t, void *), void *) = NULL;
    if (!g_module_symbol(dl_handle, "custom_sort", (gpointer *)&sort_fn) || !sort_fn) {
        g_module_close(dl_handle);
        g_set_error(error, sort_error_quark(), 1, "Function custom_sort was not found in compiled code");
        g_free(build_dir);
        g_free(source_path);
        g_free(library_path);
        return FALSE;
    }

    handle->build_dir = build_dir;
    handle->source_path = source_path;
    handle->library_path = library_path;
    handle->dl_handle = dl_handle;
    handle->custom_sort_fn = sort_fn;

    return TRUE;
}

#ifndef G_OS_WIN32
static gboolean write_all_fd(int fd, const char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t nwrite = write(fd, buf + written, len - written);
        if (nwrite < 0) {
            if (errno == EINTR) {
                continue;
            }
            return FALSE;
        }
        written += (size_t)nwrite;
    }
    return TRUE;
}

static gchar *resolve_worker_path(void) {
    gchar *exe_path = g_file_read_link("/proc/self/exe", NULL);
    if (exe_path) {
        gchar *dir = g_path_get_dirname(exe_path);
        gchar *candidate = g_build_filename(dir, "sort-visualizer-worker", NULL);
        g_free(dir);
        g_free(exe_path);
        if (g_file_test(candidate, G_FILE_TEST_IS_EXECUTABLE)) {
            return candidate;
        }
        g_free(candidate);
    }
    return g_strdup("sort-visualizer-worker");
}

static gboolean parse_frame_values(const gchar *payload, size_t n, int *values, GError **error) {
    gchar **tokens = g_strsplit(payload, ",", -1);
    size_t count = 0;

    for (gchar **it = tokens; it && *it; ++it) {
        gchar *tok = g_strstrip(*it);
        if (!tok || !*tok) {
            continue;
        }

        gchar *endptr = NULL;
        long v = strtol(tok, &endptr, 10);
        if (endptr == tok || *endptr != '\0' || v < INT_MIN || v > INT_MAX) {
            g_set_error(error, sort_error_quark(), 1, "Worker emitted invalid frame token: %s", tok);
            g_strfreev(tokens);
            return FALSE;
        }

        if (count >= n) {
            g_set_error(error, sort_error_quark(), 1, "Worker emitted too many values in frame");
            g_strfreev(tokens);
            return FALSE;
        }

        values[count++] = (int)v;
    }

    g_strfreev(tokens);
    if (count != n) {
        g_set_error(error, sort_error_quark(), 1, "Worker emitted %zu values, expected %zu", count, n);
        return FALSE;
    }
    return TRUE;
}

static gboolean process_worker_line(
    const gchar *line,
    size_t n,
    SortFrames *frames,
    int *tmp_values,
    int *result_code,
    gboolean *saw_result,
    gchar **worker_error,
    GError **error) {
    if (g_str_has_prefix(line, "FRAME ")) {
        if (!parse_frame_values(line + 6, n, tmp_values, error)) {
            return FALSE;
        }
        return sort_frames_capture(frames, tmp_values, n, error);
    }

    if (g_str_has_prefix(line, "RESULT ")) {
        gchar *endptr = NULL;
        long code = strtol(line + 7, &endptr, 10);
        if (endptr == line + 7 || *endptr != '\0' || code < INT_MIN || code > INT_MAX) {
            g_set_error(error, sort_error_quark(), 1, "Worker emitted invalid result line: %s", line);
            return FALSE;
        }
        *result_code = (int)code;
        *saw_result = TRUE;
        return TRUE;
    }

    if (g_str_has_prefix(line, "ERROR ")) {
        g_free(*worker_error);
        *worker_error = g_strdup(line + 6);
        return TRUE;
    }

    return TRUE;
}

static gboolean consume_worker_buffer(
    GString *buffer,
    size_t n,
    SortFrames *frames,
    int *tmp_values,
    int *result_code,
    gboolean *saw_result,
    gchar **worker_error,
    GError **error) {
    while (TRUE) {
        gchar *newline = strchr(buffer->str, '\n');
        if (!newline) {
            return TRUE;
        }

        size_t line_len = (size_t)(newline - buffer->str);
        gchar *line = g_strndup(buffer->str, line_len);
        g_string_erase(buffer, 0, line_len + 1);

        gboolean ok = process_worker_line(line, n, frames, tmp_values, result_code, saw_result, worker_error, error);
        g_free(line);
        if (!ok) {
            return FALSE;
        }
    }
}
#endif

typedef struct {
    int *arr;
    size_t n;
    SortFrames *frames;
    GError **error;
    gboolean ok;
} CustomSwapContext;

#ifdef G_OS_WIN32
static void custom_swap_cb(size_t i, size_t j, void *user_data) {
    CustomSwapContext *ctx = (CustomSwapContext *)user_data;
    if (!ctx || !ctx->ok || i >= ctx->n || j >= ctx->n) {
        return;
    }

    int tmp = ctx->arr[i];
    ctx->arr[i] = ctx->arr[j];
    ctx->arr[j] = tmp;

    ctx->ok = sort_frames_capture(ctx->frames, ctx->arr, ctx->n, ctx->error);
}
#endif

gboolean custom_sort_run(CustomSortHandle *handle, const int *input, size_t n, SortFrames *frames, GError **error) {
    if (!handle || !handle->custom_sort_fn) {
        g_set_error(error, sort_error_quark(), 1, "No compiled custom sort available");
        return FALSE;
    }

    sort_frames_clear(frames);
    sort_frames_init(frames);

    int *work = g_new(int, n);
    if (!work) {
        g_set_error(error, sort_error_quark(), 1, "Out of memory while running custom sort");
        return FALSE;
    }

    memcpy(work, input, sizeof(int) * n);
    if (!sort_frames_capture(frames, work, n, error)) {
        g_free(work);
        return FALSE;
    }

#ifdef G_OS_WIN32
    CustomSwapContext swap_ctx = {
        .arr = work,
        .n = n,
        .frames = frames,
        .error = error,
        .ok = TRUE,
    };

    int result = handle->custom_sort_fn(work, n, custom_swap_cb, &swap_ctx);
    if (!swap_ctx.ok) {
        g_free(work);
        return FALSE;
    }

    if (result != 0) {
        g_set_error(error, sort_error_quark(), 1, "custom_sort returned error code %d", result);
        g_free(work);
        return FALSE;
    }

    if (frames->frame_count == 0 || memcmp(frames->frames[frames->frame_count - 1], work, sizeof(int) * n) != 0) {
        if (!sort_frames_capture(frames, work, n, error)) {
            g_free(work);
            return FALSE;
        }
    }

    g_free(work);
    return TRUE;
#else
    gchar *worker_path = resolve_worker_path();
    gchar *argv[] = {worker_path, handle->library_path, NULL};

    GPid pid = 0;
    gint stdin_fd = -1;
    gint stdout_fd = -1;
    gint stderr_fd = -1;

    if (!g_spawn_async_with_pipes(
            NULL,
            argv,
            NULL,
            G_SPAWN_DO_NOT_REAP_CHILD,
            NULL,
            NULL,
            &pid,
            &stdin_fd,
            &stdout_fd,
            &stderr_fd,
            error)) {
        g_free(worker_path);
        g_free(work);
        return FALSE;
    }

    g_free(worker_path);

    GString *input_buf = g_string_new(NULL);
    g_string_append_printf(input_buf, "%zu\n", n);
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) {
            g_string_append_c(input_buf, ',');
        }
        g_string_append_printf(input_buf, "%d", work[i]);
    }
    g_string_append_c(input_buf, '\n');

    gboolean wrote_ok = write_all_fd(stdin_fd, input_buf->str, input_buf->len);
    g_string_free(input_buf, TRUE);
    close(stdin_fd);
    stdin_fd = -1;

    if (!wrote_ok) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        g_spawn_close_pid(pid);
        close(stdout_fd);
        close(stderr_fd);
        g_set_error(error, sort_error_quark(), 1, "Failed to send input to custom worker");
        g_free(work);
        return FALSE;
    }

    fcntl(stdout_fd, F_SETFL, fcntl(stdout_fd, F_GETFL, 0) | O_NONBLOCK);
    fcntl(stderr_fd, F_SETFL, fcntl(stderr_fd, F_GETFL, 0) | O_NONBLOCK);

    GString *stdout_buf = g_string_new(NULL);
    GString *stderr_buf = g_string_new(NULL);
    gchar *worker_error = NULL;
    int *tmp_values = g_new(int, n);
    int result_code = 0;
    gboolean saw_result = FALSE;
    gboolean ok = TRUE;
    gboolean timed_out = FALSE;

    gint64 start_us = g_get_monotonic_time();
    gint status = 0;
    gboolean child_exited = FALSE;

    while (!child_exited) {
        gint64 elapsed_ms = (g_get_monotonic_time() - start_us) / 1000;
        if (elapsed_ms > CUSTOM_WORKER_TIMEOUT_MS) {
            timed_out = TRUE;
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            child_exited = TRUE;
            break;
        }

        struct pollfd fds[2];
        fds[0].fd = stdout_fd;
        fds[0].events = POLLIN;
        fds[1].fd = stderr_fd;
        fds[1].events = POLLIN;

        int poll_timeout = 50;
        int pr = poll(fds, 2, poll_timeout);
        if (pr < 0 && errno != EINTR) {
            g_set_error(error, sort_error_quark(), 1, "IPC poll failed while waiting for custom worker");
            ok = FALSE;
            break;
        }

        if (pr > 0) {
            if (fds[0].revents & POLLIN) {
                char buf[4096];
                ssize_t nr = read(stdout_fd, buf, sizeof(buf));
                if (nr > 0) {
                    g_string_append_len(stdout_buf, buf, nr);
                    if (!consume_worker_buffer(stdout_buf, n, frames, tmp_values, &result_code, &saw_result, &worker_error, error)) {
                        ok = FALSE;
                        break;
                    }
                }
            }

            if (fds[1].revents & POLLIN) {
                char buf[1024];
                ssize_t nr = read(stderr_fd, buf, sizeof(buf));
                if (nr > 0 && stderr_buf->len < 32768) {
                    g_string_append_len(stderr_buf, buf, nr);
                }
            }
        }

        pid_t wr = waitpid(pid, &status, WNOHANG);
        if (wr == pid) {
            child_exited = TRUE;
        }
    }

    close(stdout_fd);
    close(stderr_fd);
    g_spawn_close_pid(pid);

    if (ok && stdout_buf->len > 0) {
        if (!consume_worker_buffer(stdout_buf, n, frames, tmp_values, &result_code, &saw_result, &worker_error, error)) {
            ok = FALSE;
        }
    }

    g_string_free(stdout_buf, TRUE);

    if (timed_out) {
        g_set_error(error, sort_error_quark(), 1, "Custom sort timed out after %d ms", CUSTOM_WORKER_TIMEOUT_MS);
        ok = FALSE;
    }

    if (ok && worker_error) {
        g_set_error(error, sort_error_quark(), 1, "%s", worker_error);
        ok = FALSE;
    }

    if (ok && !saw_result) {
        g_set_error(error, sort_error_quark(), 1, "Custom worker exited without a result line");
        ok = FALSE;
    }

    if (ok && result_code != 0) {
        g_set_error(error, sort_error_quark(), 1, "custom_sort returned error code %d", result_code);
        ok = FALSE;
    }

    if (ok && frames->frame_count == 0) {
        g_set_error(error, sort_error_quark(), 1, "Custom worker did not emit any frames");
        ok = FALSE;
    }

    if (!ok && stderr_buf->len > 0 && error && *error) {
        gchar *msg = g_strdup_printf("%s\nWorker stderr:\n%s", (*error)->message, stderr_buf->str);
        g_clear_error(error);
        g_set_error(error, sort_error_quark(), 1, "%s", msg);
        g_free(msg);
    }

    g_string_free(stderr_buf, TRUE);
    g_free(worker_error);
    g_free(tmp_values);
    g_free(work);
    return ok;
#endif
}
