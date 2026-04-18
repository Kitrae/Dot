#pragma once

#include "McpJson.h"

#include <optional>
#include <string>

namespace Dot
{

struct McpBridgeCommandRequest
{
    std::string id;
    std::string method;
    McpJson::Value params = McpJson::Value::MakeObject();
};

struct McpBridgeCommandResponse
{
    std::string id;
    bool ok = false;
    McpJson::Value result = McpJson::Value::MakeObject();
    std::string errorCode;
    std::string errorMessage;
};

struct McpBridgeEvent
{
    std::string name;
    McpJson::Value params = McpJson::Value::MakeObject();
};

struct McpBridgeMessage
{
    enum class Type
    {
        Invalid,
        Request,
        Response,
        Event,
    };

    Type type = Type::Invalid;
    McpBridgeCommandRequest request;
    McpBridgeCommandResponse response;
    McpBridgeEvent event;
};

inline McpJson::Value ToJson(const McpBridgeCommandRequest& request)
{
    McpJson::Value value = McpJson::Value::MakeObject();
    auto& object = value.objectValue;
    object["kind"] = "request";
    object["id"] = request.id;
    object["method"] = request.method;
    object["params"] = request.params;
    return value;
}

inline McpJson::Value ToJson(const McpBridgeCommandResponse& response)
{
    McpJson::Value value = McpJson::Value::MakeObject();
    auto& object = value.objectValue;
    object["kind"] = "response";
    object["id"] = response.id;
    object["ok"] = response.ok;
    if (response.ok)
    {
        object["result"] = response.result;
    }
    else
    {
        object["result"] = response.result;
        object["errorCode"] = response.errorCode;
        object["errorMessage"] = response.errorMessage;
    }
    return value;
}

inline McpJson::Value ToJson(const McpBridgeEvent& event)
{
    McpJson::Value value = McpJson::Value::MakeObject();
    auto& object = value.objectValue;
    object["kind"] = "event";
    object["name"] = event.name;
    object["params"] = event.params;
    return value;
}

inline std::string SerializeBridgeMessage(const McpBridgeCommandRequest& request)
{
    return McpJson::Serialize(ToJson(request));
}

inline std::string SerializeBridgeMessage(const McpBridgeCommandResponse& response)
{
    return McpJson::Serialize(ToJson(response));
}

inline std::string SerializeBridgeMessage(const McpBridgeEvent& event)
{
    return McpJson::Serialize(ToJson(event));
}

inline std::optional<McpBridgeCommandRequest> ParseBridgeRequest(const McpJson::Value& value)
{
    if (!value.IsObject() || McpJson::GetStringOr(value, "kind", "") != "request")
        return std::nullopt;

    const std::optional<std::string> id = McpJson::GetString(value, "id");
    const std::optional<std::string> method = McpJson::GetString(value, "method");
    if (!id.has_value() || !method.has_value())
        return std::nullopt;

    McpBridgeCommandRequest request;
    request.id = *id;
    request.method = *method;
    if (const McpJson::Value* params = McpJson::FindObjectMember(value, "params"))
        request.params = *params;
    return request;
}

inline std::optional<McpBridgeCommandResponse> ParseBridgeResponse(const McpJson::Value& value)
{
    if (!value.IsObject() || McpJson::GetStringOr(value, "kind", "") != "response")
        return std::nullopt;

    const std::optional<std::string> id = McpJson::GetString(value, "id");
    const std::optional<bool> ok = McpJson::GetBool(value, "ok");
    if (!id.has_value() || !ok.has_value())
        return std::nullopt;

    McpBridgeCommandResponse response;
    response.id = *id;
    response.ok = *ok;
    if (const McpJson::Value* result = McpJson::FindObjectMember(value, "result"))
        response.result = *result;
    response.errorCode = McpJson::GetStringOr(value, "errorCode", "");
    response.errorMessage = McpJson::GetStringOr(value, "errorMessage", "");
    return response;
}

inline std::optional<McpBridgeEvent> ParseBridgeEvent(const McpJson::Value& value)
{
    if (!value.IsObject() || McpJson::GetStringOr(value, "kind", "") != "event")
        return std::nullopt;

    const std::optional<std::string> name = McpJson::GetString(value, "name");
    if (!name.has_value())
        return std::nullopt;

    McpBridgeEvent event;
    event.name = *name;
    if (const McpJson::Value* params = McpJson::FindObjectMember(value, "params"))
        event.params = *params;
    return event;
}

inline McpBridgeMessage ParseBridgeMessage(const std::string& payload)
{
    McpBridgeMessage message;
    const std::optional<McpJson::Value> value = McpJson::Parse(payload);
    if (!value.has_value())
        return message;

    if (std::optional<McpBridgeCommandRequest> request = ParseBridgeRequest(*value))
    {
        message.type = McpBridgeMessage::Type::Request;
        message.request = std::move(*request);
        return message;
    }

    if (std::optional<McpBridgeCommandResponse> response = ParseBridgeResponse(*value))
    {
        message.type = McpBridgeMessage::Type::Response;
        message.response = std::move(*response);
        return message;
    }

    if (std::optional<McpBridgeEvent> event = ParseBridgeEvent(*value))
    {
        message.type = McpBridgeMessage::Type::Event;
        message.event = std::move(*event);
        return message;
    }

    return message;
}

inline McpJson::Value MakeTextContent(const std::string& text)
{
    McpJson::Value content = McpJson::Value::MakeObject();
    content.objectValue["type"] = "text";
    content.objectValue["text"] = text;
    return content;
}

inline McpJson::Value MakeToolResult(const McpJson::Value& structuredContent, const std::string& text, bool isError = false)
{
    McpJson::Value result = McpJson::Value::MakeObject();
    McpJson::Value content = McpJson::Value::MakeArray();
    content.arrayValue.push_back(MakeTextContent(text));
    result.objectValue["content"] = std::move(content);
    result.objectValue["structuredContent"] = structuredContent;
    result.objectValue["isError"] = isError;
    return result;
}

inline McpBridgeCommandResponse MakeBridgeSuccess(const std::string& id, const McpJson::Value& result)
{
    McpBridgeCommandResponse response;
    response.id = id;
    response.ok = true;
    response.result = result;
    return response;
}

inline McpBridgeCommandResponse MakeBridgeError(const std::string& id,
                                                const std::string& errorCode,
                                                const std::string& errorMessage,
                                                const McpJson::Value& result = McpJson::Value::MakeObject())
{
    McpBridgeCommandResponse response;
    response.id = id;
    response.ok = false;
    response.result = result;
    response.errorCode = errorCode;
    response.errorMessage = errorMessage;
    return response;
}

} // namespace Dot
