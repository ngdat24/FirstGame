#pragma once

#include <string>

enum class TileType {
  Institute,
  Auction,
  Start,
  SpecialCorner,    // "Academic Affairs Office"
  RetakeExam,       // jail
  AcademicWarning,  // sends the player to RetakeExam
  Other             // placeholder (future cards / events)
};

// Numeric order matters: None=0, D=1, C=2, B=3, A=4, A+ = 5(used to index rentFactor).
enum class Grade {
  None,
  D,
  C,
  B,
  A,
  APlus
};

struct Tile {
  int id = 0;  // invariant: tiles[i].id == i
  std::string name;
  TileType type = TileType::Other;

  // Static tile properties.
  long long basePrice = 0;  // Auction: current price for the next purchase
  int upgradeCost = 0;  // cost of ONE grade step, D through A+
  long long baseRent = 0;     // multiplied by the grade factor and rentPercent

  // Mutable game state.
  int ownerId = -1;  // -1 = unowned
  Grade grade = Grade::None;
  long long totalInvested = 0;  // Institute: all investment; Auction: last purchase price
};
