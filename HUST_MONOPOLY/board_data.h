#pragma once

#include <string>
#include <vector>

#include "tile.h"

// Placeholder 40-tile board. Layout:
//   0 Start | 10 Retake Exam (jail) | 20 Special Corner | 30 Academic Warning
//   4, 13, 25, 34 = Auction; 15, 35 = cards; remaining 30 = Institutes
// Prices, upgrade costs and rents are PLACEHOLDERS to be tuned by simulation.
inline std::vector<Tile> makeBoard() {
  static const char* kNamed[] = {
      "School of Applied Mathematics and Informatics",
      "School of Engineering Physics",
      "Mechanical Workshop",
  };

  std::vector<Tile> tiles(40);
  int instituteIndex = 0;

  for (int i = 0; i < 40; ++i) {
    Tile& t = tiles[i];
    t.id = i;
    // Preserve prices/names of surviving Institutes from the original board.
    const int originalIndex = instituteIndex;
    if (i % 10 != 0 && i % 10 != 5) ++instituteIndex;

    if (i == 0) {
      t.type = TileType::Start;
      t.name = "Start";
    } else if (i == 10) {
      t.type = TileType::RetakeExam;
      t.name = "Retake Exam Room";
    } else if (i == 20) {
      t.type = TileType::SpecialCorner;
      t.name = "Academic Affairs Office";
    } else if (i == 30) {
      t.type = TileType::AcademicWarning;
      t.name = "Academic Warning";
    } else if (i == 4 || i == 13 || i == 25 || i == 34) {
      t.type = TileType::Auction;
      t.name = i == 4 ? "Quảng Trường C1" : i == 13 ? "Hội Trường C2" :
               i == 25 ? "Thư Viện Tạ Quang Bửu" : "Sân Vận Động Bách Khoa";
    } else if (i == 15 || i == 35) {
      t.type = TileType::Other;
      t.name = "Chance / Bad Luck (Cơ hội / Vận xui)";
    } else {
      t.type = TileType::Institute;
      t.name = i == 5 ? "Viện Cơ Sở" : (originalIndex < 3) ? kNamed[originalIndex]
                                    : "Institute " + std::to_string(originalIndex + 1);
      t.basePrice = i == 5 ? 120000 : 100000 + 10000 * originalIndex;
      t.upgradeCost = t.basePrice / 2;
      t.baseRent = t.basePrice / 10;                   // rent at grade D
    }
  }
  return tiles;
}
