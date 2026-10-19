# Tóm tắt thay đổi HUST Monopoly ngày 20 tháng 10 năm 2026

Ghi chú: đây là bản tóm tắt đợt v1.4. Cơ chế chọn điểm mới ở v1.5 được mô tả trong
[TOM_TAT_THAY_DOI_DIEM_D_C_B.md](TOM_TAT_THAY_DOI_DIEM_D_C_B.md).

Theo yêu cầu cập nhật, vốn đầu mặc định đã đổi về **1.000.000 VND bằng nhau cho cả bốn người**.
Rules v1.4 bổ sung báo cáo simulator về thanh khoản, phát triển viện và rủi ro bị loại.
Thay đổi nằm trên máy local, chưa commit,
push, tạo PR hay merge; Copilot tiếp tục phụ trách các bước Git theo phân công của dự án.

## Thay đổi gameplay đã thực hiện

Vốn đầu bằng nhau, không phụ thuộc ghế hoặc thứ tự lượt:

| Lượt | Vốn đầu |
|---|---:|
| Thứ nhất | 1.000.000 VND |
| Thứ hai | 1.000.000 VND |
| Thứ ba | 1.000.000 VND |
| Thứ tư | 1.000.000 VND |

Tổng vốn vẫn là **4.000.000 VND**. Engine dùng cùng cách khởi tạo cho simulator, text client và
Godot qua HTTP. Sự kiện bắt đầu ván thông báo vốn đầu mỗi người, không còn mô tả bù lợi thế lượt đi.

Giữ bộ stat local đã có trước đợt sửa này: vốn trung bình 1 triệu, lương 200k, phí dịch chuyển
20k, rent D/C/B/A/A+ là 3/6/9/15/30, học bổng A/A+ là 150k/400k và trận kéo dài 48 tempos.
Các chỉnh sửa stat này vốn chưa commit; không coi chúng là thay đổi mới do riêng đợt này tạo ra.

## Sửa cách thử nghiệm và đọc kết quả

- `--rentpct` scale số tiền rent cuối cùng rồi làm tròn xuống đến VND, không cắt cụt hệ số bậc.
  Ví dụ viện có baseRent 10k, bậc D: 80% cho rent **24k**, thay vì **20k** như cách cũ.
- `--cash` đặt vốn đầu chung khi dùng mặc định `--orderstep 0`. Bù vốn theo lượt đã tắt mặc định;
  chỉ bật cho thử nghiệm khi truyền rõ `--orderstep N` lớn hơn 0.
- Thêm `--reserve N` và `--upgrade-first` để thử hành vi bot. Bot mặc định vẫn giữ dự phòng 100k
  và chính sách cũ. Chưa có giải đấu nhiều kiểu bot trong cùng một ván.
- Simulator in timestamp biên dịch, phiên bản rules, seed, config và chính sách bot ngay đầu báo cáo.
- Đổi nhãn gây hiểu nhầm `Money supply growth` thành `Net worth growth`; thêm `Cash supply growth`
  để tách tiền mặt khỏi vốn đã đầu tư vào viện.
- Thêm chênh lệch tỷ lệ thắng theo lượt và độ bất định xấp xỉ; theo dõi thời điểm bị đuổi đầu tiên,
  A/A+ đầu tiên, tỷ trọng tài sản của người giàu nhất và checkpoint mỗi phần tư trận.
- Checkpoint báo số người còn chơi, trung vị tiền mặt, tỷ lệ thiếu tiền so với một lần nâng cấp
  điển hình và số viện theo thứ tự lượt. Ngưỡng tham chiếu hiện là **127.500 VND**.
- Chạy invariant sau mọi action ở cả mô phỏng lớn; trước đây run trên 2.000 ván bỏ qua kiểm tra này.
- Tách ván hoàn thành khỏi ván chưa hoàn thành khi tính trung bình; từ chối tham số CLI không hợp lệ.
- Thêm `build.ps1` để build lại executable dễ hơn và tránh vô tình đánh giá bản cũ.

## Kết quả thử nghiệm trước khi đổi mặc định về vốn bằng nhau

Các báo cáo dưới đây là dữ liệu đã lưu trước lần đổi mặc định này: mỗi phương án **20.000 ván**,
seed **20261020**. Cột bù vốn hiện chỉ tái hiện khi truyền rõ `--orderstep 50000`:

| Chỉ số | Vốn bằng nhau, mặc định hiện tại | Bù vốn thử nghiệm, orderstep 50000 |
|---|---:|---:|
| Thắng lượt thứ nhất | 29,07% | 25,86% |
| Thắng lượt thứ hai | 26,12% | 24,83% |
| Thắng lượt thứ ba | 23,50% | 24,52% |
| Thắng lượt thứ tư | 21,30% | 24,79% |
| Chênh lệch lớn nhất | 7,77 điểm phần trăm | 1,34 điểm phần trăm |
| Tăng tổng tài sản | ×2,25 | ×2,25 |
| Ván có người bị đuổi | 40,89% | 41,27% |

Run v1.4 kiểm tra **7.460.189 trạng thái sau action**, không có action bot sai luật,
lỗi invariant, ván chưa hoàn thành hoặc ván chạm action cap.

Kết quả xác nhận cải thiện công bằng trong mô hình bot hiện tại, chưa chứng minh cân bằng cho
mọi chiến thuật hoặc người thật. Điểm nghẽn phát triển vẫn còn: A đầu tiên trung bình ở tempo
32,10 trong các ván đạt A; A+ đầu tiên ở tempo 38,05 trong các ván đạt A+.

Báo cáo gốc nằm tại:

- [Bản mặc định v1.4](HUST_MONOPOLY/balance_v1.4_20000.txt)
- [Baseline vốn bằng nhau](HUST_MONOPOLY/balance_baseline_20000.txt)
- [Fuzz 100 ván](HUST_MONOPOLY/balance_fuzz_100.txt)

## Kiểm thử hồi quy

Sau lần đổi mặc định về vốn bằng nhau, đã chạy lại test luật, protocol, simulator và mô phỏng 1.000 ván:
vốn khởi tạo là `1000000/1000000/1000000/1000000`, không có action bot sai luật hoặc lỗi invariant.
Các kết quả fuzz, HTTP và Godot ghi bên dưới thuộc lần triển khai trước khi đổi mặc định.

- Rules: **1.625/1.625 checks pass**.
- Protocol quick: **12.635/12.635 checks pass**, bao gồm vốn đầu trên JSON snapshot và thông tin rules.
- Simulator: **7/7 bài regression pass**, gồm 15 trường hợp tham số sai, replay, scale rent,
  override vốn và kiểm tra invariant trên run 2.001 ván.
- Fuzz 100 ván: **110.593 đối chiếu legal/apply**, 0 mismatch và 0 rejected-but-changed-state.
- HTTP end-to-end: **28/28 checks pass**, gồm chơi hết ván, replay và 12 client đồng thời.
- Godot 4.7.2 headless: **smoke test PASS** sau 334 actions, đến tempo 48; **UI test PASS** sau
  366 bước, qua đủ các pha chờ đổ, chọn ô, chọn dịch chuyển và thanh lý; đã kiểm tra điều khiển cược.
  Kiểm tra chạy trên bản sao dự án chỉ đổi URL tới một server riêng, không ghi vào Godot project gốc.

HTTP test cần chạy ngoài sandbox vì sandbox chặn kết nối localhost với WinError 10013.
Đây là giới hạn môi trường kiểm thử, không phải lỗi engine. Chưa chạy sanitizers trong đợt này.

## Tài liệu và file chính

- `GameState.h`, `GameLogic.h/.cpp`: cấu hình, khởi tạo vốn theo lượt, scale rent.
- `protocol.cpp`: các trường rules bổ sung; snapshot/action API vẫn dùng protocol v1.
- `main.cpp`, `simulation_metrics.h`: tham số và báo cáo cân bằng.
- `tests.cpp`, `tests_protocol.cpp`, `test_simulator.py`: regression tests.
- `build.ps1`: build simulator, tests, server và text client trên Windows MinGW.
- `RULES.md`, `PROTOCOL.md`, `README.md`: đồng bộ với code hiện tại, sửa các thông số cũ.
- [BALANCE.md](HUST_MONOPOLY/BALANCE.md): định nghĩa chỉ số, mục tiêu tạm thời, cách tái hiện và
  đánh giá preset. Không tự chấm điểm chất lượng cho các phần chưa có dữ liệu người thật.

Không chỉnh sửa `godot/project.godot` hoặc `steal-bonus-expiry.patch` đã có thay đổi từ trước.
Không ghi đè file Word đặc tả trong Downloads.

## Phần chưa triển khai và bước tiếp theo

Chưa thêm quyền nâng cấp mỗi học kỳ, chưa đổi sức mạnh A+, chưa thêm bốn ô Đấu Giá và chưa tăng
lương lên 250k. Đây là các phương án cần thí nghiệm riêng, đặc biệt vì mở cơ hội nâng có thể
khuếch đại rent, học bổng và khóa cướp của A+.

Claude review thay đổi luật và kiến trúc; Gemini cùng Codex tiếp tục phân tích/code theo yêu cầu.
Copilot có thể dùng bản tóm tắt và các báo cáo này để chuẩn bị commit, PR và merge sau review.

## Chạy trên máy

Trong PowerShell:

```powershell
Set-Location 'C:\Users\lenovo\Documents\du_an\HUST_MONOPOLY'
.\build.ps1 -Target sim
.\sim.exe --games 10000 --seed 12345
```

Để kiểm tra đủ các thành phần:

```powershell
.\build.ps1 -Target all
.\tests.exe
.\tests_protocol.exe --quick
python test_simulator.py
python test_server.py .\server.exe
```

Muốn chơi bằng Godot cần dùng `server.exe` đã build lại, vì luật nằm trong server.
Dừng server đang chạy trước khi build lại `server.exe`, vì Windows khóa executable đang sử dụng.
