# Hướng dẫn chạy Bài 2 (Barrier) trên máy của bạn

Mục tiêu: chạy test và benchmark trên máy của bạn, rồi gửi lại **một file kết quả** `results_<tên>.tar.gz`.
Toàn bộ chỉ cần **một lệnh**, mất khoảng 10–20 phút.

## Bước 1: Cài trình biên dịch (chỉ làm một lần)

**Windows**

1. Mở PowerShell, chạy: `winget install MSYS2.MSYS2`
   (hoặc tải bản cài từ https://www.msys2.org/).
2. Mở Start Menu → **MSYS2 UCRT64** (đúng tên này).
3. Trong cửa sổ đó, chạy lệnh sau và gõ `Y` khi được hỏi:
   ```bash
   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libgomp
   ```

**macOS**: mở Terminal, chạy `xcode-select --install`.

**Linux**: `sudo apt install g++`

## Bước 2: Chuẩn bị máy

- Cắm sạc (với laptop) và chọn chế độ nguồn **Best performance**.
- Tắt các chương trình nặng (game, trình duyệt nhiều tab, ...).
- Không dùng máy trong lúc chạy.

## Bước 3: Chạy

Giải nén `ex2_barrier.zip`. Mở terminal (Windows: cửa sổ **MSYS2 UCRT64**) và `cd` vào thư mục vừa giải nén.
Ví dụ, nếu bạn giải nén ra `D:\Downloads\ex2_barrier`:

```bash
cd /d/Downloads/ex2_barrier
./run_all.sh platform2_trang      # thay bằng tên của bạn, không dấu, không khoảng trắng
```

Nếu gặp lỗi `Permission denied`, chạy `bash run_all.sh platform2_trang`.

Script sẽ tự động:
1. build chương trình;
2. ghi lại cấu hình máy (CPU, RAM, hệ điều hành, compiler);
3. chạy test tính đúng đắn: mọi dòng phải là **PASS**;
4. chạy các barrier bị lỗi: các dòng **FAIL/HANG** ở bước này là **đúng như mong đợi**;
5. chạy benchmark;
6. chạy benchmark OpenMP ở 2 chế độ chờ.

Có thể sẽ có lúc màn hình đứng yên vài phút (khi số luồng bằng số nhân). Đó là bình thường, cứ để chạy tiếp.

## Bước 4: Gửi kết quả

Khi thấy dòng `Done. Send this file back: ...`, gửi lại file **`results_<tên>.tar.gz`** nằm trong thư mục `ex2_barrier`.

## Nếu gặp lỗi

| Lỗi | Cách sửa |
|---|---|
| `g++: command not found` | Chưa cài compiler, hoặc đang không mở đúng cửa sổ **MSYS2 UCRT64** |
| `cannot find -lgomp` | Chưa cài `mingw-w64-ucrt-x86_64-libgomp` (xem Bước 1) |
| `note: ... cannot build with -fopenmp` (macOS) | Bình thường: Apple clang không có OpenMP, phần OpenMP được bỏ qua |
| Có dòng `FAIL` ở bước 3 | Gửi ngay file kết quả kèm ảnh chụp màn hình |
