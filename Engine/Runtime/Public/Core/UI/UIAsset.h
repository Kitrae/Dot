// =============================================================================
// Dot Engine - UI Asset Model
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/Math/Vec2.h"
#include "Core/Math/Vec4.h"

#include <string>
#include <vector>

namespace Dot
{

enum class UIWidgetType : uint8
{
    Panel = 0,
    Text,
    Image,
    Button,
    ProgressBar,
    Spacer
};

struct UILayout
{
    Vec2 anchorMin{0.0f, 0.0f};
    Vec2 anchorMax{1.0f, 1.0f};
    Vec2 offsetMin{0.0f, 0.0f};
    Vec2 offsetMax{0.0f, 0.0f};
    Vec2 size{0.0f, 0.0f};
    int zOrder = 0;
};

struct UIStyle
{
    Vec4 backgroundColor{0.0f, 0.0f, 0.0f, 0.0f};
    Vec4 textColor{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4 borderColor{0.0f, 0.0f, 0.0f, 1.0f};
    Vec4 padding{0.0f, 0.0f, 0.0f, 0.0f};
    float borderThickness = 0.0f;
    float opacity = 1.0f;
    std::string fontPath;
    std::string imagePath;
    std::string materialPath;
};

enum class UIStateValueType : uint8
{
    None = 0,
    String,
    Number,
    Boolean,
    Color,
    Progress
};

struct UIStateValue
{
    UIStateValueType type = UIStateValueType::None;
    std::string stringValue;
    Vec4 colorValue{1.0f, 1.0f, 1.0f, 1.0f};
    float numberValue = 0.0f;
    float progressValue = 0.0f;
    bool boolValue = false;
};

struct UIWidgetBinding
{
    std::string eventName;
    std::string callbackName;
};

struct UIWidgetNode
{
    std::string id;
    UIWidgetType type = UIWidgetType::Panel;
    bool visible = true;
    bool enabled = true;
    bool interactive = false;
    std::string text;
    std::string imagePath;
    std::string dataBindingKey;
    float progress = 0.0f;
    UILayout layout;
    UIStyle style;
    std::vector<UIWidgetBinding> eventBindings;
    std::vector<UIWidgetNode> children;
};

struct UIAsset
{
    uint32 version = 1;
    std::string name = "UI";
    UIWidgetNode root;
};

} // namespace Dot
