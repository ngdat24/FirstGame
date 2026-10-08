#pragma once

#include <string>

enum class TileType {
  Institute,
  Start,
  SpecialCorner,
  RetakeExam,
  Other
};

enum class Grade {
  None,
  D,
  C,
  B,
  A
};

struct Tile {
  int id;
  std::string name;
  TileType type;

  // Static tile properties.
  int basePrice = 0;
  int upgradeCost = 0;
  int baseRent = 0;

  // Mutable game state.
  int ownerId = -1;
  Grade grade = Grade::None;
  int totalInvested = 0;
};
#ifndef TILE_H
#define TILE_H
#include <string>
using namespace std;

enum class TileType {Institute, Start, SpecialCorner, RetakeExam, Other};
enum class Grade {None, D, C, B, A};

struct Tile{
  int id;
  string name;
  TileType type;

  // các chỉ số tĩnh
  int basePrice = 0;
  int upgradeCost = 0;  
  int baseRent = 0;

  // các trạng thái động
  int OwnerId = -1;
  Grade grade = Grade::None;
  int TotalInvested = 0; // tiền đưa để thi hộ
};

#endif