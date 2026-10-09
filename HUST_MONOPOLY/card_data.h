#pragma once

#include "card.h"

// The shared deck drawn on the "Other" tiles (5, 15, 25, 35).
// Titles/descriptions are flavour text: rewrite them freely, only
// effectType and value matter to the rules. Amounts are in VND and are
// PLACEHOLDERS to be tuned with the simulator.
inline CardDeck makeCardDeck() {
  CardDeck d;
  auto add = [&d](const char* title, const char* description, CardEffectType type,
                  long long value) {
    Card c;
    c.id = static_cast<int>(d.cards.size()) + 1;
    c.title = title;
    c.description = description;
    c.effectType = type;
    c.value = value;
    d.cards.push_back(c);
  };

  // --- Anti-snowball cards -------------------------------------------------
  add("Thanh tra đồ án đột xuất",
      "Nộp 30.000 VND x bậc điểm cho mỗi Institute đang sở hữu (D=1, C=2, B=4, A=8).",
      CardEffectType::MoneyPerProperty, 30000);
  add("Quỹ khuyến học cựu sinh viên",
      "Người có tổng tài sản thấp nhất bàn nhận 300.000 VND.",
      CardEffectType::PovertySubsidy, 300000);
  add("Cúp điện toàn trường",
      "Không ai phải trả tiền thuê trong một vòng tiếp theo.",
      CardEffectType::RentHoliday, 0);
  add("Phao thi chất lượng cao",
      "Lần thi hộ kế tiếp của bạn được cộng 25% tỷ lệ thành công.",
      CardEffectType::StealBuff, 25);

  // --- Tempo / jail cards --------------------------------------------------
  add("Quên điểm danh giải tích",
      "Nhận cảnh cáo học tập: vào phòng Thi lại.",
      CardEffectType::GoToJail, 0);
  add("Đi chơi với bạn gái quên học",
      "Lượt kế tiếp của bạn bị bỏ qua.",
      CardEffectType::SkipTurn, 0);
  add("Đi học bù ca 4 kẹt xe",
      "Lần di chuyển kế tiếp chỉ đi được một nửa số bước (làm tròn xuống).",
      CardEffectType::HalveNextDice, 0);

  // --- Money cards ---------------------------------------------------------
  add("Đóng tiền học phí kỳ phụ",
      "Nộp 150.000 VND học phí.",
      CardEffectType::MoneyFlat, -150000);
  add("Ăn phải quán cổng phụ đau bụng",
      "Tiền viện phí và thuốc: nộp 100.000 VND.",
      CardEffectType::MoneyFlat, -100000);
  add("Mất vé xe ở thư viện Tạ Quang Bửu",
      "Nộp phạt 50.000 VND.",
      CardEffectType::MoneyFlat, -50000);
  add("Ăn cơm tiệm gần cổng Parabol trúng thưởng",
      "Nhận 100.000 VND tiền mặt.",
      CardEffectType::MoneyFlat, 100000);

  return d;
}
