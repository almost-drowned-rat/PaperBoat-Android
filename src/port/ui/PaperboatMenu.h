#pragma once

#include <libultraship/libultraship.h>
#include "UIWidgets.hpp"
#include "Menu.h"
#include <fast/backends/gfx_rendering_api.h>
#include "port/ui/cvar_prefixes.h"
#include "port/ui/enhancementTypes.h"

#ifdef __ANDROID__
// The card menu's accent colours (MenuCards.cpp kAccents, same order); PaperBoat's own is Gold (2).
static const std::unordered_map<int32_t, const char*> menuAccentOptions = {
    { 0, "Tomato" }, { 1, "Orange" }, { 2, "Gold" },   { 3, "Green" }, { 4, "Teal" },
    { 5, "Blue" },   { 6, "Violet" }, { 7, "Pink" },   { 8, "Gray" },
};
#endif

namespace PaperboatGui {
class PaperboatMenu : public Ship::Menu {
  public:
    PaperboatMenu(const std::string& consoleVariable, const std::string& name);

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
    void Draw() override;

    void AddSidebarEntry(std::string sectionName, std::string sidbarName, uint32_t columnCount);
    WidgetInfo& AddWidget(WidgetPath& pathInfo, std::string widgetName, WidgetType widgetType);
    void AddMenuSettings();
    void AddMenuEnhancements();
    void AddMenuShaderSettings();
    void AddMenuDevTools();

  private:
    char mGitCommitHashTruncated[8];
    bool mIsTaggedVersion;
};
} // namespace PaperboatGui
