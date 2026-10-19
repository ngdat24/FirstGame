# HUST Monopoly — Tóm tắt cập nhật v1.6

## 1. Nâng cấp sau khi cướp Viện

- Cướp thành công Viện D: được chọn nâng lên C hoặc B, hoặc bỏ qua.
- Cướp thành công Viện C: được chọn nâng lên B, hoặc bỏ qua.
- Cướp thành công Viện B: được chọn nâng lên A, hoặc bỏ qua.
- Cướp thành công Viện A: đổi chủ, giữ nguyên bậc A và tổng vốn đầu tư; kết thúc xử lý ô ngay, không mở lựa chọn nâng A+.
- Chủ sở hữu có thể nâng Viện A lên A+ khi đáp lại vào Viện của mình hoặc dịch chuyển tới đó qua ô #20.
- Viện A+ vẫn không thể bị cướp. Các lần nâng cấp vẫn phải đáp ứng điều kiện tiền mặt.

## 2. Bản đồ mới

| Ô | Loại / tên | Thông số |
|---|---|---|
| #4 | Đấu Giá — Quảng Trường C1 | Giá đầu 50.000, phí ghé 25.000 |
| #13 | Đấu Giá — Hội Trường C2 | Giá đầu 50.000, phí ghé 25.000 |
| #25 | Đấu Giá — Thư Viện Tạ Quang Bửu | Giá đầu 50.000, phí ghé 25.000 |
| #34 | Đấu Giá — Sân Vận Động Bách Khoa | Giá đầu 50.000, phí ghé 25.000 |
| #5 | Viện Cơ Sở | Giá gốc 120.000, chi phí mỗi bậc 60.000, tiền thuê cơ sở 12.000 |
| #15, #35 | Ô rút thẻ | Giữ chức năng rút thẻ |

Bàn cờ gồm 30 Viện, 4 ô Đấu Giá, 2 ô rút thẻ và 4 ô góc. Vốn đầu của cả 4 người chơi vẫn bằng nhau: **1.000.000 VND/người**.

## 3. Cách hoạt động của ô Đấu Giá

- **Chưa có chủ:** người đáp vào được mua theo giá hiện tại nếu đủ tiền, hoặc bỏ qua.
- **Ô của mình:** kết thúc xử lý ô, không trả phí và không nâng cấp.
- **Ô của đối thủ:** trả phí ghé cho chủ hiện tại trước, rồi được chọn mua đè nếu tiền mặt còn lại đủ trả giá hiện tại.
- Phí ghé mặc định **25.000 VND**, có áp dụng hệ số `rentPercent` và được miễn khi Rent Holiday đang có hiệu lực.
- Nếu thiếu tiền trả phí, người chơi phải thanh lý như khi nợ tiền thuê. Sau khi trả đủ nợ, quyền mua đè vẫn được mở nếu còn đủ tiền.
- Tiền mua mới hoặc mua đè nộp vào **ngân hàng**; chủ cũ chỉ nhận phí ghé.
- Mỗi lần mua thành công tăng giá tiếp theo thêm **20.000 VND**: 50.000 → 70.000 → 90.000 → …
- Tổng vốn đầu tư của ô được thay bằng đúng số tiền chủ mới vừa trả.
- Không thể cướp hoặc nâng bậc điểm trên ô Đấu Giá.
- Thanh lý nhận **50% vốn lần mua gần nhất**; ô trở thành vô chủ nhưng **giữ nguyên giá mua hiện tại**.
- Ô #20 chỉ cho dịch chuyển tới ô Đấu Giá của chính mình, trả phí quản lý mặc định 20.000 VND; không tăng giá ô.
- Mua mới hoặc mua đè để sở hữu đủ **4/4 ô Đấu Giá sẽ thắng ngay**, kể cả khi đang có lượt đổ đôi hoặc chưa hết tempo.

## 4. Code, protocol và giao diện

- `tile.h`, `board_data.h`, `GameState.h`: thêm loại ô Đấu Giá, bố trí bàn cờ và cấu hình giá/phí; bổ sung thông tin tiếp tục xử lý sau khi thanh toán nợ.
- `GameLogic.h`, `GameLogic.cpp`: triển khai mua đè, tăng giá, thanh lý, dịch chuyển, đếm ô Đấu Giá, thắng tức thì và giới hạn nâng cấp sau cướp A.
- `protocol.cpp`: xuất loại ô `auction`, số Viện và số ô Đấu Giá của mỗi người; action mua Đấu Giá có `cost` theo giá hiện tại và không cần `target_grade`.
- `main.cpp`: bot ưu tiên mua ô Đấu Giá thứ tư để thắng nếu đủ tiền, bỏ qua yêu cầu tiền dự phòng trong trường hợp này; các lần mua khác vẫn giữ dự phòng.
- `simulation_metrics.h`: thống kê số Viện theo thứ tự lượt không tính lẫn ô Đấu Giá.
- `play.cpp`: hỗ trợ hiển thị và mô tả thao tác với ô Đấu Giá trong client dòng lệnh.
- `godot/main.gd`: ô Đấu Giá có màu vàng, nhãn `[ĐG]`, giá hiện tại; inspector và nút hành động hỗ trợ mua đè; bảng người chơi hiển thị `ĐG: X/4`.
- `tests.cpp`, `tests_protocol.cpp`: cập nhật tình huống theo bản đồ mới và bổ sung kiểm tra luật v1.6.

## 5. Kết quả kiểm thử

Đã build toàn bộ bằng `build.ps1 -Target all`, sau đó chạy:

| Kiểm tra | Kết quả |
|---|---|
| `tests.exe` | 2.058/2.058 checks đạt |
| `tests_protocol.exe --quick` | 11.316/11.316 checks đạt |
| `sim.exe --games 500 --seed 12345` | Hoàn tất 500/500 ván |
| Hành động bot không hợp lệ | 0 |
| Lỗi invariant | 0 |
| Ván chạm giới hạn hành động | 0 |

Một số số liệu tham khảo từ mẫu 500 ván:

- Tempo kết thúc trung bình: **45,65**.
- Số người bị đuổi trung bình: **0,58/4**; **48,8%** số ván có người bị đuổi.
- Tổng giá trị tài sản cuối/đầu: **×2,10**; riêng lượng tiền mặt cuối/đầu: **×0,38**.
- Tỷ lệ thắng theo thứ tự lượt: **29,2% / 25,8% / 22,8% / 22,2%**.

Đây là mẫu kiểm tra nhanh, chưa đủ để kết luận cân bằng hoặc công bằng theo thứ tự lượt. Giao diện Godot đã được chỉnh mã nhưng chưa chạy kiểm thử UI. Chưa commit, push, tạo PR hoặc merge.
