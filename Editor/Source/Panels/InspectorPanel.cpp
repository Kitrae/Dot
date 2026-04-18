// =============================================================================
// Dot Engine - Inspector Panel Implementation
// =============================================================================

#include "InspectorPanel.h"
#include "PanelChrome.h"

#include "Core/Assets/AssetManager.h"
#include "Core/ECS/World.h"
#include "Core/Gameplay/HealthComponent.h"
#include "Core/Input/PlayerInputComponent.h"
#include "Core/Log.h"
#include "Core/Navigation/NavAgentComponent.h"
#include "Core/Physics/BoxColliderComponent.h"
#include "Core/Physics/CharacterControllerComponent.h"
#include "Core/Physics/CollisionLayers.h"
#include "Core/Physics/RigidBodyComponent.h"
#include "Core/Physics/SphereColliderComponent.h"
#include "Core/Scene/AttachmentResolver.h"
#include "Core/Scene/CameraComponent.h"
#include "Core/Scene/Components.h"
#include "Core/Scene/LightComponent.h"
#include "Core/Scene/MaterialComponent.h"
#include "Core/Scene/MeshComponent.h"
#include "Core/Scene/ScriptComponent.h"
#include "Core/Scene/SkyboxComponent.h"

#include "../Commands/EntityClipboard.h"
#include "../Commands/CommandRegistry.h"
#include "../Toolbox/ToolboxManager.h"
#include "Utils/FbxImportUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <unordered_set>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <commdlg.h>

namespace Dot
{

namespace
{

void PushInspectorSectionStyle()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.17f, 0.20f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.23f, 0.29f, 0.37f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.28f, 0.36f, 0.46f, 1.0f));
}

void PopInspectorSectionStyle()
{
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

std::string BuildCollisionMaskSummary(uint32 mask)
{
    auto& layers = CollisionLayers::Get();
    const uint32 activeMask = layers.GetActiveLayerMask();
    const uint32 effectiveMask = activeMask != 0 ? (mask & activeMask) : mask;

    if (effectiveMask == 0)
        return "Nothing";
    if (activeMask != 0 && effectiveMask == activeMask)
        return "Everything";

    int count = 0;
    uint8 lastLayer = 0;
    for (uint8 i = 0; i < CollisionLayers::kMaxLayers; ++i)
    {
        if ((effectiveMask & CollisionLayers::LayerBit(i)) != 0)
        {
            lastLayer = i;
            ++count;
        }
    }

    if (count == 1)
        return layers.GetLayerDisplayName(lastLayer);

    return std::to_string(count) + " layers";
}

bool DrawCollisionLayerRow(const char* comboId, uint8& collisionLayer, const char* previewOverride = nullptr)
{
    auto& layers = CollisionLayers::Get();
    const std::string preview = previewOverride ? previewOverride : layers.GetLayerDisplayName(collisionLayer);
    bool changed = false;

    if (ImGui::BeginCombo(comboId, preview.c_str()))
    {
        for (uint8 i = 0; i < CollisionLayers::kMaxLayers; ++i)
        {
            std::string label = layers.GetLayerDisplayName(i);
            if (!layers.IsLayerActive(i))
                label += " (unused)";

            const bool isSelected = (collisionLayer == i);
            if (ImGui::Selectable(label.c_str(), isSelected))
            {
                collisionLayer = i;
                changed = true;
            }

            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    return changed;
}

bool DrawCollisionMaskRow(const char* comboId, uint32& collisionMask, const char* previewOverride = nullptr)
{
    auto& layers = CollisionLayers::Get();
    const std::string preview = previewOverride ? previewOverride : BuildCollisionMaskSummary(collisionMask);
    bool changed = false;
    if (ImGui::BeginCombo(comboId, preview.c_str()))
    {
        if (ImGui::Button("All"))
        {
            collisionMask = CollisionLayers::kAllLayersMask;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("None"))
        {
            collisionMask = 0;
            changed = true;
        }

        ImGui::Separator();

        for (uint8 i = 0; i < CollisionLayers::kMaxLayers; ++i)
        {
            const bool showLayer = layers.IsLayerActive(i) || (collisionMask & CollisionLayers::LayerBit(i)) != 0;
            if (!showLayer)
                continue;

            bool enabled = (collisionMask & CollisionLayers::LayerBit(i)) != 0;
            std::string checkboxLabel = layers.GetLayerDisplayName(i) + "##MaskLayer" + std::to_string(i);
            if (ImGui::Checkbox(checkboxLabel.c_str(), &enabled))
            {
                if (enabled)
                    collisionMask |= CollisionLayers::LayerBit(i);
                else
                    collisionMask &= ~CollisionLayers::LayerBit(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    return changed;
}

struct EntityCollisionSettingsState
{
    bool hasAny = false;
    bool mixedLayer = false;
    bool mixedMask = false;
    uint8 layer = 0;
    uint32 mask = CollisionLayers::kAllLayersMask;
    int componentCount = 0;
};

EntityCollisionSettingsState CollectEntityCollisionSettings(const BoxColliderComponent* box,
                                                            const SphereColliderComponent* sphere,
                                                            const CharacterControllerComponent* cc)
{
    EntityCollisionSettingsState state;

    auto consume = [&](uint8 layer, uint32 mask)
    {
        if (!state.hasAny)
        {
            state.hasAny = true;
            state.layer = layer;
            state.mask = mask;
        }
        else
        {
            state.mixedLayer |= (state.layer != layer);
            state.mixedMask |= (state.mask != mask);
        }
        ++state.componentCount;
    };

    if (box)
        consume(box->collisionLayer, box->collisionMask);
    if (sphere)
        consume(sphere->collisionLayer, sphere->collisionMask);
    if (cc)
        consume(cc->collisionLayer, cc->collisionMask);

    return state;
}

void ApplyEntityCollisionLayer(uint8 layer, BoxColliderComponent* box, SphereColliderComponent* sphere,
                               CharacterControllerComponent* cc)
{
    if (box)
        box->collisionLayer = layer;
    if (sphere)
        sphere->collisionLayer = layer;
    if (cc)
        cc->collisionLayer = layer;
}

void ApplyEntityCollisionMask(uint32 mask, BoxColliderComponent* box, SphereColliderComponent* sphere,
                              CharacterControllerComponent* cc)
{
    if (box)
        box->collisionMask = mask;
    if (sphere)
        sphere->collisionMask = mask;
    if (cc)
        cc->collisionMask = mask;
}

template <typename T>
void AddComponentToEntity(World& world, Entity entity)
{
    world.AddComponent<T>(entity);
}

struct AddComponentEntry
{
    const char* label;
    const char* keywords;
    bool available;
    void (*addFn)(World&, Entity);
};

std::string ToLowerCopy(const char* text)
{
    if (!text)
        return {};

    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

bool MatchesAddComponentSearch(const AddComponentEntry& entry, const char* search)
{
    if (!search || search[0] == '\0')
        return true;

    const std::string searchLower = ToLowerCopy(search);
    const std::string haystack = ToLowerCopy((std::string(entry.label) + " " + entry.keywords).c_str());
    return haystack.find(searchLower) != std::string::npos;
}

std::string GetEntityDisplayName(World& world, Entity entity)
{
    if (!entity.IsValid() || !world.IsAlive(entity))
        return "(None)";

    if (NameComponent* name = world.GetComponent<NameComponent>(entity); name && !name->name.empty())
        return name->name;

    return "Entity " + std::to_string(entity.GetIndex());
}

const char* GetAttachmentTargetModeName(AttachmentTargetMode mode)
{
    switch (mode)
    {
        case AttachmentTargetMode::Entity:
            return "Entity";
        case AttachmentTargetMode::ActiveCamera:
            return "Active Camera";
        default:
            return "Unknown";
    }
}

std::string GetAttachmentAxisMaskSummary(uint8 mask)
{
    if (mask == AttachmentAxisMask::None)
        return "None";

    std::string summary;
    if ((mask & AttachmentAxisMask::X) != 0)
        summary += "X";
    if ((mask & AttachmentAxisMask::Y) != 0)
        summary += "Y";
    if ((mask & AttachmentAxisMask::Z) != 0)
        summary += "Z";
    return summary;
}

std::string BuildAttachmentSocketLabel(World& world, Entity socketEntity, const std::string& socketName)
{
    if (socketName.empty())
        return "Root";

    const std::string ownerName = GetEntityDisplayName(world, socketEntity);
    return socketName + " [" + ownerName + "]";
}

bool DrawAxisMaskControl(const char* idPrefix, uint8& mask)
{
    bool changed = false;
    const struct AxisEntry
    {
        const char* label;
        uint8 bit;
    } axisEntries[] = {{"X", AttachmentAxisMask::X}, {"Y", AttachmentAxisMask::Y}, {"Z", AttachmentAxisMask::Z}};

    for (int i = 0; i < 3; ++i)
    {
        if (i > 0)
            ImGui::SameLine();

        bool enabled = (mask & axisEntries[i].bit) != 0;
        std::string checkboxId = std::string(axisEntries[i].label) + "##" + idPrefix;
        if (ImGui::Checkbox(checkboxId.c_str(), &enabled))
        {
            if (enabled)
                mask |= axisEntries[i].bit;
            else
                mask &= ~axisEntries[i].bit;
            changed = true;
        }
    }

    ImGui::SameLine();
    if (ImGui::SmallButton((std::string("All##") + idPrefix).c_str()))
    {
        mask = AttachmentAxisMask::XYZ;
        changed = true;
    }

    ImGui::SameLine();
    if (ImGui::SmallButton((std::string("None##") + idPrefix).c_str()))
    {
        mask = AttachmentAxisMask::None;
        changed = true;
    }

    return changed;
}

void CollectAttachmentSockets(World& world, Entity root, std::vector<std::pair<Entity, std::string>>& sockets,
                              std::unordered_set<uint32>& visited)
{
    if (!root.IsValid() || !world.IsAlive(root) || !visited.insert(root.id).second)
        return;

    if (AttachmentPointComponent* point = world.GetComponent<AttachmentPointComponent>(root))
    {
        if (!point->socketName.empty())
            sockets.emplace_back(root, point->socketName);
    }

    if (HierarchyComponent* hierarchy = world.GetComponent<HierarchyComponent>(root))
    {
        for (Entity child : hierarchy->children)
            CollectAttachmentSockets(world, child, sockets, visited);
    }
}

bool DrawAddComponentEntry(const AddComponentEntry& entry, const char* search, World& world, Entity entity,
                           bool& matchedAnything)
{
    if (!MatchesAddComponentSearch(entry, search))
        return false;

    matchedAnything = true;
    if (ImGui::MenuItem(entry.label, nullptr, false, entry.available))
    {
        entry.addFn(world, entity);
        return true;
    }

    return false;
}

bool DrawDisabledModuleSection(const char* label, const char* moduleName)
{
    if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    ImGui::TextColored(ImVec4(0.95f, 0.73f, 0.28f, 1.0f), "Disabled by Toolbox");
    ImGui::TextWrapped("%s is currently disabled. Existing data is preserved, but editing and runtime behavior are unavailable until the module is re-enabled.", moduleName);
    return true;
}

bool DrawDisabledModuleComponent(const char* label, const char* moduleName, bool* removeComponent)
{
    if (removeComponent)
        *removeComponent = false;

    if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    ImGui::TextColored(ImVec4(0.95f, 0.73f, 0.28f, 1.0f), "Disabled by Toolbox");
    ImGui::TextWrapped("%s is currently disabled. Existing data is preserved, but editing and runtime behavior are unavailable until the module is re-enabled.", moduleName);
    return true;
}

} // namespace

// Helper: Draw a component header with an optional removal button
// Returns true if the header is open (content should be drawn)
// Sets removeComponent to true if user clicked remove
static bool DrawComponentHeader(const char* label, bool* removeComponent, bool canRemove = true)
{
    ImGui::PushID(label);

    bool open = false;
    const float lineHeight = ImGui::GetFrameHeight();

    ImGuiTableFlags tableFlags =
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX;
    if (ImGui::BeginTable("##ComponentHeaderLayout", canRemove ? 2 : 1, tableFlags))
    {
        ImGui::TableSetupColumn("Header", ImGuiTableColumnFlags_WidthStretch);
        if (canRemove)
            ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, lineHeight + 4.0f);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        PushInspectorSectionStyle();
        open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
        PopInspectorSectionStyle();

        if (canRemove && ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Remove Component"))
            {
                *removeComponent = true;
            }
            ImGui::EndPopup();
        }

        if (canRemove)
        {
            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.20f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.24f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.52f, 0.28f, 0.31f, 1.0f));
            if (ImGui::Button("X##RemoveComponent", ImVec2(lineHeight, lineHeight)))
            {
                *removeComponent = true;
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Remove this component");
            }
        }

        ImGui::EndTable();
    }

    ImGui::PopID();
    return open;
}

// Helpers for two-column property layout
static bool BeginPropertyGrid(const char* label, float labelWidth = 120.0f)
{
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable(label, 2, flags))
    {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }
    return false;
}

static void EndPropertyGrid()
{
    ImGui::EndTable();
}

static void PropertyRow(const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushItemWidth(-1);
}

static void EndPropertyRow()
{
    ImGui::PopItemWidth();
}

void InspectorPanel::OnImGui()
{
    if (!m_Open)
        return;

    BeginChromeWindow(m_Name.c_str(), &m_Open);

    const bool inspectorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool canHandleShortcuts = inspectorFocused && !ImGui::IsAnyItemActive();
    auto notifySelectionChanged = [this](Entity entity)
    {
        m_SelectedEntity = entity;
        m_EditBaselineEntity = entity;
        m_HasEditBaseline = false;
        if (m_OnSelectionChanged)
            m_OnSelectionChanged(entity);
    };
    EntityClipboard& clipboard = GetEntityClipboard();
    auto hasSelection = [this]() { return m_World && m_SelectedEntity.IsValid() && m_World->IsAlive(m_SelectedEntity); };
    auto copySelectedEntity = [&](bool wasCut)
    {
        if (!hasSelection())
            return false;

        clipboard.entries = CreateClipboardEntries(*m_World, {m_SelectedEntity});
        clipboard.hasData = true;
        clipboard.wasCut = wasCut;
        return true;
    };
    auto deleteSelectedEntity = [&]()
    {
        if (!hasSelection())
            return false;

        auto cmd = std::make_unique<DeleteEntityCommand>(m_World, m_SelectedEntity, &m_SelectedEntity);
        CommandRegistry::Get().ExecuteCommand(std::move(cmd));
        notifySelectionChanged(m_SelectedEntity);
        return true;
    };
    auto pasteClipboardEntities = [&]()
    {
        if (!m_World || !clipboard.hasData || clipboard.entries.empty())
            return false;

        Entity pastedEntity = kNullEntity;
        std::vector<Entity> pastedEntities;
        auto cmd =
            std::make_unique<PasteEntitiesCommand>(m_World, clipboard.entries, &pastedEntities, &pastedEntity,
                                                   !clipboard.wasCut);
        CommandRegistry::Get().ExecuteCommand(std::move(cmd));
        if (!pastedEntity.IsValid())
            return false;

        notifySelectionChanged(pastedEntity);
        if (clipboard.wasCut)
            clipboard = {};
        return true;
    };
    auto duplicateSelectedEntity = [&]()
    {
        if (!hasSelection())
            return false;

        Entity duplicatedEntity = kNullEntity;
        std::vector<Entity> duplicatedEntities;
        auto cmd = std::make_unique<PasteEntitiesCommand>(m_World, CreateClipboardEntries(*m_World, {m_SelectedEntity}),
                                                          &duplicatedEntities, &duplicatedEntity, true);
        CommandRegistry::Get().ExecuteCommand(std::move(cmd));
        if (!duplicatedEntity.IsValid())
            return false;

        notifySelectionChanged(duplicatedEntity);
        return true;
    };
    auto cutSelectedEntity = [&]()
    {
        if (!copySelectedEntity(true))
            return false;
        return deleteSelectedEntity();
    };

    if (m_World && canHandleShortcuts && ImGui::GetIO().KeyCtrl)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_C, false))
            copySelectedEntity(false);

        if (ImGui::IsKeyPressed(ImGuiKey_X, false))
            cutSelectedEntity();

        if (ImGui::IsKeyPressed(ImGuiKey_V, false))
            pasteClipboardEntities();

        if (ImGui::IsKeyPressed(ImGuiKey_D, false))
            duplicateSelectedEntity();
    }

    if (canHandleShortcuts && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)))
        deleteSelectedEntity();

    if (!m_World)
    {
        ImGui::TextDisabled("No world assigned");
        ImGui::End();
        return;
    }

    if (!m_SelectedEntity.IsValid() || !m_World->IsAlive(m_SelectedEntity))
    {
        ImGui::TextDisabled("No entity selected");
        ImGui::End();
        return;
    }

    if (m_EditBaselineEntity != m_SelectedEntity)
    {
        m_EditBaselineEntity = m_SelectedEntity;
        m_HasEditBaseline = false;
    }

    const EntityComponentSnapshot preFrameSnapshot = CaptureEntitySnapshot(*m_World, m_SelectedEntity);
    bool editedThisFrame = false;
    auto markEdited = [&](bool changed)
    {
        editedThisFrame |= changed;
        return changed;
    };

    // ---- Name Component ----
    NameComponent* name = m_World->GetComponent<NameComponent>(m_SelectedEntity);
    if (name)
    {
        ImGui::BeginChild("InspectorEntityHeader", ImVec2(0, 62), true);
        ImGui::TextDisabled("NODE");
        ImGui::Text("%s", name->name.c_str());
        if (ImGui::BeginPopupContextWindow("InspectorEntityContext"))
        {
            if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection()))
                copySelectedEntity(false);
            if (ImGui::MenuItem("Cut", "Ctrl+X", false, hasSelection()))
                cutSelectedEntity();
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, clipboard.hasData && !clipboard.entries.empty()))
                pasteClipboardEntities();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection()))
                duplicateSelectedEntity();
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del", false, hasSelection()))
                deleteSelectedEntity();
            ImGui::EndPopup();
        }
        if (BeginPropertyGrid("NameGrid"))
        {
            char nameBuf[256];
            strncpy(nameBuf, name->name.c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';

            PropertyRow("Name");
            if (markEdited(ImGui::InputText("##Name", nameBuf, sizeof(nameBuf))))
            {
                name->name = nameBuf;
            }
            EndPropertyRow();

            EndPropertyGrid();
        }
        ImGui::EndChild();
    }

    ImGui::Separator();

    // ---- Transform Component ----
    TransformComponent* transform = m_World->GetComponent<TransformComponent>(m_SelectedEntity);
    if (transform)
    {
        PushInspectorSectionStyle();
        const bool showTransform = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
        PopInspectorSectionStyle();
        if (showTransform)
        {
            if (BeginPropertyGrid("TransformGrid"))
            {
                PropertyRow("Position");
                editedThisFrame |= ImGui::DragFloat3("##Position", &transform->position.x, 0.1f);
                EndPropertyRow();

                PropertyRow("Rotation");
                editedThisFrame |= ImGui::DragFloat3("##Rotation", &transform->rotation.x, 1.0f);
                EndPropertyRow();

                PropertyRow("Scale");
                editedThisFrame |= ImGui::DragFloat3("##Scale", &transform->scale.x, 0.1f);
                EndPropertyRow();

                EndPropertyGrid();
            }
        }
    }

    RenderLayerComponent* renderLayer = m_World->GetComponent<RenderLayerComponent>(m_SelectedEntity);
    if (renderLayer)
    {
        bool removeRenderLayer = false;
        if (DrawComponentHeader("Render Layer", &removeRenderLayer))
        {
            if (BeginPropertyGrid("RenderLayerGrid"))
            {
                PropertyRow("World");
                bool worldLayerEnabled = (renderLayer->mask & RenderLayerMask::World) != 0;
                if (markEdited(ImGui::Checkbox("##RenderLayerWorld", &worldLayerEnabled)))
                {
                    if (worldLayerEnabled)
                        renderLayer->mask |= RenderLayerMask::World;
                    else
                        renderLayer->mask &= ~RenderLayerMask::World;
                }
                EndPropertyRow();

                PropertyRow("Viewmodel");
                bool viewmodelLayerEnabled = (renderLayer->mask & RenderLayerMask::Viewmodel) != 0;
                if (markEdited(ImGui::Checkbox("##RenderLayerViewmodel", &viewmodelLayerEnabled)))
                {
                    if (viewmodelLayerEnabled)
                        renderLayer->mask |= RenderLayerMask::Viewmodel;
                    else
                        renderLayer->mask &= ~RenderLayerMask::Viewmodel;
                }
                EndPropertyRow();

                EndPropertyGrid();
            }
        }
        if (removeRenderLayer)
        {
            m_World->RemoveComponent<RenderLayerComponent>(m_SelectedEntity);
            editedThisFrame = true;
        }
    }

    AttachmentPointComponent* attachmentPoint = m_World->GetComponent<AttachmentPointComponent>(m_SelectedEntity);
    if (attachmentPoint)
    {
        bool removeAttachmentPoint = false;
        if (DrawComponentHeader("Attachment Point", &removeAttachmentPoint))
        {
            if (BeginPropertyGrid("AttachmentPointGrid"))
            {
                PropertyRow("Socket Name");
                char socketNameBuffer[128] = {};
                std::snprintf(socketNameBuffer, sizeof(socketNameBuffer), "%s", attachmentPoint->socketName.c_str());
                if (markEdited(ImGui::InputText("##AttachmentPointSocketName", socketNameBuffer, sizeof(socketNameBuffer))))
                    attachmentPoint->socketName = socketNameBuffer;
                EndPropertyRow();

                EndPropertyGrid();
            }
        }
        if (removeAttachmentPoint)
        {
            m_World->RemoveComponent<AttachmentPointComponent>(m_SelectedEntity);
            editedThisFrame = true;
        }
    }

    AttachmentBindingComponent* attachmentBinding = m_World->GetComponent<AttachmentBindingComponent>(m_SelectedEntity);
    if (attachmentBinding)
    {
        bool removeAttachmentBinding = false;
        if (DrawComponentHeader("Attachment Binding", &removeAttachmentBinding))
        {
            Entity socketRoot = kNullEntity;
            std::string attachmentSourceSummary = "(No target selected)";
            std::string sourceDetail;
            if (attachmentBinding->targetMode == AttachmentTargetMode::Entity)
            {
                socketRoot = attachmentBinding->targetEntity;
                attachmentSourceSummary = socketRoot.IsValid() ? GetEntityDisplayName(*m_World, socketRoot) : "(No entity selected)";
                sourceDetail = "Attach this entity to another entity or one of its sockets.";
            }
            else
            {
                socketRoot = FindActiveCameraEntity(*m_World);
                attachmentSourceSummary =
                    socketRoot.IsValid() ? GetEntityDisplayName(*m_World, socketRoot) : "(No active camera)";
                sourceDetail = "Attach this entity to the current active camera or one of its sockets.";
            }

            std::vector<std::pair<Entity, std::string>> socketOptions;
            if (socketRoot.IsValid())
            {
                std::unordered_set<uint32> visited;
                CollectAttachmentSockets(*m_World, socketRoot, socketOptions, visited);
            }

            bool hasSelectedSocket = attachmentBinding->socketName.empty();
            for (const auto& socketOption : socketOptions)
            {
                if (socketOption.second == attachmentBinding->socketName)
                {
                    hasSelectedSocket = true;
                    break;
                }
            }

            ImGui::TextDisabled("Local transform is used as the attachment offset.");
            ImGui::Text("Source: %s", attachmentSourceSummary.c_str());
            ImGui::Text("Socket: %s",
                        attachmentBinding->socketName.empty() ? "Root" : attachmentBinding->socketName.c_str());
            ImGui::TextDisabled("%s", sourceDetail.c_str());

            if (BeginPropertyGrid("AttachmentBindingGrid"))
            {
                PropertyRow("Enabled");
                editedThisFrame |= ImGui::Checkbox("##AttachmentEnabled", &attachmentBinding->enabled);
                EndPropertyRow();

                PropertyRow("Attach To");
                int targetMode = static_cast<int>(attachmentBinding->targetMode);
                if (markEdited(ImGui::Combo(
                        "##AttachmentTargetMode", &targetMode,
                        [](void*, int index) -> const char*
                        {
                            return GetAttachmentTargetModeName(static_cast<AttachmentTargetMode>(index));
                        },
                        nullptr, 2)))
                {
                    attachmentBinding->targetMode = static_cast<AttachmentTargetMode>(targetMode);
                    if (attachmentBinding->targetMode == AttachmentTargetMode::ActiveCamera)
                        attachmentBinding->targetEntity = kNullEntity;
                }
                EndPropertyRow();

                if (attachmentBinding->targetMode == AttachmentTargetMode::Entity)
                {
                    PropertyRow("Entity");
                    std::string currentTargetName = attachmentBinding->targetEntity.IsValid()
                                                        ? GetEntityDisplayName(*m_World, attachmentBinding->targetEntity)
                                                        : "(Select entity)";
                    if (ImGui::BeginCombo("##AttachmentTargetEntity", currentTargetName.c_str()))
                    {
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::InputTextWithHint("##AttachmentTargetSearch", "Search entities...",
                                                 m_AttachmentTargetSearch.data(), m_AttachmentTargetSearch.size());
                        ImGui::Separator();

                        const std::string entitySearch = ToLowerCopy(m_AttachmentTargetSearch.data());
                        const bool noneSelected = !attachmentBinding->targetEntity.IsValid();
                        if ((entitySearch.empty() || std::string("(no entity)").find(entitySearch) != std::string::npos) &&
                            ImGui::Selectable("(No entity)", noneSelected))
                        {
                            attachmentBinding->targetEntity = kNullEntity;
                            m_AttachmentSocketSearch[0] = '\0';
                            editedThisFrame = true;
                        }
                        if (noneSelected)
                            ImGui::SetItemDefaultFocus();

                        m_World->EachEntity(
                            [&](Entity entity)
                            {
                                if (entity == m_SelectedEntity)
                                    return;

                                const std::string label = GetEntityDisplayName(*m_World, entity);
                                if (!entitySearch.empty() &&
                                    ToLowerCopy(label.c_str()).find(entitySearch) == std::string::npos)
                                {
                                    return;
                                }

                                const bool isSelected = (attachmentBinding->targetEntity == entity);
                                if (ImGui::Selectable(label.c_str(), isSelected))
                                {
                                    attachmentBinding->targetEntity = entity;
                                    m_AttachmentSocketSearch[0] = '\0';
                                    editedThisFrame = true;
                                }
                                if (isSelected)
                                    ImGui::SetItemDefaultFocus();
                            });
                        ImGui::EndCombo();
                    }
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_REPARENT"))
                        {
                            const Entity droppedEntity = *(const Entity*)payload->Data;
                            if (droppedEntity.IsValid() && droppedEntity != m_SelectedEntity)
                            {
                                attachmentBinding->targetEntity = droppedEntity;
                                m_AttachmentSocketSearch[0] = '\0';
                                editedThisFrame = true;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    EndPropertyRow();

                    PropertyRow("Quick Target");
                    const Entity parentEntity = m_World->GetComponent<HierarchyComponent>(m_SelectedEntity)
                                                    ? m_World->GetComponent<HierarchyComponent>(m_SelectedEntity)->parent
                                                    : kNullEntity;
                    const bool canUseParent = parentEntity.IsValid() && parentEntity != m_SelectedEntity;
                    if (!canUseParent)
                        ImGui::BeginDisabled();
                    if (ImGui::Button("Use Parent##Attachment"))
                    {
                        attachmentBinding->targetEntity = parentEntity;
                        m_AttachmentSocketSearch[0] = '\0';
                        editedThisFrame = true;
                    }
                    if (!canUseParent)
                        ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##AttachmentTarget"))
                    {
                        attachmentBinding->targetEntity = kNullEntity;
                        attachmentBinding->socketName.clear();
                        m_AttachmentSocketSearch[0] = '\0';
                        editedThisFrame = true;
                    }
                    EndPropertyRow();
                }
                else
                {
                    PropertyRow("Camera");
                    ImGui::TextUnformatted(socketRoot.IsValid() ? attachmentSourceSummary.c_str() : "(No active camera)");
                    EndPropertyRow();
                }

                PropertyRow("Socket");
                std::string currentSocketLabel =
                    attachmentBinding->socketName.empty() ? "Root" : attachmentBinding->socketName;
                if (!hasSelectedSocket && !attachmentBinding->socketName.empty())
                    currentSocketLabel += " (Missing)";
                const bool canChooseSocket = socketRoot.IsValid();
                if (!canChooseSocket)
                    ImGui::BeginDisabled();
                if (ImGui::BeginCombo("##AttachmentSocket", currentSocketLabel.c_str()))
                {
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputTextWithHint("##AttachmentSocketSearch", "Search sockets...", m_AttachmentSocketSearch.data(),
                                             m_AttachmentSocketSearch.size());
                    ImGui::Separator();

                    const std::string socketSearch = ToLowerCopy(m_AttachmentSocketSearch.data());
                    const bool rootSelected = attachmentBinding->socketName.empty();
                    const std::string rootLabel = "Root";
                    if ((socketSearch.empty() || ToLowerCopy(rootLabel.c_str()).find(socketSearch) != std::string::npos) &&
                        ImGui::Selectable("Root", rootSelected))
                    {
                        attachmentBinding->socketName.clear();
                        editedThisFrame = true;
                    }
                    if (rootSelected)
                        ImGui::SetItemDefaultFocus();

                    bool drewSocketOption = false;
                    for (const auto& socketOption : socketOptions)
                    {
                        const std::string optionLabel =
                            BuildAttachmentSocketLabel(*m_World, socketOption.first, socketOption.second);
                        if (!socketSearch.empty() &&
                            ToLowerCopy(optionLabel.c_str()).find(socketSearch) == std::string::npos)
                        {
                            continue;
                        }

                        drewSocketOption = true;
                        const bool isSelected = attachmentBinding->socketName == socketOption.second;
                        if (ImGui::Selectable(optionLabel.c_str(), isSelected))
                        {
                            attachmentBinding->socketName = socketOption.second;
                            editedThisFrame = true;
                        }
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    if (!drewSocketOption && !socketOptions.empty())
                        ImGui::TextDisabled("No sockets match the current search.");
                    if (socketOptions.empty())
                        ImGui::TextDisabled("No sockets on the current source.");
                    ImGui::EndCombo();
                }
                if (!canChooseSocket)
                    ImGui::EndDisabled();
                EndPropertyRow();

                PropertyRow("Position");
                editedThisFrame |= ImGui::Checkbox("##AttachmentFollowPosition", &attachmentBinding->followPosition);
                EndPropertyRow();
                if (attachmentBinding->followPosition)
                {
                    PropertyRow("Pos Axes");
                    editedThisFrame |= DrawAxisMaskControl("AttachmentPosAxes", attachmentBinding->positionAxes);
                    EndPropertyRow();
                }
                else
                {
                    PropertyRow("Pos Axes");
                    ImGui::TextDisabled("Local only");
                    EndPropertyRow();
                }

                PropertyRow("Rotation");
                editedThisFrame |= ImGui::Checkbox("##AttachmentFollowRotation", &attachmentBinding->followRotation);
                EndPropertyRow();
                if (attachmentBinding->followRotation)
                {
                    PropertyRow("Rot Axes");
                    editedThisFrame |= DrawAxisMaskControl("AttachmentRotAxes", attachmentBinding->rotationAxes);
                    EndPropertyRow();
                }
                else
                {
                    PropertyRow("Rot Axes");
                    ImGui::TextDisabled("Local only");
                    EndPropertyRow();
                }

                PropertyRow("Scale");
                editedThisFrame |= ImGui::Checkbox("##AttachmentFollowScale", &attachmentBinding->followScale);
                EndPropertyRow();
                if (attachmentBinding->followScale)
                {
                    PropertyRow("Scale Axes");
                    editedThisFrame |= DrawAxisMaskControl("AttachmentScaleAxes", attachmentBinding->scaleAxes);
                    EndPropertyRow();
                }
                else
                {
                    PropertyRow("Scale Axes");
                    ImGui::TextDisabled("Local only");
                    EndPropertyRow();
                }

                EndPropertyGrid();
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Inheritance: Pos %s | Rot %s | Scale %s",
                                attachmentBinding->followPosition
                                    ? GetAttachmentAxisMaskSummary(attachmentBinding->positionAxes).c_str()
                                    : "Local",
                                attachmentBinding->followRotation
                                    ? GetAttachmentAxisMaskSummary(attachmentBinding->rotationAxes).c_str()
                                    : "Local",
                                attachmentBinding->followScale ? GetAttachmentAxisMaskSummary(attachmentBinding->scaleAxes).c_str()
                                                               : "Local");
            if (!hasSelectedSocket && !attachmentBinding->socketName.empty())
            {
                ImGui::TextColored(ImVec4(0.95f, 0.73f, 0.28f, 1.0f),
                                   "Selected socket was not found on the current attachment source.");
            }

            if (HierarchyComponent* hierarchy = m_World->GetComponent<HierarchyComponent>(m_SelectedEntity))
            {
                if (attachmentBinding->enabled && hierarchy->parent.IsValid())
                {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.95f, 0.73f, 0.28f, 1.0f),
                                       "Attachment overrides hierarchy parenting for world placement.");
                }
            }
        }
        if (removeAttachmentBinding)
        {
            m_World->RemoveComponent<AttachmentBindingComponent>(m_SelectedEntity);
            editedThisFrame = true;
        }
    }

    // ---- Primitive Component ----
    PrimitiveComponent* primitive = m_World->GetComponent<PrimitiveComponent>(m_SelectedEntity);
    if (primitive)
    {
        bool removePrimitive = false;
        if (DrawComponentHeader("Primitive", &removePrimitive))
        {
            if (BeginPropertyGrid("PrimitiveGrid"))
            {
                // Dropdown for primitive type
                int currentType = static_cast<int>(primitive->type);
                float defaultLod1ScreenHeight = 0.0f;
                float defaultLod2ScreenHeight = 0.0f;
                GetPrimitiveDefaultLodScreenHeightThresholds(primitive->type, defaultLod1ScreenHeight,
                                                             defaultLod2ScreenHeight);
                PropertyRow("Shape");
                if (markEdited(ImGui::Combo(
                        "##Shape", &currentType,
                        [](void*, int idx) -> const char*
                        {
                            return GetPrimitiveTypeName(static_cast<PrimitiveType>(idx));
                        },
                        nullptr, static_cast<int>(PrimitiveType::Count))))
                {
                    primitive->type = static_cast<PrimitiveType>(currentType);
                    if (!primitive->overrideLodThresholds)
                    {
                        GetPrimitiveDefaultLodScreenHeightThresholds(primitive->type, primitive->lod1ScreenHeight,
                                                                     primitive->lod2ScreenHeight);
                    }
                }
                EndPropertyRow();

                GetPrimitiveDefaultLodScreenHeightThresholds(primitive->type, defaultLod1ScreenHeight,
                                                             defaultLod2ScreenHeight);

                PropertyRow("Custom LOD");
                if (markEdited(ImGui::Checkbox("##PrimitiveOverrideLodThresholds", &primitive->overrideLodThresholds)))
                {
                    if (primitive->overrideLodThresholds)
                    {
                        primitive->lod1ScreenHeight = std::max(primitive->lod1ScreenHeight, 0.0f);
                        primitive->lod2ScreenHeight =
                            std::clamp(primitive->lod2ScreenHeight, 0.0f, primitive->lod1ScreenHeight);
                    }
                    else
                    {
                        primitive->lod1ScreenHeight = defaultLod1ScreenHeight;
                        primitive->lod2ScreenHeight = defaultLod2ScreenHeight;
                    }
                }
                EndPropertyRow();

                if (primitive->overrideLodThresholds)
                {
                    PropertyRow("LOD1 Screen Height");
                    if (markEdited(ImGui::DragFloat("##PrimitiveLod1ScreenHeight", &primitive->lod1ScreenHeight, 0.005f,
                                                    0.0f, 1.0f, "%.3f")))
                    {
                        primitive->lod1ScreenHeight = std::clamp(primitive->lod1ScreenHeight, 0.0f, 1.0f);
                        primitive->lod2ScreenHeight =
                            std::clamp(primitive->lod2ScreenHeight, 0.0f, primitive->lod1ScreenHeight);
                    }
                    EndPropertyRow();

                    PropertyRow("LOD2 Screen Height");
                    if (markEdited(ImGui::DragFloat("##PrimitiveLod2ScreenHeight", &primitive->lod2ScreenHeight, 0.005f,
                                                    0.0f, primitive->lod1ScreenHeight, "%.3f")))
                    {
                        primitive->lod2ScreenHeight =
                            std::clamp(primitive->lod2ScreenHeight, 0.0f, primitive->lod1ScreenHeight);
                    }
                    EndPropertyRow();
                }
                else
                {
                    PropertyRow("LOD1 Default");
                    ImGui::Text("%.3f", defaultLod1ScreenHeight);
                    EndPropertyRow();

                    PropertyRow("LOD2 Default");
                    ImGui::Text("%.3f", defaultLod2ScreenHeight);
                    EndPropertyRow();
                }

                EndPropertyGrid();
            }
        }
        if (removePrimitive)
        {
            m_World->RemoveComponent<PrimitiveComponent>(m_SelectedEntity);
            editedThisFrame = true;
        }
    }

    // ---- Mesh Component ----
    MeshComponent* mesh = m_World->GetComponent<MeshComponent>(m_SelectedEntity);
    if (mesh)
    {
        PushInspectorSectionStyle();
        const bool showMesh = ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen);
        PopInspectorSectionStyle();
        if (showMesh)
        {
            if (BeginPropertyGrid("MeshGrid"))
            {
                PropertyRow("Mesh File");
                std::string displayPath = mesh->meshPath.empty() ? "(None)" : mesh->meshPath;
                ImGui::Text("%s", displayPath.c_str());
                EndPropertyRow();

                PropertyRow("Actions");
                if (markEdited(ImGui::Button("Import...")))
                {
                    OPENFILENAMEA ofn;
                    char szFile[260] = {0};
                    ZeroMemory(&ofn, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = nullptr;
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = sizeof(szFile);
                    ofn.lpstrFilter = "3D Models\0*.obj;*.fbx;*.gltf;*.glb\0OBJ Files\0*.obj\0FBX Files\0*.fbx\0glTF "
                                      "Files\0*.gltf;*.glb\0All Files\0*.*\0";
                    ofn.nFilterIndex = 1;
                    ofn.lpstrFileTitle = nullptr;
                    ofn.nMaxFileTitle = 0;
                    ofn.lpstrInitialDir = nullptr;
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                    if (GetOpenFileNameA(&ofn))
                    {
                        // Copy file to Assets/Models folder using AssetManager root
                        std::filesystem::path sourcePath(szFile);
                        std::filesystem::path rootPath = AssetManager::Get().GetRootPath();
                        std::filesystem::path modelsPath = rootPath / "Models";

                        // Create directory if it doesn't exist
                        if (!std::filesystem::exists(modelsPath))
                        {
                            std::filesystem::create_directories(modelsPath);
                        }

                        std::filesystem::path destPath = modelsPath / sourcePath.filename();
                        try
                        {
                            std::filesystem::copy_file(sourcePath, destPath,
                                                       std::filesystem::copy_options::overwrite_existing);
                            CopyFbxSidecarDirectoriesIfPresent(sourcePath, modelsPath);
                            std::string importedPath = "Models/" + sourcePath.filename().string();

                            // Check if FBX and get submesh count
                            std::string ext = sourcePath.extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(),
                                           [](unsigned char c) { return (char)std::tolower(c); });

                            if (ext == ".fbx")
                            {
                                ImportFbxMaterialsForAsset(importedPath);
                                const std::vector<std::string> materialPaths = GetFbxSubmeshMaterialPaths(importedPath);
                                size_t submeshCount = GetFbxSubmeshCountForAsset(importedPath);

                                if (submeshCount <= 1)
                                {
                                    mesh->meshPath = importedPath;
                                    mesh->submeshIndex = 0;
                                    mesh->isLoaded = false;
                                    AutoAssignImportedMeshMaterial(*m_World, m_SelectedEntity, materialPaths, 0);
                                }
                                else
                                {
                                    mesh->meshPath = importedPath;
                                    mesh->submeshIndex = -1;
                                    mesh->isLoaded = false;
                                    ClearExplicitMeshMaterialOverride(*m_World, m_SelectedEntity);

                                    DOT_LOG_INFO("Imported FBX as single entity with %zu submeshes: %s", submeshCount,
                                                 importedPath.c_str());
                                }
                            }
                            else
                            {
                                mesh->meshPath = importedPath;
                                mesh->isLoaded = false;
                            }
                        }
                        catch (const std::exception& e)
                        {
                            DOT_LOG_ERROR("Failed to copy mesh: %s", e.what());
                            // Fallback - use original path
                            mesh->meshPath = sourcePath.string();
                        }
                    }
                }
                ImGui::SameLine();
                if (markEdited(ImGui::Button("Clear")))
                {
                    mesh->meshPath.clear();
                    mesh->isLoaded = false;
                }
                EndPropertyRow();

                PropertyRow("Cast Shadow");
                editedThisFrame |= ImGui::Checkbox("##CastShadow", &mesh->castShadow);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Disable for large static geometry to improve shadow pass performance");
                }
                EndPropertyRow();

                EndPropertyGrid();
            }

            // Dropdown for existing meshes in Assets/Models
            ImGui::Separator();
            if (BeginPropertyGrid("MeshSelectionGrid"))
            {
                static std::vector<std::string> availableMeshes;
                static bool meshListDirty = true;

                // Refresh mesh list
                PropertyRow("Available");
                if (ImGui::Button("Refresh List") || meshListDirty)
                {
                    availableMeshes.clear();
                    std::filesystem::path modelsPath =
                        std::filesystem::path(AssetManager::Get().GetRootPath()) / "Models";
                    if (std::filesystem::exists(modelsPath))
                    {
                        for (const auto& entry : std::filesystem::directory_iterator(modelsPath))
                        {
                            if (!entry.is_directory())
                            {
                                std::string ext = entry.path().extension().string();
                                std::transform(ext.begin(), ext.end(), ext.begin(),
                                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
                                {
                                    availableMeshes.push_back(entry.path().filename().string());
                                }
                            }
                        }
                    }
                    meshListDirty = false;
                }
                EndPropertyRow();

                // Mesh dropdown
                if (!availableMeshes.empty())
                {
                    PropertyRow("Asset");
                    int currentIdx = -1;
                    std::string currentFile = mesh->meshPath;
                    // Extract just the filename if it's a full path
                    if (currentFile.find('/') != std::string::npos)
                        currentFile = currentFile.substr(currentFile.rfind('/') + 1);

                    for (int i = 0; i < static_cast<int>(availableMeshes.size()); ++i)
                    {
                        if (availableMeshes[i] == currentFile)
                        {
                            currentIdx = i;
                            break;
                        }
                    }

                    if (ImGui::BeginCombo("##MeshFile",
                                          currentIdx >= 0 ? availableMeshes[currentIdx].c_str() : "Select..."))
                    {
                        for (int i = 0; i < static_cast<int>(availableMeshes.size()); ++i)
                        {
                            bool isSelected = (currentIdx == i);
                            if (markEdited(ImGui::Selectable(availableMeshes[i].c_str(), isSelected)))
                            {
                                std::string selectedPath = "Models/" + availableMeshes[i];

                                // Check if FBX and get submesh count
                                std::string ext = std::filesystem::path(selectedPath).extension().string();
                                std::transform(ext.begin(), ext.end(), ext.begin(),
                                               [](unsigned char c) { return (char)std::tolower(c); });

                                if (ext == ".fbx")
                                {
                                    ImportFbxMaterialsForAsset(selectedPath);
                                    const std::vector<std::string> materialPaths = GetFbxSubmeshMaterialPaths(selectedPath);
                                    size_t submeshCount = GetFbxSubmeshCountForAsset(selectedPath);

                                    if (submeshCount <= 1)
                                    {
                                        // Single submesh - just set path on current entity
                                        mesh->meshPath = selectedPath;
                                        mesh->submeshIndex = 0;
                                        mesh->isLoaded = false;
                                        AutoAssignImportedMeshMaterial(*m_World, m_SelectedEntity, materialPaths, 0);
                                    }
                                    else
                                    {
                                        mesh->meshPath = selectedPath;
                                        mesh->submeshIndex = -1;
                                        mesh->isLoaded = false;
                                        ClearExplicitMeshMaterialOverride(*m_World, m_SelectedEntity);

                                        DOT_LOG_INFO("Assigned FBX as single entity with %zu submeshes: %s",
                                                     submeshCount, selectedPath.c_str());
                                    }
                                }
                                else
                                {
                                    // Non-FBX - just set path
                                    mesh->meshPath = selectedPath;
                                    mesh->isLoaded = false;
                                }
                            }
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    EndPropertyRow();
                }
                else
                {
                    PropertyRow("Asset");
                    ImGui::TextDisabled("No meshes in Assets/Models");
                    EndPropertyRow();
                }

                EndPropertyGrid();
            }
        }
    }

    // ---- Material Component ----
    MaterialComponent* material = m_World->GetComponent<MaterialComponent>(m_SelectedEntity);
    if (material)
    {
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (BeginPropertyGrid("MaterialGrid"))
            {
                PropertyRow("Use File");
                editedThisFrame |= ImGui::Checkbox("##UseMaterialFile", &material->useMaterialFile);
                EndPropertyRow();

                EndPropertyGrid();
            }

            if (material->useMaterialFile)
            {
                // Show material file selection
                std::string displayPath =
                    material->materialPath.empty() ? "(No material selected)" : material->materialPath;
                ImGui::Text("Material: %s", displayPath.c_str());

                // Dropdown for existing materials in Assets/Materials
                static std::vector<std::string> availableMaterials;
                static bool materialListDirty = true;

                // Refresh material list
                if (ImGui::Button("Refresh##Materials") || materialListDirty)
                {
                    availableMaterials.clear();
                    std::filesystem::path materialsPath =
                        std::filesystem::path(AssetManager::Get().GetRootPath()) / "Materials";
                    if (std::filesystem::exists(materialsPath))
                    {
                        for (const auto& entry : std::filesystem::directory_iterator(materialsPath))
                        {
                            if (!entry.is_directory())
                            {
                                std::string ext = entry.path().extension().string();
                                if (ext == ".dotmat")
                                {
                                    availableMaterials.push_back(entry.path().filename().string());
                                }
                            }
                        }
                    }
                    materialListDirty = false;
                }

                // Material dropdown
                if (!availableMaterials.empty())
                {
                    int currentIdx = -1;
                    std::string currentFile =
                        std::filesystem::path(AssetManager::Get().GetFullPath(material->materialPath))
                            .filename()
                            .string();

                    for (int i = 0; i < static_cast<int>(availableMaterials.size()); ++i)
                    {
                        if (availableMaterials[i] == currentFile)
                        {
                            currentIdx = i;
                            break;
                        }
                    }

                    if (ImGui::BeginCombo("Material File",
                                          currentIdx >= 0 ? availableMaterials[currentIdx].c_str() : "Select..."))
                    {
                        for (int i = 0; i < static_cast<int>(availableMaterials.size()); ++i)
                        {
                            bool isSelected = (currentIdx == i);
                            if (markEdited(ImGui::Selectable(availableMaterials[i].c_str(), isSelected)))
                            {
                                material->materialPath = "Materials/" + availableMaterials[i];
                            }
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                {
                    ImGui::TextDisabled("No materials in Assets/Materials");
                    ImGui::TextDisabled("Use Material Graph to create one!");
                }
            }
            else
            {
                // Show inline material properties
                float color[3] = {material->baseColor.x, material->baseColor.y, material->baseColor.z};
                if (markEdited(ImGui::ColorEdit3("Base Color", color)))
                {
                    material->baseColor.x = color[0];
                    material->baseColor.y = color[1];
                    material->baseColor.z = color[2];
                }
                float emissive[3] = {material->emissiveColor.x, material->emissiveColor.y, material->emissiveColor.z};
                if (markEdited(ImGui::ColorEdit3("Emissive", emissive)))
                {
                    material->emissiveColor.x = emissive[0];
                    material->emissiveColor.y = emissive[1];
                    material->emissiveColor.z = emissive[2];
                }
                editedThisFrame |= ImGui::SliderFloat("Metallic", &material->metallic, 0.0f, 1.0f);
                editedThisFrame |= ImGui::SliderFloat("Roughness", &material->roughness, 0.0f, 1.0f);
                editedThisFrame |= ImGui::SliderFloat("Emissive Strength", &material->emissiveStrength, 0.0f, 10.0f);
            }
        }
    }
    else
    {
        // Show button to add MaterialComponent if entity has primitive or mesh
        if (primitive || mesh)
        {
            if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextDisabled("No material assigned");
                if (markEdited(ImGui::Button("+ Add Material")))
                {
                    m_World->AddComponent<MaterialComponent>(m_SelectedEntity);
                }
            }
        }
    }

    // ---- Skybox Component ----
    SkyboxComponent* skyboxComp = m_World->GetComponent<SkyboxComponent>(m_SelectedEntity);
    if (skyboxComp)
    {
        if (ImGui::CollapsingHeader("Skybox", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Display current cubemap path
            std::string displayPath =
                skyboxComp->cubemapPath.empty() ? "(No cubemap selected)" : skyboxComp->cubemapPath;
            ImGui::Text("Cubemap: %s", displayPath.c_str());

            // Import button - opens file dialog
            if (markEdited(ImGui::Button("Import Cubemap...")))
            {
                OPENFILENAMEA ofn;
                char szFile[260] = {0};
                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = nullptr;
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = sizeof(szFile);
                ofn.lpstrFilter = "Cubemap Images\0*.png;*.jpg;*.jpeg;*.hdr\0PNG Files\0*.png\0JPEG "
                                  "Files\0*.jpg;*.jpeg\0HDR Files\0*.hdr\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrFileTitle = nullptr;
                ofn.nMaxFileTitle = 0;
                ofn.lpstrInitialDir = nullptr;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                if (GetOpenFileNameA(&ofn))
                {
                    // Copy file to Assets/Cubemaps folder
                    std::filesystem::path sourcePath(szFile);
                    std::filesystem::path assetsPath =
                        std::filesystem::path(AssetManager::Get().GetRootPath()) / "Cubemaps";
                    if (!std::filesystem::exists(assetsPath))
                    {
                        std::filesystem::create_directories(assetsPath);
                    }

                    std::filesystem::path destPath = assetsPath / sourcePath.filename();
                    try
                    {
                        std::filesystem::copy_file(sourcePath, destPath,
                                                   std::filesystem::copy_options::overwrite_existing);
                        skyboxComp->cubemapPath = "Cubemaps/" + sourcePath.filename().string();
                        skyboxComp->isLoaded = false; // Mark for reload
                    }
                    catch (const std::exception&)
                    {
                        // Failed to copy - just use original path
                        skyboxComp->cubemapPath = sourcePath.string();
                    }
                }
            }

            ImGui::SameLine();

            // Clear button
            if (markEdited(ImGui::Button("Clear##Cubemap")))
            {
                skyboxComp->cubemapPath.clear();
                skyboxComp->isLoaded = false;
            }

            // Dropdown for existing cubemaps in Assets/Cubemaps
            ImGui::Separator();
            ImGui::Text("Or select from Assets:");

            static std::vector<std::string> availableCubemaps;
            static bool cubemapListDirty = true;

            // Refresh cubemap list
            if (ImGui::Button("Refresh Cubemaps") || cubemapListDirty)
            {
                availableCubemaps.clear();
                std::filesystem::path cubemapsPath =
                    std::filesystem::path(AssetManager::Get().GetRootPath()) / "Cubemaps";
                if (std::filesystem::exists(cubemapsPath))
                {
                    for (const auto& entry : std::filesystem::directory_iterator(cubemapsPath))
                    {
                        if (!entry.is_directory())
                        {
                            std::string ext = entry.path().extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(),
                                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".hdr")
                            {
                                availableCubemaps.push_back(entry.path().filename().string());
                            }
                        }
                    }
                }
                cubemapListDirty = false;
            }

            // Cubemap dropdown
            if (!availableCubemaps.empty())
            {
                int currentIdx = -1;
                std::string currentFile = skyboxComp->cubemapPath;
                // Extract just the filename if it's a full path
                if (currentFile.find('/') != std::string::npos)
                    currentFile = currentFile.substr(currentFile.rfind('/') + 1);

                for (int i = 0; i < static_cast<int>(availableCubemaps.size()); ++i)
                {
                    if (availableCubemaps[i] == currentFile)
                    {
                        currentIdx = i;
                        break;
                    }
                }

                if (ImGui::BeginCombo("Cubemap File",
                                      currentIdx >= 0 ? availableCubemaps[currentIdx].c_str() : "Select..."))
                {
                    for (int i = 0; i < static_cast<int>(availableCubemaps.size()); ++i)
                    {
                        bool isSelected = (currentIdx == i);
                        if (markEdited(ImGui::Selectable(availableCubemaps[i].c_str(), isSelected)))
                        {
                            skyboxComp->cubemapPath = "Cubemaps/" + availableCubemaps[i];
                            skyboxComp->isLoaded = false;
                        }
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            else
            {
                ImGui::TextDisabled("No cubemaps in Assets/Cubemaps");
            }

            // Tint color
            ImGui::Separator();
            float tintColor[3] = {skyboxComp->tintR, skyboxComp->tintG, skyboxComp->tintB};
            if (markEdited(ImGui::ColorEdit3("Tint Color", tintColor)))
            {
                skyboxComp->tintR = tintColor[0];
                skyboxComp->tintG = tintColor[1];
                skyboxComp->tintB = tintColor[2];
            }

            // Wrap mode dropdown
            const char* wrapModes[] = {"Clamp", "Repeat", "Mirror"};
            int currentWrapMode = static_cast<int>(skyboxComp->wrapMode);
            if (markEdited(ImGui::Combo("Wrap Mode", &currentWrapMode, wrapModes, IM_ARRAYSIZE(wrapModes))))
            {
                skyboxComp->wrapMode = static_cast<SkyboxWrapMode>(currentWrapMode);
                skyboxComp->isLoaded = false; // Force reload with new wrap mode
            }

            // Rotation slider
            editedThisFrame |= ImGui::SliderFloat("Rotation", &skyboxComp->rotation, 0.0f, 360.0f, "%.1f°");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Horizontal rotation of the skybox in degrees");
            }

            // Debug markers toggle
            if (markEdited(ImGui::Checkbox("Show Markers", &skyboxComp->showMarkers)))
            {
                skyboxComp->isLoaded = false; // Force reload to update markers
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Show face labels (F/B/L/R/U/D) on each side of the skybox");
            }

            ImGui::Separator();
            ImGui::Text("Ambient Light");
            if (BeginPropertyGrid("SkyAmbientGrid"))
            {
                PropertyRow("Enabled");
                editedThisFrame |= ImGui::Checkbox("##SkyAmbientEnabled", &skyboxComp->ambientEnabled);
                EndPropertyRow();

                if (skyboxComp->ambientEnabled)
                {
                    PropertyRow("Ambient Color");
                    float ambientColor[3] = {skyboxComp->ambientColorR, skyboxComp->ambientColorG,
                                             skyboxComp->ambientColorB};
                    if (markEdited(ImGui::ColorEdit3("##SkyAmbientColor", ambientColor)))
                    {
                        skyboxComp->ambientColorR = ambientColor[0];
                        skyboxComp->ambientColorG = ambientColor[1];
                        skyboxComp->ambientColorB = ambientColor[2];
                    }
                    EndPropertyRow();

                    PropertyRow("Ambient Intensity");
                    editedThisFrame |=
                        ImGui::SliderFloat("##SkyAmbientIntensity", &skyboxComp->ambientIntensity, 0.0f, 10.0f, "%.2f");
                    EndPropertyRow();
                }

                EndPropertyGrid();
            }

            // ---- Sun Light Settings ----
            ImGui::Separator();
            ImGui::Text("Sun Light");

            if (BeginPropertyGrid("SunGrid"))
            {
                PropertyRow("Enabled");
                editedThisFrame |= ImGui::Checkbox("##SunEnabled", &skyboxComp->sunEnabled);
                EndPropertyRow();

                if (skyboxComp->sunEnabled)
                {
                    PropertyRow("Sun Pitch");
                    editedThisFrame |= ImGui::SliderFloat("##SunPitch", &skyboxComp->sunRotationX, -90.0f, 90.0f, "%.1f°");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Vertical angle: negative = sun from above, positive = from below");
                    EndPropertyRow();

                    PropertyRow("Sun Yaw");
                    editedThisFrame |= ImGui::SliderFloat("##SunYaw", &skyboxComp->sunRotationY, 0.0f, 360.0f, "%.1f°");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Horizontal angle: direction the sun is coming from");
                    EndPropertyRow();

                    PropertyRow("Sun Color");
                    float sunColor[3] = {skyboxComp->sunColorR, skyboxComp->sunColorG, skyboxComp->sunColorB};
                    if (markEdited(ImGui::ColorEdit3("##SunColor", sunColor)))
                    {
                        skyboxComp->sunColorR = sunColor[0];
                        skyboxComp->sunColorG = sunColor[1];
                        skyboxComp->sunColorB = sunColor[2];
                    }
                    EndPropertyRow();

                    PropertyRow("Sun Intensity");
                    editedThisFrame |= ImGui::SliderFloat("##SunIntensity", &skyboxComp->sunIntensity, 0.0f, 10.0f, "%.2f");
                    EndPropertyRow();
                }
                EndPropertyGrid();
            }

            if (skyboxComp->sunEnabled)
            {
                if (ImGui::TreeNodeEx("Sun Shadows", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (BeginPropertyGrid("SunShadowGrid"))
                    {
                        PropertyRow("Cast Shadows");
                        editedThisFrame |= ImGui::Checkbox("##SunCastShadows", &skyboxComp->sunCastShadows);
                        EndPropertyRow();

                        PropertyRow("Shadow Distance");
                        editedThisFrame |= ImGui::DragFloat("##SunShadowDistance", &skyboxComp->sunShadowDistance, 1.0f,
                                                            1.0f, 1000.0f, "%.0f");
                        EndPropertyRow();

                        PropertyRow("Shadow Bias");
                        editedThisFrame |= ImGui::DragFloat("##SunShadowBias", &skyboxComp->sunShadowBias, 0.0001f, 0.0f,
                                                            0.1f, "%.5f");
                        EndPropertyRow();

                        EndPropertyGrid();
                    }
                    ImGui::TreePop();
                }
            }
        }
    }

    // ---- Directional Light Component ----
    DirectionalLightComponent* dirLight = m_World->GetComponent<DirectionalLightComponent>(m_SelectedEntity);
    if (dirLight)
    {
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (BeginPropertyGrid("DirLightGrid"))
            {
                PropertyRow("Color");
                float color[3] = {dirLight->color.x, dirLight->color.y, dirLight->color.z};
                if (markEdited(ImGui::ColorEdit3("##Color", color)))
                {
                    dirLight->color.x = color[0];
                    dirLight->color.y = color[1];
                    dirLight->color.z = color[2];
                }
                EndPropertyRow();

                PropertyRow("Intensity");
                editedThisFrame |= ImGui::DragFloat("##Intensity", &dirLight->intensity, 0.05f, 0.0f, 10.0f);
                EndPropertyRow();

                EndPropertyGrid();
            }

            if (ImGui::TreeNodeEx("Shadows", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (BeginPropertyGrid("DirShadowGrid"))
                {
                    PropertyRow("Cast Shadows");
                    editedThisFrame |= ImGui::Checkbox("##CastShadows", &dirLight->castShadows);
                    EndPropertyRow();

                    PropertyRow("Shadow Distance");
                    editedThisFrame |= ImGui::DragFloat("##ShadowDistance", &dirLight->shadowDistance, 1.0f, 1.0f, 1000.0f,
                                                        "%.0f");
                    EndPropertyRow();

                    PropertyRow("Shadow Bias");
                    editedThisFrame |= ImGui::DragFloat("##ShadowBias", &dirLight->shadowBias, 0.0001f, 0.0f, 0.1f, "%.5f");
                    EndPropertyRow();

                    EndPropertyGrid();
                }
                ImGui::TreePop();
            }
        }
    }

    // ---- Point Light Component ----
    PointLightComponent* pointLight = m_World->GetComponent<PointLightComponent>(m_SelectedEntity);
    if (pointLight)
    {
        if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (BeginPropertyGrid("PointLightGrid"))
            {
                PropertyRow("Color");
                float color[3] = {pointLight->color.x, pointLight->color.y, pointLight->color.z};
                if (markEdited(ImGui::ColorEdit3("##Color", color)))
                {
                    pointLight->color.x = color[0];
                    pointLight->color.y = color[1];
                    pointLight->color.z = color[2];
                }
                EndPropertyRow();

                PropertyRow("Intensity");
                editedThisFrame |= ImGui::DragFloat("##Intensity", &pointLight->intensity, 0.05f, 0.0f, 10.0f);
                EndPropertyRow();

                PropertyRow("Range");
                editedThisFrame |= ImGui::DragFloat("##Range", &pointLight->range, 0.1f, 0.1f, 100.0f);
                EndPropertyRow();

                PropertyRow("Cast Shadows");
                editedThisFrame |= ImGui::Checkbox("##PointCastShadows", &pointLight->castShadows);
                EndPropertyRow();

                if (pointLight->castShadows)
                {
                    PropertyRow("Shadow Bias");
                    editedThisFrame |= ImGui::DragFloat("##PointShadowBias", &pointLight->shadowBias, 0.0001f, 0.0f, 0.05f,
                                                        "%.4f");
                    EndPropertyRow();
                }

                EndPropertyGrid();
            }
        }
    }

    // ---- Spot Light Component ----
    SpotLightComponent* spotLight = m_World->GetComponent<SpotLightComponent>(m_SelectedEntity);
    if (spotLight)
    {
        if (ImGui::CollapsingHeader("Spot Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (BeginPropertyGrid("SpotLightGrid"))
            {
                PropertyRow("Color");
                float color[3] = {spotLight->color.x, spotLight->color.y, spotLight->color.z};
                if (markEdited(ImGui::ColorEdit3("##Color", color)))
                {
                    spotLight->color.x = color[0];
                    spotLight->color.y = color[1];
                    spotLight->color.z = color[2];
                }
                EndPropertyRow();

                PropertyRow("Intensity");
                editedThisFrame |= ImGui::DragFloat("##Intensity", &spotLight->intensity, 0.05f, 0.0f, 10.0f);
                EndPropertyRow();

                PropertyRow("Range");
                editedThisFrame |= ImGui::DragFloat("##Range", &spotLight->range, 0.1f, 0.1f, 100.0f);
                EndPropertyRow();

                PropertyRow("Inner Angle");
                editedThisFrame |= ImGui::DragFloat("##InnerAngle", &spotLight->innerConeAngle, 0.5f, 1.0f,
                                                    spotLight->outerConeAngle);
                EndPropertyRow();

                PropertyRow("Outer Angle");
                editedThisFrame |= ImGui::DragFloat("##OuterAngle", &spotLight->outerConeAngle, 0.5f,
                                                    spotLight->innerConeAngle, 90.0f);
                EndPropertyRow();

                PropertyRow("Cast Shadows");
                editedThisFrame |= ImGui::Checkbox("##SpotCastShadows", &spotLight->castShadows);
                EndPropertyRow();

                if (spotLight->castShadows)
                {
                    PropertyRow("Shadow Bias");
                    editedThisFrame |= ImGui::DragFloat("##SpotShadowBias", &spotLight->shadowBias, 0.0001f, 0.0f,
                                                        0.05f, "%.4f");
                    EndPropertyRow();
                }

                EndPropertyGrid();
            }
        }
    }

    // ---- Reflection Probe Component ----
    ReflectionProbeComponent* reflectionProbe = m_World->GetComponent<ReflectionProbeComponent>(m_SelectedEntity);
    if (reflectionProbe)
    {
        if (ImGui::CollapsingHeader("Reflection Probe", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const bool usingSceneSkybox =
                reflectionProbe->sourceMode == ReflectionProbeSourceMode::AutoSceneSkybox || reflectionProbe->cubemapPath.empty();
            std::string displayPath;
            if (reflectionProbe->sourceMode == ReflectionProbeSourceMode::AutoSceneSkybox)
                displayPath = "(Auto: Scene Skybox)";
            else if (reflectionProbe->cubemapPath.empty())
                displayPath = "(Manual empty - falls back to Scene Skybox)";
            else
                displayPath = reflectionProbe->cubemapPath;
            const bool manualSource = reflectionProbe->sourceMode == ReflectionProbeSourceMode::ManualCubemap;

            ImGui::Text("Source: %s",
                        usingSceneSkybox ? "Scene Skybox" : GetReflectionProbeSourceModeName(reflectionProbe->sourceMode));
            ImGui::Text("Cubemap: %s", displayPath.c_str());

            if (!manualSource)
                ImGui::BeginDisabled();
            if (markEdited(ImGui::Button("Import Cubemap##ReflectionProbe")))
            {
                OPENFILENAMEA ofn;
                char szFile[260] = {0};
                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = nullptr;
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = sizeof(szFile);
                ofn.lpstrFilter = "Cubemap Images\0*.png;*.jpg;*.jpeg;*.hdr\0PNG Files\0*.png\0JPEG "
                                  "Files\0*.jpg;*.jpeg\0HDR Files\0*.hdr\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrFileTitle = nullptr;
                ofn.nMaxFileTitle = 0;
                ofn.lpstrInitialDir = nullptr;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                if (GetOpenFileNameA(&ofn))
                {
                    std::filesystem::path sourcePath(szFile);
                    std::filesystem::path assetsPath =
                        std::filesystem::path(AssetManager::Get().GetRootPath()) / "Cubemaps";
                    if (!std::filesystem::exists(assetsPath))
                        std::filesystem::create_directories(assetsPath);

                    std::filesystem::path destPath = assetsPath / sourcePath.filename();
                    try
                    {
                        std::filesystem::copy_file(sourcePath, destPath,
                                                   std::filesystem::copy_options::overwrite_existing);
                        reflectionProbe->cubemapPath = "Cubemaps/" + sourcePath.filename().string();
                    }
                    catch (const std::exception&)
                    {
                        reflectionProbe->cubemapPath = sourcePath.string();
                    }
                }
            }
            if (!manualSource)
                ImGui::EndDisabled();

            ImGui::SameLine();
            if (!manualSource)
                ImGui::BeginDisabled();
            if (markEdited(ImGui::Button("Clear##ReflectionProbeCubemap")))
                reflectionProbe->cubemapPath.clear();
            if (!manualSource)
                ImGui::EndDisabled();

            ImGui::SameLine();
            const bool canBakeProbe = m_OnBakeReflectionProbe != nullptr && m_SelectedEntity.IsValid();
            if (!canBakeProbe)
                ImGui::BeginDisabled();
            if (ImGui::Button("Bake Local Capture##ReflectionProbe"))
                m_OnBakeReflectionProbe(m_SelectedEntity);
            if (!canBakeProbe)
                ImGui::EndDisabled();

            if (BeginPropertyGrid("ReflectionProbeGrid"))
            {
                PropertyRow("Enabled");
                editedThisFrame |= ImGui::Checkbox("##ReflectionProbeEnabled", &reflectionProbe->enabled);
                EndPropertyRow();

                PropertyRow("Source");
                {
                    int sourceMode = static_cast<int>(reflectionProbe->sourceMode);
                    const char* sourceLabels[] = {"Manual Cubemap", "Auto Scene Sky"};
                    if (markEdited(ImGui::Combo("##ReflectionProbeSource", &sourceMode, sourceLabels,
                                                IM_ARRAYSIZE(sourceLabels))))
                    {
                        if (sourceMode == static_cast<int>(ReflectionProbeSourceMode::AutoSceneSkybox))
                            reflectionProbe->sourceMode = ReflectionProbeSourceMode::AutoSceneSkybox;
                        else
                            reflectionProbe->sourceMode = ReflectionProbeSourceMode::ManualCubemap;
                    }
                }
                EndPropertyRow();

                PropertyRow("Tint");
                float tint[3] = {reflectionProbe->tint.x, reflectionProbe->tint.y, reflectionProbe->tint.z};
                if (markEdited(ImGui::ColorEdit3("##ReflectionProbeTint", tint)))
                {
                    reflectionProbe->tint.x = tint[0];
                    reflectionProbe->tint.y = tint[1];
                    reflectionProbe->tint.z = tint[2];
                }
                EndPropertyRow();

                PropertyRow("Intensity");
                editedThisFrame |= ImGui::DragFloat("##ReflectionProbeIntensity", &reflectionProbe->intensity, 0.05f, 0.0f,
                                                    16.0f, "%.2f");
                EndPropertyRow();

                PropertyRow("Radius");
                editedThisFrame |=
                    ImGui::DragFloat("##ReflectionProbeRadius", &reflectionProbe->radius, 0.1f, 0.1f, 1000.0f, "%.2f");
                EndPropertyRow();

                PropertyRow("Box Extents");
                editedThisFrame |= ImGui::DragFloat3("##ReflectionProbeBoxExtents", &reflectionProbe->boxExtents.x, 0.1f,
                                                     0.0f, 1000.0f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Half-extents for box projection. Leave at 0,0,0 to use Radius as a cube.");
                EndPropertyRow();

                PropertyRow("Edge Fade");
                editedThisFrame |= ImGui::SliderFloat("##ReflectionProbeFalloff", &reflectionProbe->falloff, 0.0f, 1.0f,
                                                      "%.2f");
                EndPropertyRow();

                EndPropertyGrid();
            }

            ImGui::Separator();
            if (BeginPropertyGrid("ReflectionProbeAssetGrid"))
            {
                static std::vector<std::string> availableCubemaps;
                static bool cubemapListDirty = true;

                PropertyRow("Available");
                if (manualSource && (ImGui::Button("Refresh List##ReflectionProbe") || cubemapListDirty))
                {
                    availableCubemaps.clear();
                    std::filesystem::path cubemapPath =
                        std::filesystem::path(AssetManager::Get().GetRootPath()) / "Cubemaps";
                    if (std::filesystem::exists(cubemapPath))
                    {
                        for (const auto& entry : std::filesystem::directory_iterator(cubemapPath))
                        {
                            if (entry.is_directory())
                            {
                                availableCubemaps.push_back(
                                    "Cubemaps/" + entry.path().filename().generic_string());
                                continue;
                            }

                            std::string ext = entry.path().extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(),
                                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".hdr")
                                availableCubemaps.push_back("Cubemaps/" + entry.path().filename().generic_string());
                        }
                    }
                    std::sort(availableCubemaps.begin(), availableCubemaps.end());
                    cubemapListDirty = false;
                }
                else if (!manualSource)
                {
                    ImGui::TextDisabled("Scene skybox is used automatically");
                }
                EndPropertyRow();

                PropertyRow("Asset");
                if (!manualSource)
                {
                    ImGui::TextDisabled("Resolved from scene skybox");
                }
                else if (!availableCubemaps.empty())
                {
                    int currentIdx = -1;
                    for (int i = 0; i < static_cast<int>(availableCubemaps.size()); ++i)
                    {
                        if (availableCubemaps[i] == reflectionProbe->cubemapPath)
                        {
                            currentIdx = i;
                            break;
                        }
                    }

                    if (ImGui::BeginCombo("##ReflectionProbeAsset",
                                          currentIdx >= 0 ? availableCubemaps[currentIdx].c_str() : "Select..."))
                    {
                        for (int i = 0; i < static_cast<int>(availableCubemaps.size()); ++i)
                        {
                            const bool isSelected = currentIdx == i;
                            if (markEdited(ImGui::Selectable(availableCubemaps[i].c_str(), isSelected)))
                                reflectionProbe->cubemapPath = availableCubemaps[i];
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                {
                    ImGui::TextDisabled("No cubemaps in Assets/Cubemaps");
                }
                EndPropertyRow();

                EndPropertyGrid();
            }

            ImGui::Spacing();
            ImGui::TextDisabled(
                "Uses entity position as the probe origin. Bake Local Capture writes a cubemap folder and switches the probe to Manual Cubemap.");
        }
    }

    // ---- Camera Component ----
    CameraComponent* camera = m_World->GetComponent<CameraComponent>(m_SelectedEntity);
    if (camera)
    {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (BeginPropertyGrid("CameraGrid"))
            {
                PropertyRow("Field of View");
                editedThisFrame |= ImGui::SliderFloat("##FOV", &camera->fov, 1.0f, 179.0f, "%.1f deg");
                EndPropertyRow();

                PropertyRow("Near Plane");
                editedThisFrame |=
                    ImGui::DragFloat("##NearPlane", &camera->nearPlane, 0.001f, 0.001f, camera->farPlane - 0.01f,
                                     "%.3f");
                EndPropertyRow();

                PropertyRow("Far Plane");
                editedThisFrame |= ImGui::DragFloat("##FarPlane", &camera->farPlane, 1.0f, camera->nearPlane + 0.01f,
                                                    10000.0f);
                EndPropertyRow();

                PropertyRow("Main Camera");
                bool wasActive = camera->isActive;
                if (markEdited(ImGui::Checkbox("##IsActive", &camera->isActive)))
                {
                    // If we just enabled this camera, disable all other cameras
                    if (camera->isActive && !wasActive)
                    {
                        m_World->Each<CameraComponent>(
                            [this, camera](Entity entity, CameraComponent& otherCam)
                            {
                                (void)entity; // Unused
                                if (&otherCam != camera)
                                {
                                    otherCam.isActive = false;
                                }
                            });
                    }
                }
                EndPropertyRow();

                PropertyRow("Render World");
                bool renderWorld = (camera->renderMask & RenderLayerMask::World) != 0;
                if (markEdited(ImGui::Checkbox("##CameraRenderWorld", &renderWorld)))
                {
                    if (renderWorld)
                        camera->renderMask |= RenderLayerMask::World;
                    else
                        camera->renderMask &= ~RenderLayerMask::World;
                }
                EndPropertyRow();

                PropertyRow("Render Viewmodel");
                bool renderViewmodel = (camera->renderMask & RenderLayerMask::Viewmodel) != 0;
                if (markEdited(ImGui::Checkbox("##CameraRenderViewmodel", &renderViewmodel)))
                {
                    if (renderViewmodel)
                        camera->renderMask |= RenderLayerMask::Viewmodel;
                    else
                        camera->renderMask &= ~RenderLayerMask::Viewmodel;
                }
                EndPropertyRow();

                PropertyRow("Viewmodel Pass");
                editedThisFrame |= ImGui::Checkbox("##CameraEnableViewmodelPass", &camera->enableViewmodelPass);
                EndPropertyRow();

                if (camera->enableViewmodelPass)
                {
                    PropertyRow("Viewmodel Mask");
                    bool viewmodelMaskEnabled = (camera->viewmodelMask & RenderLayerMask::Viewmodel) != 0;
                    if (markEdited(ImGui::Checkbox("##CameraViewmodelMask", &viewmodelMaskEnabled)))
                    {
                        if (viewmodelMaskEnabled)
                            camera->viewmodelMask |= RenderLayerMask::Viewmodel;
                        else
                            camera->viewmodelMask &= ~RenderLayerMask::Viewmodel;
                    }
                    EndPropertyRow();

                    PropertyRow("Viewmodel FOV");
                    editedThisFrame |= ImGui::SliderFloat("##CameraViewmodelFov", &camera->viewmodelFov, 1.0f, 179.0f,
                                                          "%.1f deg");
                    EndPropertyRow();

                    PropertyRow("Viewmodel Near");
                    editedThisFrame |=
                        ImGui::DragFloat("##CameraViewmodelNearPlane", &camera->viewmodelNearPlane, 0.001f, 0.001f,
                                         camera->farPlane - 0.01f, "%.3f");
                    EndPropertyRow();
                }

                EndPropertyGrid();
            }

            if (camera->isActive)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "This camera will be used during Play");
            }
        }
    }

    // ---- RigidBody Component ----
    const bool physicsModuleEnabled = ToolboxManager::Get().IsPhysicsEnabled();
    const bool navigationModuleEnabled = ToolboxManager::Get().IsNavigationEnabled();
    const bool playerInputModuleEnabled = ToolboxManager::Get().IsPlayerInputEnabled();
    const bool scriptingModuleEnabled = ToolboxManager::Get().IsScriptingEnabled();
    RigidBodyComponent* rb = m_World->GetComponent<RigidBodyComponent>(m_SelectedEntity);
    BoxColliderComponent* box = m_World->GetComponent<BoxColliderComponent>(m_SelectedEntity);
    SphereColliderComponent* sphere = m_World->GetComponent<SphereColliderComponent>(m_SelectedEntity);
    CharacterControllerComponent* cc = m_World->GetComponent<CharacterControllerComponent>(m_SelectedEntity);
    if (rb)
    {
        if (!physicsModuleEnabled)
        {
            DrawDisabledModuleSection("Rigid Body", "Physics");
        }
        else if (ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (BeginPropertyGrid("RigidBodyGrid"))
            {
                PropertyRow("Mass");
                editedThisFrame |= ImGui::DragFloat("##Mass", &rb->mass, 0.1f, 0.01f, 1000.0f);
                EndPropertyRow();

                PropertyRow("Drag");
                editedThisFrame |= ImGui::DragFloat("##Drag", &rb->drag, 0.01f, 0.0f, 10.0f);
                EndPropertyRow();

                PropertyRow("Angular Drag");
                editedThisFrame |= ImGui::DragFloat("##AngularDrag", &rb->angularDrag, 0.01f, 0.0f, 10.0f);
                EndPropertyRow();

                PropertyRow("Use Gravity");
                editedThisFrame |= ImGui::Checkbox("##UseGravity", &rb->useGravity);
                EndPropertyRow();

                PropertyRow("Is Kinematic");
                editedThisFrame |= ImGui::Checkbox("##IsKinematic", &rb->isKinematic);
                EndPropertyRow();

                PropertyRow("Friction");
                editedThisFrame |= ImGui::SliderFloat("##Friction", &rb->friction, 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("0 = ice (no friction), 1 = sticky (high friction)");
                EndPropertyRow();

                PropertyRow("Bounciness");
                editedThisFrame |= ImGui::SliderFloat("##Bounciness", &rb->bounciness, 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("0 = no bounce, 1 = perfect bounce");
                EndPropertyRow();

                PropertyRow("Freeze Rotation");
                editedThisFrame |= ImGui::Checkbox("##FreezeRotation", &rb->freezeRotation);
                EndPropertyRow();

                EndPropertyGrid();
            }
        }
    }

    const EntityCollisionSettingsState collisionState = CollectEntityCollisionSettings(box, sphere, cc);
    if (collisionState.hasAny)
    {
        if (!physicsModuleEnabled)
        {
            DrawDisabledModuleSection("Collision", "Physics");
        }
        else if (ImGui::CollapsingHeader("Collision", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (BeginPropertyGrid("EntityCollisionGrid"))
            {
                PropertyRow("Applies To");
                ImGui::Text("%d component%s", collisionState.componentCount,
                            collisionState.componentCount == 1 ? "" : "s");
                EndPropertyRow();

                uint8 unifiedLayer = collisionState.layer;
                PropertyRow("Layer");
                if (markEdited(DrawCollisionLayerRow("##EntityCollisionLayer", unifiedLayer,
                                                     collisionState.mixedLayer ? "Multiple Values" : nullptr)))
                {
                    ApplyEntityCollisionLayer(unifiedLayer, box, sphere, cc);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Applies the selected layer to every collider/controller on this entity");
                EndPropertyRow();

                uint32 unifiedMask = collisionState.mask;
                PropertyRow("Mask");
                if (markEdited(DrawCollisionMaskRow("##EntityCollisionMask", unifiedMask,
                                                    collisionState.mixedMask ? "Multiple Values" : nullptr)))
                {
                    ApplyEntityCollisionMask(unifiedMask, box, sphere, cc);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(
                        "Applies the selected mask to every collider/controller on this entity. Project collision matrix still applies.");
                }
                EndPropertyRow();

                EndPropertyGrid();
            }

            if (collisionState.mixedLayer || collisionState.mixedMask)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.95f, 0.73f, 0.28f, 1.0f), "Mixed collision settings across attached components");
                ImGui::SameLine();
                if (markEdited(ImGui::Button("Sync to First Component")))
                {
                    ApplyEntityCollisionLayer(collisionState.layer, box, sphere, cc);
                    ApplyEntityCollisionMask(collisionState.mask, box, sphere, cc);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(
                        "Copies the first attached collision component's layer and mask to the rest of this entity's collision components");
                }
            }
        }
    }

    // ---- Box Collider Component ----
    if (box)
    {
        if (!physicsModuleEnabled)
        {
            DrawDisabledModuleSection("Box Collider", "Physics");
        }
        else if (ImGui::CollapsingHeader("Box Collider", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (BeginPropertyGrid("BoxColliderGrid"))
            {
                PropertyRow("Size");
                editedThisFrame |= ImGui::DragFloat3("##Size", &box->size.x, 0.1f);
                EndPropertyRow();

                PropertyRow("Center");
                editedThisFrame |= ImGui::DragFloat3("##Center", &box->center.x, 0.1f);
                EndPropertyRow();

                PropertyRow("Is Trigger");
                editedThisFrame |= ImGui::Checkbox("##IsTrigger", &box->isTrigger);
                EndPropertyRow();

                PropertyRow("Layer");
                editedThisFrame |= DrawCollisionLayerRow("##BoxCollisionLayer", box->collisionLayer);
                EndPropertyRow();

                PropertyRow("Mask");
                editedThisFrame |= DrawCollisionMaskRow("##BoxCollisionMask", box->collisionMask);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Project collision matrix also applies on top of this mask");
                EndPropertyRow();

                EndPropertyGrid();
            }
        }
    }

    // ---- Sphere Collider Component ----
    if (sphere)
    {
        if (!physicsModuleEnabled)
        {
            DrawDisabledModuleSection("Sphere Collider", "Physics");
        }
        else if (ImGui::CollapsingHeader("Sphere Collider", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (BeginPropertyGrid("SphereColliderGrid"))
            {
                PropertyRow("Radius");
                editedThisFrame |= ImGui::DragFloat("##Radius", &sphere->radius, 0.1f, 0.01f, 1000.0f);
                EndPropertyRow();

                PropertyRow("Center");
                editedThisFrame |= ImGui::DragFloat3("##Center", &sphere->center.x, 0.1f);
                EndPropertyRow();

                PropertyRow("Is Trigger");
                editedThisFrame |= ImGui::Checkbox("##IsTrigger", &sphere->isTrigger);
                EndPropertyRow();

                PropertyRow("Layer");
                editedThisFrame |= DrawCollisionLayerRow("##SphereCollisionLayer", sphere->collisionLayer);
                EndPropertyRow();

                PropertyRow("Mask");
                editedThisFrame |= DrawCollisionMaskRow("##SphereCollisionMask", sphere->collisionMask);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Project collision matrix also applies on top of this mask");
                EndPropertyRow();

                EndPropertyGrid();
            }
        }
    }

    // ---- Character Controller Component ----
    if (cc)
    {
        if (!physicsModuleEnabled)
        {
            DrawDisabledModuleSection("Character Controller", "Physics");
        }
        else if (ImGui::CollapsingHeader("Character Controller", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (BeginPropertyGrid("MovementGrid"))
            {
                PropertyRow("Move Speed");
                editedThisFrame |= ImGui::DragFloat("##MoveSpeed", &cc->moveSpeed, 0.1f, 0.0f, 100.0f);
                EndPropertyRow();

                PropertyRow("Sprint Multiplier");
                editedThisFrame |= ImGui::DragFloat("##SprintMultiplier", &cc->sprintMultiplier, 0.1f, 1.0f, 5.0f);
                EndPropertyRow();

                PropertyRow("Air Control");
                editedThisFrame |= ImGui::SliderFloat("##AirControl", &cc->airControl, 0.0f, 1.0f);
                EndPropertyRow();

                PropertyRow("Acceleration");
                editedThisFrame |= ImGui::DragFloat("##Acceleration", &cc->acceleration, 0.5f, 0.0f, 100.0f);
                EndPropertyRow();

                PropertyRow("Deceleration");
                editedThisFrame |= ImGui::DragFloat("##Deceleration", &cc->deceleration, 0.5f, 0.0f, 100.0f);
                EndPropertyRow();

                PropertyRow("Use Gravity");
                editedThisFrame |= ImGui::Checkbox("##CCUseGravity", &cc->useGravity);
                EndPropertyRow();

                PropertyRow("Gravity Mult");
                editedThisFrame |= ImGui::DragFloat("##GravityMult", &cc->gravityMultiplier, 0.1f, 0.0f, 5.0f);
                EndPropertyRow();

                PropertyRow("Jump Height");
                editedThisFrame |= ImGui::DragFloat("##JumpHeight", &cc->jumpHeight, 0.1f, 0.0f, 20.0f);
                EndPropertyRow();

                PropertyRow("Max Jumps");
                editedThisFrame |= ImGui::DragInt("##MaxJumps", &cc->maxJumps, 1, 1, 5);
                EndPropertyRow();

                PropertyRow("Coyote Time");
                editedThisFrame |= ImGui::DragFloat("##CoyoteTime", &cc->coyoteTime, 0.01f, 0.0f, 0.5f);
                EndPropertyRow();

                PropertyRow("Jump Buffer");
                editedThisFrame |= ImGui::DragFloat("##JumpBuffer", &cc->jumpBufferTime, 0.01f, 0.0f, 0.5f);
                EndPropertyRow();

                PropertyRow("Ground Dist");
                editedThisFrame |= ImGui::DragFloat("##GroundDist", &cc->groundCheckDistance, 0.01f, 0.0f, 1.0f);
                EndPropertyRow();

                PropertyRow("Is Grounded");
                ImGui::BeginDisabled();
                ImGui::Checkbox("##IsGrounded", &cc->isGrounded);
                ImGui::EndDisabled();
                EndPropertyRow();

                PropertyRow("Slope Limit");
                editedThisFrame |= ImGui::SliderFloat("##SlopeLimit", &cc->slopeLimit, 0.0f, 90.0f, "%.0f deg");
                EndPropertyRow();

                PropertyRow("Slide Steep");
                editedThisFrame |= ImGui::Checkbox("##SlideSteep", &cc->slideOnSteepSlopes);
                EndPropertyRow();

                if (cc->slideOnSteepSlopes)
                {
                    PropertyRow("Slide Speed");
                    editedThisFrame |= ImGui::DragFloat("##SlideSpeed", &cc->slideSpeed, 0.1f, 0.0f, 20.0f);
                    EndPropertyRow();
                }

                PropertyRow("Maintain Slope V");
                editedThisFrame |= ImGui::Checkbox("##MaintainSlopeV", &cc->maintainVelocityOnSlopes);
                EndPropertyRow();

                PropertyRow("Step Height");
                editedThisFrame |= ImGui::DragFloat("##StepHeight", &cc->stepHeight, 0.01f, 0.0f, 1.0f);
                EndPropertyRow();

                PropertyRow("Enable Stepping");
                editedThisFrame |= ImGui::Checkbox("##EnableStepping", &cc->enableStepping);
                EndPropertyRow();

                if (cc->enableStepping)
                {
                    PropertyRow("Step Smoothing");
                    editedThisFrame |= ImGui::Checkbox("##StepSmoothing", &cc->stepSmoothing);
                    EndPropertyRow();
                }

                PropertyRow("Push RBs");
                editedThisFrame |= ImGui::Checkbox("##PushRBs", &cc->pushRigidbodies);
                EndPropertyRow();

                if (cc->pushRigidbodies)
                {
                    PropertyRow("Push Force");
                    editedThisFrame |= ImGui::DragFloat("##PushForce", &cc->pushForce, 0.1f, 0.0f, 20.0f);
                    EndPropertyRow();
                }

                PropertyRow("Can Be Pushed");
                editedThisFrame |= ImGui::Checkbox("##CanBePushed", &cc->canBePushed);
                EndPropertyRow();

                PropertyRow("Skin Width");
                editedThisFrame |= ImGui::DragFloat("##SkinWidth", &cc->skinWidth, 0.001f, 0.001f, 0.1f);
                EndPropertyRow();

                PropertyRow("Slide Walls");
                editedThisFrame |= ImGui::Checkbox("##SlideWalls", &cc->slideAlongWalls);
                EndPropertyRow();

                PropertyRow("Max Slide Iter");
                editedThisFrame |= ImGui::DragInt("##MaxSlideIter", &cc->maxSlideIterations, 1, 1, 16);
                EndPropertyRow();

                PropertyRow("Layer");
                editedThisFrame |= DrawCollisionLayerRow("##CCCollisionLayer", cc->collisionLayer);
                EndPropertyRow();

                PropertyRow("Mask");
                editedThisFrame |= DrawCollisionMaskRow("##CCCollisionMask", cc->collisionMask);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Used by controller ground checks and wall sliding");
                EndPropertyRow();

                EndPropertyGrid();
            }
        }
    }

    // ---- Nav Agent Component ----
    NavAgentComponent* navAgent = m_World->GetComponent<NavAgentComponent>(m_SelectedEntity);
    if (navAgent)
    {
        bool removeNavAgent = false;
        if (!navigationModuleEnabled)
        {
            DrawDisabledModuleComponent("Nav Agent", "Navigation", &removeNavAgent);
        }
        else if (DrawComponentHeader("Nav Agent", &removeNavAgent))
        {
            if (BeginPropertyGrid("NavAgentGrid"))
            {
                PropertyRow("Move Speed");
                editedThisFrame |= ImGui::DragFloat("##NavAgentMoveSpeed", &navAgent->moveSpeed, 0.1f, 0.0f, 100.0f);
                EndPropertyRow();

                PropertyRow("Stopping Distance");
                editedThisFrame |=
                    ImGui::DragFloat("##NavAgentStoppingDistance", &navAgent->stoppingDistance, 0.01f, 0.0f, 20.0f);
                EndPropertyRow();

                PropertyRow("Projection Extent");
                editedThisFrame |=
                    ImGui::DragFloat3("##NavAgentProjectionExtent", &navAgent->projectionExtent.x, 0.1f, 0.1f, 32.0f);
                EndPropertyRow();

                EndPropertyGrid();
            }
        }
        if (removeNavAgent)
        {
            m_World->RemoveComponent<NavAgentComponent>(m_SelectedEntity);
            editedThisFrame = true;
        }
    }

    // ---- Player Input Component ----
    PlayerInputComponent* playerInput = m_World->GetComponent<PlayerInputComponent>(m_SelectedEntity);
    if (playerInput)
    {
        if (!playerInputModuleEnabled)
        {
            DrawDisabledModuleSection("Player Input", "Player Input");
        }
        else if (ImGui::CollapsingHeader("Player Input", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (BeginPropertyGrid("PlayerInputGrid"))
            {
                PropertyRow("Forward");
                ImGui::Text("%s", PlayerInputComponent::GetKeyName(playerInput->keyForward));
                EndPropertyRow();

                PropertyRow("Backward");
                ImGui::Text("%s", PlayerInputComponent::GetKeyName(playerInput->keyBackward));
                EndPropertyRow();

                PropertyRow("Left");
                ImGui::Text("%s", PlayerInputComponent::GetKeyName(playerInput->keyLeft));
                EndPropertyRow();

                PropertyRow("Right");
                ImGui::Text("%s", PlayerInputComponent::GetKeyName(playerInput->keyRight));
                EndPropertyRow();

                PropertyRow("Jump");
                ImGui::Text("%s", PlayerInputComponent::GetKeyName(playerInput->keyJump));
                EndPropertyRow();

                PropertyRow("Sprint");
                ImGui::Text("%s", PlayerInputComponent::GetKeyName(playerInput->keySprint));
                EndPropertyRow();

                PropertyRow("Mouse Look");
                editedThisFrame |= ImGui::Checkbox("##EnableMouseLook", &playerInput->enableMouseLook);
                EndPropertyRow();

                if (playerInput->enableMouseLook)
                {
                    PropertyRow("Sensitivity");
                    editedThisFrame |= ImGui::SliderFloat("##Sensitivity", &playerInput->mouseSensitivity, 0.01f, 1.0f);
                    EndPropertyRow();

                    PropertyRow("Invert Y");
                    editedThisFrame |= ImGui::Checkbox("##InvertY", &playerInput->invertY);
                    EndPropertyRow();

                    PropertyRow("Pitch Min");
                    editedThisFrame |= ImGui::DragFloat("##PitchMin", &playerInput->pitchMin, 1.0f, -89.0f, 0.0f);
                    EndPropertyRow();

                    PropertyRow("Pitch Max");
                    editedThisFrame |= ImGui::DragFloat("##PitchMax", &playerInput->pitchMax, 1.0f, 0.0f, 89.0f);
                    EndPropertyRow();
                }

                EndPropertyGrid();
            }

            // Runtime info
            if (playerInput->enableMouseLook)
            {
                ImGui::Spacing();
                ImGui::TextDisabled("Runtime Info:");
                if (BeginPropertyGrid("PlayerInputRuntimeGrid"))
                {
                    PropertyRow("Yaw");
                    ImGui::Text("%.2f", playerInput->lookYaw);
                    EndPropertyRow();

                    PropertyRow("Pitch");
                    ImGui::Text("%.2f", playerInput->lookPitch);
                    EndPropertyRow();

                    EndPropertyGrid();
                }
            }
        }
    }

    HealthComponent* health = m_World->GetComponent<HealthComponent>(m_SelectedEntity);
    if (health)
    {
        bool removeHealth = false;
        if (DrawComponentHeader("Health", &removeHealth))
        {
            health->Clamp();

            if (BeginPropertyGrid("HealthGrid"))
            {
                PropertyRow("Current");
                if (markEdited(ImGui::DragFloat("##CurrentHealth", &health->currentHealth, 1.0f, 0.0f, health->maxHealth)))
                {
                    health->Clamp();
                }
                EndPropertyRow();

                PropertyRow("Max");
                if (markEdited(ImGui::DragFloat("##MaxHealth", &health->maxHealth, 1.0f, 0.0f, 100000.0f)))
                {
                    health->Clamp();
                }
                EndPropertyRow();

                PropertyRow("Health %");
                const float healthPercent = health->GetHealthFraction();
                ImGui::ProgressBar(healthPercent, ImVec2(-1.0f, 0.0f));
                EndPropertyRow();

                PropertyRow("Invulnerable");
                editedThisFrame |= ImGui::Checkbox("##Invulnerable", &health->invulnerable);
                EndPropertyRow();

                PropertyRow("Destroy On Death");
                editedThisFrame |= ImGui::Checkbox("##DestroyOnDeath", &health->destroyEntityOnDeath);
                EndPropertyRow();

                PropertyRow("Is Dead");
                bool isDead = health->IsDead();
                ImGui::BeginDisabled();
                ImGui::Checkbox("##IsDead", &isDead);
                ImGui::EndDisabled();
                EndPropertyRow();

                EndPropertyGrid();
            }

            if (markEdited(ImGui::Button("Full Heal")))
            {
                health->RestoreFullHealth();
            }
            ImGui::SameLine();
            if (markEdited(ImGui::Button("Kill")))
            {
                health->SetCurrentHealth(0.0f);
            }
        }
        if (removeHealth)
        {
            m_World->RemoveComponent<HealthComponent>(m_SelectedEntity);
            editedThisFrame = true;
        }
    }

    // ---- Script Component ----
    ScriptComponent* scriptComp = m_World->GetComponent<ScriptComponent>(m_SelectedEntity);
    if (scriptComp)
    {
        bool removeScript = false;
        if (!scriptingModuleEnabled)
        {
            DrawDisabledModuleComponent("Script", "Scripting", &removeScript);
        }
        else if (DrawComponentHeader("Script", &removeScript))
        {
            if (BeginPropertyGrid("ScriptGrid"))
            {
                PropertyRow("Script Path");
                char pathBuf[256];
                strncpy(pathBuf, scriptComp->scriptPath.c_str(), sizeof(pathBuf) - 1);
                pathBuf[sizeof(pathBuf) - 1] = '\0';
                if (markEdited(ImGui::InputText("##ScriptPath", pathBuf, sizeof(pathBuf))))
                {
                    scriptComp->scriptPath = pathBuf;
                }
                EndPropertyRow();

                PropertyRow("Enabled");
                editedThisFrame |= ImGui::Checkbox("##ScriptEnabled", &scriptComp->enabled);
                EndPropertyRow();

                PropertyRow("Status");
                if (scriptComp->scriptRef >= 0)
                {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Loaded");
                }
                else if (!scriptComp->scriptPath.empty())
                {
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.0f), "Not loaded");
                }
                else
                {
                    ImGui::TextDisabled("No script");
                }
                EndPropertyRow();

                EndPropertyGrid();
            }
        }
        if (removeScript)
        {
            m_World->RemoveComponent<ScriptComponent>(m_SelectedEntity);
            editedThisFrame = true;
        }
    }

    // ---- Add Component Button ----
    ImGui::Separator();
    static char addComponentSearch[128] = {};
    if (ImGui::Button("Add Component", ImVec2(-1, 0)))
    {
        addComponentSearch[0] = '\0';
        ImGui::OpenPopup("AddComponentPopup");
    }

    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("AddComponentPopup"))
    {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##AddComponentSearch", "Search components...", addComponentSearch,
                                 sizeof(addComponentSearch));
        ImGui::Spacing();

        // Re-get pointers for MenuItems (to check for existence)
        DirectionalLightComponent* hasDirLight = m_World->GetComponent<DirectionalLightComponent>(m_SelectedEntity);
        PointLightComponent* hasPointLight = m_World->GetComponent<PointLightComponent>(m_SelectedEntity);
        SpotLightComponent* hasSpotLight = m_World->GetComponent<SpotLightComponent>(m_SelectedEntity);
        ReflectionProbeComponent* hasReflectionProbe = m_World->GetComponent<ReflectionProbeComponent>(m_SelectedEntity);
        CameraComponent* hasCamera = m_World->GetComponent<CameraComponent>(m_SelectedEntity);
        SkyboxComponent* hasSkybox = m_World->GetComponent<SkyboxComponent>(m_SelectedEntity);
        RenderLayerComponent* hasRenderLayer = m_World->GetComponent<RenderLayerComponent>(m_SelectedEntity);
        AttachmentPointComponent* hasAttachmentPoint = m_World->GetComponent<AttachmentPointComponent>(m_SelectedEntity);
        AttachmentBindingComponent* hasAttachmentBinding = m_World->GetComponent<AttachmentBindingComponent>(m_SelectedEntity);
        RigidBodyComponent* hasRigidBody = m_World->GetComponent<RigidBodyComponent>(m_SelectedEntity);
        BoxColliderComponent* hasBox = m_World->GetComponent<BoxColliderComponent>(m_SelectedEntity);
        SphereColliderComponent* hasSphere = m_World->GetComponent<SphereColliderComponent>(m_SelectedEntity);
        CharacterControllerComponent* hasCC = m_World->GetComponent<CharacterControllerComponent>(m_SelectedEntity);
        NavAgentComponent* hasNavAgent = m_World->GetComponent<NavAgentComponent>(m_SelectedEntity);
        PlayerInputComponent* hasPlayerInput = m_World->GetComponent<PlayerInputComponent>(m_SelectedEntity);
        HealthComponent* hasHealth = m_World->GetComponent<HealthComponent>(m_SelectedEntity);
        ScriptComponent* script = m_World->GetComponent<ScriptComponent>(m_SelectedEntity);
        const bool physicsEnabled = ToolboxManager::Get().IsPhysicsEnabled();
        const bool navigationEnabled = ToolboxManager::Get().IsNavigationEnabled();
        const bool playerInputEnabled = ToolboxManager::Get().IsPlayerInputEnabled();
        const bool scriptingEnabled = ToolboxManager::Get().IsScriptingEnabled();

        const AddComponentEntry renderingEntries[] = {
            {"Camera", "render view projection gameplay", !hasCamera, &AddComponentToEntity<CameraComponent>},
            {"Skybox", "environment cubemap sky background", !hasSkybox, &AddComponentToEntity<SkyboxComponent>},
            {"Render Layer", "world viewmodel overlay render mask", !hasRenderLayer, &AddComponentToEntity<RenderLayerComponent>},
            {"Attachment Point", "socket marker attach point", !hasAttachmentPoint, &AddComponentToEntity<AttachmentPointComponent>},
            {"Attachment Binding", "attach bind follow camera entity socket", !hasAttachmentBinding,
             &AddComponentToEntity<AttachmentBindingComponent>},
        };
        const AddComponentEntry lightingEntries[] = {
            {"Directional Light", "sun shadow light", !hasDirLight, &AddComponentToEntity<DirectionalLightComponent>},
            {"Point Light", "omni bulb light", !hasPointLight, &AddComponentToEntity<PointLightComponent>},
            {"Spot Light", "cone flashlight light", !hasSpotLight, &AddComponentToEntity<SpotLightComponent>},
            {"Reflection Probe", "local cubemap environment reflection", !hasReflectionProbe,
             &AddComponentToEntity<ReflectionProbeComponent>},
        };
        const AddComponentEntry physicsEntries[] = {
            {"Rigid Body", "mass velocity physics simulation", physicsEnabled && !hasRigidBody,
             &AddComponentToEntity<RigidBodyComponent>},
            {"Box Collider", "collision hitbox trigger box", physicsEnabled && !hasBox,
             &AddComponentToEntity<BoxColliderComponent>},
            {"Sphere Collider", "collision hitbox trigger sphere", physicsEnabled && !hasSphere,
             &AddComponentToEntity<SphereColliderComponent>},
            {"Character Controller", "player movement controller", physicsEnabled && !hasCC,
             &AddComponentToEntity<CharacterControllerComponent>},
        };
        const AddComponentEntry gameplayEntries[] = {
            {"Nav Agent", "navigation move pathfinding ai agent", navigationEnabled && !hasNavAgent,
             &AddComponentToEntity<NavAgentComponent>},
            {"Player Input", "wasd mouse look controls input", playerInputEnabled && !hasPlayerInput,
             &AddComponentToEntity<PlayerInputComponent>},
            {"Health", "damage heal death fps gameplay", !hasHealth, &AddComponentToEntity<HealthComponent>},
        };
        const AddComponentEntry scriptingEntries[] = {
            {"Script", "lua behavior gameplay logic", scriptingEnabled && !script, &AddComponentToEntity<ScriptComponent>},
        };

        bool addedComponent = false;
        bool matchedAnything = false;

        auto drawSection = [&](const char* title, const AddComponentEntry* entries, size_t count)
        {
            bool sectionHasMatches = false;
            for (size_t i = 0; i < count; ++i)
            {
                if (MatchesAddComponentSearch(entries[i], addComponentSearch))
                {
                    sectionHasMatches = true;
                    break;
                }
            }

            if (!sectionHasMatches)
                return;

            ImGui::TextDisabled("%s", title);
            for (size_t i = 0; i < count; ++i)
            {
                addedComponent |=
                    DrawAddComponentEntry(entries[i], addComponentSearch, *m_World, m_SelectedEntity, matchedAnything);
            }
            ImGui::Separator();
        };

        ImGui::BeginChild("AddComponentList", ImVec2(0.0f, 280.0f), false);
        drawSection("Rendering", renderingEntries, sizeof(renderingEntries) / sizeof(renderingEntries[0]));
        drawSection("Lighting", lightingEntries, sizeof(lightingEntries) / sizeof(lightingEntries[0]));
        drawSection("Physics", physicsEntries, sizeof(physicsEntries) / sizeof(physicsEntries[0]));
        drawSection("Gameplay", gameplayEntries, sizeof(gameplayEntries) / sizeof(gameplayEntries[0]));
        drawSection("Scripting", scriptingEntries, sizeof(scriptingEntries) / sizeof(scriptingEntries[0]));

        if (!matchedAnything)
            ImGui::TextDisabled("No components match \"%s\"", addComponentSearch);
        ImGui::EndChild();

        if (addedComponent)
        {
            editedThisFrame = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (editedThisFrame && !m_HasEditBaseline)
    {
        m_EditBaselineSnapshot = preFrameSnapshot;
        m_HasEditBaseline = true;
    }

    if (m_HasEditBaseline && !ImGui::IsAnyItemActive())
    {
        CommandRegistry::Get().PushCommand(std::make_unique<EntitySnapshotCommand>(
            m_World, m_SelectedEntity, m_EditBaselineSnapshot, CaptureEntitySnapshot(*m_World, m_SelectedEntity),
            "Edit Entity"));
        m_HasEditBaseline = false;
    }

    ImGui::End();
}

void InspectorPanel::ApplyFbxMaterial(const std::string& meshPath)
{
    ImportFbxMaterialsForAsset(meshPath);
    MeshComponent* mesh = m_World ? m_World->GetComponent<MeshComponent>(m_SelectedEntity) : nullptr;
    if (!mesh)
        return;

    if (mesh->submeshIndex < 0)
    {
        ClearExplicitMeshMaterialOverride(*m_World, m_SelectedEntity);
        return;
    }

    const std::vector<std::string> materialPaths = GetFbxSubmeshMaterialPaths(meshPath);
    AutoAssignImportedMeshMaterial(*m_World, m_SelectedEntity, materialPaths, mesh->submeshIndex);
}

} // namespace Dot
