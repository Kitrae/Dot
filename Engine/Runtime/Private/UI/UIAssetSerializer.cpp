#include "Core/UI/UIAssetSerializer.h"

#include <fstream>
#include <iomanip>

namespace Dot
{

namespace
{

void WriteVec2(std::ostream& out, const char* prefix, const Vec2& value)
{
    out << prefix << " " << value.x << " " << value.y << "\n";
}

void WriteVec4(std::ostream& out, const char* prefix, const Vec4& value)
{
    out << prefix << " " << value.x << " " << value.y << " " << value.z << " " << value.w << "\n";
}

void WriteWidget(std::ostream& out, const UIWidgetNode& widget)
{
    out << "WIDGET " << static_cast<uint32>(widget.type) << " " << std::quoted(widget.id) << " " << widget.visible << " "
        << widget.enabled << " " << widget.interactive << " " << std::quoted(widget.text) << " "
        << std::quoted(widget.imagePath) << " " << std::quoted(widget.dataBindingKey) << " " << widget.progress << "\n";
    WriteVec2(out, "ANCHOR_MIN", widget.layout.anchorMin);
    WriteVec2(out, "ANCHOR_MAX", widget.layout.anchorMax);
    WriteVec2(out, "OFFSET_MIN", widget.layout.offsetMin);
    WriteVec2(out, "OFFSET_MAX", widget.layout.offsetMax);
    WriteVec2(out, "SIZE", widget.layout.size);
    out << "Z_ORDER " << widget.layout.zOrder << "\n";
    WriteVec4(out, "BACKGROUND", widget.style.backgroundColor);
    WriteVec4(out, "TEXT_COLOR", widget.style.textColor);
    WriteVec4(out, "BORDER_COLOR", widget.style.borderColor);
    WriteVec4(out, "PADDING", widget.style.padding);
    out << "STYLE " << widget.style.borderThickness << " " << widget.style.opacity << " "
        << std::quoted(widget.style.fontPath) << " " << std::quoted(widget.style.imagePath) << " "
        << std::quoted(widget.style.materialPath) << "\n";
    out << "BINDING_COUNT " << widget.eventBindings.size() << "\n";
    for (const UIWidgetBinding& binding : widget.eventBindings)
        out << "BINDING " << std::quoted(binding.eventName) << " " << std::quoted(binding.callbackName) << "\n";
    out << "CHILD_COUNT " << widget.children.size() << "\n";
    for (const UIWidgetNode& child : widget.children)
        WriteWidget(out, child);
    out << "END_WIDGET\n";
}

bool ReadVec2(std::istream& input, const char* expected, Vec2& outValue)
{
    std::string token;
    if (!(input >> token) || token != expected)
        return false;
    input >> outValue.x >> outValue.y;
    return true;
}

bool ReadVec4(std::istream& input, const char* expected, Vec4& outValue)
{
    std::string token;
    if (!(input >> token) || token != expected)
        return false;
    input >> outValue.x >> outValue.y >> outValue.z >> outValue.w;
    return true;
}

bool ReadWidget(std::istream& input, UIWidgetNode& outWidget)
{
    std::string token;
    if (!(input >> token) || token != "WIDGET")
        return false;

    uint32 type = 0;
    input >> type >> std::quoted(outWidget.id) >> outWidget.visible >> outWidget.enabled >> outWidget.interactive >>
        std::quoted(outWidget.text) >> std::quoted(outWidget.imagePath) >> std::quoted(outWidget.dataBindingKey) >>
        outWidget.progress;
    outWidget.type = static_cast<UIWidgetType>(type);

    if (!ReadVec2(input, "ANCHOR_MIN", outWidget.layout.anchorMin) || !ReadVec2(input, "ANCHOR_MAX", outWidget.layout.anchorMax) ||
        !ReadVec2(input, "OFFSET_MIN", outWidget.layout.offsetMin) || !ReadVec2(input, "OFFSET_MAX", outWidget.layout.offsetMax) ||
        !ReadVec2(input, "SIZE", outWidget.layout.size))
    {
        return false;
    }

    input >> token;
    if (token != "Z_ORDER")
        return false;
    input >> outWidget.layout.zOrder;

    if (!ReadVec4(input, "BACKGROUND", outWidget.style.backgroundColor) ||
        !ReadVec4(input, "TEXT_COLOR", outWidget.style.textColor) ||
        !ReadVec4(input, "BORDER_COLOR", outWidget.style.borderColor) || !ReadVec4(input, "PADDING", outWidget.style.padding))
    {
        return false;
    }

    input >> token;
    if (token != "STYLE")
        return false;
    input >> outWidget.style.borderThickness >> outWidget.style.opacity >> std::quoted(outWidget.style.fontPath) >>
        std::quoted(outWidget.style.imagePath) >> std::quoted(outWidget.style.materialPath);

    size_t bindingCount = 0;
    input >> token;
    if (token != "BINDING_COUNT")
        return false;
    input >> bindingCount;
    outWidget.eventBindings.clear();
    outWidget.eventBindings.reserve(bindingCount);
    for (size_t i = 0; i < bindingCount; ++i)
    {
        UIWidgetBinding binding;
        input >> token;
        if (token != "BINDING")
            return false;
        input >> std::quoted(binding.eventName) >> std::quoted(binding.callbackName);
        outWidget.eventBindings.push_back(std::move(binding));
    }

    size_t childCount = 0;
    input >> token;
    if (token != "CHILD_COUNT")
        return false;
    input >> childCount;
    outWidget.children.clear();
    outWidget.children.reserve(childCount);
    for (size_t i = 0; i < childCount; ++i)
    {
        UIWidgetNode child;
        if (!ReadWidget(input, child))
            return false;
        outWidget.children.push_back(std::move(child));
    }

    input >> token;
    return token == "END_WIDGET";
}

} // namespace

bool UIAssetSerializer::Save(const UIAsset& asset, const std::filesystem::path& path)
{
    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open())
    {
        m_LastError = "Failed to open UI asset for writing: " + path.string();
        return false;
    }

    output << "DOTUI 1\n";
    output << "NAME " << std::quoted(asset.name) << "\n";
    WriteWidget(output, asset.root);
    m_LastError.clear();
    return true;
}

bool UIAssetSerializer::Load(UIAsset& asset, const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input.is_open())
    {
        m_LastError = "Failed to open UI asset for reading: " + path.string();
        return false;
    }

    std::string token;
    uint32 version = 0;
    input >> token >> version;
    if (token != "DOTUI" || version != 1)
    {
        m_LastError = "Invalid UI asset header";
        return false;
    }

    asset = UIAsset{};
    asset.version = version;

    input >> token;
    if (token != "NAME")
    {
        m_LastError = "Expected NAME";
        return false;
    }
    input >> std::quoted(asset.name);

    if (!ReadWidget(input, asset.root))
    {
        m_LastError = "Failed to parse UI widget tree";
        return false;
    }

    m_LastError.clear();
    return true;
}

} // namespace Dot
