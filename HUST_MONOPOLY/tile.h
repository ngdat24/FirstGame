#pragma once

#include <string>

enum class TileType {
  Institute,
  Start,
  SpecialCorner,    // "Academic Affairs Office"
  RetakeExam,       // jail
  AcademicWarning,  // sends the player to RetakeExam
  Other             // placeholder (future cards / events)
};

// Numeric order matters: None=0, D=1, C=2, B=3, A=4 (used to index rentFactor).
enum class Grade {
  None,
  D,
  C,
  B,
  A
};

struct Tile {
  int id = 0;  // invariant: tiles[i].id == i
  std::string name;
  TileType type = TileType::Other;

  // Static tile properties.
  int basePrice = 0;
  int upgradeCost = 0;  // cost of ONE grade step (D->C, C->B, B->A)
  int baseRent = 0;     // rent at grade D; higher grades multiply it

  // Mutable game state.
  int ownerId = -1;  // -1 = unowned
  Grade grade = Grade::None;
  int totalInvested = 0;  // basePrice + all upgrade costs paid
};
