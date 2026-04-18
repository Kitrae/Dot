// =============================================================================
// Dot Engine - Node Definition Macros
// =============================================================================
// Simplified macros for defining material nodes with minimal boilerplate.
// =============================================================================

#pragma once

#include "Core/Material/MaterialNode.h"
#include "Core/Material/NodeProperty.h"

#include <memory>

namespace Dot
{

// =============================================================================
// Helper: Pin Creation
// =============================================================================

inline MaterialPin MakeInputPin(int& pinIdCounter, const char* name, MaterialPinType type, float defaultFloat = 0.0f,
                                Vec3 defaultVec3 = Vec3(0, 0, 0))
{
    MaterialPin pin;
    pin.id = pinIdCounter++;
    pin.name = name;
    pin.type = type;
    pin.isInput = true;
    pin.defaultFloat = defaultFloat;
    pin.defaultVec3 = defaultVec3;
    return pin;
}

inline MaterialPin MakeOutputPin(int& pinIdCounter, const char* name, MaterialPinType type)
{
    MaterialPin pin;
    pin.id = pinIdCounter++;
    pin.name = name;
    pin.type = type;
    pin.isInput = false;
    return pin;
}

// =============================================================================
// Node Builder - Fluent API for defining nodes
// =============================================================================

class NodeBuilder
{
public:
    NodeBuilder& Input(const char* name, MaterialPinType type, float defaultVal = 0.0f)
    {
        m_Inputs.push_back({name, type, defaultVal, Vec3(0, 0, 0)});
        return *this;
    }

    NodeBuilder& InputVec3(const char* name, Vec3 defaultVal = Vec3(0, 0, 0))
    {
        m_Inputs.push_back({name, MaterialPinType::Vec3, 0.0f, defaultVal});
        return *this;
    }

    NodeBuilder& Output(const char* name, MaterialPinType type)
    {
        m_Outputs.push_back({name, type});
        return *this;
    }

    template <typename PropType> NodeBuilder& Property()
    {
        m_PropertyFactories.push_back([]() { return std::make_unique<PropType>(); });
        return *this;
    }

    // Build pins into a node
    void BuildPins(std::vector<MaterialPin>& inputs, std::vector<MaterialPin>& outputs, int& pinIdCounter) const
    {
        for (const auto& inp : m_Inputs)
        {
            inputs.push_back(MakeInputPin(pinIdCounter, inp.name, inp.type, inp.defaultFloat, inp.defaultVec3));
        }
        for (const auto& outp : m_Outputs)
        {
            outputs.push_back(MakeOutputPin(pinIdCounter, outp.name, outp.type));
        }
    }

    // Build properties
    std::vector<std::unique_ptr<NodeProperty>> BuildProperties() const
    {
        std::vector<std::unique_ptr<NodeProperty>> props;
        for (const auto& factory : m_PropertyFactories)
        {
            props.push_back(factory());
        }
        return props;
    }

private:
    struct InputDef
    {
        const char* name;
        MaterialPinType type;
        float defaultFloat;
        Vec3 defaultVec3;
    };
    struct OutputDef
    {
        const char* name;
        MaterialPinType type;
    };

    std::vector<InputDef> m_Inputs;
    std::vector<OutputDef> m_Outputs;
    std::vector<std::function<std::unique_ptr<NodeProperty>()>> m_PropertyFactories;
};

} // namespace Dot

// =============================================================================
// MACROS FOR SIMPLE NODE DEFINITIONS
// =============================================================================

/// Begin a node class definition
#define BEGIN_NODE_CLASS(ClassName, NodeTypeName, DisplayName)                                                         \
    class DOT_CORE_API ClassName : public MaterialNode                                                                 \
    {                                                                                                                  \
    public:                                                                                                            \
        ClassName();                                                                                                   \
        std::string GenerateHLSL(int outputIndex) const override;

/// End a node class definition
#define END_NODE_CLASS()                                                                                               \
    }                                                                                                                  \
    ;

/// Define a simple unary math node (1 input, 1 output, single HLSL expression)
/// Usage: DEFINE_UNARY_MATH_NODE(SinNode, Sin, "Sin", "sin($IN)")
#define DEFINE_UNARY_MATH_NODE_IMPL(ClassName, TypeName, DisplayName, HLSLExpr)                                        \
    ClassName::ClassName() : MaterialNode(MaterialNodeType::TypeName, DisplayName)                                     \
    {                                                                                                                  \
        MaterialPin in;                                                                                                \
        in.id = AllocatePinId();                                                                                       \
        in.name = "In";                                                                                                \
        in.type = MaterialPinType::Float;                                                                              \
        in.isInput = true;                                                                                             \
        m_Inputs.push_back(in);                                                                                        \
        MaterialPin out;                                                                                               \
        out.id = AllocatePinId();                                                                                      \
        out.name = "Out";                                                                                              \
        out.type = MaterialPinType::Float;                                                                             \
        out.isInput = false;                                                                                           \
        m_Outputs.push_back(out);                                                                                      \
    }                                                                                                                  \
    std::string ClassName::GenerateHLSL(int) const                                                                     \
    {                                                                                                                  \
        std::ostringstream ss;                                                                                         \
        ss << "float " << GetVarName(0) << " = " << HLSLExpr << ";";                                                   \
        return ss.str();                                                                                               \
    }

/// Define a simple binary math node (2 inputs, 1 output, single HLSL expression)
/// Usage: DEFINE_BINARY_MATH_NODE(AddNode, Add, "Add", "A + B")
#define DEFINE_BINARY_MATH_NODE_IMPL(ClassName, TypeName, DisplayName, HLSLExpr)                                       \
    ClassName::ClassName() : MaterialNode(MaterialNodeType::TypeName, DisplayName)                                     \
    {                                                                                                                  \
        MaterialPin a;                                                                                                 \
        a.id = AllocatePinId();                                                                                        \
        a.name = "A";                                                                                                  \
        a.type = MaterialPinType::Float;                                                                               \
        a.isInput = true;                                                                                              \
        m_Inputs.push_back(a);                                                                                         \
        MaterialPin b;                                                                                                 \
        b.id = AllocatePinId();                                                                                        \
        b.name = "B";                                                                                                  \
        b.type = MaterialPinType::Float;                                                                               \
        b.isInput = true;                                                                                              \
        m_Inputs.push_back(b);                                                                                         \
        MaterialPin out;                                                                                               \
        out.id = AllocatePinId();                                                                                      \
        out.name = "Result";                                                                                           \
        out.type = MaterialPinType::Float;                                                                             \
        out.isInput = false;                                                                                           \
        m_Outputs.push_back(out);                                                                                      \
    }                                                                                                                  \
    std::string ClassName::GenerateHLSL(int) const                                                                     \
    {                                                                                                                  \
        std::ostringstream ss;                                                                                         \
        ss << "float " << GetVarName(0) << " = " << HLSLExpr << ";";                                                   \
        return ss.str();                                                                                               \
    }

// =============================================================================
// NODE REGISTRATION MACRO
// =============================================================================

/// Register a node type in the factory switch statement
#define REGISTER_NODE_TYPE(TypeName, ClassName)                                                                        \
    case MaterialNodeType::TypeName:                                                                                   \
        return std::make_unique<ClassName>();
