/*
  Dual-Pivot QuickSort (Median-of-Five + Dutch National Flag)
      an toan bo nho (null-check + free dung), 
      random hoa vi tri
      lay mau pivot (chong adversarial input), 
      song song hoa bang pthreads
 
  Bien dich:  gcc -O2 -pthread -o sort sort.c -lm
  Chay:       ./sort
              SORT7_MAX_THREADS=8 ./sort   (ep so luong worker de test)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>

/* ===================== Cau hinh song song ===================== */
#define PARALLEL_MIN_SIZE   50000   /* chi tach thread cho doan >= nguong nay */
#define MAX_PENDING_THREADS 128     /* du cho depth_limit thuc te (~2*log2 n) */

static _Atomic int g_active_workers = 1;
static int g_max_workers = 1;

/* ===================== RNG nhanh, an toan theo luong (b) ===================== */
static __thread unsigned g_rng_state = 0;

static inline unsigned xorshift32(unsigned *state) {
    unsigned x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline unsigned next_rand(void) {
    if (g_rng_state == 0) {
        g_rng_state = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)pthread_self()
                     ^ (unsigned)(uintptr_t)&g_rng_state;
        if (g_rng_state == 0) g_rng_state = 0xA5A5A5A5u;
    }
    return xorshift32(&g_rng_state);
}

/* ---------- SWAPPING ---------- */
static inline void swap(int *a, int *b) {
    int t = *a; *a = *b; *b = t;
}

/* ---------- HEAPSORT LOOP (fallback khi depth_limit het) ---------- */
static void heapify_iterative(int *arr, int n, int i, int offset) {
    while (1) {
        int largest = i;
        int left  = (i << 1) | 1;
        int right = (i + 1) << 1;
        if (left < n  && arr[offset + left]  > arr[offset + largest]) largest = left;
        if (right < n && arr[offset + right] > arr[offset + largest]) largest = right;
        if (largest != i) {
            swap(&arr[offset + i], &arr[offset + largest]);
            i = largest;
        } else break;
    }
}

static void heapsort_fallback(int *arr, int low, int high) {
    int n = high - low + 1;
    for (int i = (n >> 1) - 1; i >= 0; i--) heapify_iterative(arr, n, i, low);
    for (int i = n - 1; i > 0; i--) {
        swap(&arr[low], &arr[low + i]);
        heapify_iterative(arr, i, 0, low);
    }
}

static void dual_pivot_recursive(int *arr, int low, int high, int depth_limit);

/* ---------- Goi cho thread con (c) ---------- */
typedef struct {
    int *arr;
    int low, high, depth_limit;
} thread_args_t;

static void *thread_worker(void *p) {
    thread_args_t *args = (thread_args_t *)p;
    dual_pivot_recursive(args->arr, args->low, args->high, args->depth_limit);
    free(args);
    atomic_fetch_sub_explicit(&g_active_workers, 1, memory_order_relaxed);
    return NULL;
}

/* Thu tach mot doan ra thread rieng. Tra ve 1 va gan tid neu thanh cong. */
static int try_spawn(int *arr, int low, int high, int depth_limit, pthread_t *tid_out) {
    int segment_size = high - low + 1;
    if (segment_size < PARALLEL_MIN_SIZE) return 0;
    if (atomic_load_explicit(&g_active_workers, memory_order_relaxed) >= g_max_workers) return 0;

    thread_args_t *targs = malloc(sizeof(thread_args_t));
    if (!targs) return 0; /* het bo nho -> cu chay tuan tu, khong crash */
    targs->arr = arr; targs->low = low; targs->high = high; targs->depth_limit = depth_limit;

    if (pthread_create(tid_out, NULL, thread_worker, targs) != 0) {
        free(targs);
        return 0;
    }
    atomic_fetch_add_explicit(&g_active_workers, 1, memory_order_relaxed);
    return 1;
}

/* ---------- Dual-Pivot QuickSort de quy (tail-call -> while) ---------- */
static void dual_pivot_recursive(int *arr, int low, int high, int depth_limit) {
    pthread_t pending[MAX_PENDING_THREADS];
    int npending = 0;

    while (low < high) {
        int size = high - low + 1;

        /* Insertion Sort cho mang nho (< 32) */
        if (size < 32) {
            for (int i = low + 1; i <= high; i++) {
                int val = arr[i];
                int j = i - 1;
                while (j >= low && arr[j] > val) { arr[j + 1] = arr[j]; j--; }
                arr[j + 1] = val;
            }
            goto cleanup;
        }

        if (depth_limit == 0) {
            heapsort_fallback(arr, low, high);
            goto cleanup;
        }
        depth_limit--;

        /* (b) Median-of-Five voi vi tri lay mau NGAU NHIEN thay vi co dinh.
         * QUAN TRONG: chi lay mau trong khoang MO (low, high), khong bao gio
         * cham dung low/high - neu de mau trung dung low hoac high, hai lenh
         * swap(&arr[low],...) va swap(&arr[high],...) ke tiep nhau se giam
         * len nhau va lam sai gia tri pivot b (da phat hien qua test thuc te). */
        int indices[5];
        int chosen = 0;
        while (chosen < 5) {
            int cand = low + 1 + (int)(next_rand() % (unsigned)(size - 2));
            int dup = 0;
            for (int i = 0; i < chosen; i++) if (indices[i] == cand) { dup = 1; break; }
            if (!dup) indices[chosen++] = cand;
        }
        for (int i = 1; i < 5; i++) {
            int v = indices[i];
            int val_v = arr[v];
            int j = i - 1;
            while (j >= 0 && arr[indices[j]] > val_v) {
                arr[indices[j+1]] = arr[indices[j]];
                j--;
            }
            arr[indices[j+1]] = val_v;
        }

        swap(&arr[low], &arr[indices[1]]);
        swap(&arr[high], &arr[indices[3]]);

        int a = arr[low], b = arr[high];

        /* Xu ly Pivot trung (Dutch National Flag) */
        if (a == b) {
            int lt = low, gt = high, k = low;
            while (k <= gt) {
                int val_k = arr[k];
                if (val_k < a) { swap(&arr[k], &arr[lt]); lt++; k++; }
                else if (val_k > a) { swap(&arr[k], &arr[gt]); gt--; }
                else k++;
            }

            /*  thu tach doan trai ra thread rieng */
            pthread_t tid;
            if (npending < MAX_PENDING_THREADS &&
                try_spawn(arr, low, lt - 1, depth_limit, &tid)) {
                pending[npending++] = tid;
            } else {
                dual_pivot_recursive(arr, low, lt - 1, depth_limit);
            }
            low = gt + 1;
            continue;
        }

        /* Phan vung Dual-Pivot chuan */
        int lp = low + 1, rp = high - 1, k = lp;
        while (k <= rp) {
            int val_k = arr[k];
            if (val_k < a) { swap(&arr[k], &arr[lp]); lp++; }
            else if (val_k >= b) {
                while (arr[rp] > b && k < rp) rp--;
                swap(&arr[k], &arr[rp]); rp--;
                if (arr[k] < a) { swap(&arr[k], &arr[lp]); lp++; }
            }
            k++;
        }
        lp--; rp++;
        swap(&arr[low], &arr[lp]);
        swap(&arr[high], &arr[rp]);

        /*  thu tach doan trai ra thread rieng, doan giua chay tai cho */
        pthread_t tid;
        if (npending < MAX_PENDING_THREADS &&
            try_spawn(arr, low, lp - 1, depth_limit, &tid)) {
            pending[npending++] = tid;
        } else {
            dual_pivot_recursive(arr, low, lp - 1, depth_limit);
        }
        dual_pivot_recursive(arr, lp + 1, rp - 1, depth_limit);

        low = rp + 1; /* tail recursion -> vong lap */
    }

cleanup:
    for (int i = 0; i < npending; i++) pthread_join(pending[i], NULL);
}

void dual_pivot_sort(int *arr, int n) {
    if (n <= 1) return;

    /* xac dinh so worker toi da: bien moi truong ghi de, mac dinh = so core */
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) cores = 1;
    const char *env = getenv("SORT7_MAX_THREADS");
    if (env && atoi(env) > 0) cores = atoi(env);
    g_max_workers = (int)cores;
    atomic_store_explicit(&g_active_workers, 1, memory_order_relaxed);

    int depth_limit = (int)(2 * floor(log2((double)n)));
    dual_pivot_recursive(arr, 0, n - 1, depth_limit);
}

/* ---------- So sanh tieu bieu: qsort() chuan thu vien C ---------- */
static int cmp_int(const void *x, const void *y) {
    int a = *(const int *)x, b = *(const int *)y;
    return (a > b) - (a < b);
}

static int is_equal(const int *a, const int *b, int n) {
    return memcmp(a, b, n * sizeof(int)) == 0;
}

/* ---------- (a) Sinh du lieu test: co null-check ---------- */
static int *make_random(int n, int lo, int hi) {
    int *a = malloc((size_t)n * sizeof(int));
    if (!a) { fprintf(stderr, "LOI: khong cap phat duoc bo nho (make_random, n=%d)\n", n); exit(1); }
    for (int i = 0; i < n; i++) a[i] = lo + rand() % (hi - lo + 1);
    return a;
}

static int *make_sorted(int n) {
    int *a = malloc((size_t)n * sizeof(int));
    if (!a) { fprintf(stderr, "LOI: khong cap phat duoc bo nho (make_sorted, n=%d)\n", n); exit(1); }
    for (int i = 0; i < n; i++) a[i] = i;
    return a;
}

static double elapsed_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1e6;
}

int main(void) {
    srand((unsigned)time(NULL));

    const int size = 1000000;
    printf("--- Dang kiem tra voi N = %d | max_workers (mac dinh = so core, co the ep bang SORT7_MAX_THREADS) ---\n", size);

    const char *names[3] = { "Ngau nhien", "Nhieu trung lap (0-100)", "Da sap xep" };
    int *base_data[3] = { NULL, NULL, NULL };
    base_data[0] = make_random(size, 0, 1000);
    base_data[1] = make_random(size, 0, 100);
    base_data[2] = make_sorted(size);

    int exit_code = 0;

    for (int t = 0; t < 3 && exit_code == 0; t++) {
        printf("\nBo du lieu: %s\n", names[t]);

        int *arr_builtin = malloc((size_t)size * sizeof(int));
        int *arr_custom  = malloc((size_t)size * sizeof(int));
        if (!arr_builtin || !arr_custom) {
            fprintf(stderr, "LOI: khong cap phat duoc bo nho cho vong benchmark\n");
            free(arr_builtin); free(arr_custom);
            exit_code = 1;
            break;
        }
        memcpy(arr_builtin, base_data[t], (size_t)size * sizeof(int));
        memcpy(arr_custom,  base_data[t], (size_t)size * sizeof(int));

        struct timespec start, end;

        clock_gettime(CLOCK_MONOTONIC, &start);
        qsort(arr_builtin, size, sizeof(int), cmp_int);
        clock_gettime(CLOCK_MONOTONIC, &end);
        double time_builtin = elapsed_ms(start, end);

        clock_gettime(CLOCK_MONOTONIC, &start);
        dual_pivot_sort(arr_custom, size);
        clock_gettime(CLOCK_MONOTONIC, &end);
        double time_custom = elapsed_ms(start, end);

        printf("  - qsort (C, libc):  %10.2f ms\n", time_builtin);
        printf("  - Dual-Pivot v2:     %10.2f ms\n", time_custom);
        printf("  - Ty le:             %10.2fx %s\n",
               time_custom > time_builtin ? time_custom / time_builtin : time_builtin / time_custom,
               time_custom > time_builtin ? "cham hon" : "nhanh hon");

        if (!is_equal(arr_custom, arr_builtin, size)) {
            fprintf(stderr, "  LOI: Sap xep sai!\n");
            exit_code = 1;
        }

        free(arr_builtin);
        free(arr_custom);
    }

    for (int t = 0; t < 3; t++) free(base_data[t]);

    return exit_code;
}