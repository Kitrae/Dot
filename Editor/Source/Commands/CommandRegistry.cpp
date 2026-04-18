// =============================================================================
// Dot Engine - Command Registry Implementation
// =============================================================================

#include "CommandRegistry.h"

#include "Core/ECS/Entity.h"

#include <imgui.h>
#include <set>

namespace Dot
{

Entity CommandRegistry::DrawContextMenu(const std::string& category, World* world)
{
    Entity createdEntity = kNullEntity;

    // Build a tree structure from paths
    std::set<std::string> drawnSubmenus;
    std::string prefix = category.empty() ? "" : category + "/";

    for (const auto& [path, info] : m_Commands)
    {
        // Skip commands not in this category
        if (!prefix.empty() && path.find(prefix) != 0)
            continue;

        // Get the relative path (after category prefix)
        std::string relativePath = prefix.empty() ? path : path.substr(prefix.length());

        // Check if this is a submenu or direct item
        size_t slashPos = relativePath.find('/');

        if (slashPos != std::string::npos)
        {
            // This is a submenu - extract submenu name
            std::string submenuName = relativePath.substr(0, slashPos);
            std::string fullSubmenuPath = prefix + submenuName;

            // Only draw each submenu once
            if (drawnSubmenus.find(fullSubmenuPath) == drawnSubmenus.end())
            {
                drawnSubmenus.insert(fullSubmenuPath);

                if (ImGui::BeginMenu(submenuName.c_str()))
                {
                    // Recursively draw submenu
                    Entity subEntity = DrawContextMenu(fullSubmenuPath, world);
                    if (subEntity.IsValid())
                        createdEntity = subEntity;
                    ImGui::EndMenu();
                }
            }
        }
        else
        {
            // Direct menu item
            if (ImGui::MenuItem(info.name.c_str()))
            {
                Execute(info.menuPath, world, &createdEntity);
            }
        }
    }

    return createdEntity;
}

} // namespace Dot
