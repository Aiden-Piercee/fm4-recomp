// ---------------------------------------------------------------------------
// Engine builder: state shared by engine_builder.cpp (database, physics) and builder_ui.cpp (overlay).
// Design: docs/ENGINE_BUILDER.md.
// ---------------------------------------------------------------------------
#pragma once

#include "catalogue.h"
#include "dyno.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace eb {

// The player's current garage car as the builder sees it. Copied out under a lock (Snapshot()).
struct CarState {
  int garage_id = 0;          // Career_Garage.Id, 0 = unknown
  int car_id = 0;             // Data_Car.Id
  int year = 0;               // model year of the car
  int engine_id = 0;          // Data_Engine.EngineID of the fitted engine (an engine swap counts)
  bool info_ok = false;       // engine row read from the database
  bool build_ok = false;      // build rows read from the database
  int32_t parts[kNumSlots] = {};  // fitted PartId per slot, 0 = stock
  // FM4 data of the fitted engine
  double stock_redline = 0.0;     // stock camshaft RedlineRPM
  double fitted_redline = 0.0;    // fitted camshaft RedlineRPM (an FM4 cam upgrade raises it)
  double curve_max_rpm = 0.0;     // fitted camshaft TorqueCurveMaxRPM (end of the stock curve)
  double fm4_upgrade_scale = 1.0; // product of the fitted FM4 engine upgrade TorqueScales (divided out)
  bool fm4_cam_upgraded = false;  // a non-stock FM4 camshaft is fitted (its curve shape is not divided out)
  bool fm4_fi_upgraded = false;   // a non-stock FM4 turbo / supercharger is fitted (the builder blower is ignored)
};

// What the physics hook uses: torque ratio table and the rev limit, rebuilt when the car or build changes.
struct LiveBuild {
  bool active = false;            // a build with at least one part, for an engine in the catalogue
  double rpm_max = 0.0;
  static constexpr int kPoints = DynoResult::kPoints;
  float ratio[kPoints] = {};      // already divided by fm4_upgrade_scale and clamped
  float limiter_rpm = 0.0f;       // 0 = leave the stock limiter
};

// Loads fm4_engine_parts.tsv once (search order in engine_builder.cpp).
void LoadCatalogueOnce(const std::filesystem::path& user_root);
const Catalogue* GetCatalogue();                 // nullptr until loaded
std::string CatalogueSource();                   // file it came from, or a reason it is missing
CarState Snapshot();
bool BuilderEnabled();                           // cvar fm4_engine_builder and FM4_EVO / FM4_ENGINE_BUILDER env
bool CareerAllowed();                            // cvar fm4_engine_builder_career
void SetCareerAllowed(bool on);
bool LastRaceWasCareer();                        // the current / last race is a career event (see the doc)
int DecadeYearLimit();                           // newest year whose parts are on sale (Evolution decade + 9), 0 = any

// Fits a part (0 = back to stock) on the current garage car: updates the state now, saves to the profile.
void FitPart(int slot, int32_t part_id);
// Removes the whole build from the current garage car.
void ClearBuild();

// Model results for the overlay (cached; recomputed on change). parts = nullptr means the fitted build.
DynoResult RunFor(const StockEngine& e, const int32_t (*parts)[kNumSlots]);

}  // namespace eb

// ---- entry points called from other files (global names, no namespace) ------------------------------------------------
// part_tune.cpp: the player's garage car changed (or was confirmed, about every 2 s).
void EngineBuilderSetGarage(int garage_id);
// overheat.cpp: the engine substep saw a new player car object.
void EngineBuilderSetPlayerCar(uint32_t car_phys);
// True when the player car has a build that applies in the current event (the part tune then keeps only wear).
bool EngineBuilderActive(uint32_t car_phys);
// overheat.cpp: torque multiplier at this rpm for this car (1 when no build applies).
float EngineBuilderTorqueRatio(uint32_t car_phys, float rpm);
// overheat.cpp: rev limiter for this car in rad/s, or 0 to leave it. stock_rad_s is the limiter the game set.
float EngineBuilderLimiter(uint32_t car_phys, float stock_rad_s);
