#pragma once

#include "Core/Core.h"
#include "Core/Math/Vec2.h"
#include "Core/Math/Vec4.h"

#include <string>
#include <vector>

namespace Dot
{

struct UIOverlayItem
{
    enum class Type : uint8
    {
        Text = 0,
        Image
    };

    std::string id;
    Type type = Type::Text;
    std::string text;
    std::string imagePath;
    Vec2 position{0.0f, 0.0f};
    Vec2 size{0.0f, 0.0f};
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    bool visible = true;
};

class DOT_CORE_API UIOverlayContext
{
public:
    void SetText(const std::string& id, const std::string& text, const Vec2& position,
                 const Vec4& color = Vec4(1.0f));
    void SetImage(const std::string& id, const std::string& imagePath, const Vec2& position, const Vec2& size,
                  const Vec4& color = Vec4(1.0f));
    void Remove(const std::string& id);
    void Clear();

    const std::vector<UIOverlayItem>& GetItems() const { return m_Items; }

private:
    UIOverlayItem* Find(const std::string& id);

    std::vector<UIOverlayItem> m_Items;
};

} // namespace Dot
