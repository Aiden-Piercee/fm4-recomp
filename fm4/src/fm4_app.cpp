#include "generated/default/fm4_init.h"

#include "fm4_app.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xcontent.h>

REXCVAR_DEFINE_STRING(
    fm4_disc2_content_root, "", "Forza Motorsport 4",
    "Directory containing legally obtained FM4 Disc 2 content packages");
REXCVAR_DEFINE_BOOL(fm4_auto_install_disc2_content, true, "Forza Motorsport 4",
                    "Install validated FM4 Disc 2 content packages at startup");
REXCVAR_DEFINE_STRING(
    fm4_dlc_content_root, "", "Forza Motorsport 4",
    "Directory containing legally obtained FM4 downloadable content packages "
    "(searched recursively; any FM4 marketplace STFS package is accepted)");
REXCVAR_DEFINE_BOOL(fm4_auto_install_dlc_content, true, "Forza Motorsport 4",
                    "Install validated FM4 downloadable content packages at "
                    "startup");

namespace {

constexpr uint32_t kFm4TitleId = 0x4D530910;
constexpr std::string_view kDisc2DropDirectory = "disc2-content";
constexpr std::string_view kDlcDropDirectory = "dlc-content";

// Which filter a source folder uses. Disc 2 folders only accept the four
// known Disc 2 package names; DLC folders accept any file whose STFS header
// identifies it as FM4 marketplace content (DLC files have 42-character
// hash names that differ per pack).
enum class SourceKind { kDisc2, kDlc };

struct SourceDirectory {
  std::filesystem::path path;
  SourceKind kind;
};

// Cheap pre-check so unrelated files in a DLC folder (readme, .nfo, images)
// are skipped silently instead of being reported as rejected packages.
bool HasStfsMagic(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  std::array<char, 4> magic{};
  if (!file.read(magic.data(), magic.size())) {
    return false;
  }
  const std::string_view tag(magic.data(), magic.size());
  return tag == "LIVE" || tag == "PIRS" || tag == "CON ";
}

bool IsDisc2PackageName(std::string_view name) {
  return name == "4d53091000000001" || name == "4d53091000000002" ||
         name == "4d53091000000003" || name == "4d53091000000004";
}

std::string Hex8(uint32_t value) {
  std::ostringstream stream;
  stream << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
         << value;
  return stream.str();
}

std::string EnvironmentRoot(const char *name) {
#ifdef _WIN32
  char *value = nullptr;
  size_t length = 0;
  if (_dupenv_s(&value, &length, name) != 0 || !value) {
    return {};
  }
  std::string root(value);
  std::free(value);
  return root;
#else
  const char *value = std::getenv(name);
  return value ? value : "";
#endif
}

std::filesystem::path
InstalledContentPath(const std::filesystem::path &user_data_root,
                     const std::filesystem::path &package_path) {
  return user_data_root / "0000000000000000" / Hex8(kFm4TitleId) / "00000002" /
         package_path.filename();
}

std::filesystem::path
InstalledHeaderPath(const std::filesystem::path &user_data_root,
                    const std::filesystem::path &package_path) {
  return user_data_root / "0000000000000000" / Hex8(kFm4TitleId) / "Headers" /
         "00000002" / (package_path.filename().string() + ".header");
}

bool IsInstalled(const std::filesystem::path &user_data_root,
                 const std::filesystem::path &package_path) {
  return std::filesystem::is_directory(
             InstalledContentPath(user_data_root, package_path)) &&
         std::filesystem::is_regular_file(
             InstalledHeaderPath(user_data_root, package_path));
}

std::vector<SourceDirectory>
DiscoverSourceDirectories(const std::filesystem::path &app_root,
                          const std::filesystem::path &user_data_root) {
  std::vector<SourceDirectory> directories;
  auto add_directory = [&](std::filesystem::path directory, SourceKind kind) {
    if (directory.empty()) {
      return;
    }
    std::error_code ec;
    directory = std::filesystem::absolute(directory, ec);
    if (ec) {
      return;
    }
    for (const auto &existing : directories) {
      if (existing.kind == kind &&
          std::filesystem::equivalent(existing.path, directory, ec)) {
        return;
      }
      ec.clear();
    }
    directories.push_back({std::move(directory), kind});
  };

  if (REXCVAR_GET(fm4_auto_install_disc2_content)) {
    add_directory(EnvironmentRoot("FM4_DISC2_ROOT"), SourceKind::kDisc2);
    add_directory(std::string(REXCVAR_GET(fm4_disc2_content_root)),
                  SourceKind::kDisc2);
    add_directory(app_root / kDisc2DropDirectory, SourceKind::kDisc2);
    add_directory(user_data_root / kDisc2DropDirectory, SourceKind::kDisc2);
  }

  if (REXCVAR_GET(fm4_auto_install_dlc_content)) {
    add_directory(EnvironmentRoot("FM4_DLC_ROOT"), SourceKind::kDlc);
    add_directory(std::string(REXCVAR_GET(fm4_dlc_content_root)),
                  SourceKind::kDlc);
    add_directory(app_root / kDlcDropDirectory, SourceKind::kDlc);
    add_directory(user_data_root / kDlcDropDirectory, SourceKind::kDlc);
  }
  return directories;
}

} // namespace

void Fm4App::OnPostSetup() {
  if ((!REXCVAR_GET(fm4_auto_install_disc2_content) &&
       !REXCVAR_GET(fm4_auto_install_dlc_content)) ||
      !runtime() ||
      !runtime()->kernel_state() ||
      !runtime()->kernel_state()->content_manager()) {
    return;
  }

  const uint32_t title_id = runtime()->kernel_state()->title_id();
  if (title_id != kFm4TitleId) {
    REXLOG_WARN("Skipping FM4 content install; running title ID is {:08X}",
                title_id);
    return;
  }

  const auto user_data_root = runtime()->user_data_root();
  const auto source_directories = DiscoverSourceDirectories(
      rex::filesystem::GetExecutableFolder(), user_data_root);
  std::unordered_set<std::string> seen_packages;
  size_t installed_count = 0;
  size_t existing_count = 0;
  size_t rejected_count = 0;

  for (const auto &source : source_directories) {
    const auto &source_directory = source.path;
    const bool is_dlc = source.kind == SourceKind::kDlc;
    const char *kind_label = is_dlc ? "DLC" : "Disc 2";
    if (!std::filesystem::is_directory(source_directory)) {
      continue;
    }
    REXLOG_INFO("Scanning FM4 {} folder: {}", kind_label,
                source_directory.string());

    std::error_code iter_ec;
    std::filesystem::recursive_directory_iterator iterator(
        source_directory,
        std::filesystem::directory_options::skip_permission_denied, iter_ec);
    const std::filesystem::recursive_directory_iterator end;
    while (!iter_ec && iterator != end) {
      const auto entry = *iterator;
      iterator.increment(iter_ec);
      if (!entry.is_regular_file()) {
        continue;
      }
      if (is_dlc ? !HasStfsMagic(entry.path())
                 : !IsDisc2PackageName(entry.path().filename().string())) {
        continue;
      }

      const auto package_path = entry.path();
      std::error_code canonical_ec;
      auto package_key =
          std::filesystem::weakly_canonical(package_path, canonical_ec)
              .string();
      if (canonical_ec) {
        package_key = std::filesystem::absolute(package_path).string();
      }
      // Also de-duplicate by file name: the install location is keyed by it,
      // so the same pack found in two folders must only be installed once.
      if (!seen_packages.insert(package_key).second ||
          !seen_packages.insert("name:" + package_path.filename().string())
               .second) {
        continue;
      }

      const auto header =
          rex::filesystem::StfsContainerDevice::ReadPackageHeader(package_path);
      if (!header) {
        REXLOG_WARN("Rejecting FM4 {} package with invalid STFS header: {}",
                    kind_label, package_path.string());
        ++rejected_count;
        continue;
      }

      const auto content_type =
          static_cast<rex::system::XContentType>(header->metadata.content_type);
      const uint32_t package_title_id =
          header->metadata.execution_info.title_id;
      if (content_type != rex::system::XContentType::kMarketplaceContent ||
          package_title_id != kFm4TitleId) {
        REXLOG_WARN("Rejecting {} package {} (type {:08X}, title {:08X})",
                    kind_label, package_path.filename().string(),
                    static_cast<uint32_t>(content_type), package_title_id);
        ++rejected_count;
        continue;
      }

      if (IsInstalled(user_data_root, package_path)) {
        REXLOG_INFO("FM4 {} package already installed: {}", kind_label,
                    package_path.filename().string());
        ++existing_count;
        continue;
      }

      REXLOG_INFO("Installing FM4 {} package: {}", kind_label,
                  package_path.string());
      const auto result =
          runtime()->kernel_state()->content_manager()->InstallContent(
              package_path);
      if (result == 0) {
        ++installed_count;
        REXLOG_INFO("Installed FM4 {} package: {}", kind_label,
                    package_path.filename().string());
      } else {
        ++rejected_count;
        REXLOG_WARN("Failed to install FM4 {} package {}: {:08X}",
                    kind_label, package_path.filename().string(),
                    static_cast<uint32_t>(result));
      }
    }
    if (iter_ec) {
      REXLOG_WARN("Could not finish scanning FM4 {} folder {}: {}",
                  kind_label, source_directory.string(), iter_ec.message());
    }
  }

  REXLOG_INFO(
      "FM4 content scan complete: {} installed, {} already present, {} "
      "rejected",
      installed_count, existing_count, rejected_count);
}

// Engine temperature / oil pressure readout above the speedometer (overheat.cpp).
std::unique_ptr<rex::ui::ImGuiDialog> CreateOverheatHud(rex::ui::ImGuiDrawer* drawer);

// OPTIONS & MODS main-menu overlay (mods_menu.cpp).
std::unique_ptr<rex::ui::ImGuiDialog> CreateModsOverlay(
    rex::ui::ImGuiDrawer* drawer, const std::filesystem::path& user_root);

// Evolution progress readout, top left (dev_keys.cpp).
std::unique_ptr<rex::ui::ImGuiDialog> CreateEvolutionHud(rex::ui::ImGuiDrawer* drawer);

// Rear rim readout in the upgrade shop (staggered_wheels.cpp).
std::unique_ptr<rex::ui::ImGuiDialog> CreateStaggerHud(rex::ui::ImGuiDrawer* drawer);

// Rim Style picker overlay (rim_picker.cpp).
std::unique_ptr<rex::ui::ImGuiDialog> CreateRimPicker(rex::ui::ImGuiDrawer* drawer,
                                                      const std::filesystem::path& user_root);

void Fm4App::OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) {
  stagger_hud_ = CreateStaggerHud(drawer);
  if (runtime()) rim_picker_ = CreateRimPicker(drawer, runtime()->user_data_root());
  overheat_hud_ = CreateOverheatHud(drawer);
  evo_hud_ = CreateEvolutionHud(drawer);
  if (runtime()) mods_overlay_ = CreateModsOverlay(drawer, runtime()->user_data_root());
}
