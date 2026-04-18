// =============================================================================
// Dot Engine - Material Node System
// =============================================================================
// Base node class and concrete node types for the material graph editor.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/Math/Vec3.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Dot
{
// Forward declaration for property system
class NodeProperty;

// =============================================================================
// Pin Types (data types that flow between nodes)
// =============================================================================

enum class MaterialPinType : uint8_t
{
    Float,  // Single float value
    Vec2,   // 2D vector
    Vec3,   // 3D vector (color, direction)
    Vec4,   // 4D vector (color with alpha)
    Texture // Texture sampler
};

// =============================================================================
// Pin Definition
// =============================================================================

struct MaterialPin
{
    int id = 0;       // Unique pin ID
    std::string name; // Display name
    MaterialPinType type = MaterialPinType::Float;
    bool isInput = true;  // Input or output pin
    int linkedPinId = -1; // Connected pin (-1 = not connected)

    // Default value (for inputs without connections)
    float defaultFloat = 0.0f;
    Vec3 defaultVec3{0.0f, 0.0f, 0.0f};
};

// =============================================================================
// Node Types
// =============================================================================

enum class MaterialNodeType : uint8_t
{
    // Output
    PBROutput, // Final material output (albedo, metallic, roughness, etc.)

    // Constants
    ConstFloat, // Float constant
    ConstVec3,  // Vec3 constant (color)
    ConstVec4,  // Vec4 constant (color + alpha)

    // Texture
    Texture2D,    // Sample a 2D texture
    TextureCoord, // Get UV coordinates

    // Math - Two inputs
    Add,      // A + B
    Subtract, // A - B
    Multiply, // A * B
    Divide,   // A / B
    Power,    // A ^ B
    Min,      // min(A, B)
    Max,      // max(A, B)

    // Math - Single input
    Abs,      // |A|
    Negate,   // -A
    OneMinus, // 1 - A
    Saturate, // clamp(A, 0, 1)
    Sin,      // sin(A)
    Cos,      // cos(A)
    Frac,     // frac(A)
    Floor,    // floor(A)
    Ceil,     // ceil(A)
    Sqrt,     // sqrt(A)

    // Math - Three inputs
    Lerp,       // Linear interpolate
    Clamp,      // Clamp to min/max
    Smoothstep, // Smoothstep interpolation

    // Vector
    SplitVec3,    // Break Vec3 into R, G, B floats
    MakeVec3,     // Combine R, G, B floats into Vec3
    Normalize,    // Normalize vector
    Dot,          // Dot product
    CrossProduct, // Cross product

    // Utility
    Time,    // Game time
    Fresnel, // Fresnel effect
    Panner,  // Animated UV panning
    Perlin,  // Perlin noise
};

// =============================================================================
// Material Node Base Class
// =============================================================================

class DOT_CORE_API MaterialNode
{
public:
    MaterialNode(MaterialNodeType type, const std::string& name);
    virtual ~MaterialNode();

    // Accessors
    int GetId() const { return m_Id; }
    MaterialNodeType GetType() const { return m_Type; }
    const std::string& GetName() const { return m_Name; }

    const std::vector<MaterialPin>& GetInputs() const { return m_Inputs; }
    const std::vector<MaterialPin>& GetOutputs() const { return m_Outputs; }
    std::vector<MaterialPin>& GetInputs() { return m_Inputs; }
    std::vector<MaterialPin>& GetOutputs() { return m_Outputs; }

    // Position in graph editor
    float posX = 0.0f;
    float posY = 0.0f;

    // Generate HLSL code for this node's output
    virtual std::string GenerateHLSL(int outputIndex) const = 0;

    // Get variable name for code generation
    std::string GetVarName(int outputIndex) const;

    // Helper to find which output index a pin ID belongs to
    int GetOutputIndex(int pinId) const;

protected:
    int m_Id;
    MaterialNodeType m_Type;
    std::string m_Name;
    std::vector<MaterialPin> m_Inputs;
    std::vector<MaterialPin> m_Outputs;
    std::vector<std::unique_ptr<NodeProperty>> m_Properties;

public:
    // Property system accessors
    const std::vector<std::unique_ptr<NodeProperty>>& GetProperties() const { return m_Properties; }
    std::vector<std::unique_ptr<NodeProperty>>& GetProperties() { return m_Properties; }

    // Add a property (returns reference for method chaining)
    template <typename T> T& AddProperty()
    {
        auto prop = std::make_unique<T>();
        T& ref = *prop;
        m_Properties.push_back(std::move(prop));
        return ref;
    }

    // Get property by type (first match)
    template <typename T> T* GetProperty()
    {
        for (auto& prop : m_Properties)
        {
            if (auto* typed = dynamic_cast<T*>(prop.get()))
                return typed;
        }
        return nullptr;
    }

    template <typename T> const T* GetProperty() const
    {
        for (const auto& prop : m_Properties)
        {
            if (const auto* typed = dynamic_cast<const T*>(prop.get()))
                return typed;
        }
        return nullptr;
    }

protected:
    static int s_NextNodeId;
    static int s_NextPinId;

    int AllocateNodeId();
    int AllocatePinId();
};

// =============================================================================
// Concrete Node Types
// =============================================================================

/// PBR Output - final surface properties
class DOT_CORE_API PBROutputNode : public MaterialNode
{
public:
    PBROutputNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Float Constant
class DOT_CORE_API ConstFloatNode : public MaterialNode
{
public:
    ConstFloatNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Vec3 Constant (Color)
class DOT_CORE_API ConstVec3Node : public MaterialNode
{
public:
    ConstVec3Node();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Add
class DOT_CORE_API AddNode : public MaterialNode
{
public:
    AddNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Multiply
class DOT_CORE_API MultiplyNode : public MaterialNode
{
public:
    MultiplyNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Lerp
class DOT_CORE_API LerpNode : public MaterialNode
{
public:
    LerpNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Smoothstep
class DOT_CORE_API SmoothstepNode : public MaterialNode
{
public:
    SmoothstepNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Texture2D Sample
class DOT_CORE_API Texture2DNode : public MaterialNode
{
public:
    Texture2DNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Texture Coordinates
class DOT_CORE_API TextureCoordNode : public MaterialNode
{
public:
    TextureCoordNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Time
class DOT_CORE_API TimeNode : public MaterialNode
{
public:
    TimeNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Panner - Animated UV offset based on time
class DOT_CORE_API PannerNode : public MaterialNode
{
public:
    PannerNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

// =============================================================================
// New Math Nodes - Two Inputs
// =============================================================================

/// Math: Subtract
class DOT_CORE_API SubtractNode : public MaterialNode
{
public:
    SubtractNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Divide
class DOT_CORE_API DivideNode : public MaterialNode
{
public:
    DivideNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Power
class DOT_CORE_API PowerNode : public MaterialNode
{
public:
    PowerNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Min
class DOT_CORE_API MinNode : public MaterialNode
{
public:
    MinNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Max
class DOT_CORE_API MaxNode : public MaterialNode
{
public:
    MaxNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

// =============================================================================
// New Math Nodes - Single Input
// =============================================================================

/// Math: Abs
class DOT_CORE_API AbsNode : public MaterialNode
{
public:
    AbsNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Negate
class DOT_CORE_API NegateNode : public MaterialNode
{
public:
    NegateNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: OneMinus (1 - x)
class DOT_CORE_API OneMinusNode : public MaterialNode
{
public:
    OneMinusNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Saturate (clamp 0-1)
class DOT_CORE_API SaturateNode : public MaterialNode
{
public:
    SaturateNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Sin
class DOT_CORE_API SinNode : public MaterialNode
{
public:
    SinNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Cos
class DOT_CORE_API CosNode : public MaterialNode
{
public:
    CosNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Frac (fractional part)
class DOT_CORE_API FracNode : public MaterialNode
{
public:
    FracNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Floor
class DOT_CORE_API FloorNode : public MaterialNode
{
public:
    FloorNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Ceil
class DOT_CORE_API CeilNode : public MaterialNode
{
public:
    CeilNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Sqrt
class DOT_CORE_API SqrtNode : public MaterialNode
{
public:
    SqrtNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Math: Clamp (value, min, max)
class DOT_CORE_API ClampNode : public MaterialNode
{
public:
    ClampNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

// =============================================================================
// Vector Nodes
// =============================================================================

/// Split Vec3 into components
class DOT_CORE_API SplitVec3Node : public MaterialNode
{
public:
    SplitVec3Node();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Make Vec3 from components
class DOT_CORE_API MakeVec3Node : public MaterialNode
{
public:
    MakeVec3Node();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Normalize vector
class DOT_CORE_API NormalizeNode : public MaterialNode
{
public:
    NormalizeNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Dot product
class DOT_CORE_API DotNode : public MaterialNode
{
public:
    DotNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Fresnel effect
class DOT_CORE_API FresnelNode : public MaterialNode
{
public:
    FresnelNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

/// Perlin Noise - Fractal Brownian Motion
class DOT_CORE_API PerlinNode : public MaterialNode
{
public:
    PerlinNode();
    std::string GenerateHLSL(int outputIndex) const override;
};

// Factory function
std::unique_ptr<MaterialNode> CreateMaterialNode(MaterialNodeType type);

} // namespace Dot
