# Dual-Pivot QuickSort (song song, an toàn bộ nhớ, chống adversarial input)

Cài đặt sắp xếp mảng số nguyên bằng **Dual-Pivot QuickSort**, kết hợp:

- **Median-of-Five** với vị trí lấy mẫu **ngẫu nhiên** — chống input adversarial (input cố tình gây worst-case O(n²)).
- **Dutch National Flag partitioning** — xử lý hiệu quả trường hợp nhiều phần tử trùng lặp.
- **Fallback Heapsort** khi độ sâu đệ quy vượt giới hạn (`depth_limit`) — đảm bảo worst-case vẫn là O(n log n) (kiểu introsort).
- **Song song hóa bằng pthreads** — tự tách các đoạn con đủ lớn (`>= PARALLEL_MIN_SIZE`) ra thread riêng, giới hạn theo số core CPU.
- **An toàn bộ nhớ** — kiểm tra `malloc` thất bại, free đúng, không leak khi tạo thread thất bại.

## Biên dịch

```bash
gcc -O2 -pthread -o sort sort.c -lm
```

## Chạy

```bash
./sort
```

Ép số lượng worker thread (mặc định = số core CPU):

```bash
SORT7_MAX_THREADS=8 ./sort
```

## Kết quả benchmark

Chạy trên máy thực tế với N = 1,000,000 phần tử, so sánh với `qsort()` chuẩn của libc:

| Bộ dữ liệu                  | qsort (libc) | Dual-Pivot v2 | Tỷ lệ         |
|------------------------------|-------------:|--------------:|---------------|
| Ngẫu nhiên                   | 76.78 ms     | 15.50 ms       | **4.95x** nhanh hơn |
| Nhiều trùng lặp (0–100)       | 67.97 ms     | 15.97 ms       | **4.26x** nhanh hơn |
| Đã sắp xếp                    | 20.34 ms     | 4.96 ms        | **4.10x** nhanh hơn |

Kết quả đúng (sort không sai lệch) trên cả 3 bộ dữ liệu.

> Lưu ý: tốc độ thực tế phụ thuộc số core CPU, tải hệ thống, và giá trị `SORT7_MAX_THREADS`. Số liệu trên chỉ mang tính tham khảo cho lần chạy cụ thể.

## Cấu hình quan trọng

| Hằng số / biến               | Ý nghĩa                                                        |
|-------------------------------|-----------------------------------------------------------------|
| `PARALLEL_MIN_SIZE`            | Kích thước đoạn tối thiểu để tách thread riêng (mặc định 50,000) |
| `MAX_PENDING_THREADS`          | Số thread con tối đa chờ join trong một lần gọi đệ quy           |
| `SORT7_MAX_THREADS` (env var)  | Ép số worker thread tối đa, mặc định = số core CPU               |

## Ghi chú

- Ngưỡng chuyển sang **Insertion Sort** khi đoạn còn lại nhỏ hơn 32 phần tử để giảm overhead.
- Hàm `dual_pivot_sort(arr, n)` là API chính, dùng tương tự `qsort()`.