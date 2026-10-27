BÀI 3: chạy trên mỗi máy của nhóm
Giải nén vào thư mục riêng. Cài Python 3, Matplotlib và compiler C++17 (g++).
Tại thư mục Bai3, chạy: python -m pip install matplotlib
Sau đó chạy: python run.py 500000 5
Nếu g++ không có trong PATH, đặt biến môi trường CXX thành đường dẫn tới compiler.
Runner sẽ kiểm tra tính đúng, thử dọn dẹp luồng, đo (mất khoảng 15-20 phút) và phân tích dữ liệu.
Đóng Excel/trình soạn thảo đang mở counter.csv trước khi chạy.
Gửi lại counter.csv, counter_study.log, environment.json, scalability_summary.csv,
scalability.png; kèm tên máy và thành viên. Không đổi nội dung dữ liệu bằng tay.