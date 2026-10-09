#pragma once

#include <string>
#include <vector>

#include "tile.h"

// Placeholder 40-tile board. Layout:
//   0 Start | 10 Retake Exam (jail) | 20 Special Corner | 30 Academic Warning
//   5, 15, 25, 35 = "Other" slots reserved for cards/events later
//   everything else = Institute (32 of them)
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
    } else if (i % 10 == 5) {
      t.type = TileType::Other;
      t.name = "Event (placeholder)";
    } else {
      t.type = TileType::Institute;
      t.name = (instituteIndex < 3) ? kNamed[instituteIndex]
                                    : "Institute " + std::to_string(instituteIndex + 1);
      t.basePrice = 100000 + 10000 * instituteIndex;  // 100k .. 410k
      t.upgradeCost = t.basePrice / 2;
      t.baseRent = t.basePrice / 10;                   // rent at grade D
      ++instituteIndex;
    }
  }
  return tiles;
}
