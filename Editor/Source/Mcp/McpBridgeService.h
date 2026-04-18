#pragma once

#include "McpBridgeProtocol.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace Dot
{

class McpBridgeService
{
public:
    using Completion = std::function<void(McpBridgeCommandResponse)>;
    using RequestHandler = std::function<void(const McpBridgeCommandRequest&, Completion)>;

    struct Status
    {
        bool enabled = false;
        bool running = false;
        size_t connectedClients = 0;
        std::string pipeName;
        std::string lastError;
        std::filesystem::path manifestPath;
    };

    McpBridgeService();
    ~McpBridgeService();

    void Configure(bool enabled, const std::filesystem::path& projectPath, const std::string& editorVersion);
    void Shutdown();
    void Pump(const RequestHandler& handler);
    void NotifyToolsChanged();

    Status GetStatus() const;

private:
    struct ClientConnection
    {
        uint64_t id = 0;
        HANDLE pipe = INVALID_HANDLE_VALUE;
        std::mutex writeMutex;
        std::thread readThread;
        std::atomic_bool alive{false};
    };

    struct PendingRequest
    {
        struct ResponseSlot
        {
            std::mutex mutex;
            std::condition_variable cv;
            bool ready = false;
            McpBridgeCommandResponse response;
        };

        uint64_t clientId = 0;
        McpBridgeCommandRequest request;
        std::shared_ptr<ResponseSlot> responseSlot;
    };

    void StartLocked();
    void StopLocked(bool removeManifest);
    void AcceptLoop();
    void ClientReadLoop(const std::shared_ptr<ClientConnection>& connection);
    void QueueRequest(uint64_t clientId, const McpBridgeCommandRequest& request, const std::shared_ptr<PendingRequest::ResponseSlot>& responseSlot);
    void SendEventToAll(const McpBridgeEvent& event);
    void PruneDeadClients();
    bool WriteManifestLocked();

    mutable std::mutex m_StateMutex;
    mutable std::mutex m_ClientsMutex;
    mutable std::mutex m_RequestMutex;

    bool m_Enabled = false;
    bool m_Running = false;
    std::atomic_bool m_StopRequested{false};
    std::string m_PipeName;
    std::filesystem::path m_ProjectPath;
    std::string m_EditorVersion;
    std::filesystem::path m_ManifestPath;
    std::string m_LastError;

    std::thread m_AcceptThread;
    std::vector<std::shared_ptr<ClientConnection>> m_Clients;
    std::deque<PendingRequest> m_PendingRequests;
    std::atomic_uint64_t m_NextClientId{1};
};

} // namespace Dot
