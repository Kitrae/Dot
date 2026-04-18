// =============================================================================
// Dot Engine - Material Graph
// =============================================================================
// Container for material nodes and their connections.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/Material/MaterialNode.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Dot
{

// =============================================================================
// Connection between two pins
// =============================================================================

struct MaterialConnection
{
    int id = 0;           // Unique connection ID
    int outputPinId = -1; // Source pin (from node output)
    int inputPinId = -1;  // Destination pin (to node input)
};

// =============================================================================
// Material Graph - contains nodes and connections
// =============================================================================

class DOT_CORE_API MaterialGraph
{
public:
    MaterialGraph();
    ~MaterialGraph() = default;

    // Node management
    MaterialNode* AddNode(MaterialNodeType type);
    void RemoveNode(int nodeId);
    MaterialNode* GetNode(int nodeId);
    const std::vector<std::unique_ptr<MaterialNode>>& GetNodes() const { return m_Nodes; }

    // Connection management
    bool Connect(int outputPinId, int inputPinId);
    void Disconnect(int connectionId);
    void DisconnectPin(int pinId);
    const std::vector<MaterialConnection>& GetConnections() const { return m_Connections; }

    // Find connected pin
    int GetConnectedOutputPin(int inputPinId) const;
    MaterialNode* GetNodeByPinId(int pinId);
    const MaterialNode* GetNodeByPinId(int pinId) const;
    MaterialPin* GetPinById(int pinId);

    // Serialization
    bool SaveToFile(const std::string& path) const;
    bool LoadFromFile(const std::string& path);

    // Code generation
    std::string GenerateHLSL() const;

    // Get the output node (always exists)
    PBROutputNode* GetOutputNode() const;

private:
    std::vector<std::unique_ptr<MaterialNode>> m_Nodes;
    std::vector<MaterialConnection> m_Connections;
    int m_NextConnectionId = 1;

    // Helper: topological sort for code generation order
    std::vector<MaterialNode*> GetSortedNodes() const;
};

} // namespace Dot
