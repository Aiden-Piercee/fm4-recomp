// ---------------------------------------------------------------------------
// Engine builder: parts catalogue (Forza-4-Evolution). Format: docs/ENGINE_BUILDER.md, "Catalogue format".
// Pure C++17 like dyno.h, so the host test can parse the shipped file.
// ---------------------------------------------------------------------------
#pragma once

#include "dyno.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace eb {

enum Slot : int { kHeads = 0, kCam, kValvetrain, kPistons, kRotating, kIntake, kCarb, kHeaders, kExhaust, kIgnitionSlot, kNumSlots };
const char* SlotKey(int slot);    // "heads", "cam", ...
const char* SlotName(int slot);   // "Cylinder heads", "Camshaft", ...
int SlotFromKey(const std::string& key);  // -1 if unknown

// One modifier: field op value, op is '=' (set), '+' (add) or '*' (multiply).
struct Mod {
  std::string key;
  char op = '=';
  double value = 0.0;
};

struct Family {
  std::string id, name;
};

struct StockEngine {
  int32_t engine_id = 0;
  std::string family, name;
  std::vector<Mod> mods;  // applied to a default EngineSpec
};

struct Part {
  int32_t id = 0;
  int slot = -1;
  std::vector<std::string> families;  // empty = fits every family ("*")
  std::string brand, name;
  int year = 0;
  int price = 0;
  double mass_kg = 0.0;
  std::vector<Mod> mods;
  bool Fits(const std::string& family) const;
};

struct Catalogue {
  std::map<std::string, Family> families;
  std::map<int32_t, StockEngine> engines;
  std::map<int32_t, Part> parts;
  std::vector<std::string> errors;  // one line per rejected row (file:line: reason)

  const Part* FindPart(int32_t id) const;
  const StockEngine* FindEngine(int32_t engine_id) const;
  // Parts for a slot that fit a family and are on sale by `year` (0 = any year), sorted by year then name.
  std::vector<const Part*> PartsFor(int slot, const std::string& family, int year) const;
};

// Parses a catalogue file. Returns false if the file could not be opened; bad rows go to cat.errors.
bool LoadCatalogue(const std::string& path, Catalogue& cat);
bool ParseCatalogue(const std::string& text, const std::string& source_name, Catalogue& cat);

// Applies modifiers to a spec. Unknown keys are ignored (and counted in *unknown if given).
void ApplyMods(EngineSpec& spec, const std::vector<Mod>& mods, int* unknown = nullptr);

// Stock spec for an engine, and the spec with the given parts fitted (parts[slot] = PartId, 0 = stock).
EngineSpec StockSpec(const StockEngine& e);
EngineSpec BuildSpec(const Catalogue& cat, const StockEngine& e, const int32_t (&parts)[kNumSlots]);

}  // namespace eb
