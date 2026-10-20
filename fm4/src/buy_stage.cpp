// ---------------------------------------------------------------------------
// Buy Cars list: highlighted car tracker (Forza-4-Evolution).
//
// The earlier 3D stage behind the Buy Cars list was removed (it stuttered, the
// game rebuilds the car model on every swap). The list is the stock 2D one again.
// What stays is the small tracker the used lot status box (used_lot_overlay.cpp)
// needs: which car is highlighted while the buy list is open.
//
//   * BuyStageMode (0x824EF21C, CCarSelect setup sub_824EF1E8, just after
//     `li r4,3`): the list screen is opening (r4 = mode, 3 = UI_NoCar, r31 = the
//     screen). Remembers the screen. The gift popup (flow 9) is ignored.
//   * BuyStageHighlight (0x824FC6D0, CCarSelect focus handler sub_824FC670,
//     r28 = highlighted car Id): remembers the highlighted car.
//   * BuyStageModeSet (0x824FFA48, mode setter entry): another screen opened,
//     the list is closed. The list's own call right after BuyStageMode is let through.
//
// G = *(0x82DC3828): G+16 is the flow value of the garage screens.
// ---------------------------------------------------------------------------
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <cstdint>

namespace {
constexpr uint32_t kGarageGlobal = 0x82DC3828;
constexpr uint32_t kSentinelCarId = 777777;  // list entries at or above are not cars

bool g_opening = false;    // the list's own mode change is about to happen
bool g_list_open = false;  // the buy list is on screen
uint32_t g_screen = 0;
uint32_t g_pending = 0;    // highlighted car

rex::memory::Memory* Mem() { return REX_KERNEL_MEMORY(); }
bool Readable(uint32_t a, uint32_t n) { return a >= 0x10000 && a < 0xF0000000u && a + n > a; }
uint32_t Load32(uint32_t a) {
  const uint8_t* p = Mem()->TranslateVirtual<const uint8_t*>(a);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
}  // namespace

// 0x824EF21C in sub_824EF1E8 (CCarSelect setup), just after `li r4,3`:
// r4 = screen mode about to be set, r31 = the screen.
void BuyStageMode(PPCRegister& r4, PPCRegister& r31) {
  uint32_t flow = 0xFFFFFFFF;
  const uint32_t G = Load32(kGarageGlobal);
  if (Readable(G + 16, 4)) flow = Load32(G + 16);
  g_list_open = flow != 9 && r4.u32 == 3;
  g_opening = g_list_open;
  g_screen = r31.u32;
  g_pending = 0;
}

// 0x824FC6D0 in sub_824FC670 (CCarSelect focus handler): r28 = highlighted
// car Id (list entries >= 777777 are not cars), r31 = the screen.
void BuyStageHighlight(PPCRegister& r28, PPCRegister& r31) {
  if (!g_list_open || r31.u32 != g_screen) return;
  const uint32_t id = r28.u32;
  if (id == 0 || id >= kSentinelCarId) return;
  g_pending = id;
}

// Highlighted car in the open buy list, 0 when none (used_lot_overlay.cpp).
uint32_t BuyStageHighlightedCar() { return g_list_open ? g_pending : 0; }

// 0x824FFA48 (mode setter, entry): another screen is opening. The list's own
// call is let through; any other call means the list is gone.
void BuyStageModeSet(PPCRegister& r4) {
  (void)r4;
  if (g_opening) { g_opening = false; return; }
  g_list_open = false;
}
