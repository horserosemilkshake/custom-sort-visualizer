#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*CustomSortFn)(int *arr, size_t n, void (*swap_cb)(size_t, size_t, void *), void *user_data);

typedef struct {
    int *arr;
    size_t n;
    FILE *out;
    bool ok;
} WorkerContext;

static bool send_frame(FILE *out, const int *arr, size_t n) {
    if (fprintf(out, "FRAME ") < 0) {
        return false;
    }

    for (size_t i = 0; i < n; ++i) {
        if (i > 0) {
            if (fputc(',', out) == EOF) {
                return false;
            }
        }
        if (fprintf(out, "%d", arr[i]) < 0) {
            return false;
        }
    }

    if (fputc('\n', out) == EOF) {
        return false;
    }

    return fflush(out) == 0;
}

static void send_error(FILE *out, const char *msg) {
    fprintf(out, "ERROR %s\n", msg ? msg : "Unknown worker error");
    fflush(out);
}

static void swap_cb(size_t i, size_t j, void *user_data) {
    WorkerContext *ctx = (WorkerContext *)user_data;
    if (!ctx || !ctx->ok || i >= ctx->n || j >= ctx->n) {
        return;
    }

    int tmp = ctx->arr[i];
    ctx->arr[i] = ctx->arr[j];
    ctx->arr[j] = tmp;

    ctx->ok = send_frame(ctx->out, ctx->arr, ctx->n);
}

static bool parse_size_line(FILE *in, size_t *out_n) {
    char line[128];
    if (!fgets(line, sizeof(line), in)) {
        return false;
    }

    errno = 0;
    char *endptr = NULL;
    unsigned long long n = strtoull(line, &endptr, 10);
    if (errno != 0 || endptr == line) {
        return false;
    }

    while (*endptr == ' ' || *endptr == '\t') {
        ++endptr;
    }
    if (*endptr != '\n' && *endptr != '\0') {
        return false;
    }

    if (n > SIZE_MAX) {
        return false;
    }

    *out_n = (size_t)n;
    return true;
}

static bool parse_values_line(FILE *in, int *arr, size_t n) {
    size_t max_len = n * 16 + 64;
    char *line = malloc(max_len);
    if (!line) {
        return false;
    }

    bool ok = false;
    if (!fgets(line, (int)max_len, in)) {
        free(line);
        return false;
    }

    size_t count = 0;
    char *tok = strtok(line, ",\n");
    while (tok) {
        while (*tok == ' ' || *tok == '\t') {
            ++tok;
        }

        errno = 0;
        char *endptr = NULL;
        long v = strtol(tok, &endptr, 10);
        while (endptr && (*endptr == ' ' || *endptr == '\t')) {
            ++endptr;
        }

        if (errno != 0 || endptr == tok || (endptr && *endptr != '\0') || v < INT_MIN || v > INT_MAX || count >= n) {
            goto done;
        }

        arr[count++] = (int)v;
        tok = strtok(NULL, ",\n");
    }

    ok = (count == n);

done:
    free(line);
    return ok;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <custom-library-path>\n", argv[0]);
        return 2;
    }

    const char *library_path = argv[1];
    size_t n = 0;

    if (!parse_size_line(stdin, &n) || n == 0) {
        send_error(stdout, "Worker failed to parse array size");
        fprintf(stdout, "RESULT 2\n");
        return 2;
    }

    int *arr = malloc(sizeof(int) * n);
    if (!arr) {
        send_error(stdout, "Worker out of memory for input array");
        fprintf(stdout, "RESULT 2\n");
        return 2;
    }

    if (!parse_values_line(stdin, arr, n)) {
        free(arr);
        send_error(stdout, "Worker failed to parse input array values");
        fprintf(stdout, "RESULT 2\n");
        return 2;
    }

    void *dl_handle = dlopen(library_path, RTLD_NOW);
    if (!dl_handle) {
        free(arr);
        send_error(stdout, dlerror());
        fprintf(stdout, "RESULT 2\n");
        return 2;
    }

    void *symbol = dlsym(dl_handle, "custom_sort");
    CustomSortFn sort_fn = NULL;
    memcpy(&sort_fn, &symbol, sizeof(sort_fn));
    if (!sort_fn) {
        dlclose(dl_handle);
        free(arr);
        send_error(stdout, "custom_sort symbol missing in library");
        fprintf(stdout, "RESULT 2\n");
        return 2;
    }

    WorkerContext ctx = {
        .arr = arr,
        .n = n,
        .out = stdout,
        .ok = true,
    };

    if (!send_frame(stdout, arr, n)) {
        dlclose(dl_handle);
        free(arr);
        return 2;
    }

    int result = sort_fn(arr, n, swap_cb, &ctx);

    if (!ctx.ok) {
        dlclose(dl_handle);
        free(arr);
        return 2;
    }

    (void)send_frame(stdout, arr, n);
    fprintf(stdout, "RESULT %d\n", result);
    fflush(stdout);

    dlclose(dl_handle);
    free(arr);
    return 0;
}
