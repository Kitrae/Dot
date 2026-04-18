#pragma once

#include "Core/Core.h"
#include "Core/Math/Vec2.h"
#include "Core/Math/Vec3.h"
#include "Core/UI/UIAsset.h"
#include "Core/UI/UIOverlayContext.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Dot
{

class World;

class DOT_CORE_API UISystem
{
public:
    struct ActiveScreen
    {
        std::string assetPath;
        UIAsset asset;
        bool modal = false;
        bool visible = true;
    };

    struct UIEvent
    {
        std::string widgetId;
        std::string eventName;
        std::string callbackName;
        std::string screenPath;
    };

    using EventCallback =
        std::function<void(const std::string& screenPath, const std::string& widgetId, const std::string& eventName)>;

    UISystem();
    ~UISystem();

    UISystem(const UISystem&) = delete;
    UISystem& operator=(const UISystem&) = delete;

    bool Initialize(World* world);
    void Shutdown();
    void Tick(float dt);

    bool ShowScreen(const std::string& assetPath);
    bool PushModal(const std::string& assetPath);
    bool HideScreen(const std::string& assetPath);
    void PopModal();
    void ClearScreens();
    bool PushScreen(const std::string& assetPath);
    void PopScreen();

    bool SetWidgetText(const std::string& widgetId, const std::string& text);
    bool SetWidgetImage(const std::string& widgetId, const std::string& imagePath);
    bool SetWidgetProgress(const std::string& widgetId, float progress);
    bool SetWidgetVisible(const std::string& widgetId, bool visible);
    bool SetWidgetEnabled(const std::string& widgetId, bool enabled);
    bool BindWidgetEvent(const std::string& widgetId, const std::string& eventName, const std::string& callbackName);
    bool QueueWidgetEvent(const std::string& widgetId, const std::string& eventName);
    std::vector<UIEvent> ConsumeQueuedEvents();

    bool FocusWidget(const std::string& widgetId);
    void ClearFocus();
    void SetFocus(const std::string& widgetId) { (void)FocusWidget(widgetId); }
    const std::string& GetFocus() const { return m_FocusedWidgetId; }
    const std::string& GetFocusedWidgetId() const { return m_FocusedWidgetId; }

    UIOverlayContext& GetOverlayContext() { return m_OverlayContext; }
    const UIOverlayContext& GetOverlayContext() const { return m_OverlayContext; }
    void OverlayText(const std::string& id, const std::string& text, float x, float y, float r, float g, float b,
                     float a = 1.0f, float fontSize = 16.0f);
    void OverlayImage(const std::string& id, const std::string& imagePath, float x, float y, float width, float height,
                      float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    void ClearOverlay();

    void SetViewportSize(const Vec2& size) { m_ViewportSize = size; }
    const Vec2& GetViewportSize() const { return m_ViewportSize; }
    void SetCameraInfo(const Vec3& position, const Vec3& forward, const Vec3& up, float fovDegrees, float nearZ,
                       float farZ);
    void SetEventCallback(EventCallback callback) { m_EventCallback = std::move(callback); }

    const std::vector<ActiveScreen>& GetActiveScreens() const { return m_ActiveScreens; }
    bool IsInitialized() const { return m_Initialized; }

    void RenderImGui();

private:
    struct WidgetSearchResult
    {
        UIWidgetNode* widget = nullptr;
        std::string screenPath;
    };

    bool LoadAsset(UIAsset& outAsset, const std::string& assetPath) const;
    WidgetSearchResult FindWidget(const std::string& widgetId);
    static UIWidgetNode* FindWidgetRecursive(UIWidgetNode& node, const std::string& widgetId);
    static const UIWidgetNode* FindWidgetRecursive(const UIWidgetNode& node, const std::string& widgetId);
    void RenderScreen(ActiveScreen& screen, bool modal, int zBias);
    void RenderWidget(UIWidgetNode& node, const std::string& screenPath, const Vec2& canvasOrigin,
                      const Vec2& canvasSize, int zBias);
    Vec2 ComputeWidgetSize(const UIWidgetNode& node, const Vec2& parentSize) const;
    Vec2 ComputeWidgetPosition(const UIWidgetNode& node, const Vec2& canvasOrigin, const Vec2& parentSize,
                               const Vec2& widgetSize) const;
    void RenderWorldSpaceWidgets();
    bool ProjectWorldPoint(const Vec3& point, Vec2& outScreen) const;

    World* m_World = nullptr;
    bool m_Initialized = false;
    std::vector<ActiveScreen> m_ActiveScreens;
    mutable std::unordered_map<std::string, UIAsset> m_LoadedAssets;
    std::vector<UIEvent> m_QueuedEvents;
    UIOverlayContext m_OverlayContext;
    std::string m_FocusedWidgetId;
    Vec2 m_ViewportSize{1280.0f, 720.0f};
    Vec3 m_CameraPosition{0.0f, 0.0f, 0.0f};
    Vec3 m_CameraForward{0.0f, 0.0f, 1.0f};
    Vec3 m_CameraUp{0.0f, 1.0f, 0.0f};
    float m_CameraFovDegrees = 60.0f;
    float m_CameraNearZ = 0.1f;
    float m_CameraFarZ = 1000.0f;
    EventCallback m_EventCallback;
};

} // namespace Dot
