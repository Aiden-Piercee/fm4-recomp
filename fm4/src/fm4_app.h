// fm4 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/ui/imgui_dialog.h>

#include <memory>

class Fm4App : public rex::ReXApp {
public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp>
  Create(rex::ui::WindowedAppContext &ctx) {
    return std::unique_ptr<Fm4App>(new Fm4App(ctx, "fm4", PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  void OnPostSetup() override;
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override;
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  void OnShutdown() override { overheat_hud_.reset(); evo_hud_.reset(); mods_overlay_.reset(); stagger_hud_.reset(); rim_picker_.reset(); }  // before the ImGui drawer goes
  // void OnConfigurePaths(rex::PathConfig& paths) override {}

private:
  // Engine temperature / oil pressure readout (overheat.cpp).
  std::unique_ptr<rex::ui::ImGuiDialog> overheat_hud_;
  // Evolution progress readout (dev_keys.cpp).
  std::unique_ptr<rex::ui::ImGuiDialog> evo_hud_;
  // OPTIONS & MODS main-menu overlay (mods_menu.cpp).
  std::unique_ptr<rex::ui::ImGuiDialog> mods_overlay_;
  // Rear rim readout in the upgrade shop (staggered_wheels.cpp).
  std::unique_ptr<rex::ui::ImGuiDialog> stagger_hud_;
  // Rim Style picker in the upgrade shop (rim_picker.cpp).
  std::unique_ptr<rex::ui::ImGuiDialog> rim_picker_;
};
