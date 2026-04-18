#include "Core/UI/UISystem.h"

#include "Core/Assets/AssetManager.h"
#include "Core/ECS/World.h"
#include "Core/Math/Mat4.h"
#include "Core/Scene/Components.h"
#include "Core/UI/UIAssetSerializer.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace Dot
{

namespace
{

ImVec2 ToImVec2(const Vec2& value)
{
    return ImVec2(value.x, value.y);
}

ImVec4 ToImVec4(const Vec4& value)
{
    return ImVec4(value.x, value.y, value.z, value.w);
}

Vec4 Brighten(const Vec4& color, float amount)
{
    return Vec4(std::min(1.0f, color.x + amount), std::min(1.0f, color.y + amount), std::min(1.0f, color.z + amount),
                color.w);
}

} // namespace

UISystem::UISystem() = default;
UISystem::~UISystem() = default;

bool UISystem::Initialize(World* world)
{
    m_World = world;
    m_Initialized = true;
    m_ActiveScreens.clear();
    m_LoadedAssets.clear();
    m_QueuedEvents.clear();
    m_OverlayContext.Clear();
    m_FocusedWidgetId.clear();
    return true;
}

void UISystem::Shutdown()
{
    m_ActiveScreens.clear();
    m_LoadedAssets.clear();
    m_QueuedEvents.clear();
    m_OverlayContext.Clear();
    m_FocusedWidgetId.clear();
    m_EventCallback = {};
    m_World = nullptr;
    m_Initialized = false;
}

void UISystem::Tick(float)
{
}

bool UISystem::LoadAsset(UIAsset& outAsset, const std::string& assetPath) const
{
    const auto it = m_LoadedAssets.find(assetPath);
    if (it != m_LoadedAssets.end())
    {
        outAsset = it->second;
        return true;
    }

    UIAssetSerializer serializer;
    if (!serializer.Load(outAsset, AssetManager::Get().GetFullPath(assetPath)))
        return false;

    m_LoadedAssets[assetPath] = outAsset;
    return true;
}

bool UISystem::ShowScreen(const std::string& assetPath)
{
    for (ActiveScreen& screen : m_ActiveScreens)
    {
        if (screen.assetPath == assetPath)
        {
            screen.visible = true;
            screen.modal = false;
            return true;
        }
    }

    UIAsset asset;
    if (!LoadAsset(asset, assetPath))
        return false;

    m_ActiveScreens.push_back(ActiveScreen{assetPath, std::move(asset), false, true});
    return true;
}

bool UISystem::PushModal(const std::string& assetPath)
{
    UIAsset asset;
    if (!LoadAsset(asset, assetPath))
        return false;

    m_ActiveScreens.push_back(ActiveScreen{assetPath, std::move(asset), true, true});
    return true;
}

bool UISystem::HideScreen(const std::string& assetPath)
{
    for (ActiveScreen& screen : m_ActiveScreens)
    {
        if (screen.assetPath == assetPath)
        {
            screen.visible = false;
            return true;
        }
    }
    return false;
}

void UISystem::PopModal()
{
    for (auto it = m_ActiveScreens.rbegin(); it != m_ActiveScreens.rend(); ++it)
    {
        if (it->modal)
        {
            m_ActiveScreens.erase(std::next(it).base());
            return;
        }
    }
}

void UISystem::ClearScreens()
{
    m_ActiveScreens.clear();
}

bool UISystem::PushScreen(const std::string& assetPath)
{
    return PushModal(assetPath);
}

void UISystem::PopScreen()
{
    PopModal();
}

UISystem::WidgetSearchResult UISystem::FindWidget(const std::string& widgetId)
{
    for (ActiveScreen& screen : m_ActiveScreens)
    {
        if (!screen.visible)
            continue;

        if (UIWidgetNode* widget = FindWidgetRecursive(screen.asset.root, widgetId))
            return WidgetSearchResult{widget, screen.assetPath};
    }

    return {};
}

UIWidgetNode* UISystem::FindWidgetRecursive(UIWidgetNode& node, const std::string& widgetId)
{
    if (node.id == widgetId)
        return &node;

    for (UIWidgetNode& child : node.children)
    {
        if (UIWidgetNode* match = FindWidgetRecursive(child, widgetId))
            return match;
    }

    return nullptr;
}

const UIWidgetNode* UISystem::FindWidgetRecursive(const UIWidgetNode& node, const std::string& widgetId)
{
    if (node.id == widgetId)
        return &node;

    for (const UIWidgetNode& child : node.children)
    {
        if (const UIWidgetNode* match = FindWidgetRecursive(child, widgetId))
            return match;
    }

    return nullptr;
}

bool UISystem::SetWidgetText(const std::string& widgetId, const std::string& text)
{
    WidgetSearchResult result = FindWidget(widgetId);
    if (!result.widget)
        return false;
    result.widget->text = text;
    return true;
}

bool UISystem::SetWidgetImage(const std::string& widgetId, const std::string& imagePath)
{
    WidgetSearchResult result = FindWidget(widgetId);
    if (!result.widget)
        return false;
    result.widget->imagePath = imagePath;
    result.widget->style.imagePath = imagePath;
    return true;
}

bool UISystem::SetWidgetProgress(const std::string& widgetId, float progress)
{
    WidgetSearchResult result = FindWidget(widgetId);
    if (!result.widget)
        return false;
    result.widget->progress = std::clamp(progress, 0.0f, 1.0f);
    return true;
}

bool UISystem::SetWidgetVisible(const std::string& widgetId, bool visible)
{
    WidgetSearchResult result = FindWidget(widgetId);
    if (!result.widget)
        return false;
    result.widget->visible = visible;
    return true;
}

bool UISystem::SetWidgetEnabled(const std::string& widgetId, bool enabled)
{
    WidgetSearchResult result = FindWidget(widgetId);
    if (!result.widget)
        return false;
    result.widget->enabled = enabled;
    return true;
}

bool UISystem::BindWidgetEvent(const std::string& widgetId, const std::string& eventName,
                               const std::string& callbackName)
{
    WidgetSearchResult result = FindWidget(widgetId);
    if (!result.widget)
        return false;

    for (UIWidgetBinding& binding : result.widget->eventBindings)
    {
        if (binding.eventName == eventName)
        {
            binding.callbackName = callbackName;
            return true;
        }
    }

    result.widget->eventBindings.push_back(UIWidgetBinding{eventName, callbackName});
    return true;
}

bool UISystem::QueueWidgetEvent(const std::string& widgetId, const std::string& eventName)
{
    WidgetSearchResult result = FindWidget(widgetId);
    if (!result.widget)
        return false;

    for (const UIWidgetBinding& binding : result.widget->eventBindings)
    {
        if (binding.eventName == eventName)
            m_QueuedEvents.push_back(UIEvent{widgetId, eventName, binding.callbackName, result.screenPath});
    }

    return true;
}

std::vector<UISystem::UIEvent> UISystem::ConsumeQueuedEvents()
{
    std::vector<UIEvent> events;
    events.swap(m_QueuedEvents);
    return events;
}

bool UISystem::FocusWidget(const std::string& widgetId)
{
    if (widgetId.empty())
    {
        m_FocusedWidgetId.clear();
        return true;
    }

    WidgetSearchResult result = FindWidget(widgetId);
    if (!result.widget)
        return false;

    m_FocusedWidgetId = widgetId;
    return true;
}

void UISystem::ClearFocus()
{
    m_FocusedWidgetId.clear();
}

void UISystem::OverlayText(const std::string& id, const std::string& text, float x, float y, float r, float g, float b,
                           float a, float)
{
    m_OverlayContext.SetText(id, text, Vec2(x, y), Vec4(r, g, b, a));
}

void UISystem::OverlayImage(const std::string& id, const std::string& imagePath, float x, float y, float width,
                            float height, float r, float g, float b, float a)
{
    m_OverlayContext.SetImage(id, imagePath, Vec2(x, y), Vec2(width, height), Vec4(r, g, b, a));
}

void UISystem::ClearOverlay()
{
    m_OverlayContext.Clear();
}

void UISystem::SetCameraInfo(const Vec3& position, const Vec3& forward, const Vec3& up, float fovDegrees, float nearZ,
                             float farZ)
{
    m_CameraPosition = position;
    m_CameraForward = forward;
    m_CameraUp = up;
    m_CameraFovDegrees = fovDegrees;
    m_CameraNearZ = nearZ;
    m_CameraFarZ = farZ;
}

Vec2 UISystem::ComputeWidgetSize(const UIWidgetNode& node, const Vec2& parentSize) const
{
    Vec2 size = node.layout.size;
    if (size.x <= 0.0f)
        size.x = std::max(40.0f, (node.layout.anchorMax.x - node.layout.anchorMin.x) * parentSize.x +
                                     (node.layout.offsetMax.x - node.layout.offsetMin.x));
    if (size.y <= 0.0f)
        size.y = std::max(24.0f, (node.layout.anchorMax.y - node.layout.anchorMin.y) * parentSize.y +
                                     (node.layout.offsetMax.y - node.layout.offsetMin.y));
    return size;
}

Vec2 UISystem::ComputeWidgetPosition(const UIWidgetNode& node, const Vec2& canvasOrigin, const Vec2& parentSize,
                                     const Vec2&) const
{
    return Vec2(canvasOrigin.x + node.layout.anchorMin.x * parentSize.x + node.layout.offsetMin.x,
                canvasOrigin.y + node.layout.anchorMin.y * parentSize.y + node.layout.offsetMin.y);
}

void UISystem::RenderWidget(UIWidgetNode& node, const std::string& screenPath, const Vec2& canvasOrigin,
                            const Vec2& canvasSize, int)
{
    if (!node.visible)
        return;

    const Vec2 widgetSize = ComputeWidgetSize(node, canvasSize);
    const Vec2 widgetPos = ComputeWidgetPosition(node, canvasOrigin, canvasSize, widgetSize);

    ImGui::PushID(node.id.c_str());
    ImGui::SetCursorScreenPos(ToImVec2(widgetPos));
    ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(node.style.textColor));
    ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(node.style.backgroundColor));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(Brighten(node.style.backgroundColor, 0.08f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImVec4(Brighten(node.style.backgroundColor, 0.14f)));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ToImVec4(node.style.backgroundColor));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(node.style.padding.x, node.style.padding.y));

    if (!node.enabled)
        ImGui::BeginDisabled();

    switch (node.type)
    {
        case UIWidgetType::Panel:
            ImGui::BeginChild(("UIPanel##" + node.id).c_str(), ToImVec2(widgetSize), true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            for (UIWidgetNode& child : node.children)
                RenderWidget(child, screenPath, widgetPos, widgetSize, 0);
            ImGui::EndChild();
            break;
        case UIWidgetType::Text:
            ImGui::TextWrapped("%s", node.text.c_str());
            break;
        case UIWidgetType::Image:
            ImGui::BeginChild(("UIImage##" + node.id).c_str(), ToImVec2(widgetSize), true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::TextUnformatted(node.imagePath.empty() ? "(Image)" : node.imagePath.c_str());
            ImGui::EndChild();
            break;
        case UIWidgetType::Button:
            if (ImGui::Button(node.text.empty() ? "Button" : node.text.c_str(), ToImVec2(widgetSize)))
            {
                m_FocusedWidgetId = node.id;
                QueueWidgetEvent(node.id, "click");
                if (m_EventCallback)
                    m_EventCallback(screenPath, node.id, "click");
            }
            break;
        case UIWidgetType::ProgressBar:
            ImGui::ProgressBar(node.progress, ToImVec2(widgetSize), node.text.empty() ? nullptr : node.text.c_str());
            break;
        case UIWidgetType::Spacer:
            ImGui::Dummy(ToImVec2(widgetSize));
            break;
    }

    if (!node.enabled)
        ImGui::EndDisabled();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    ImGui::PopID();
}

void UISystem::RenderScreen(ActiveScreen& screen, bool modal, int)
{
    if (!screen.visible)
        return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoBackground;
    if (!modal)
        flags |= ImGuiWindowFlags_NoInputs;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowBgAlpha(0.0f);
    const std::string name = "UIScreen##" + screen.assetPath;
    if (ImGui::Begin(name.c_str(), nullptr, flags))
    {
        RenderWidget(screen.asset.root, screen.assetPath, Vec2(viewport->Pos.x, viewport->Pos.y),
                     Vec2(viewport->Size.x, viewport->Size.y), 0);
    }
    ImGui::End();
}

bool UISystem::ProjectWorldPoint(const Vec3& point, Vec2& outScreen) const
{
    if (m_ViewportSize.x <= 0.0f || m_ViewportSize.y <= 0.0f)
        return false;

    const Mat4 view = Mat4::LookAt(m_CameraPosition, m_CameraPosition + m_CameraForward, m_CameraUp);
    const Mat4 projection = Mat4::Perspective(m_CameraFovDegrees * 0.0174532925f,
                                              m_ViewportSize.x / std::max(1.0f, m_ViewportSize.y), m_CameraNearZ,
                                              m_CameraFarZ);
    const Vec4 clip = projection * (view * Vec4(point, 1.0f));
    if (std::abs(clip.w) < 1e-5f || clip.w <= 0.0f)
        return false;

    const Vec3 ndc(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
    outScreen.x = (ndc.x * 0.5f + 0.5f) * m_ViewportSize.x;
    outScreen.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * m_ViewportSize.y;
    return ndc.z >= -1.0f && ndc.z <= 1.0f;
}

void UISystem::RenderWorldSpaceWidgets()
{
    if (!m_World)
        return;

    m_World->Each<TransformComponent, UIComponent>(
        [&](Entity entity, TransformComponent& transform, UIComponent& component)
        {
            const std::string assetPath = component.assetPath.empty() ? component.uiAssetPath : component.assetPath;
            const Vec2 drawSize = (component.drawSize.x > 0.0f && component.drawSize.y > 0.0f) ? component.drawSize : Vec2(component.drawWidth, component.drawHeight);

            if (!component.visible || assetPath.empty())
                return;

            UIAsset asset;
            if (!LoadAsset(asset, assetPath))
                return;

            Vec2 screenPos;
            if (!ProjectWorldPoint(transform.worldMatrix.GetTranslation(), screenPos))
                return;

            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::SetNextWindowPos(ToImVec2(screenPos), ImGuiCond_Always,
                                    ImVec2(component.billboard ? 0.5f : 0.0f, 0.5f));
            ImGui::SetNextWindowSize(ToImVec2(drawSize), ImGuiCond_Always);
            const std::string windowName = "UIWorld##" + assetPath + "_" + std::to_string(entity.GetIndex());
            if (ImGui::Begin(windowName.c_str(), nullptr,
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoBackground))
            {
                RenderWidget(asset.root, assetPath, Vec2(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y),
                             Vec2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y), 0);
            }
            ImGui::End();
        });
}

void UISystem::RenderImGui()
{
    if (!m_Initialized)
        return;

    for (ActiveScreen& screen : m_ActiveScreens)
        RenderScreen(screen, screen.modal, 0);

    RenderWorldSpaceWidgets();

    for (const UIOverlayItem& item : m_OverlayContext.GetItems())
    {
        if (!item.visible)
            continue;

        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::SetNextWindowPos(ToImVec2(item.position), ImGuiCond_Always);
        if (item.type == UIOverlayItem::Type::Image)
            ImGui::SetNextWindowSize(ToImVec2(item.size), ImGuiCond_Always);

        const std::string name = "UIOverlay##" + item.id;
        if (ImGui::Begin(name.c_str(), nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(item.color));
            if (item.type == UIOverlayItem::Type::Text)
                ImGui::TextUnformatted(item.text.c_str());
            else
                ImGui::TextUnformatted(item.imagePath.empty() ? "(Image)" : item.imagePath.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }
}

} // namespace Dot



