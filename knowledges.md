# Tư duy tổng thể: xuất phát từ điểm yếu của Quicksort thuần

Toàn bộ thiết kế này là một chuỗi các "bản vá lý thuyết" chồng lên nhau, mỗi lớp giải quyết đúng một điểm yếu cụ thể của Quicksort cơ bản. Cách hiểu dễ nhất là đi từ câu hỏi: *"Quicksort thuần bị hỏng ở đâu, và cộng đồng khoa học máy tính đã giải quyết từng chỗ hỏng đó bằng lý thuyết gì?"*

## 1. Vì sao chọn họ Quicksort làm nền, thay vì Merge Sort hay Heap Sort

Ba lựa chọn kinh điển:

- **Merge Sort**: ổn định, worst-case O(n log n) đảm bảo, nhưng cần bộ nhớ phụ O(n) và có locality bộ nhớ (cache locality) kém hơn khi merge các đoạn xa nhau trong mảng lớn.
- **Heap Sort**: in-place, worst-case O(n log n) đảm bảo, nhưng hằng số ẩn (hidden constant) trong O(n log n) lớn hơn nhiều vì truy cập bộ nhớ theo kiểu heap (nhảy cóc theo chỉ số `2i+1`, `2i+2`) rất tệ cho cache CPU.
- **Quicksort**: trung bình O(n log n) với hằng số nhỏ nhất trong thực tế, in-place, cache-friendly (đọc/ghi tuần tự trong một cửa sổ liên tục), nhưng có **worst-case O(n²)** nếu chọn pivot dở.

→ Quicksort thắng về hiệu năng thực tế (đây là lý do `qsort()` của nhiều thư viện, và `std::sort` của C++ trước C++11, đều dựa trên biến thể quicksort). Vấn đề còn lại thuần túy là: **làm sao vá được cái worst-case O(n²) đó**, mà không đánh đổi hiệu năng trung bình.

Toàn bộ phần còn lại của thiết kế là câu trả lời cho câu hỏi đó.

## 2. Dual-pivot thay vì single-pivot: nền tảng từ Yaroslavskiy (2009)

Quicksort cổ điển (Hoare 1961) chọn 1 pivot, chia mảng thành 2 phần: `< pivot` và `≥ pivot`. Vladimir Yaroslavskiy chứng minh bằng thực nghiệm (và sau này được phân tích lý thuyết bởi Wild & Nebel) rằng **chọn 2 pivot** chia mảng thành 3 phần (`< a`, `a ≤ x ≤ b`, `> b`) giảm được **số lần so sánh trung bình** khoảng 5%, và quan trọng hơn là **giảm số lần ghi bộ nhớ (swap)** — đây là lý do thuật toán này được Oracle chọn thay thế single-pivot quicksort trong `Arrays.sort()` của Java cho kiểu nguyên thủy (`int[]`, `long[]`...) từ Java 7.

Cơ chế partition trong code (đoạn `lp`, `rp`, `k`):
```
lp: biên phải của vùng "< a"
rp: biên trái của vùng "> b"  
k:  con trỏ quét
```
Đây chính là thuật toán 3-way scan của Yaroslavskiy — quét một lượt duy nhất, mỗi phần tử được xếp vào đúng 1 trong 3 vùng, tối đa 3 lần so sánh mỗi phần tử (so với 1-2 lần của single-pivot, nhưng bù lại giảm số vùng đệ quy con từ 2 xuống vẫn 2 gọi đệ quy nhưng với kích thước nhỏ hơn đều hơn).

## 3. Median-of-Five ngẫu nhiên: lý thuyết order statistics + phòng thủ adversarial

Đây là chỗ vá "worst-case O(n²)".

**Vấn đề gốc:** nếu luôn chọn pivot cố định (ví dụ phần tử đầu, giữa, hoặc cuối), một người biết trước thuật toán có thể **cố tình dựng input** khiến pivot luôn là giá trị nhỏ nhất/lớn nhất → mỗi lần partition chỉ giảm kích thước đi 1 → O(n²). Đây không phải chuyện lý thuyết suông: năm 2004 có tấn công thực tế "algorithmic complexity attack" khai thác đúng lỗ hổng này trên các thư viện sort dùng pivot cố định.

**Cách vá — hai lớp phòng thủ độc lập:**

1. **Random hóa vị trí lấy mẫu** (`next_rand() % (size-2)`): kẻ tấn công không biết trước chương trình sẽ lấy mẫu ở đâu, nên không thể dựng input cố định để "nhắm" vào đúng vị trí pivot xấu. Đây là nguyên lý **randomized algorithm** kinh điển (giống randomized quicksort của Sedgewick/CLRS) — biến worst-case *deterministic* thành worst-case *có xác suất cực nhỏ*, vì input xấu giờ phải "đoán trúng" một chuỗi số ngẫu nhiên.

2. **Lấy median của 5 mẫu** thay vì 1 mẫu: đây là lý thuyết **order statistics**. Nếu chỉ lấy 1 mẫu ngẫu nhiên làm pivot, xác suất mẫu đó rơi vào "vùng tệ" (ví dụ 10% nhỏ nhất hoặc 10% lớn nhất của phân phối) là 20%. Nhưng nếu lấy **median của 5 mẫu độc lập**, xác suất để median cũng rơi vào vùng tệ đó giảm mạnh (cần ít nhất 3/5 mẫu đều rơi vào cùng vùng 10% đó — xác suất nhỏ hơn nhiều theo phân phối nhị thức). Nói cách khác: median-of-k là một bộ lọc giảm phương sai (variance reduction) của phép chọn pivot, càng lấy nhiều mẫu, phân phối của "chất lượng pivot" càng tập trung quanh median thật của toàn mảng.

Comment trong code còn ghi chú kỹ thuật quan trọng: **chỉ lấy mẫu trong khoảng mở (low, high)**, không bao giờ chạm đúng `low`/`high` — vì hai lệnh `swap(&arr[low],...)` và `swap(&arr[high],...)` kế tiếp nhau, nếu mẫu trùng đúng vị trí `low` hoặc `high`, phép swap thứ hai sẽ vô tình đè lên giá trị vừa swap ở bước đầu (aliasing bug) — một lớp lỗi off-by-one kinh điển khi làm việc với in-place swap.

## 4. Dutch National Flag: xử lý phần tử trùng lặp

Đây là thuật toán do Edsger Dijkstra đặt tên (ẩn dụ theo lá cờ Hà Lan 3 màu), giải quyết bài toán **3-way partitioning**: chia một mảng thành `< pivot`, `= pivot`, `> pivot` chỉ trong 1 lượt quét O(n), không cần lượt quét phụ.

**Vì sao cần:** nếu không xử lý riêng trường hợp có nhiều phần tử bằng nhau, Quicksort chuẩn sẽ liên tục đưa các phần tử `= pivot` vào một trong hai vùng con, khiến các lần đệ quy sau vẫn phải so sánh lẫn nhau dù chúng vốn đã "xong việc" (đã đúng vị trí tương đối) — với mảng có 90% giá trị trùng nhau (như bộ test "Nhiều trùng lặp 0-100"), đây suy biến gần về O(n²) nếu không xử lý. Bentley & McIlroy (1993, bài báo "Engineering a Sort Function") là người chứng minh và phổ biến kỹ thuật này cho quicksort thực dụng.

Trong code, nhánh `if (a == b)` chính là bật cờ Dutch Flag khi 2 pivot median-of-five trùng giá trị — dấu hiệu mạnh cho thấy mảng có nhiều phần tử lặp lại.

## 5. Depth-limit + Heapsort fallback: Introsort (Musser, 1997)

Dù đã có random hóa, về mặt lý thuyết **vẫn tồn tại xác suất (dù cực nhỏ) worst-case O(n²)** xảy ra — bất kỳ RNG nào cũng có thể "xui" đúng lúc. Musser giải quyết bằng ý tưởng **Introspective Sort**: theo dõi độ sâu đệ quy thực tế; nếu vượt quá ngưỡng lý thuyết `2·log₂(n)` (gấp đôi độ sâu kỳ vọng của quicksort cân bằng), tự động **chuyển sang Heapsort** cho đúng đoạn đó.

Đây là kiểu **thuật toán lai có bảo chứng (hybrid algorithm with guarantee)**: dùng Quicksort để tối ưu average-case, nhưng cắt lỗ bằng Heapsort để khóa chặt worst-case ở O(n log n) tuyệt đối — không phụ thuộc may rủi nữa. Đây chính xác là cách `std::sort` của C++ (libstdc++, libc++) hoạt động thật ngoài đời — introsort là chuẩn công nghiệp, không phải phát minh riêng của bài này.

## 6. Insertion Sort cho đoạn nhỏ: lý thuyết crossover point

Về mặt tiệm cận, Quicksort là O(n log n) còn Insertion Sort là O(n²) — Quicksort "thắng" mọi lúc. Nhưng đó chỉ đúng khi n đủ lớn để hằng số ẩn không còn quan trọng. Với n nhỏ (< 32 trong code này), overhead của đệ quy (gọi hàm, cấp phát biến cục bộ, chọn pivot...) trong Quicksort **lớn hơn** lợi ích tiệm cận nó mang lại — vì n nhỏ thì n² và n log n gần như không khác biệt về số phép so sánh tuyệt đối, mà Insertion Sort có bước lặp đơn giản, cache-friendly, branch-predictable hơn hẳn.

Con số 32 không phải tùy tiện — đây là "sweet spot" được đo thực nghiệm phổ biến trong nhiều triển khai (glibc, Java, .NET đều dùng ngưỡng cùng bậc độ lớn, dao động 16-64 tùy cài đặt cụ thể của CPU/compiler).

## 7. Song song hóa: lý thuyết Fork-Join và giới hạn của Amdahl

**Vì sao Quicksort dễ song song hóa:** sau khi partition xong, hai (hay ba, với dual-pivot) đoạn con **độc lập hoàn toàn** về mặt dữ liệu (không đoạn nào cần đọc dữ liệu của đoạn kia) — đây là điều kiện tiên quyết để song song hóa không cần đồng bộ hóa phức tạp (không có race condition trên dữ liệu, chỉ cần đồng bộ trên *việc hoàn tất*).

Đây là mô hình **Fork-Join** kinh điển (Cilk, từ MIT những năm 1990, sau này là nền tảng của Java `ForkJoinPool`): một tác vụ *fork* (tách) thành các tác vụ con chạy song song, rồi *join* (chờ) tất cả con xong trước khi tiếp tục.

**Vì sao có ngưỡng `PARALLEL_MIN_SIZE`:** đây là ứng dụng trực tiếp của **định luật Amdahl** ở khía cạnh overhead — song song hóa luôn có chi phí cố định (tạo task, đồng bộ). Nếu đoạn dữ liệu quá nhỏ, chi phí đồng bộ hóa còn lớn hơn thời gian tiết kiệm được từ việc chạy song song → phản tác dụng. Ngưỡng 50,000 là điểm mà lợi ích song song bắt đầu vượt chi phí overhead, tương tự tinh thần "granularity control" trong mọi hệ thống song song thực tế.

## 8. Thread pool + Work-stealing: lý thuyết scheduling

Ban đầu bài toán song song hóa được giải bằng cách tạo thread mới (`pthread_create`) mỗi lần tách nhánh — đơn giản nhưng có chi phí hệ điều hành thật (context switch, cấp phát stack, đăng ký với kernel scheduler) mà lý thuyết gọi là **task creation overhead**, tăng tuyến tính theo số lần tách nhánh.

Giải pháp: **thread pool cố định** + **work queue** — tách biệt khái niệm "đơn vị công việc" (task, rẻ) khỏi "luồng thực thi vật lý" (thread, đắt). Đây là mô hình **thread pool pattern**, nền tảng của mọi executor service hiện đại (Java `ExecutorService`, .NET `ThreadPool`, thread pool trong mọi web server).

Nhưng riêng việc dùng thread pool lại sinh ra một cạm bẫy lý thuyết tinh vi: nếu một worker *chờ chặn* (blocking wait) con của chính nó xong mà không giúp xử lý hàng đợi, thì khi độ sâu đệ quy vượt quá số worker trong pool, xảy ra hiện tượng gọi là **worker starvation deadlock** — về bản chất đây là một dạng của **circular wait** trong 4 điều kiện gây deadlock kinh điển (Coffman conditions): mỗi worker giữ "quyền được coi là bận" trong khi chờ một tài nguyên (kết quả của task con) mà chỉ có worker khác mới cung cấp được, nhưng tất cả worker khác cũng đang ở trạng thái y hệt.

Cách vá — **work-stealing** (Cilk scheduling, Blumofe & Leiserson 1999 là công trình lý thuyết gốc chứng minh work-stealing đạt hiệu năng tối ưu trong mô hình fork-join): một thread đang chờ không được phép "ngủ thụ động" — nó phải tham gia xử lý hàng đợi chung như một worker bình thường. Điều này phá vỡ điều kiện circular-wait vì bất kỳ lúc nào còn task trong hàng đợi, luôn tồn tại ít nhất một thread sẵn sàng xử lý nó (không có thread nào "chỉ chờ mà không làm gì").

## 9. RNG thread-local (`__thread` + xorshift): lý thuyết concurrency + PRNG

Hai vấn đề tách biệt được giải quyết cùng lúc:

- **An toàn luồng**: nếu dùng một biến trạng thái RNG toàn cục (global), nhiều thread cùng gọi `next_rand()` sẽ có race condition (đọc/ghi cùng lúc lên `g_rng_state`), dẫn đến kết quả không xác định hoặc thậm chí crash. Dùng từ khóa `__thread` (Thread-Local Storage — TLS, một cơ chế được hỗ trợ ở cấp hệ điều hành/compiler) cho mỗi thread một bản sao trạng thái RNG riêng, loại bỏ hoàn toàn race condition mà không cần mutex (không cần lock → không có overhead đồng bộ).
- **Chọn xorshift**: đây là họ PRNG (Marsaglia, 2003) rất nhanh (vài phép XOR + shift bit) nhưng đủ "ngẫu nhiên" cho mục đích chọn pivot — không cần độ ngẫu nhiên mật mã học (cryptographically secure), chỉ cần đủ khó đoán để kẻ tấn công không thể dựng input "nhắm" trúng pivot xấu.

## Tổng kết tư duy thiết kế

Nhìn toàn cục, đây không phải một thuật toán "phát minh mới" mà là **tổng hợp có chủ đích** của nhiều kỹ thuật đã được chứng minh riêng lẻ trong tài liệu khoa học máy tính, mỗi kỹ thuật vá đúng một lỗ hổng cụ thể:

| Lỗ hổng của Quicksort thuần | Kỹ thuật vá | Nền tảng lý thuyết |
|---|---|---|
| Nhiều so sánh/swap | Dual-pivot | Yaroslavskiy 2009 |
| Adversarial input → O(n²) | Random hóa pivot | Randomized algorithms |
| Pivot xấu do may rủi | Median-of-5 | Order statistics |
| Nhiều phần tử trùng | Dutch Flag | Dijkstra; Bentley-McIlroy 1993 |
| Worst-case không bảo chứng | Depth-limit + Heapsort | Introsort, Musser 1997 |
| Overhead với n nhỏ | Insertion sort | Empirical crossover analysis |
| Không tận dụng đa core | Fork-join song song | Cilk, ForkJoinPool |
| Overhead tạo thread | Thread pool | Executor pattern |
| Deadlock ở thread pool ngây thơ | Work-stealing | Blumofe-Leiserson 1999 |
| Race condition RNG | Thread-local state | TLS (concurrency) |

Mỗi dòng trong bảng này độc lập là một "bài học kinh điển" trong khoa học máy tính — bài này giá trị chính là ở việc **ghép đúng thứ tự và đúng cách** các kỹ thuật đó lại với nhau mà không để chúng xung đột nhau (ví dụ: nếu không cẩn thận, việc thêm song song hóa có thể phá vỡ tính đúng đắn của depth-limit, hoặc work-stealing có thể phá vỡ tính an toàn luồng của RNG nếu không dùng TLS).