// =============================================================================
// Dot Engine - Project Settings Sections
// =============================================================================

#pragma once

#include "../Panels/SettingsPanel.h"

#include <string_view>

namespace Dot::ProjectSettingsSections
{

bool DrawCategoryWithTitle(SettingsPanel::Category category, std::string_view searchQuery, bool& settingsDirty);

} // namespace Dot::ProjectSettingsSections
