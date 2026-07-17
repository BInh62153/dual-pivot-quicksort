/*
  Dual-Pivot QuickSort (Median-of-Five + Dutch National Flag)
      an toan bo nho (null-check + free dung),
      random hoa vi tri
      lay mau pivot (chong adversarial input),
      song song hoa bang THREAD POOL (khong con pthread_create/join moi lan tach nhanh)
      chong deadlock bang work-stealing: thread dang cho task con cua minh se
      tu lay task khac trong hang doi chung ra chay, thay vi ngu im (neu khong
      lam vay, khi do sau de quy vuot qua so worker trong pool, tat ca worker
      co the cung dung im cho nhau -> deadlock, da phat hien qua test thuc te
      voi N=100 trieu + 15 threads tren du lieu da sap xep).

  Bien dich:  gcc -O2 -pthread -o sort sort.c -lm
  Chay:       ./sort
              SORT7_MAX_THREADS=8 ./sort   (ep so luong worker trong pool)
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
#define PARALLEL_MIN_SIZE   50000   /* chi day vao pool cho doan >= nguong nay */

/* ===================== RNG nhanh, an toan theo luong ===================== */
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

/* =====================================================================
   THREAD POOL
   - Tao N thread MOT LAN duy nhat (lazy, pthread_once), tai su dung cho
     toan bo qua trinh song song, thay vi pthread_create/join moi lan
     tach nhanh trai nhu ban truoc.
   - Moi task duoc gan vao mot "task group" (bo dem tham chieu) de noi
     goi (dual_pivot_recursive) co the cho cac task con cua NO hoan tat
     truoc khi return - tuong duong ngu nghia pthread_join cu, nhung
     khong ton chi phi tao/huy thread that.
   ===================================================================== */

typedef struct {
    _Atomic int remaining;   /* so task con dang cho trong group nay */
} taskgroup_t;

typedef struct task {
    int *arr;
    int low, high, depth_limit;
    taskgroup_t *group;
    struct task *next;
} task_t;

static pthread_mutex_t g_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_queue_cond  = PTHREAD_COND_INITIALIZER;
static task_t *g_queue_head = NULL, *g_queue_tail = NULL;
static volatile int g_pool_shutdown = 0;

static pthread_t *g_pool_threads = NULL;
static int g_pool_size = 0;
static pthread_once_t g_pool_once = PTHREAD_ONCE_INIT;

static void dual_pivot_recursive(int *arr, int low, int high, int depth_limit);

/* Phai giu g_queue_mutex khi goi ham nay */
static inline task_t *dequeue_locked(void) {
    task_t *t = g_queue_head;
    if (t) {
        g_queue_head = t->next;
        if (!g_queue_head) g_queue_tail = NULL;
    }
    return t;
}

static inline void enqueue_locked(task_t *t) {
    t->next = NULL;
    if (g_queue_tail) g_queue_tail->next = t; else g_queue_head = t;
    g_queue_tail = t;
}

/* Chay mot task va bao cho ca group + hang doi biet (co the co thread khac
 * dang cho group nay hoac dang cho co viec moi). */
static void run_task(task_t *t) {
    dual_pivot_recursive(t->arr, t->low, t->high, t->depth_limit);
    taskgroup_t *group = t->group;
    free(t);

    pthread_mutex_lock(&g_queue_mutex);
    atomic_fetch_sub_explicit(&group->remaining, 1, memory_order_acq_rel);
    pthread_cond_broadcast(&g_queue_cond);
    pthread_mutex_unlock(&g_queue_mutex);
}

static void *pool_worker(void *unused) {
    (void)unused;
    pthread_mutex_lock(&g_queue_mutex);
    for (;;) {
        while (g_queue_head == NULL && !g_pool_shutdown) {
            pthread_cond_wait(&g_queue_cond, &g_queue_mutex);
        }
        if (g_queue_head == NULL && g_pool_shutdown) break;
        task_t *t = dequeue_locked();
        pthread_mutex_unlock(&g_queue_mutex);

        run_task(t);

        pthread_mutex_lock(&g_queue_mutex);
    }
    pthread_mutex_unlock(&g_queue_mutex);
    return NULL;
}

static void pool_shutdown_fn(void) {
    if (!g_pool_threads) return;
    pthread_mutex_lock(&g_queue_mutex);
    g_pool_shutdown = 1;
    pthread_mutex_unlock(&g_queue_mutex);
    pthread_cond_broadcast(&g_queue_cond);
    for (int i = 0; i < g_pool_size; i++) pthread_join(g_pool_threads[i], NULL);
    free(g_pool_threads);
    g_pool_threads = NULL;
}

static void pool_init(void) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) cores = 1;
    const char *env = getenv("SORT7_MAX_THREADS");
    if (env && atoi(env) > 0) cores = atoi(env);

    /* dung 1 worker nghia la khong can song song - de queue rong, chay tuan tu */
    if (cores <= 1) { g_pool_size = 0; return; }

    g_pool_threads = malloc(sizeof(pthread_t) * (size_t)cores);
    if (!g_pool_threads) { g_pool_size = 0; return; }

    int created = 0;
    for (int i = 0; i < cores; i++) {
        if (pthread_create(&g_pool_threads[i], NULL, pool_worker, NULL) != 0) break;
        created++;
    }
    g_pool_size = created;
    if (g_pool_size == 0) { free(g_pool_threads); g_pool_threads = NULL; return; }
    atexit(pool_shutdown_fn);
}

/* Thu day mot doan vao pool. Tra ve 1 neu thanh cong (da nhan viec). */
static int try_enqueue(int *arr, int low, int high, int depth_limit, taskgroup_t *group) {
    int segment_size = high - low + 1;
    if (segment_size < PARALLEL_MIN_SIZE) return 0;
    if (g_pool_size == 0) return 0; /* khong co pool (may 1 core / init that bai) -> chay tuan tu */

    task_t *t = malloc(sizeof(task_t));
    if (!t) return 0; /* het bo nho -> cu chay tuan tu, khong crash */
    t->arr = arr; t->low = low; t->high = high; t->depth_limit = depth_limit;
    t->group = group;

    pthread_mutex_lock(&g_queue_mutex);
    atomic_fetch_add_explicit(&group->remaining, 1, memory_order_relaxed);
    enqueue_locked(t);
    pthread_cond_signal(&g_queue_cond);
    pthread_mutex_unlock(&g_queue_mutex);
    return 1;
}

/* ---------- Dual-Pivot QuickSort de quy (tail-call -> while) ---------- */
static void dual_pivot_recursive(int *arr, int low, int high, int depth_limit) {
    taskgroup_t group;
    atomic_init(&group.remaining, 0);

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

        /* Median-of-Five voi vi tri lay mau NGAU NHIEN thay vi co dinh.
         * chi lay mau trong khoang MO (low, high), khong bao gio cham dung
         * low/high. */
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

            if (!try_enqueue(arr, low, lt - 1, depth_limit, &group)) {
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

        /* thu day doan trai vao pool, doan giua chay tai cho (tail-call) */
        if (!try_enqueue(arr, low, lp - 1, depth_limit, &group)) {
            dual_pivot_recursive(arr, low, lp - 1, depth_limit);
        }
        dual_pivot_recursive(arr, lp + 1, rp - 1, depth_limit);

        low = rp + 1; /* tail recursion -> vong lap */
    }

cleanup:
    /* Cho cac task da day vao pool (thuoc group nay) hoan tat, tuong duong
     * pthread_join cua ban cu nhung khong can giu handle thread that.
     *
     * QUAN TRONG: thay vi chi ngu im doi (co the gay DEADLOCK neu tat ca
     * worker trong pool cung dang o trang thai "cho" nhu the nay, khong con
     * ai ranh de xu ly cac task dang nam trong hang doi), thread o day se
     * CHU DONG lay task khac trong hang doi chung ra chay (work-stealing)
     * trong luc cho. Day la cach chuan de trong cac framework fork-join
     * (vd Java ForkJoinPool) dung de tranh chinh loai deadlock nay. */
    pthread_mutex_lock(&g_queue_mutex);
    while (atomic_load_explicit(&group.remaining, memory_order_acquire) > 0) {
        task_t *t = dequeue_locked();
        if (t) {
            pthread_mutex_unlock(&g_queue_mutex);
            run_task(t);
            pthread_mutex_lock(&g_queue_mutex);
        } else {
            pthread_cond_wait(&g_queue_cond, &g_queue_mutex);
        }
    }
    pthread_mutex_unlock(&g_queue_mutex);
}

void dual_pivot_sort(int *arr, int n) {
    if (n <= 1) return;

    /* Pool duoc tao MOT LAN duy nhat cho ca vong doi chuong trinh (lazy init).
     * SORT7_MAX_THREADS chi duoc doc o lan goi dau tien. */
    pthread_once(&g_pool_once, pool_init);

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

/* ---------- Sinh du lieu test: co null-check ---------- */
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

    const int size = 1000000000;
    printf("--- Dang kiem tra voi N = %d | thread pool (mac dinh = so core, co the ep bang SORT7_MAX_THREADS) ---\n", size);

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
        printf("  - Dual-Pivot v3:     %10.2f ms\n", time_custom);
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