# Lựa chọn mua và nâng điểm D C B trong HUST Monopoly

Đã triển khai rules **v1.5**: người chơi chọn mức đầu tư D/C/B thay vì luôn phải mua D rồi chờ
nâng từng bậc. A và A+ vẫn đòi hỏi các bước B→A và A→A+ riêng biệt. Vốn đầu mặc định tiếp tục
bằng nhau ở **1.000.000 VND mỗi người**. Thay đổi nằm local, chưa commit, push, PR hoặc merge.

## Nhận xét về cơ chế

Điểm thay đổi chính là người chơi có thể chủ động mua chất lượng viện ngay khi có cơ hội.
Mua D giữ thanh khoản; mua C/B tạo rent cao sớm nhưng dùng nhiều tiền hơn. Đường tới A/A+ vẫn
phải có thêm cơ hội đáp ô hoặc dịch chuyển, vì một lần xử lý ô không được nối nhiều quyết định nâng.

Tăng khả năng đầu tư không tự bảo đảm giảm nghèo: mô phỏng cho thấy bot luôn mua bậc cao nhất
gặp rủi ro phá sản lớn hơn. Vì vậy không tự đổi rent, vốn đầu, học bổng hoặc bù vốn theo lượt.
Cần đánh giá cả lựa chọn của người chơi thay vì chỉ nhìn tốc độ lên điểm.

## Luật đã thực hiện

| Trạng thái viện khi được quyền đầu tư | Các lựa chọn |
|---|---|
| Chưa có chủ | Mua D, C, B hoặc bỏ qua |
| Đang sở hữu D | Nâng C, nâng B hoặc giữ D |
| Đang sở hữu C | Nâng B hoặc giữ C |
| Đang sở hữu B | Nâng A hoặc giữ B |
| Đang sở hữu A | Nâng A+ hoặc giữ A |
| Đang sở hữu A+ | Không nâng thêm |

Với viện đối thủ, người ghé vẫn phải **trả rent hoặc cướp**. Không thể trả tiền nâng trực tiếp
cho viện đối thủ mà chưa giành quyền sở hữu. Nếu cướp thành công, viện giữ điểm và vốn đầu tư
cũ, rồi người vừa cướp được quyền chọn nâng theo bảng hoặc giữ nguyên. Tiền còn lại sau cược
quyết định các lựa chọn nâng được phép. Nếu không đủ nâng, kết thúc xử lý ô như bình thường.

Trả rent hoặc cướp thất bại không mở lựa chọn nâng. A+ tiếp tục không thể bị cướp.
Tiền cược thành công vẫn vào ngân hàng; cược thất bại vẫn chuyển cho chủ viện.

## Giá và giới hạn mỗi lần xử lý ô

Giữ nguyên giá gốc P và phí một bậc U của viện:

| Lựa chọn | Giá |
|---|---:|
| Mua D | P |
| Mua C | P + U |
| Mua B | P + 2U |
| D→C | U |
| D→B | 2U |
| C→B, B→A, A→A+ | U |

Ví dụ viện giá 200k, phí nâng 100k: mua D/C/B lần lượt là **200k/300k/400k**.
D→B tốn **200k**, sau đó phải đợi một cơ hội xử lý ô mới để B→A. Không được mua A/A+ từ ô trống,
không được D/C→A/A+, không hạ điểm hoặc trả một bậc để nhận hai bậc.

Một lần xử lý ô là một lần đáp ô hoặc lựa chọn dịch chuyển, không phải toàn bộ lượt đổ xúc xắc.
Sau khi hoàn tất đầu tư, xúc xắc đôi vẫn cho một lần di chuyển mới theo luật cũ.

Ô #20 dùng cùng lựa chọn điểm, cộng đúng một lần phí dịch chuyển **20k**. Di chuyển tới viện
A+ của mình vẫn chỉ tốn phí này. Phí dịch chuyển và tiền cược không được tính vào `totalInvested`;
chỉ giá mua và chi phí nâng thực tế được tính. Thanh lý vẫn nhận 50% vốn đầu tư.

## Luồng sau khi cướp

```text
Đáp viện đối thủ
  → Trả rent: kết thúc xử lý ô
  → Cướp thất bại: trả tiền cược cho chủ, kết thúc xử lý ô
  → Cướp thành công: đổi chủ, giữ nguyên điểm/vốn cũ
      → Có tiền nâng: chọn một bậc đích hợp lệ hoặc giữ nguyên
      → Không có tiền nâng: giữ nguyên
      → Sau đó mới xét đi tiếp vì đôi, chuyển lượt, học bổng hoặc kết thúc trận
```

Ví dụ viện C có giá gốc 200k, vốn đã đầu tư 300k: cược 450k đạt 100% khi không có bonus.
Người bắt đầu với 1 triệu còn 550k sau khi thắng; có thể trả thêm 100k lên B, còn 450k.
Vốn viện sau đó là **400k**, không phải 850k; tiền cược không làm tăng giá trị đầu tư.

## Thay đổi code và giao diện

- `GameLogic.h/.cpp`: thêm bậc đích, hàm tính giá chung, liệt kê lựa chọn đúng ngân sách và mở
  lựa chọn nâng sau khi cướp. `buyTile`/`upgradeTile` kiểm tra đường nâng trước khi trừ tiền.
- `GameState.h`: rules version 1.5; các stat và vốn đầu được giữ nguyên.
- `protocol.cpp`: trường tùy chọn `target_grade`; mỗi legal action đầu tư có `cost` do server tính.
  Yêu cầu sai bậc hoặc thiếu tiền bị từ chối mà không đổi trạng thái. Client gửi giá giả không đổi giá thật.
- `godot/main.gd`: nút mua/nâng ghi rõ bậc đích và tổng giá; lựa chọn #20 gồm phí dịch chuyển.
- `godot/engine_client.gd`: chuyển tiếp `target_grade` đúng nút được chọn.
- `play.cpp`: text client hiện bậc đích, tổng giá và gửi đúng lựa chọn.
- `main.cpp`: bot và fuzz hiểu nhiều lựa chọn cùng loại action; fuzz cả bậc hợp lệ lẫn không hợp lệ.
- `RULES.md`, `PROTOCOL.md`, `README.md`, `BALANCE.md`: đồng bộ luật, request và cách thử nghiệm.

Không thêm phase mới. Viện cướp được đã thuộc người hiện tại nên tiếp tục dùng
`await_landing_choice` với `upgrade`/`decline`. Mỗi lựa chọn xong đều kết thúc xử lý ô, tránh nâng nối tiếp.

Ví dụ request:

```json
{"cmd":"action","type":"buy","player_id":0,"target_grade":"B"}
```

Protocol vẫn là v1; trường mới bổ sung tương thích. Bỏ `target_grade` vẫn mua D hoặc nâng một bậc
như request cũ. Giao diện mới lấy giá và lựa chọn từ server, không tự quyết định luật nâng.

## Kiểm thử đã chạy

| Kiểm tra | Kết quả |
|---|---|
| Rules tests | **2.034/2.034 checks pass** |
| Protocol quick | **11.729/11.729 checks pass** |
| Simulator regression | **7/7 tests pass** |
| Fuzz 100 ván, bot mặc định | **124.155** đối chiếu legal/apply, **0** mismatch, **0** rejected-but-changed-state |
| Fuzz 100 ván, highest-grade | **101.364** đối chiếu legal/apply, **0** mismatch, **0** rejected-but-changed-state |
| HTTP end-to-end | **28/28 checks pass** |
| Godot smoke test | **PASS**, hoàn tất ván sau 294 actions |
| Godot UI test | **PASS**, 360 bước, đã qua cả pha thanh lý và kiểm tra cược |
| Godot grade-choice test mới | **PASS**, bấm nút D/C/B thật, kiểm tra điểm/tiền/vốn từ server |
| Text client auto, seed 7 | Hoàn tất ván thành công |

Regression bao gồm các đường nâng bị cấm, thiếu tiền sau cược, mua/nâng liên tiếp bị chặn,
giá #20, tổng vốn đầu tư, lượt đi tiếp vì đôi, lựa chọn nâng ở lượt cuối cùng của trận và
học bổng chỉ được phát sau khi người cướp hoàn tất lựa chọn nâng.

Test Godot chạy headless trên bản sao dự án chỉ đổi URL tới server kiểm tra riêng;
không ghi vào project Godot gốc. HTTP/Godot chạy ngoài sandbox vì sandbox chặn localhost.
Không chạy sanitizers trong đợt này.

## Hai cách chơi bot để so sánh

Bot mặc định mua bậc thấp nhất và nâng một bậc, giữ dự phòng 100k như trước. Nó vẫn có thể
nâng sau khi cướp vì đây là quyền mới của luật. `--highest-grade` chọn bậc cao nhất có thể
trả mà vẫn giữ dự phòng; cờ này chỉ ảnh hưởng bot trong simulator, không ép lựa chọn của người chơi.

Mỗi phương án chạy **10.000 ván**, seed **12345**, không có action sai luật hoặc lỗi invariant:

| Chỉ số | Bot mặc định | Bot highest-grade |
|---|---:|---:|
| Ván có người bị đuổi | 44,71% | 96,61% |
| Tổng tài sản cuối / đầu | ×2,28 | ×2,98 |
| Số viện A/A+ cuối trận | 1,97 | 6,75 |
| Tempo A đầu tiên, trong các ván đạt A | 30,88 | 8,76 |
| Tempo A+ đầu tiên, trong các ván đạt A+ | 37,25 | 23,91 |

Đây là kết quả của các bàn gồm bốn bot cùng chính sách, không phải dự báo chắc chắn về người thật.
Cả quyền đầu tư lẫn chiến thuật dùng tiền ảnh hưởng kết quả. Chưa có giải đấu bot nhiều chính sách
trong cùng một ván. Không tự đổi stat hoặc bật bù vốn để làm đẹp các chỉ số này.

Báo cáo:

- [Bot mặc định](HUST_MONOPOLY/balance_v1.5_10000.txt)
- [Bot highest-grade](HUST_MONOPOLY/balance_v1.5_highest_grade_10000.txt)
- [Fuzz mặc định](HUST_MONOPOLY/grade_choice_fuzz_100.txt)

## Cách chạy

Executables đã được build lại. Khi mở Godot, cần khởi động server mới và tạo ván mới:

```powershell
Set-Location 'C:\Users\lenovo\Documents\du_an\HUST_MONOPOLY'
.\server.exe --dev
```

Mở `godot/project.godot` rồi F5. Muốn tự build, dừng server trước khi chạy:

```powershell
.\build.ps1 -Target all
.\tests.exe
.\tests_protocol.exe --quick
python test_simulator.py
.\sim.exe --games 10000 --seed 12345
.\sim.exe --games 10000 --seed 12345 --highest-grade
```

Bản tóm tắt này phục vụ Claude/Gemini review và Copilot chuẩn bị commit/PR/merge theo phân công.
