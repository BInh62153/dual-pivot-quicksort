# Dual-Pivot QuickSort — song song hóa bằng Thread Pool

Cài đặt sắp xếp mảng số nguyên bằng **Dual-Pivot QuickSort**, nhanh hơn `qsort()` chuẩn của libc từ **3–8 lần** tùy dữ liệu và số core CPU, đồng thời vẫn đảm bảo an toàn bộ nhớ và chống được input cố tình gây worst-case.

## Thuật toán

- **Median-of-Five** với vị trí lấy mẫu **ngẫu nhiên** trong mỗi đoạn — chống input adversarial (input cố tình gây worst-case O(n²)).
- **Dutch National Flag partitioning** — xử lý hiệu quả trường hợp nhiều phần tử trùng lặp.
- **Insertion Sort** cho đoạn nhỏ hơn 32 phần tử, giảm overhead đệ quy.
- **Fallback Heapsort** khi độ sâu đệ quy vượt giới hạn (`depth_limit = 2·log₂(n)`) — đảm bảo worst-case vẫn là O(n log n), kiểu introsort.
- **An toàn bộ nhớ** — kiểm tra `malloc` thất bại ở mọi nơi, free đúng, không leak.

## Song song hóa: Thread Pool

Thay vì `pthread_create()`/`pthread_join()` cho mỗi lần tách nhánh (tốn kém khi phải tách hàng trăm/nghìn lần), chương trình tạo **một pool thread cố định duy nhất** (số lượng = số core CPU, tạo lười biếng ở lần gọi `dual_pivot_sort()` đầu tiên, tái sử dụng cho mọi lần sort sau đó trong cùng chương trình).

Khi cần chạy song song, một đoạn chỉ đơn giản được **đẩy vào hàng đợi dùng chung**; các worker trong pool tự lấy việc ra xử lý. Một cơ chế đếm nhỏ (`taskgroup_t`) thay thế cho việc giữ handle thread để `join`.

**Điểm quan trọng — chống deadlock bằng work-stealing:** khi một thread đang chờ các đoạn con của chính nó xử lý xong, nó không ngủ im mà **chủ động lấy bất kỳ đoạn nào khác đang chờ trong hàng đợi ra chạy trước**. Đây là kỹ thuật chuẩn của các framework fork-join (Java `ForkJoinPool` dùng y hệt) — nếu bỏ qua bước này, đệ quy đủ sâu có thể khiến toàn bộ worker cùng lúc "đứng chờ nhau" và deadlock vĩnh viễn (xem mục [Sự cố đã gặp](#sự-cố-đã-gặp-và-cách-sửa) bên dưới).

## Biên dịch & chạy

```bash
gcc -O2 -pthread -o sort sort.c -lm
./sort
```

Ép số lượng worker thread trong pool (mặc định = số core CPU):

```bash
SORT7_MAX_THREADS=8 ./sort
```

> Lưu ý: biến môi trường này chỉ được đọc ở **lần gọi `dual_pivot_sort()` đầu tiên** vì pool chỉ tạo một lần cho cả vòng đời chương trình.

## Kết quả benchmark thực tế

Đo trên máy 20-core, so với `qsort()` chuẩn libc:

**N = 1,000,000**

| Bộ dữ liệu              | qsort (libc) | Dual-Pivot | Tỷ lệ |
|--------------------------|-------------:|-----------:|-------|
| Ngẫu nhiên               | ~76 ms       | ~15–20 ms  | **~4–5x** nhanh hơn |
| Nhiều trùng lặp (0–100)  | ~65 ms       | ~11–17 ms  | **~4–6x** nhanh hơn |
| Đã sắp xếp               | ~20 ms       | ~5 ms      | **~3.5–4x** nhanh hơn |

**N = 1,000,000,000 (1 tỷ phần tử, `SORT7_MAX_THREADS=8`)**

| Bộ dữ liệu              | qsort (libc) | Dual-Pivot | Tỷ lệ |
|--------------------------|-------------:|-----------:|-------|
| Ngẫu nhiên               | 95,962.88 ms | 11,985.26 ms | **8.01x** nhanh hơn |
| Nhiều trùng lặp (0–100)  | 85,813.11 ms | 11,089.31 ms | **7.74x** nhanh hơn |
| Đã sắp xếp               | 30,539.94 ms | 8,784.56 ms  | **3.48x** nhanh hơn |

Kết quả đúng (khớp 100% với `qsort()`) trên mọi bộ dữ liệu, mọi quy mô đã test. Tỷ lệ tăng tốc ở quy mô tỷ phần tử cao hơn hẳn quy mô triệu phần tử — hợp lý vì dữ liệu càng lớn, overhead cố định càng chiếm tỷ trọng nhỏ và song song hóa càng phát huy tác dụng.

> Tốc độ thực tế phụ thuộc số core CPU, tải hệ thống, và `SORT7_MAX_THREADS`. Với dữ liệu cỡ GB, cần đủ RAM trống cho cả `qsort()` (cần buffer tạm) lẫn bản sort riêng — thiếu RAM sẽ khiến hệ điều hành phải swap hoặc OOM-kill tiến trình, dễ nhầm với "bị treo".

## Cấu hình quan trọng

| Hằng số / biến               | Ý nghĩa                                                        |
|-------------------------------|-----------------------------------------------------------------|
| `PARALLEL_MIN_SIZE`            | Kích thước đoạn tối thiểu để đẩy vào pool chạy song song (mặc định 50,000) |
| `SORT7_MAX_THREADS` (env var)  | Ép số worker thread tối đa trong pool, mặc định = số core CPU     |

## API

```c
void dual_pivot_sort(int *arr, int n);
```
Dùng tương tự `qsort()`, chỉ hỗ trợ mảng `int` ở phiên bản này.

---

## Sự cố đã gặp và cách sửa

### Deadlock ở dữ liệu lớn + nhiều thread

**Triệu chứng:** chạy thực tế với `N=100,000,000`, dữ liệu **đã sắp xếp sẵn**, `SORT7_MAX_THREADS=15` — chương trình treo, phải hủy tiến trình. Không xảy ra ở N nhỏ hơn hoặc ít thread hơn.

**Nguyên nhân:** thiết kế thread pool ban đầu cho một thread đang chờ đoạn con của chính nó **ngủ im hoàn toàn** (`pthread_cond_wait`) thay vì tranh thủ làm việc khác. Khi đệ quy đủ sâu, toàn bộ worker trong pool có thể cùng lúc rơi vào trạng thái "đứng chờ nhau" — không còn thread nào rảnh để xử lý các đoạn đang nằm chờ trong hàng đợi. Cả hệ thống khóa chặt vĩnh viễn.

Xác nhận bằng `gdb` (attach vào tiến trình đang treo, xem backtrace tất cả thread): toàn bộ 15 worker đều dừng ở đúng một dòng — vòng chờ đoạn con hoàn tất — không thread nào đang thực thi công việc thật sự.

**Cách sửa:** thread đang chờ giờ sẽ **chủ động lấy đoạn khác trong hàng đợi chung ra chạy** trong lúc chờ, chỉ thật sự ngủ khi hàng đợi trống (work-stealing). Luôn có ít nhất một thread đang xử lý bất kỳ đoạn nào còn tồn đọng, thay vì tất cả cùng đứng im.

**Đã xác minh:**
- Tái hiện đúng kịch bản gây treo (N=100 triệu, đã sắp xếp, 15 threads), chạy lại nhiều lần liên tiếp — không còn treo.
- Xác nhận trên máy thật với N=1 tỷ (lớn hơn 10 lần kịch bản gây treo cũ) — chạy xong hoàn toàn, kết quả đúng (xem bảng benchmark ở trên).
- **ThreadSanitizer** (`-fsanitize=thread`) trên toàn bộ bộ test — không phát hiện race condition.
- Bộ test edge-case (n=0,1,2, ranh giới insertion sort 30–34, toàn phần tử giống nhau, số âm, stress lặp lại) — pass 100%.

**Bài học:** một thread pool tưởng đơn giản (worker chờ task con mà không giúp xử lý hàng đợi) có thể đúng ở quy mô nhỏ nhưng deadlock ở quy mô lớn. Loại bug này rất khó lộ ra nếu chỉ test với N nhỏ — nên luôn test ở nhiều quy mô dữ liệu khác nhau trước khi coi là ổn định.