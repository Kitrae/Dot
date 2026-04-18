#include "UIAssetDocument.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <fstream>
#include <map>
#include <optional>
#include <unordered_set>
#include <sstream>

namespace Dot
{

namespace
{

struct JsonValue
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Object,
        Array
    };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::map<std::string, JsonValue> objectValue;
    std::vector<JsonValue> arrayValue;
};

class JsonParser
{
public:
    explicit JsonParser(const std::string& text) : m_Text(text) {}

    std::optional<JsonValue> Parse()
    {
        SkipWhitespace();
        const std::optional<JsonValue> value = ParseValue();
        if (!value.has_value())
            return std::nullopt;
        SkipWhitespace();
        return m_Pos == m_Text.size() ? value : std::nullopt;
    }

private:
    std::optional<JsonValue> ParseValue()
    {
        SkipWhitespace();
        if (m_Pos >= m_Text.size())
            return std::nullopt;

        const char ch = m_Text[m_Pos];
        switch (ch)
        {
            case '{':
                return ParseObject();
            case '[':
                return ParseArray();
            case '"':
                return ParseString();
            case 't':
            case 'f':
                return ParseBool();
            case 'n':
                return ParseNull();
            default:
                return ParseNumber();
        }
    }

    std::optional<JsonValue> ParseObject()
    {
        if (!Consume('{'))
            return std::nullopt;

        JsonValue value;
        value.type = JsonValue::Type::Object;

        SkipWhitespace();
        if (Consume('}'))
            return value;

        while (m_Pos < m_Text.size())
        {
            auto key = ParseString();
            if (!key.has_value())
                return std::nullopt;

            SkipWhitespace();
            if (!Consume(':'))
                return std::nullopt;

            auto entry = ParseValue();
            if (!entry.has_value())
                return std::nullopt;

            value.objectValue.emplace(std::move(key->stringValue), std::move(*entry));

            SkipWhitespace();
            if (Consume('}'))
                return value;
            if (!Consume(','))
                return std::nullopt;
        }

        return std::nullopt;
    }

    std::optional<JsonValue> ParseArray()
    {
        if (!Consume('['))
            return std::nullopt;

        JsonValue value;
        value.type = JsonValue::Type::Array;

        SkipWhitespace();
        if (Consume(']'))
            return value;

        while (m_Pos < m_Text.size())
        {
            auto entry = ParseValue();
            if (!entry.has_value())
                return std::nullopt;
            value.arrayValue.push_back(std::move(*entry));

            SkipWhitespace();
            if (Consume(']'))
                return value;
            if (!Consume(','))
                return std::nullopt;
        }

        return std::nullopt;
    }

    std::optional<JsonValue> ParseString()
    {
        if (!Consume('"'))
            return std::nullopt;

        JsonValue value;
        value.type = JsonValue::Type::String;

        std::string result;
        while (m_Pos < m_Text.size())
        {
            const char ch = m_Text[m_Pos++];
            if (ch == '"')
            {
                value.stringValue = std::move(result);
                return value;
            }

            if (ch == '\\')
            {
                if (m_Pos >= m_Text.size())
                    return std::nullopt;
                const char escaped = m_Text[m_Pos++];
                switch (escaped)
                {
                    case '"':
                        result.push_back('"');
                        break;
                    case '\\':
                        result.push_back('\\');
                        break;
                    case '/':
                        result.push_back('/');
                        break;
                    case 'b':
                        result.push_back('\b');
                        break;
                    case 'f':
                        result.push_back('\f');
                        break;
                    case 'n':
                        result.push_back('\n');
                        break;
                    case 'r':
                        result.push_back('\r');
                        break;
                    case 't':
                        result.push_back('\t');
                        break;
                    default:
                        result.push_back(escaped);
                        break;
                }
                continue;
            }

            result.push_back(ch);
        }

        return std::nullopt;
    }

    std::optional<JsonValue> ParseNumber()
    {
        const size_t start = m_Pos;
        if (m_Text[m_Pos] == '-')
            ++m_Pos;
        while (m_Pos < m_Text.size() && std::isdigit(static_cast<unsigned char>(m_Text[m_Pos])))
            ++m_Pos;
        if (m_Pos < m_Text.size() && m_Text[m_Pos] == '.')
        {
            ++m_Pos;
            while (m_Pos < m_Text.size() && std::isdigit(static_cast<unsigned char>(m_Text[m_Pos])))
                ++m_Pos;
        }
        if (m_Pos < m_Text.size() && (m_Text[m_Pos] == 'e' || m_Text[m_Pos] == 'E'))
        {
            ++m_Pos;
            if (m_Pos < m_Text.size() && (m_Text[m_Pos] == '+' || m_Text[m_Pos] == '-'))
                ++m_Pos;
            while (m_Pos < m_Text.size() && std::isdigit(static_cast<unsigned char>(m_Text[m_Pos])))
                ++m_Pos;
        }

        if (start == m_Pos)
            return std::nullopt;

        JsonValue value;
        value.type = JsonValue::Type::Number;
        try
        {
            value.numberValue = std::stod(m_Text.substr(start, m_Pos - start));
        }
        catch (...)
        {
            return std::nullopt;
        }
        return value;
    }

    std::optional<JsonValue> ParseBool()
    {
        JsonValue value;
        value.type = JsonValue::Type::Bool;
        if (m_Text.compare(m_Pos, 4, "true") == 0)
        {
            m_Pos += 4;
            value.boolValue = true;
            return value;
        }
        if (m_Text.compare(m_Pos, 5, "false") == 0)
        {
            m_Pos += 5;
            value.boolValue = false;
            return value;
        }
        return std::nullopt;
    }

    std::optional<JsonValue> ParseNull()
    {
        if (m_Text.compare(m_Pos, 4, "null") != 0)
            return std::nullopt;
        m_Pos += 4;
        JsonValue value;
        value.type = JsonValue::Type::Null;
        return value;
    }

    void SkipWhitespace()
    {
        while (m_Pos < m_Text.size() &&
               std::isspace(static_cast<unsigned char>(m_Text[m_Pos])))
            ++m_Pos;
    }

    bool Consume(char expected)
    {
        if (m_Pos >= m_Text.size() || m_Text[m_Pos] != expected)
            return false;
        ++m_Pos;
        return true;
    }

    const std::string& m_Text;
    size_t m_Pos = 0;
};

void Indent(std::ostream& os, int depth)
{
    for (int i = 0; i < depth; ++i)
        os << "  ";
}

void WriteEscapedString(std::ostream& os, const std::string& value)
{
    os << '"';
    for (char ch : value)
    {
        switch (ch)
        {
            case '"':
                os << "\\\"";
                break;
            case '\\':
                os << "\\\\";
                break;
            case '\n':
                os << "\\n";
                break;
            case '\r':
                os << "\\r";
                break;
            case '\t':
                os << "\\t";
                break;
            default:
                os << ch;
                break;
        }
    }
    os << '"';
}

JsonValue MakeStringValue(const std::string& value)
{
    JsonValue json;
    json.type = JsonValue::Type::String;
    json.stringValue = value;
    return json;
}

JsonValue MakeBoolValue(bool value)
{
    JsonValue json;
    json.type = JsonValue::Type::Bool;
    json.boolValue = value;
    return json;
}

JsonValue MakeNumberValue(double value)
{
    JsonValue json;
    json.type = JsonValue::Type::Number;
    json.numberValue = value;
    return json;
}

JsonValue MakeArrayValue(std::initializer_list<JsonValue> values)
{
    JsonValue json;
    json.type = JsonValue::Type::Array;
    json.arrayValue.assign(values.begin(), values.end());
    return json;
}

const JsonValue* FindField(const JsonValue& object, const char* key)
{
    if (object.type != JsonValue::Type::Object)
        return nullptr;
    const auto it = object.objectValue.find(key);
    return it != object.objectValue.end() ? &it->second : nullptr;
}

std::string GetString(const JsonValue* value, const std::string& fallback = {})
{
    if (!value || value->type != JsonValue::Type::String)
        return fallback;
    return value->stringValue;
}

bool GetBool(const JsonValue* value, bool fallback = false)
{
    if (!value || value->type != JsonValue::Type::Bool)
        return fallback;
    return value->boolValue;
}

float GetFloat(const JsonValue* value, float fallback = 0.0f)
{
    if (!value || value->type != JsonValue::Type::Number)
        return fallback;
    return static_cast<float>(value->numberValue);
}

int GetInt(const JsonValue* value, int fallback = 0)
{
    if (!value || value->type != JsonValue::Type::Number)
        return fallback;
    return static_cast<int>(value->numberValue);
}

JsonValue WriteLayoutJson(const UILayout& layout)
{
    JsonValue value;
    value.type = JsonValue::Type::Object;
    value.objectValue.emplace("anchorMin", JsonValue{JsonValue::Type::Array, false, 0.0, {}, {}, {MakeNumberValue(layout.anchorMinX), MakeNumberValue(layout.anchorMinY)}});
    value.objectValue.emplace("anchorMax", JsonValue{JsonValue::Type::Array, false, 0.0, {}, {}, {MakeNumberValue(layout.anchorMaxX), MakeNumberValue(layout.anchorMaxY)}});
    value.objectValue.emplace("offset", JsonValue{JsonValue::Type::Array, false, 0.0, {}, {}, {MakeNumberValue(layout.offsetX), MakeNumberValue(layout.offsetY)}});
    value.objectValue.emplace("size", JsonValue{JsonValue::Type::Array, false, 0.0, {}, {}, {MakeNumberValue(layout.sizeX), MakeNumberValue(layout.sizeY)}});
    value.objectValue.emplace("align", JsonValue{JsonValue::Type::Array, false, 0.0, {}, {}, {MakeNumberValue(layout.alignX), MakeNumberValue(layout.alignY)}});
    value.objectValue.emplace("zOrder", MakeNumberValue(layout.zOrder));
    value.objectValue.emplace("visible", MakeBoolValue(layout.visible));
    return value;
}

JsonValue WriteStyleJson(const UIStyle& style)
{
    JsonValue value;
    value.type = JsonValue::Type::Object;
    value.objectValue.emplace("background", JsonValue{JsonValue::Type::Array, false, 0.0, {}, {}, {MakeNumberValue(style.backgroundR), MakeNumberValue(style.backgroundG), MakeNumberValue(style.backgroundB), MakeNumberValue(style.backgroundA)}});
    value.objectValue.emplace("textColor", JsonValue{JsonValue::Type::Array, false, 0.0, {}, {}, {MakeNumberValue(style.textR), MakeNumberValue(style.textG), MakeNumberValue(style.textB), MakeNumberValue(style.textA)}});
    value.objectValue.emplace("borderColor", JsonValue{JsonValue::Type::Array, false, 0.0, {}, {}, {MakeNumberValue(style.borderR), MakeNumberValue(style.borderG), MakeNumberValue(style.borderB), MakeNumberValue(style.borderA)}});
    value.objectValue.emplace("borderThickness", MakeNumberValue(style.borderThickness));
    value.objectValue.emplace("padding", JsonValue{JsonValue::Type::Array, false, 0.0, {}, {}, {MakeNumberValue(style.paddingX), MakeNumberValue(style.paddingY)}});
    value.objectValue.emplace("opacity", MakeNumberValue(style.opacity));
    value.objectValue.emplace("fontPath", MakeStringValue(style.fontPath));
    value.objectValue.emplace("imagePath", MakeStringValue(style.imagePath));
    value.objectValue.emplace("materialPath", MakeStringValue(style.materialPath));
    return value;
}

JsonValue WriteWidgetJson(const UIWidgetNode& node)
{
    JsonValue value;
    value.type = JsonValue::Type::Object;
    value.objectValue.emplace("id", MakeStringValue(node.id));
    value.objectValue.emplace("name", MakeStringValue(node.name));
    value.objectValue.emplace("type", MakeStringValue(GetUIWidgetTypeName(node.type)));
    value.objectValue.emplace("text", MakeStringValue(node.text));
    value.objectValue.emplace("bindingKey", MakeStringValue(node.bindingKey));
    value.objectValue.emplace("onClickEvent", MakeStringValue(node.onClickEvent));
    value.objectValue.emplace("onChangeEvent", MakeStringValue(node.onChangeEvent));
    value.objectValue.emplace("progress", MakeNumberValue(node.progress));
    value.objectValue.emplace("enabled", MakeBoolValue(node.enabled));
    value.objectValue.emplace("layout", WriteLayoutJson(node.layout));
    value.objectValue.emplace("style", WriteStyleJson(node.style));

    JsonValue children;
    children.type = JsonValue::Type::Array;
    for (const UIWidgetNode& child : node.children)
        children.arrayValue.push_back(WriteWidgetJson(child));
    value.objectValue.emplace("children", std::move(children));
    return value;
}

bool ReadFloatPair(const JsonValue* value, float& a, float& b)
{
    if (!value || value->type != JsonValue::Type::Array || value->arrayValue.size() < 2)
        return false;
    a = GetFloat(&value->arrayValue[0], a);
    b = GetFloat(&value->arrayValue[1], b);
    return true;
}

bool ReadFloatQuad(const JsonValue* value, float& a, float& b, float& c, float& d)
{
    if (!value || value->type != JsonValue::Type::Array || value->arrayValue.size() < 4)
        return false;
    a = GetFloat(&value->arrayValue[0], a);
    b = GetFloat(&value->arrayValue[1], b);
    c = GetFloat(&value->arrayValue[2], c);
    d = GetFloat(&value->arrayValue[3], d);
    return true;
}

void ReadLayoutJson(const JsonValue* value, UILayout& layout)
{
    if (!value || value->type != JsonValue::Type::Object)
        return;
    ReadFloatPair(FindField(*value, "anchorMin"), layout.anchorMinX, layout.anchorMinY);
    ReadFloatPair(FindField(*value, "anchorMax"), layout.anchorMaxX, layout.anchorMaxY);
    ReadFloatPair(FindField(*value, "offset"), layout.offsetX, layout.offsetY);
    ReadFloatPair(FindField(*value, "size"), layout.sizeX, layout.sizeY);
    ReadFloatPair(FindField(*value, "align"), layout.alignX, layout.alignY);
    layout.zOrder = GetInt(FindField(*value, "zOrder"), layout.zOrder);
    layout.visible = GetBool(FindField(*value, "visible"), layout.visible);
}

void ReadStyleJson(const JsonValue* value, UIStyle& style)
{
    if (!value || value->type != JsonValue::Type::Object)
        return;
    ReadFloatQuad(FindField(*value, "background"), style.backgroundR, style.backgroundG, style.backgroundB, style.backgroundA);
    ReadFloatQuad(FindField(*value, "textColor"), style.textR, style.textG, style.textB, style.textA);
    ReadFloatQuad(FindField(*value, "borderColor"), style.borderR, style.borderG, style.borderB, style.borderA);
    ReadFloatPair(FindField(*value, "padding"), style.paddingX, style.paddingY);
    style.borderThickness = GetFloat(FindField(*value, "borderThickness"), style.borderThickness);
    style.opacity = GetFloat(FindField(*value, "opacity"), style.opacity);
    style.fontPath = GetString(FindField(*value, "fontPath"), style.fontPath);
    style.imagePath = GetString(FindField(*value, "imagePath"), style.imagePath);
    style.materialPath = GetString(FindField(*value, "materialPath"), style.materialPath);
}

void ReadWidgetJson(const JsonValue& value, UIWidgetNode& node)
{
    if (value.type != JsonValue::Type::Object)
        return;
    node.id = GetString(FindField(value, "id"), node.id);
    node.name = GetString(FindField(value, "name"), node.name);
    node.type = ParseUIWidgetType(GetString(FindField(value, "type"), GetUIWidgetTypeName(node.type)));
    node.text = GetString(FindField(value, "text"), node.text);
    node.bindingKey = GetString(FindField(value, "bindingKey"), node.bindingKey);
    node.onClickEvent = GetString(FindField(value, "onClickEvent"), node.onClickEvent);
    node.onChangeEvent = GetString(FindField(value, "onChangeEvent"), node.onChangeEvent);
    node.progress = GetFloat(FindField(value, "progress"), node.progress);
    node.enabled = GetBool(FindField(value, "enabled"), node.enabled);
    ReadLayoutJson(FindField(value, "layout"), node.layout);
    ReadStyleJson(FindField(value, "style"), node.style);

    node.children.clear();
    const JsonValue* children = FindField(value, "children");
    if (children && children->type == JsonValue::Type::Array)
    {
        for (const JsonValue& childValue : children->arrayValue)
        {
            UIWidgetNode child;
            ReadWidgetJson(childValue, child);
            node.children.push_back(std::move(child));
        }
    }
}

void WriteJson(std::ostream& os, const JsonValue& value, int indent)
{
    switch (value.type)
    {
        case JsonValue::Type::Null:
            os << "null";
            break;
        case JsonValue::Type::Bool:
            os << (value.boolValue ? "true" : "false");
            break;
        case JsonValue::Type::Number:
            os << value.numberValue;
            break;
        case JsonValue::Type::String:
            WriteEscapedString(os, value.stringValue);
            break;
        case JsonValue::Type::Array:
        {
            os << "[";
            if (!value.arrayValue.empty())
            {
                os << "\n";
                for (size_t i = 0; i < value.arrayValue.size(); ++i)
                {
                    Indent(os, indent + 1);
                    WriteJson(os, value.arrayValue[i], indent + 1);
                    if (i + 1 < value.arrayValue.size())
                        os << ",";
                    os << "\n";
                }
                Indent(os, indent);
            }
            os << "]";
            break;
        }
        case JsonValue::Type::Object:
        {
            os << "{";
            if (!value.objectValue.empty())
            {
                os << "\n";
                size_t index = 0;
                for (const auto& [key, entry] : value.objectValue)
                {
                    Indent(os, indent + 1);
                    WriteEscapedString(os, key);
                    os << ": ";
                    WriteJson(os, entry, indent + 1);
                    if (++index < value.objectValue.size())
                        os << ",";
                    os << "\n";
                }
                Indent(os, indent);
            }
            os << "}";
            break;
        }
    }
}

JsonValue BuildUIAssetJson(const UIAsset& asset)
{
    JsonValue root;
    root.type = JsonValue::Type::Object;

    JsonValue ui;
    ui.type = JsonValue::Type::Object;
    ui.objectValue.emplace("version", MakeNumberValue(asset.version));
    ui.objectValue.emplace("name", MakeStringValue(asset.name));
    ui.objectValue.emplace("root", WriteWidgetJson(asset.root));

    root.objectValue.emplace("ui", std::move(ui));
    return root;
}

bool ParseUIAssetJson(const JsonValue& root, UIAsset& asset)
{
    const JsonValue* ui = FindField(root, "ui");
    if (!ui || ui->type != JsonValue::Type::Object)
        return false;

    asset.version = GetInt(FindField(*ui, "version"), asset.version);
    asset.name = GetString(FindField(*ui, "name"), asset.name);
    const JsonValue* rootNode = FindField(*ui, "root");
    if (!rootNode)
        return false;
    ReadWidgetJson(*rootNode, asset.root);
    return true;
}

std::string GetDefaultChildId(const UIWidgetNode& parent, UIWidgetType type)
{
    const char* typeName = GetUIWidgetTypeName(type);
    int suffix = static_cast<int>(parent.children.size()) + 1;
    return std::string(typeName) + "_" + std::to_string(suffix);
}

std::string SanitizeWidgetIdImpl(const std::string& value)
{
    std::string result;
    result.reserve(value.size());

    bool lastWasUnderscore = false;
    for (char ch : value)
    {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch))
        {
            result.push_back(ch);
            lastWasUnderscore = false;
        }
        else if (!lastWasUnderscore)
        {
            result.push_back('_');
            lastWasUnderscore = true;
        }
    }

    while (!result.empty() && result.front() == '_')
        result.erase(result.begin());
    while (!result.empty() && result.back() == '_')
        result.pop_back();

    if (result.empty())
        result = "Widget";
    if (std::isdigit(static_cast<unsigned char>(result.front())))
        result.insert(result.begin(), '_');
    return result;
}

void CollectWidgetIds(const UIWidgetNode& node, std::unordered_set<std::string>& ids)
{
    ids.insert(node.id);
    for (const UIWidgetNode& child : node.children)
        CollectWidgetIds(child, ids);
}

bool FindWidgetPathRecursive(UIWidgetNode& node, const std::string& id, std::vector<UIWidgetNode*>& path)
{
    path.push_back(&node);
    if (node.id == id)
        return true;

    for (UIWidgetNode& child : node.children)
    {
        if (FindWidgetPathRecursive(child, id, path))
            return true;
    }

    path.pop_back();
    return false;
}

bool FindWidgetPathRecursive(const UIWidgetNode& node, const std::string& id, std::vector<const UIWidgetNode*>& path)
{
    path.push_back(&node);
    if (node.id == id)
        return true;

    for (const UIWidgetNode& child : node.children)
    {
        if (FindWidgetPathRecursive(child, id, path))
            return true;
    }

    path.pop_back();
    return false;
}

UIWidgetNode* FindParentRecursive(UIWidgetNode& node, const std::string& id)
{
    for (UIWidgetNode& child : node.children)
    {
        if (child.id == id)
            return &node;
        if (UIWidgetNode* found = FindParentRecursive(child, id))
            return found;
    }
    return nullptr;
}

const UIWidgetNode* FindParentRecursive(const UIWidgetNode& node, const std::string& id)
{
    for (const UIWidgetNode& child : node.children)
    {
        if (child.id == id)
            return &node;
        if (const UIWidgetNode* found = FindParentRecursive(child, id))
            return found;
    }
    return nullptr;
}

} // namespace

const char* GetUIWidgetTypeName(UIWidgetType type)
{
    switch (type)
    {
        case UIWidgetType::Panel:
            return "Panel";
        case UIWidgetType::Text:
            return "Text";
        case UIWidgetType::Image:
            return "Image";
        case UIWidgetType::Button:
            return "Button";
        case UIWidgetType::ProgressBar:
            return "ProgressBar";
        case UIWidgetType::Spacer:
            return "Spacer";
        default:
            return "Panel";
    }
}

UIWidgetType ParseUIWidgetType(const std::string& name)
{
    if (name == "Text")
        return UIWidgetType::Text;
    if (name == "Image")
        return UIWidgetType::Image;
    if (name == "Button")
        return UIWidgetType::Button;
    if (name == "ProgressBar")
        return UIWidgetType::ProgressBar;
    if (name == "Spacer")
        return UIWidgetType::Spacer;
    return UIWidgetType::Panel;
}

UIAssetDocument::UIAssetDocument()
{
    New();
}

void UIAssetDocument::New()
{
    m_Asset = UIAsset{};
    m_Asset.root.id = "Root";
    m_Asset.root.name = "Root";
    m_Asset.root.type = UIWidgetType::Panel;
    m_Asset.root.layout.anchorMinX = 0.0f;
    m_Asset.root.layout.anchorMinY = 0.0f;
    m_Asset.root.layout.anchorMaxX = 1.0f;
    m_Asset.root.layout.anchorMaxY = 1.0f;
    m_Asset.root.layout.sizeX = 0.0f;
    m_Asset.root.layout.sizeY = 0.0f;
    m_Path.clear();
    m_LastError.clear();
    m_Dirty = false;
    ++m_Revision;
    m_IdCacheRevision = 0;
    m_CachedWidgetIds.clear();
}

bool UIAssetDocument::Load(const std::filesystem::path& path)
{
    m_LastError.clear();
    std::ifstream file(path);
    if (!file.is_open())
    {
        m_LastError = "Failed to open UI asset for reading: " + path.string();
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    JsonParser parser(content);
    const std::optional<JsonValue> json = parser.Parse();
    if (!json.has_value() || !ParseUIAssetJson(*json, m_Asset))
    {
        m_LastError = "Failed to parse UI asset: " + path.string();
        return false;
    }

    NormalizeWidgetIds();
    m_Path = path;
    m_Dirty = false;
    ++m_Revision;
    return true;
}

bool UIAssetDocument::Save()
{
    if (m_Path.empty())
    {
        m_LastError = "UI asset path is not set.";
        return false;
    }
    return SaveAs(m_Path);
}

bool UIAssetDocument::SaveAs(const std::filesystem::path& path)
{
    m_LastError.clear();
    std::filesystem::create_directories(path.parent_path());

    std::ofstream file(path);
    if (!file.is_open())
    {
        m_LastError = "Failed to open UI asset for writing: " + path.string();
        return false;
    }

    const JsonValue json = BuildUIAssetJson(m_Asset);
    WriteJson(file, json, 0);
    file << "\n";

    m_Path = path;
    m_Dirty = false;
    ++m_Revision;
    return true;
}

UIWidgetNode* UIAssetDocument::FindWidget(const std::string& id)
{
    if (id.empty())
        return nullptr;

    std::function<UIWidgetNode*(UIWidgetNode&)> findRecursive = [&](UIWidgetNode& node) -> UIWidgetNode*
    {
        if (node.id == id)
            return &node;
        for (UIWidgetNode& child : node.children)
        {
            if (UIWidgetNode* found = findRecursive(child))
                return found;
        }
        return nullptr;
    };

    return findRecursive(m_Asset.root);
}

const UIWidgetNode* UIAssetDocument::FindWidget(const std::string& id) const
{
    if (id.empty())
        return nullptr;

    std::function<const UIWidgetNode*(const UIWidgetNode&)> findRecursive = [&](const UIWidgetNode& node) -> const UIWidgetNode*
    {
        if (node.id == id)
            return &node;
        for (const UIWidgetNode& child : node.children)
        {
            if (const UIWidgetNode* found = findRecursive(child))
                return found;
        }
        return nullptr;
    };

    return findRecursive(m_Asset.root);
}

UIWidgetNode* UIAssetDocument::FindParentWidget(const std::string& id)
{
    if (id.empty() || id == m_Asset.root.id)
        return nullptr;
    return FindParentRecursive(m_Asset.root, id);
}

const UIWidgetNode* UIAssetDocument::FindParentWidget(const std::string& id) const
{
    if (id.empty() || id == m_Asset.root.id)
        return nullptr;
    return FindParentRecursive(m_Asset.root, id);
}

std::vector<UIWidgetNode*> UIAssetDocument::GetWidgetPath(const std::string& id)
{
    std::vector<UIWidgetNode*> path;
    if (!id.empty())
        FindWidgetPathRecursive(m_Asset.root, id, path);
    return path;
}

std::vector<const UIWidgetNode*> UIAssetDocument::GetWidgetPath(const std::string& id) const
{
    std::vector<const UIWidgetNode*> path;
    if (!id.empty())
        FindWidgetPathRecursive(m_Asset.root, id, path);
    return path;
}

std::string UIAssetDocument::GetWidgetPathString(const std::string& id, const char* separator) const
{
    const std::vector<const UIWidgetNode*> path = GetWidgetPath(id);
    if (path.empty())
        return {};

    std::string result;
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (!result.empty())
            result += (separator ? separator : "/");
        result += path[i]->name.empty() ? path[i]->id : path[i]->name;
    }
    return result;
}

std::string UIAssetDocument::MakeUniqueWidgetId(const std::string& desiredId) const
{
    std::string base = SanitizeWidgetId(desiredId);
    if (base.empty())
        base = "Widget";

    EnsureIdCache();

    std::string candidate = base;
    int suffix = 1;
    while (m_CachedWidgetIds.find(candidate) != m_CachedWidgetIds.end())
        candidate = base + "_" + std::to_string(suffix++);
    return candidate;
}

std::string UIAssetDocument::SanitizeWidgetId(const std::string& value)
{
    return SanitizeWidgetIdImpl(value);
}

void UIAssetDocument::MarkDirty()
{
    m_Dirty = true;
    ++m_Revision;
}

void UIAssetDocument::EnsureIdCache() const
{
    if (m_IdCacheRevision == m_Revision)
        return;

    m_CachedWidgetIds.clear();
    CollectWidgetIds(m_Asset.root, m_CachedWidgetIds);
    m_IdCacheRevision = m_Revision;
}

void UIAssetDocument::NormalizeWidgetIds()
{
    std::unordered_set<std::string> ids;
    std::function<void(UIWidgetNode&)> normalize = [&](UIWidgetNode& node)
    {
        std::string candidate = SanitizeWidgetId(node.id);
        if (candidate.empty())
            candidate = "Widget";

        std::string unique = candidate;
        int suffix = 1;
        while (ids.find(unique) != ids.end())
            unique = candidate + "_" + std::to_string(suffix++);
        node.id = unique;
        ids.insert(unique);

        if (node.name.empty())
            node.name = unique;

        for (UIWidgetNode& child : node.children)
            normalize(child);
    };

    normalize(m_Asset.root);
    m_CachedWidgetIds = std::move(ids);
    m_IdCacheRevision = m_Revision;
}

} // namespace Dot
