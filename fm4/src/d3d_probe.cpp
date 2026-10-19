// D3D library probe for the native-renderer work (docs/NATIVE_RENDERER.md).
//
// Every recompiled function `sub_X` is a weak alias of `__imp__sub_X`, so a
// strong definition here replaces it everywhere (direct calls and the dispatch
// table) while `__imp__sub_X` still runs the original translation. These
// wrappers only observe: they count calls per frame and log the first few
// argument registers, then forward to the original code. With the cvar off
// they cost one relaxed load per call.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <string>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(fm4_d3d_probe, false, "Forza Motorsport 4",
                    "Log per-frame call counts and sample arguments for FM4's D3D library "
                    "functions (native renderer research)");
REXCVAR_DEFINE_INT32(fm4_d3d_probe_interval, 300, "Forza Motorsport 4",
                     "Frames between D3D probe summaries");

namespace {

// Probed functions. Roles are hypotheses from static analysis; the probe is
// how they get confirmed or rejected.
#define FM4_D3D_PROBE_LIST(X)                                                   \
  X(8237F598, Swap)                                                          \
  X(8233A718, DrawAuto_A)                                                    \
  X(8234CD90, DrawAuto_B)                                                    \
  X(82310B30, DrawIndexed_A)                                                 \
  X(8234D600, DrawIndexed_B)                                                 \
  X(826FBFA0, InternalDraw)                                                  \
  X(823488E0, Resolve)                                                       \
  X(82343170, ShaderCommit)                                                  \
  X(823430C0, LoadAluConstants)                                              \
  X(82310928, FlushShaderConstants)                                          \
  X(82310FA8, FlushRenderState)                                              \
  X(8230F4F0, FlushFetchConstants)                                           \
  X(823614E8, SetRenderTargetState)                                          \
  X(822B9AC0, IndirectBuffer)                                                \
  X(822DA868, KickOff)                                                       \
  X(822FE9C8, MakeSpace)                                                     \
  X(822A8108, FenceA)                                                        \
  X(822C1DD8, FenceB)                                                        \
  X(822A7F40, OcclusionQuery)

enum ProbeId : size_t {
#define X(addr, name) k##name,
  FM4_D3D_PROBE_LIST(X)
#undef X
      kProbeCount
};

constexpr const char* kProbeNames[kProbeCount] = {
#define X(addr, name) #name,
    FM4_D3D_PROBE_LIST(X)
#undef X
};

constexpr uint32_t kProbeAddresses[kProbeCount] = {
#define X(addr, name) 0x##addr,
    FM4_D3D_PROBE_LIST(X)
#undef X
};

constexpr uint32_t kSampleCallsPerFunction = 4;

std::array<std::atomic<uint64_t>, kProbeCount> g_window_calls{};
std::array<std::atomic<uint32_t>, kProbeCount> g_samples_logged{};
std::atomic<uint64_t> g_window_frames{0};

inline bool ProbeEnabled() { return REXCVAR_GET(fm4_d3d_probe); }

void SampleArguments(size_t id, const PPCContext& ctx) {
  if (g_samples_logged[id].fetch_add(1, std::memory_order_relaxed) >= kSampleCallsPerFunction) {
    return;
  }
  REXLOG_INFO(
      "d3d probe sample {} ({:08X}): lr={:08X} r3={:08X} r4={:08X} r5={:08X} r6={:08X} "
      "r7={:08X} r8={:08X}",
      kProbeNames[id], kProbeAddresses[id], uint32_t(ctx.lr), ctx.r3.u32, ctx.r4.u32,
      ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32);
}

void LogWindowSummary() {
  uint64_t frames = g_window_frames.exchange(0, std::memory_order_relaxed);
  if (!frames) {
    return;
  }
  std::string line;
  for (size_t i = 0; i < kProbeCount; ++i) {
    uint64_t calls = g_window_calls[i].exchange(0, std::memory_order_relaxed);
    line += fmt::format(" {}={:.1f}", kProbeNames[i], double(calls) / double(frames));
  }
  REXLOG_INFO("d3d probe per-frame over {} frames:{}", frames, line);
}

inline void OnCall(size_t id, const PPCContext& ctx) {
  if (!ProbeEnabled()) {
    return;
  }
  g_window_calls[id].fetch_add(1, std::memory_order_relaxed);
  SampleArguments(id, ctx);
  if (id == kSwap) {
    int32_t interval = REXCVAR_GET(fm4_d3d_probe_interval);
    if (g_window_frames.fetch_add(1, std::memory_order_relaxed) + 1 >=
        uint64_t(interval > 0 ? interval : 300)) {
      LogWindowSummary();
    }
  }
}

}  // namespace

#define X(addr, name)                                    \
  REX_EXTERN(__imp__sub_##addr);                         \
  extern "C" REX_FUNC(sub_##addr) {                      \
    OnCall(k##name, ctx);                                \
    __imp__sub_##addr(ctx, base);                        \
  }
FM4_D3D_PROBE_LIST(X)
#undef X
