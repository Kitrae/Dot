#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace Dot::McpJson
{

struct Value
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Object,
        Array,
    };

    using Object = std::map<std::string, Value>;
    using Array = std::vector<Value>;

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    Object objectValue;
    Array arrayValue;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool value) : type(Type::Bool), boolValue(value) {}
    Value(int value) : type(Type::Number), numberValue(static_cast<double>(value)) {}
    Value(uint32_t value) : type(Type::Number), numberValue(static_cast<double>(value)) {}
    Value(uint64_t value) : type(Type::Number), numberValue(static_cast<double>(value)) {}
    Value(double value) : type(Type::Number), numberValue(value) {}
    Value(std::string value) : type(Type::String), stringValue(std::move(value)) {}
    Value(const char* value) : type(Type::String), stringValue(value != nullptr ? value : "") {}

    static Value MakeObject()
    {
        Value value;
        value.type = Type::Object;
        return value;
    }

    static Value MakeArray()
    {
        Value value;
        value.type = Type::Array;
        return value;
    }

    bool IsNull() const { return type == Type::Null; }
    bool IsBool() const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsObject() const { return type == Type::Object; }
    bool IsArray() const { return type == Type::Array; }
};

class Parser
{
public:
    explicit Parser(std::string_view text) : m_Text(text) {}

    std::optional<Value> Parse()
    {
        SkipWhitespace();
        const std::optional<Value> value = ParseValue();
        if (!value.has_value())
            return std::nullopt;

        SkipWhitespace();
        return m_Pos == m_Text.size() ? value : std::nullopt;
    }

private:
    std::optional<Value> ParseValue()
    {
        SkipWhitespace();
        if (m_Pos >= m_Text.size())
            return std::nullopt;

        switch (m_Text[m_Pos])
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

    std::optional<Value> ParseObject()
    {
        if (!Consume('{'))
            return std::nullopt;

        Value value = Value::MakeObject();
        SkipWhitespace();
        if (Consume('}'))
            return value;

        while (m_Pos < m_Text.size())
        {
            std::optional<Value> key = ParseString();
            if (!key.has_value())
                return std::nullopt;

            SkipWhitespace();
            if (!Consume(':'))
                return std::nullopt;

            std::optional<Value> entry = ParseValue();
            if (!entry.has_value())
                return std::nullopt;

            value.objectValue.emplace(key->stringValue, std::move(*entry));

            SkipWhitespace();
            if (Consume('}'))
                return value;
            if (!Consume(','))
                return std::nullopt;
        }

        return std::nullopt;
    }

    std::optional<Value> ParseArray()
    {
        if (!Consume('['))
            return std::nullopt;

        Value value = Value::MakeArray();
        SkipWhitespace();
        if (Consume(']'))
            return value;

        while (m_Pos < m_Text.size())
        {
            std::optional<Value> entry = ParseValue();
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

    std::optional<Value> ParseString()
    {
        if (!Consume('"'))
            return std::nullopt;

        Value value;
        value.type = Value::Type::String;

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
                    case 'u':
                    {
                        if (m_Pos + 4 > m_Text.size())
                            return std::nullopt;

                        unsigned valueCode = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            const char hex = m_Text[m_Pos++];
                            valueCode <<= 4;
                            if (hex >= '0' && hex <= '9')
                                valueCode |= static_cast<unsigned>(hex - '0');
                            else if (hex >= 'a' && hex <= 'f')
                                valueCode |= static_cast<unsigned>(hex - 'a' + 10);
                            else if (hex >= 'A' && hex <= 'F')
                                valueCode |= static_cast<unsigned>(hex - 'A' + 10);
                            else
                                return std::nullopt;
                        }

                        if (valueCode <= 0x7F)
                        {
                            result.push_back(static_cast<char>(valueCode));
                        }
                        else if (valueCode <= 0x7FF)
                        {
                            result.push_back(static_cast<char>(0xC0 | ((valueCode >> 6) & 0x1F)));
                            result.push_back(static_cast<char>(0x80 | (valueCode & 0x3F)));
                        }
                        else
                        {
                            result.push_back(static_cast<char>(0xE0 | ((valueCode >> 12) & 0x0F)));
                            result.push_back(static_cast<char>(0x80 | ((valueCode >> 6) & 0x3F)));
                            result.push_back(static_cast<char>(0x80 | (valueCode & 0x3F)));
                        }
                        break;
                    }
                    default:
                        return std::nullopt;
                }
                continue;
            }

            result.push_back(ch);
        }

        return std::nullopt;
    }

    std::optional<Value> ParseNumber()
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

        Value value;
        value.type = Value::Type::Number;
        try
        {
            value.numberValue = std::stod(std::string(m_Text.substr(start, m_Pos - start)));
        }
        catch (...)
        {
            return std::nullopt;
        }
        return value;
    }

    std::optional<Value> ParseBool()
    {
        Value value;
        value.type = Value::Type::Bool;

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

    std::optional<Value> ParseNull()
    {
        if (m_Text.compare(m_Pos, 4, "null") != 0)
            return std::nullopt;

        m_Pos += 4;
        return Value{};
    }

    void SkipWhitespace()
    {
        while (m_Pos < m_Text.size() && std::isspace(static_cast<unsigned char>(m_Text[m_Pos])))
            ++m_Pos;
    }

    bool Consume(char ch)
    {
        SkipWhitespace();
        if (m_Pos < m_Text.size() && m_Text[m_Pos] == ch)
        {
            ++m_Pos;
            return true;
        }
        return false;
    }

    std::string_view m_Text;
    size_t m_Pos = 0;
};

inline void WriteEscapedString(std::ostream& stream, std::string_view text)
{
    stream.put('"');
    for (unsigned char ch : text)
    {
        switch (ch)
        {
            case '"':
                stream << "\\\"";
                break;
            case '\\':
                stream << "\\\\";
                break;
            case '\b':
                stream << "\\b";
                break;
            case '\f':
                stream << "\\f";
                break;
            case '\n':
                stream << "\\n";
                break;
            case '\r':
                stream << "\\r";
                break;
            case '\t':
                stream << "\\t";
                break;
            default:
                if (ch < 0x20)
                {
                    stream << "\\u" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(ch) << std::nouppercase << std::dec << std::setfill(' ');
                }
                else
                {
                    stream.put(static_cast<char>(ch));
                }
                break;
        }
    }
    stream.put('"');
}

inline void WriteValue(std::ostream& stream, const Value& value)
{
    switch (value.type)
    {
        case Value::Type::Null:
            stream << "null";
            return;
        case Value::Type::Bool:
            stream << (value.boolValue ? "true" : "false");
            return;
        case Value::Type::Number:
        {
            std::ostringstream numberStream;
            numberStream << std::setprecision(15) << value.numberValue;
            stream << numberStream.str();
            return;
        }
        case Value::Type::String:
            WriteEscapedString(stream, value.stringValue);
            return;
        case Value::Type::Object:
        {
            stream.put('{');
            bool first = true;
            for (const auto& [key, child] : value.objectValue)
            {
                if (!first)
                    stream.put(',');
                first = false;
                WriteEscapedString(stream, key);
                stream.put(':');
                WriteValue(stream, child);
            }
            stream.put('}');
            return;
        }
        case Value::Type::Array:
        {
            stream.put('[');
            for (size_t i = 0; i < value.arrayValue.size(); ++i)
            {
                if (i > 0)
                    stream.put(',');
                WriteValue(stream, value.arrayValue[i]);
            }
            stream.put(']');
            return;
        }
    }
}

inline std::string Serialize(const Value& value)
{
    std::ostringstream stream;
    WriteValue(stream, value);
    return stream.str();
}

inline std::optional<Value> Parse(std::string_view text)
{
    Parser parser(text);
    return parser.Parse();
}

inline Value::Object& EnsureObject(Value& value)
{
    if (!value.IsObject())
        value = Value::MakeObject();
    return value.objectValue;
}

inline Value::Array& EnsureArray(Value& value)
{
    if (!value.IsArray())
        value = Value::MakeArray();
    return value.arrayValue;
}

inline const Value* FindObjectMember(const Value& value, std::string_view key)
{
    if (!value.IsObject())
        return nullptr;

    const auto iterator = value.objectValue.find(std::string(key));
    return iterator != value.objectValue.end() ? &iterator->second : nullptr;
}

inline std::optional<std::string> GetString(const Value& value)
{
    if (!value.IsString())
        return std::nullopt;
    return value.stringValue;
}

inline std::optional<std::string> GetString(const Value& objectValue, std::string_view key)
{
    const Value* value = FindObjectMember(objectValue, key);
    return value != nullptr ? GetString(*value) : std::nullopt;
}

inline std::optional<double> GetNumber(const Value& value)
{
    if (!value.IsNumber())
        return std::nullopt;
    return value.numberValue;
}

inline std::optional<double> GetNumber(const Value& objectValue, std::string_view key)
{
    const Value* value = FindObjectMember(objectValue, key);
    return value != nullptr ? GetNumber(*value) : std::nullopt;
}

inline std::optional<bool> GetBool(const Value& value)
{
    if (!value.IsBool())
        return std::nullopt;
    return value.boolValue;
}

inline std::optional<bool> GetBool(const Value& objectValue, std::string_view key)
{
    const Value* value = FindObjectMember(objectValue, key);
    return value != nullptr ? GetBool(*value) : std::nullopt;
}

inline std::string GetStringOr(const Value& objectValue, std::string_view key, const std::string& fallback)
{
    const std::optional<std::string> value = GetString(objectValue, key);
    return value.has_value() ? *value : fallback;
}

inline bool GetBoolOr(const Value& objectValue, std::string_view key, bool fallback)
{
    const std::optional<bool> value = GetBool(objectValue, key);
    return value.has_value() ? *value : fallback;
}

} // namespace Dot::McpJson
