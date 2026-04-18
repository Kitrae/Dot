#include "Core/UI/UIOverlayContext.h"

#include <algorithm>

namespace Dot
{

UIOverlayItem* UIOverlayContext::Find(const std::string& id)
{
    for (UIOverlayItem& item : m_Items)
    {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

void UIOverlayContext::SetText(const std::string& id, const std::string& text, const Vec2& position,
                               const Vec4& color)
{
    UIOverlayItem* item = Find(id);
    if (!item)
    {
        m_Items.push_back(UIOverlayItem{});
        item = &m_Items.back();
        item->id = id;
    }

    item->type = UIOverlayItem::Type::Text;
    item->text = text;
    item->imagePath.clear();
    item->position = position;
    item->size = Vec2(0.0f, 0.0f);
    item->color = color;
    item->visible = true;
}

void UIOverlayContext::SetImage(const std::string& id, const std::string& imagePath, const Vec2& position,
                                const Vec2& size, const Vec4& color)
{
    UIOverlayItem* item = Find(id);
    if (!item)
    {
        m_Items.push_back(UIOverlayItem{});
        item = &m_Items.back();
        item->id = id;
    }

    item->type = UIOverlayItem::Type::Image;
    item->text.clear();
    item->imagePath = imagePath;
    item->position = position;
    item->size = size;
    item->color = color;
    item->visible = true;
}

void UIOverlayContext::Remove(const std::string& id)
{
    m_Items.erase(std::remove_if(m_Items.begin(), m_Items.end(),
                                 [&](const UIOverlayItem& item) { return item.id == id; }),
                  m_Items.end());
}

void UIOverlayContext::Clear()
{
    m_Items.clear();
}

} // namespace Dot
