// =============================================================================
// Dot Engine - MCP Bridge Service
// =============================================================================

#include "McpBridgeService.h"

#include "McpWindowsTransport.h"

#include "Core/Log.h"

#include <algorithm>
#include <system_error>

namespace Dot
{

McpBridgeService::McpBridgeService() = default;

McpBridgeService::~McpBridgeService()
{
    Shutdown();
}

void McpBridgeService::Configure(bool enabled, const std::filesystem::path& projectPath, const std::string& editorVersion)
{
    std::lock_guard<std::mutex> lock(m_StateMutex);

    m_Enabled = enabled;
    m_ProjectPath = projectPath;
    m_EditorVersion = editorVersion;
    m_PipeName = MakeMcpPipeName(::GetCurrentProcessId());
    m_ManifestPath = GetMcpManifestPath(::GetCurrentProcessId());

    if (enabled)
    {
        if (!m_Running)
            StartLocked();
        else
            WriteManifestLocked();
    }
    else
    {
        if (m_Running)
            StopLocked(false);
        else
            WriteManifestLocked();
    }
}

void McpBridgeService::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_StateMutex);
    StopLocked(true);
}

void McpBridgeService::StartLocked()
{
    m_StopRequested = false;
    m_LastError.clear();

    if (!WriteManifestLocked())
        return;

    m_Running = true;
    m_AcceptThread = std::thread([this]() { AcceptLoop(); });
    DOT_LOG_INFO("MCP bridge listening on %s", m_PipeName.c_str());
}

void McpBridgeService::StopLocked(bool removeManifest)
{
    m_StopRequested = true;

    const std::string pipeName = m_PipeName;
    if (m_Running)
    {
        HANDLE wakeClient =
            CreateFileA(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (wakeClient != INVALID_HANDLE_VALUE)
            CloseHandle(wakeClient);
    }

    if (m_AcceptThread.joinable())
        m_AcceptThread.join();
    m_Running = false;

    std::vector<std::shared_ptr<ClientConnection>> clients;
    {
        std::lock_guard<std::mutex> clientsLock(m_ClientsMutex);
        clients.swap(m_Clients);
    }

    for (const std::shared_ptr<ClientConnection>& client : clients)
    {
        client->alive = false;
        CloseHandleIfValid(client->pipe);
        if (client->readThread.joinable())
            client->readThread.join();
    }

    {
        std::lock_guard<std::mutex> requestLock(m_RequestMutex);
        m_PendingRequests.clear();
    }

    if (removeManifest)
    {
        std::error_code errorCode;
        std::filesystem::remove(m_ManifestPath, errorCode);
    }
    else
    {
        WriteManifestLocked();
    }
}

bool McpBridgeService::WriteManifestLocked()
{
    McpInstanceManifest manifest;
    manifest.pid = static_cast<uint32_t>(::GetCurrentProcessId());
    manifest.pipeName = m_PipeName;
    manifest.projectPath = m_ProjectPath.string();
    manifest.processStartTime = GetCurrentProcessStartTimeUnixSeconds();
    manifest.bridgeEnabled = m_Enabled;
    manifest.editorVersion = m_EditorVersion;

    if (!WriteManifestFile(manifest, &m_LastError))
    {
        DOT_LOG_ERROR("Failed to write MCP manifest: %s", m_LastError.c_str());
        return false;
    }

    return true;
}

void McpBridgeService::AcceptLoop()
{
    for (;;)
    {
        HANDLE pipe = CreateNamedPipeA(m_PipeName.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                       PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            std::lock_guard<std::mutex> lock(m_StateMutex);
            m_LastError = WindowsErrorMessage(GetLastError());
            DOT_LOG_ERROR("Failed to create MCP named pipe: %s", m_LastError.c_str());
            return;
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected)
        {
            const DWORD errorCode = GetLastError();
            CloseHandleIfValid(pipe);

            if (m_StopRequested)
                return;

            std::lock_guard<std::mutex> lock(m_StateMutex);
            m_LastError = WindowsErrorMessage(errorCode);
            DOT_LOG_ERROR("Failed to accept MCP client: %s", m_LastError.c_str());
            continue;
        }

        if (m_StopRequested)
        {
            CloseHandleIfValid(pipe);
            return;
        }

        auto connection = std::make_shared<ClientConnection>();
        connection->id = m_NextClientId.fetch_add(1);
        connection->pipe = pipe;
        connection->alive = true;
        DOT_LOG_INFO("MCP bridge accepted client %llu", static_cast<unsigned long long>(connection->id));

        {
            std::lock_guard<std::mutex> clientsLock(m_ClientsMutex);
            m_Clients.push_back(connection);
        }

        connection->readThread = std::thread([this, connection]() { ClientReadLoop(connection); });
    }
}

void McpBridgeService::ClientReadLoop(const std::shared_ptr<ClientConnection>& connection)
{
    while (connection->alive)
    {
        std::string payload;
        std::string errorMessage;
        if (!ReadFramedMessage(connection->pipe, payload, &errorMessage))
        {
            DOT_LOG_WARN("MCP bridge read failed for client %llu: %s",
                         static_cast<unsigned long long>(connection->id),
                         errorMessage.c_str());
            break;
        }

        const McpBridgeMessage message = ParseBridgeMessage(payload);
        if (message.type != McpBridgeMessage::Type::Request)
        {
            DOT_LOG_WARN("Ignoring invalid MCP bridge message from client %llu",
                         static_cast<unsigned long long>(connection->id));
            continue;
        }

        DOT_LOG_INFO("MCP bridge queued request from client %llu: %s",
                     static_cast<unsigned long long>(connection->id),
                     message.request.method.c_str());
        auto responseSlot = std::make_shared<PendingRequest::ResponseSlot>();
        QueueRequest(connection->id, message.request, responseSlot);

        std::unique_lock<std::mutex> responseLock(responseSlot->mutex);
        responseSlot->cv.wait(responseLock,
                              [this, connection, &responseSlot]()
                              {
                                  return responseSlot->ready || !connection->alive || m_StopRequested;
                              });
        if (!responseSlot->ready || !connection->alive)
            break;

        responseLock.unlock();

        std::lock_guard<std::mutex> writeLock(connection->writeMutex);
        if (!connection->alive || connection->pipe == INVALID_HANDLE_VALUE)
            break;

        DOT_LOG_INFO("MCP bridge sending response to client %llu for request %s",
                     static_cast<unsigned long long>(connection->id),
                     responseSlot->response.id.c_str());
        std::string writeError;
        if (!WriteFramedMessage(connection->pipe, SerializeBridgeMessage(responseSlot->response), &writeError))
        {
            connection->alive = false;
            DOT_LOG_WARN("Failed to write MCP bridge response: %s", writeError.c_str());
            CloseHandleIfValid(connection->pipe);
            break;
        }
    }

    connection->alive = false;
    CloseHandleIfValid(connection->pipe);
}

void McpBridgeService::QueueRequest(uint64_t clientId,
                                    const McpBridgeCommandRequest& request,
                                    const std::shared_ptr<PendingRequest::ResponseSlot>& responseSlot)
{
    std::lock_guard<std::mutex> lock(m_RequestMutex);
    PendingRequest pendingRequest;
    pendingRequest.clientId = clientId;
    pendingRequest.request = request;
    pendingRequest.responseSlot = responseSlot;
    m_PendingRequests.push_back(std::move(pendingRequest));
}

void McpBridgeService::Pump(const RequestHandler& handler)
{
    if (!handler)
        return;

    std::deque<PendingRequest> pending;
    {
        std::lock_guard<std::mutex> lock(m_RequestMutex);
        pending.swap(m_PendingRequests);
    }

    PruneDeadClients();

    for (PendingRequest& pendingRequest : pending)
    {
        const uint64_t clientId = pendingRequest.clientId;
        DOT_LOG_INFO("MCP bridge dispatching client %llu request %s",
                     static_cast<unsigned long long>(clientId),
                     pendingRequest.request.method.c_str());
        handler(
            pendingRequest.request,
            [responseSlot = pendingRequest.responseSlot](McpBridgeCommandResponse response)
            {
                if (!responseSlot)
                    return;

                std::lock_guard<std::mutex> responseLock(responseSlot->mutex);
                responseSlot->response = std::move(response);
                responseSlot->ready = true;
                responseSlot->cv.notify_one();
            });
    }
}

void McpBridgeService::NotifyToolsChanged()
{
    SendEventToAll(McpBridgeEvent{"tools_changed", McpJson::Value::MakeObject()});
}

void McpBridgeService::SendEventToAll(const McpBridgeEvent& event)
{
    std::vector<std::shared_ptr<ClientConnection>> clients;
    {
        std::lock_guard<std::mutex> lock(m_ClientsMutex);
        clients = m_Clients;
    }

    const std::string payload = SerializeBridgeMessage(event);
    for (const std::shared_ptr<ClientConnection>& client : clients)
    {
        if (!client || !client->alive)
            continue;

        std::lock_guard<std::mutex> writeLock(client->writeMutex);
        if (!client->alive || client->pipe == INVALID_HANDLE_VALUE)
            continue;

        std::string errorMessage;
        if (!WriteFramedMessage(client->pipe, payload, &errorMessage))
        {
            client->alive = false;
            DOT_LOG_WARN("Failed to write MCP bridge event: %s", errorMessage.c_str());
            CloseHandleIfValid(client->pipe);
        }
    }

    PruneDeadClients();
}

void McpBridgeService::PruneDeadClients()
{
    std::vector<std::shared_ptr<ClientConnection>> threadsToJoin;
    {
        std::lock_guard<std::mutex> lock(m_ClientsMutex);
        auto iterator = m_Clients.begin();
        while (iterator != m_Clients.end())
        {
            if (!(*iterator)->alive)
            {
                threadsToJoin.push_back(*iterator);
                iterator = m_Clients.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    for (const std::shared_ptr<ClientConnection>& connection : threadsToJoin)
    {
        if (connection->readThread.joinable() && connection->readThread.get_id() != std::this_thread::get_id())
            connection->readThread.join();
    }
}

McpBridgeService::Status McpBridgeService::GetStatus() const
{
    Status status;

    {
        std::lock_guard<std::mutex> lock(m_StateMutex);
        status.enabled = m_Enabled;
        status.running = m_Running;
        status.pipeName = m_PipeName;
        status.lastError = m_LastError;
        status.manifestPath = m_ManifestPath;
    }

    {
        std::lock_guard<std::mutex> lock(m_ClientsMutex);
        status.connectedClients = m_Clients.size();
    }

    return status;
}

} // namespace Dot
