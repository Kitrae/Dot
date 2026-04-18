// =============================================================================
// Dot Engine - Material Node Implementation
// =============================================================================

#include "Core/Material/MaterialNode.h"

#include "Core/Material/MaterialTextureUtils.h"
#include "Core/Material/NodeProperty.h"

#include <algorithm>
#include <sstream>

namespace Dot
{

// Static ID counters
int MaterialNode::s_NextNodeId = 1;
int MaterialNode::s_NextPinId = 1;

// =============================================================================
// MaterialNode Base
// =============================================================================

MaterialNode::MaterialNode(MaterialNodeType type, const std::string& name) : m_Type(type), m_Name(name)
{
    m_Id = AllocateNodeId();
}

// Destructor defined here where NodeProperty is complete
MaterialNode::~MaterialNode() = default;

int MaterialNode::AllocateNodeId()
{
    return s_NextNodeId++;
}

int MaterialNode::AllocatePinId()
{
    return s_NextPinId++;
}

std::string MaterialNode::GetVarName(int outputIndex) const
{
    std::ostringstream ss;
    ss << "node" << m_Id << "_out" << outputIndex;
    return ss.str();
}

int MaterialNode::GetOutputIndex(int pinId) const
{
    for (int i = 0; i < (int)m_Outputs.size(); i++)
    {
        if (m_Outputs[i].id == pinId)
            return i;
    }
    return -1;
}

// =============================================================================
// PBROutputNode
// =============================================================================

PBROutputNode::PBROutputNode() : MaterialNode(MaterialNodeType::PBROutput, "PBR Output")
{
    // Inputs only - this is the final output
    MaterialPin albedo;
    albedo.id = AllocatePinId();
    albedo.name = "Albedo";
    albedo.type = MaterialPinType::Vec3;
    albedo.isInput = true;
    albedo.defaultVec3 = Vec3(0.7f, 0.7f, 0.7f);
    m_Inputs.push_back(albedo);

    MaterialPin metallic;
    metallic.id = AllocatePinId();
    metallic.name = "Metallic";
    metallic.type = MaterialPinType::Float;
    metallic.isInput = true;
    metallic.defaultFloat = 0.0f;
    m_Inputs.push_back(metallic);

    MaterialPin roughness;
    roughness.id = AllocatePinId();
    roughness.name = "Roughness";
    roughness.type = MaterialPinType::Float;
    roughness.isInput = true;
    roughness.defaultFloat = 0.5f;
    m_Inputs.push_back(roughness);

    MaterialPin normal;
    normal.id = AllocatePinId();
    normal.name = "Normal";
    normal.type = MaterialPinType::Vec3;
    normal.isInput = true;
    normal.defaultVec3 = Vec3(0.0f, 0.0f, 1.0f);
    m_Inputs.push_back(normal);

    MaterialPin emissive;
    emissive.id = AllocatePinId();
    emissive.name = "Emissive";
    emissive.type = MaterialPinType::Vec3;
    emissive.isInput = true;
    emissive.defaultVec3 = Vec3(0.0f, 0.0f, 0.0f);
    m_Inputs.push_back(emissive);

    MaterialPin ambientOcclusion;
    ambientOcclusion.id = AllocatePinId();
    ambientOcclusion.name = "Ambient Occlusion";
    ambientOcclusion.type = MaterialPinType::Float;
    ambientOcclusion.isInput = true;
    ambientOcclusion.defaultFloat = 1.0f;
    m_Inputs.push_back(ambientOcclusion);
}

std::string PBROutputNode::GenerateHLSL(int) const
{
    // This node doesn't generate a variable - it's the final output assignment
    return "";
}

// =============================================================================
// ConstFloatNode
// =============================================================================

ConstFloatNode::ConstFloatNode() : MaterialNode(MaterialNodeType::ConstFloat, "Float")
{
    MaterialPin output;
    output.id = AllocatePinId();
    output.name = "Value";
    output.type = MaterialPinType::Float;
    output.isInput = false;
    m_Outputs.push_back(output);

    // Use composable property system
    AddProperty<FloatProperty>();
}

std::string ConstFloatNode::GenerateHLSL(int) const
{
    std::ostringstream ss;
    auto* prop = GetProperty<FloatProperty>();
    float val = prop ? prop->value : 0.0f;
    ss << "float " << GetVarName(0) << " = " << val << ";";
    return ss.str();
}

// =============================================================================
// ConstVec3Node
// =============================================================================

ConstVec3Node::ConstVec3Node() : MaterialNode(MaterialNodeType::ConstVec3, "Color")
{
    MaterialPin output;
    output.id = AllocatePinId();
    output.name = "RGB";
    output.type = MaterialPinType::Vec3;
    output.isInput = false;
    m_Outputs.push_back(output);

    // Use composable property system
    AddProperty<Vec3Property>();
}

std::string ConstVec3Node::GenerateHLSL(int) const
{
    std::ostringstream ss;
    auto* prop = GetProperty<Vec3Property>();
    Vec3 val = prop ? prop->value : Vec3(1, 1, 1);
    ss << "float3 " << GetVarName(0) << " = float3(" << val.x << ", " << val.y << ", " << val.z << ");";
    return ss.str();
}

// =============================================================================
// AddNode
// =============================================================================

AddNode::AddNode() : MaterialNode(MaterialNodeType::Add, "Add")
{
    MaterialPin a;
    a.id = AllocatePinId();
    a.name = "A";
    a.type = MaterialPinType::Float;
    a.isInput = true;
    m_Inputs.push_back(a);

    MaterialPin b;
    b.id = AllocatePinId();
    b.name = "B";
    b.type = MaterialPinType::Float;
    b.isInput = true;
    m_Inputs.push_back(b);

    MaterialPin out;
    out.id = AllocatePinId();
    out.name = "Result";
    out.type = MaterialPinType::Float;
    out.isInput = false;
    m_Outputs.push_back(out);
}

std::string AddNode::GenerateHLSL(int) const
{
    if (m_Inputs.size() < 2 || m_Inputs[0].linkedPinId == -1 || m_Inputs[1].linkedPinId == -1)
        return "";

    std::ostringstream ss;
    ss << "float4 " << GetVarName(0) << " = DotToFloat4(pin_" << m_Inputs[0].id << ") + DotToFloat4(pin_"
       << m_Inputs[1].id << ");";
    return ss.str();
}

// =============================================================================
// MultiplyNode
// =============================================================================

MultiplyNode::MultiplyNode() : MaterialNode(MaterialNodeType::Multiply, "Multiply")
{
    MaterialPin a;
    a.id = AllocatePinId();
    a.name = "A";
    a.type = MaterialPinType::Float;
    a.isInput = true;
    m_Inputs.push_back(a);

    MaterialPin b;
    b.id = AllocatePinId();
    b.name = "B";
    b.type = MaterialPinType::Float;
    b.isInput = true;
    m_Inputs.push_back(b);

    MaterialPin out;
    out.id = AllocatePinId();
    out.name = "Result";
    out.type = MaterialPinType::Float;
    out.isInput = false;
    m_Outputs.push_back(out);
}

std::string MultiplyNode::GenerateHLSL(int) const
{
    if (m_Inputs.size() < 2 || m_Inputs[0].linkedPinId == -1 || m_Inputs[1].linkedPinId == -1)
        return "";

    std::ostringstream ss;
    ss << "float4 " << GetVarName(0) << " = DotToFloat4(pin_" << m_Inputs[0].id << ") * DotToFloat4(pin_"
       << m_Inputs[1].id << ");";
    return ss.str();
}

// =============================================================================
// LerpNode
// =============================================================================

LerpNode::LerpNode() : MaterialNode(MaterialNodeType::Lerp, "Lerp")
{
    MaterialPin a;
    a.id = AllocatePinId();
    a.name = "A";
    a.type = MaterialPinType::Vec3;
    a.isInput = true;
    m_Inputs.push_back(a);

    MaterialPin b;
    b.id = AllocatePinId();
    b.name = "B";
    b.type = MaterialPinType::Vec3;
    b.isInput = true;
    m_Inputs.push_back(b);

    MaterialPin alpha;
    alpha.id = AllocatePinId();
    alpha.name = "Alpha";
    alpha.type = MaterialPinType::Float;
    alpha.isInput = true;
    alpha.defaultFloat = 0.5f;
    m_Inputs.push_back(alpha);

    MaterialPin out;
    out.id = AllocatePinId();
    out.name = "Result";
    out.type = MaterialPinType::Vec3;
    out.isInput = false;
    m_Outputs.push_back(out);
}

std::string LerpNode::GenerateHLSL(int) const
{
    if (m_Inputs.size() < 3)
        return "";

    // A - use pin if connected, otherwise default black
    std::string aVal;
    if (m_Inputs[0].linkedPinId != -1)
        aVal = "pin_" + std::to_string(m_Inputs[0].id);
    else
        aVal = "float3(0, 0, 0)";

    // B - use pin if connected, otherwise default white
    std::string bVal;
    if (m_Inputs[1].linkedPinId != -1)
        bVal = "pin_" + std::to_string(m_Inputs[1].id);
    else
        bVal = "float3(1, 1, 1)";

    // Alpha - use pin if connected, otherwise use property or default 0.5
    std::string alpha;
    if (m_Inputs[2].linkedPinId != -1)
        alpha = "pin_" + std::to_string(m_Inputs[2].id);
    else
    {
        auto* prop = GetProperty<FloatProperty>();
        alpha = std::to_string(prop ? prop->value : 0.5f);
    }

    std::ostringstream ss;
    ss << "float4 " << GetVarName(0) << " = lerp(DotToFloat4(" << aVal << "), DotToFloat4(" << bVal
       << "), DotToFloat4(" << alpha << "));";
    return ss.str();
}

// =============================================================================
// SmoothstepNode
// =============================================================================

SmoothstepNode::SmoothstepNode() : MaterialNode(MaterialNodeType::Smoothstep, "Smoothstep")
{
    // Input: Edge0 (min edge)
    MaterialPin edge0;
    edge0.id = AllocatePinId();
    edge0.name = "Edge0";
    edge0.type = MaterialPinType::Float;
    edge0.isInput = true;
    edge0.defaultFloat = 0.0f;
    m_Inputs.push_back(edge0);

    // Input: Edge1 (max edge)
    MaterialPin edge1;
    edge1.id = AllocatePinId();
    edge1.name = "Edge1";
    edge1.type = MaterialPinType::Float;
    edge1.isInput = true;
    edge1.defaultFloat = 1.0f;
    m_Inputs.push_back(edge1);

    // Input: X (value to interpolate)
    MaterialPin x;
    x.id = AllocatePinId();
    x.name = "X";
    x.type = MaterialPinType::Float;
    x.isInput = true;
    m_Inputs.push_back(x);

    // Output: Result
    MaterialPin out;
    out.id = AllocatePinId();
    out.name = "Result";
    out.type = MaterialPinType::Float;
    out.isInput = false;
    m_Outputs.push_back(out);

    // Properties for default edge values
    m_Properties.push_back(std::make_unique<FloatProperty>("Edge0", 0.0f, 0.0f, 1.0f));
    m_Properties.push_back(std::make_unique<FloatProperty>("Edge1", 1.0f, 0.0f, 1.0f));
}

std::string SmoothstepNode::GenerateHLSL(int) const
{
    if (m_Inputs.size() < 3)
        return "";

    // Edge0 - use property if not connected
    std::string edge0;
    if (m_Inputs[0].linkedPinId != -1)
        edge0 = "pin_" + std::to_string(m_Inputs[0].id);
    else
        edge0 =
            std::to_string(m_Properties.size() > 0 ? static_cast<FloatProperty*>(m_Properties[0].get())->value : 0.0f);

    // Edge1 - use property if not connected
    std::string edge1;
    if (m_Inputs[1].linkedPinId != -1)
        edge1 = "pin_" + std::to_string(m_Inputs[1].id);
    else
        edge1 =
            std::to_string(m_Properties.size() > 1 ? static_cast<FloatProperty*>(m_Properties[1].get())->value : 1.0f);

    // X - use pin if connected, otherwise use 0.5 as default
    std::string xVal;
    if (m_Inputs[2].linkedPinId != -1)
        xVal = "pin_" + std::to_string(m_Inputs[2].id);
    else
        xVal = "0.5";

    std::ostringstream ss;
    ss << "float " << GetVarName(0) << " = smoothstep(" << edge0 << ", " << edge1 << ", " << xVal << ");";
    return ss.str();
}

// =============================================================================
// Texture2DNode
// =============================================================================

Texture2DNode::Texture2DNode() : MaterialNode(MaterialNodeType::Texture2D, "Texture 2D")
{
    // Input pin: UV coordinates
    MaterialPin uv;
    uv.id = AllocatePinId();
    uv.name = "UV";
    uv.type = MaterialPinType::Vec2;
    uv.isInput = true;
    m_Inputs.push_back(uv);

    // Output pins
    MaterialPin rgba;
    rgba.id = AllocatePinId();
    rgba.name = "RGBA";
    rgba.type = MaterialPinType::Vec4;
    rgba.isInput = false;
    m_Outputs.push_back(rgba);

    MaterialPin rgb;
    rgb.id = AllocatePinId();
    rgb.name = "RGB";
    rgb.type = MaterialPinType::Vec3;
    rgb.isInput = false;
    m_Outputs.push_back(rgb);

    MaterialPin r;
    r.id = AllocatePinId();
    r.name = "R";
    r.type = MaterialPinType::Float;
    r.isInput = false;
    m_Outputs.push_back(r);

    MaterialPin g;
    g.id = AllocatePinId();
    g.name = "G";
    g.type = MaterialPinType::Float;
    g.isInput = false;
    m_Outputs.push_back(g);

    MaterialPin b;
    b.id = AllocatePinId();
    b.name = "B";
    b.type = MaterialPinType::Float;
    b.isInput = false;
    m_Outputs.push_back(b);

    MaterialPin a;
    a.id = AllocatePinId();
    a.name = "A";
    a.type = MaterialPinType::Float;
    a.isInput = false;
    m_Outputs.push_back(a);

    // Composable properties - use shared property types!
    AddProperty<TexturePathProperty>(); // Use texture-specific property with image file filter
    AddProperty<FilterModeProperty>();
    AddProperty<WrapModeProperty>();
    AddProperty<TilingProperty>();
    AddProperty<OffsetProperty>();
    AddProperty<TextureSlotProperty>();
    AddProperty<TextureSampleTypeProperty>();
    AddProperty<MipmapProperty>(); // Enable mipmap generation
}

std::string Texture2DNode::GenerateHLSL(int) const
{
    std::ostringstream ss;
    auto* slotProp = GetProperty<TextureSlotProperty>();
    auto* pathProp = GetProperty<TexturePathProperty>();
    auto* tilingProp = GetProperty<TilingProperty>();
    auto* offsetProp = GetProperty<OffsetProperty>();
    auto* sampleTypeProp = GetProperty<TextureSampleTypeProperty>();
    const int slot = std::clamp(slotProp ? slotProp->value : 0, 0, 3);
    const float tilingU = tilingProp ? tilingProp->tilingU : 1.0f;
    const float tilingV = tilingProp ? tilingProp->tilingV : 1.0f;
    const float offsetU = offsetProp ? offsetProp->offsetU : 0.0f;
    const float offsetV = offsetProp ? offsetProp->offsetV : 0.0f;
    int sampleType = sampleTypeProp ? sampleTypeProp->value : static_cast<int>(TextureSampleType::Color);
    if (sampleType == static_cast<int>(TextureSampleType::Color) && pathProp)
        sampleType = static_cast<int>(GuessTextureSampleTypeFromPath(pathProp->path));

    // UV - use connected pin or default to function's uv parameter
    std::string uvInput;
    if (!m_Inputs.empty() && m_Inputs[0].linkedPinId != -1)
        uvInput = "pin_" + std::to_string(m_Inputs[0].id);
    else
        uvInput = "uv"; // Default UV from function parameter

    const std::string uvVar = GetVarName(0) + "_uv";

    // Declare all outputs: RGBA (out0), RGB (out1), R/G/B/A (out2..out5)
    ss << "float2 " << uvVar << " = DotToFloat2(" << uvInput << ") * float2(" << tilingU << ", " << tilingV << ") + float2("
       << offsetU << ", " << offsetV << "); ";
    ss << "float4 " << GetVarName(0) << " = tex" << slot << ".Sample(sampler" << slot << ", " << uvVar << "); ";
    if (sampleType == static_cast<int>(TextureSampleType::Normal))
    {
        ss << "float3 " << GetVarName(1) << " = DotDecodeNormalSample(" << GetVarName(0) << ".xyz, " << uvVar
           << ", worldPos, worldNormal, worldTangent); ";
    }
    else
    {
        ss << "float3 " << GetVarName(1) << " = " << GetVarName(0) << ".rgb; ";
    }
    ss << "float " << GetVarName(2) << " = " << GetVarName(0) << ".r;";
    ss << "float " << GetVarName(3) << " = " << GetVarName(0) << ".g;";
    ss << "float " << GetVarName(4) << " = " << GetVarName(0) << ".b;";
    ss << "float " << GetVarName(5) << " = " << GetVarName(0) << ".a;";
    return ss.str();
}

// =============================================================================
// TextureCoordNode
// =============================================================================

TextureCoordNode::TextureCoordNode() : MaterialNode(MaterialNodeType::TextureCoord, "Texture Coord")
{
    MaterialPin uv;
    uv.id = AllocatePinId();
    uv.name = "UV";
    uv.type = MaterialPinType::Vec2;
    uv.isInput = false;
    m_Outputs.push_back(uv);
}

std::string TextureCoordNode::GenerateHLSL(int) const
{
    std::ostringstream ss;
    ss << "float2 " << GetVarName(0) << " = uv;";
    return ss.str();
}

// =============================================================================
// TimeNode
// =============================================================================

TimeNode::TimeNode() : MaterialNode(MaterialNodeType::Time, "Time")
{
    MaterialPin time;
    time.id = AllocatePinId();
    time.name = "Time";
    time.type = MaterialPinType::Float;
    time.isInput = false;
    m_Outputs.push_back(time);

    MaterialPin sine;
    sine.id = AllocatePinId();
    sine.name = "Sine";
    sine.type = MaterialPinType::Float;
    sine.isInput = false;
    m_Outputs.push_back(sine);
}

std::string TimeNode::GenerateHLSL(int outputIndex) const
{
    std::ostringstream ss;
    if (outputIndex == 0)
    {
        ss << "float " << GetVarName(0) << " = _Time;";
    }
    else
    {
        ss << "float " << GetVarName(1) << " = sin(_Time);";
    }
    return ss.str();
}

// =============================================================================
// PannerNode
// =============================================================================

PannerNode::PannerNode() : MaterialNode(MaterialNodeType::Panner, "Panner")
{
    // Input: UV coordinates
    MaterialPin uv;
    uv.id = AllocatePinId();
    uv.name = "UV";
    uv.type = MaterialPinType::Vec2;
    uv.isInput = true;
    m_Inputs.push_back(uv);

    // Input: Time (optional - uses _Time if not connected)
    MaterialPin time;
    time.id = AllocatePinId();
    time.name = "Time";
    time.type = MaterialPinType::Float;
    time.isInput = true;
    m_Inputs.push_back(time);

    // Output: Animated UV
    MaterialPin outUV;
    outUV.id = AllocatePinId();
    outUV.name = "UV";
    outUV.type = MaterialPinType::Vec2;
    outUV.isInput = false;
    m_Outputs.push_back(outUV);

    // Speed property (UV scroll speed)
    AddProperty<PannerSpeedProperty>();
    AddProperty<PannerMethodProperty>();
    AddProperty<PannerLinkProperty>();
}

std::string PannerNode::GenerateHLSL(int outputIndex) const
{
    (void)outputIndex;
    std::ostringstream ss;

    // Get properties
    auto* speedProp = GetProperty<PannerSpeedProperty>();
    float speedU = speedProp ? speedProp->speedU : 0.5f;
    float speedV = speedProp ? speedProp->speedV : 0.0f;

    auto* linkProp = GetProperty<PannerLinkProperty>();
    if (linkProp && linkProp->value)
        speedV = speedU;

    auto* methodProp = GetProperty<PannerMethodProperty>();
    int method = methodProp ? methodProp->value : 0; // 0=Linear, 1=Sine, 2=ZigZag, 3=Rotate

    // Input handling
    std::string uvInput = "uv"; // Use function parameter, not input struct
    if (!m_Inputs.empty() && m_Inputs[0].linkedPinId != -1)
        uvInput = "pin_" + std::to_string(m_Inputs[0].id);

    std::string timeInput = "_Time";
    if (m_Inputs.size() >= 2 && m_Inputs[1].linkedPinId != -1)
        timeInput = "pin_" + std::to_string(m_Inputs[1].id);

    std::string varName = GetVarName(0);

    switch (method)
    {
        case 0: // Linear
            ss << "float2 " << varName << " = DotToFloat2(" << uvInput << ") + DotToFloat(" << timeInput << ") * float2(" << speedU << ", "
               << speedV << ");";
            break;
        case 1: // Sine
            ss << "float2 " << varName << " = DotToFloat2(" << uvInput << ") + float2(" << speedU << ", " << speedV
               << ") * sin(DotToFloat(" << timeInput << "));";
            break;
        case 2: // ZigZag
            ss << "float2 " << varName << " = DotToFloat2(" << uvInput << ") + float2(" << speedU << ", " << speedV
               << ") * (abs(frac(DotToFloat(" << timeInput << ") * 0.5f) * 2.0f - 1.0f) * 2.0f - 1.0f);";
            break;
        case 3: // Rotate
        {
            // Rotation around (0.5, 0.5)
            ss << "{ float s, c; sincos(DotToFloat(" << timeInput << ") * " << speedU << ", s, c); ";
            ss << "float2 " << varName << " = mul(DotToFloat2(" << uvInput << ") - 0.5f, float2x2(c, -s, s, c)) + 0.5f; }";
        }
        break;
        default:
            ss << "float2 " << varName << " = DotToFloat2(" << uvInput << ");";
            break;
    }

    return ss.str();
}

// =============================================================================
// Two-Input Math Nodes
// =============================================================================

// Helper macro for binary math nodes
#define IMPLEMENT_BINARY_MATH_NODE(ClassName, NodeType, NodeName, HLSLOp)                                              \
    ClassName::ClassName() : MaterialNode(MaterialNodeType::NodeType, NodeName)                                        \
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
        ss << "float4 A4 = DotToFloat4(A); ";                                                                          \
        ss << "float4 B4 = DotToFloat4(B); ";                                                                          \
        ss << "float4 " << GetVarName(0) << " = " << HLSLOp << ";";                                                    \
        return ss.str();                                                                                               \
    }

IMPLEMENT_BINARY_MATH_NODE(SubtractNode, Subtract, "Subtract", "A4 - B4")
IMPLEMENT_BINARY_MATH_NODE(DivideNode, Divide, "Divide", "A4 / max(B4, 1e-6)")
IMPLEMENT_BINARY_MATH_NODE(PowerNode, Power, "Power", "pow(abs(A4), B4)")
IMPLEMENT_BINARY_MATH_NODE(MinNode, Min, "Min", "min(A4, B4)")
IMPLEMENT_BINARY_MATH_NODE(MaxNode, Max, "Max", "max(A4, B4)")

// =============================================================================
// Single-Input Math Nodes
// =============================================================================

// Helper macro for unary math nodes
#define IMPLEMENT_UNARY_MATH_NODE(ClassName, NodeType, NodeName, HLSLOp)                                               \
    ClassName::ClassName() : MaterialNode(MaterialNodeType::NodeType, NodeName)                                        \
    {                                                                                                                  \
        MaterialPin a;                                                                                                 \
        a.id = AllocatePinId();                                                                                        \
        a.name = "In";                                                                                                 \
        a.type = MaterialPinType::Float;                                                                               \
        a.isInput = true;                                                                                              \
        m_Inputs.push_back(a);                                                                                         \
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
        ss << "float4 In4 = DotToFloat4(In); ";                                                                        \
        ss << "float4 " << GetVarName(0) << " = " << HLSLOp << ";";                                                    \
        return ss.str();                                                                                               \
    }

IMPLEMENT_UNARY_MATH_NODE(AbsNode, Abs, "Abs", "abs(In4)")
IMPLEMENT_UNARY_MATH_NODE(NegateNode, Negate, "Negate", "-In4")
IMPLEMENT_UNARY_MATH_NODE(OneMinusNode, OneMinus, "One Minus", "1.0 - In4")
IMPLEMENT_UNARY_MATH_NODE(SaturateNode, Saturate, "Saturate", "saturate(In4)")
IMPLEMENT_UNARY_MATH_NODE(SinNode, Sin, "Sin", "sin(In4)")
IMPLEMENT_UNARY_MATH_NODE(CosNode, Cos, "Cos", "cos(In4)")
IMPLEMENT_UNARY_MATH_NODE(FracNode, Frac, "Frac", "frac(In4)")
IMPLEMENT_UNARY_MATH_NODE(FloorNode, Floor, "Floor", "floor(In4)")
IMPLEMENT_UNARY_MATH_NODE(CeilNode, Ceil, "Ceil", "ceil(In4)")
IMPLEMENT_UNARY_MATH_NODE(SqrtNode, Sqrt, "Sqrt", "sqrt(max(In4, 0.0))")

// =============================================================================
// Clamp Node (3 inputs)
// =============================================================================

ClampNode::ClampNode() : MaterialNode(MaterialNodeType::Clamp, "Clamp")
{
    MaterialPin val;
    val.id = AllocatePinId();
    val.name = "Value";
    val.type = MaterialPinType::Float;
    val.isInput = true;
    m_Inputs.push_back(val);

    MaterialPin minPin;
    minPin.id = AllocatePinId();
    minPin.name = "Min";
    minPin.type = MaterialPinType::Float;
    minPin.isInput = true;
    minPin.defaultFloat = 0.0f;
    m_Inputs.push_back(minPin);

    MaterialPin maxPin;
    maxPin.id = AllocatePinId();
    maxPin.name = "Max";
    maxPin.type = MaterialPinType::Float;
    maxPin.isInput = true;
    maxPin.defaultFloat = 1.0f;
    m_Inputs.push_back(maxPin);

    MaterialPin out;
    out.id = AllocatePinId();
    out.name = "Result";
    out.type = MaterialPinType::Float;
    out.isInput = false;
    m_Outputs.push_back(out);
}

std::string ClampNode::GenerateHLSL(int) const
{
    std::ostringstream ss;
    ss << "float4 " << GetVarName(0) << " = clamp(DotToFloat4(Value), DotToFloat4(Min), DotToFloat4(Max));";
    return ss.str();
}

// =============================================================================
// Vector Nodes
// =============================================================================

SplitVec3Node::SplitVec3Node() : MaterialNode(MaterialNodeType::SplitVec3, "Split RGB")
{
    MaterialPin in;
    in.id = AllocatePinId();
    in.name = "RGB";
    in.type = MaterialPinType::Vec3;
    in.isInput = true;
    m_Inputs.push_back(in);

    MaterialPin r;
    r.id = AllocatePinId();
    r.name = "R";
    r.type = MaterialPinType::Float;
    r.isInput = false;
    m_Outputs.push_back(r);
    MaterialPin g;
    g.id = AllocatePinId();
    g.name = "G";
    g.type = MaterialPinType::Float;
    g.isInput = false;
    m_Outputs.push_back(g);
    MaterialPin b;
    b.id = AllocatePinId();
    b.name = "B";
    b.type = MaterialPinType::Float;
    b.isInput = false;
    m_Outputs.push_back(b);
}

std::string SplitVec3Node::GenerateHLSL(int outputIndex) const
{
    if (m_Inputs.empty() || m_Inputs[0].linkedPinId == -1)
        return "";

    std::ostringstream ss;
    const char* comp[] = {"r", "g", "b"};
    ss << "float3 " << GetVarName(0) << "_rgb = DotToFloat3(RGB); ";
    ss << "float " << GetVarName(outputIndex) << " = " << GetVarName(0) << "_rgb." << comp[outputIndex] << ";";
    return ss.str();
}

MakeVec3Node::MakeVec3Node() : MaterialNode(MaterialNodeType::MakeVec3, "Make RGB")
{
    MaterialPin r;
    r.id = AllocatePinId();
    r.name = "R";
    r.type = MaterialPinType::Float;
    r.isInput = true;
    m_Inputs.push_back(r);
    MaterialPin g;
    g.id = AllocatePinId();
    g.name = "G";
    g.type = MaterialPinType::Float;
    g.isInput = true;
    m_Inputs.push_back(g);
    MaterialPin b;
    b.id = AllocatePinId();
    b.name = "B";
    b.type = MaterialPinType::Float;
    b.isInput = true;
    m_Inputs.push_back(b);

    MaterialPin out;
    out.id = AllocatePinId();
    out.name = "RGB";
    out.type = MaterialPinType::Vec3;
    out.isInput = false;
    m_Outputs.push_back(out);
}

std::string MakeVec3Node::GenerateHLSL(int) const
{
    if (m_Inputs.size() < 3 || m_Inputs[0].linkedPinId == -1 || m_Inputs[1].linkedPinId == -1 ||
        m_Inputs[2].linkedPinId == -1)
        return "";

    std::ostringstream ss;
    ss << "float3 " << GetVarName(0) << " = float3(DotToFloat(R), DotToFloat(G), DotToFloat(B));";
    return ss.str();
}

NormalizeNode::NormalizeNode() : MaterialNode(MaterialNodeType::Normalize, "Normalize")
{
    MaterialPin in;
    in.id = AllocatePinId();
    in.name = "Vector";
    in.type = MaterialPinType::Vec3;
    in.isInput = true;
    m_Inputs.push_back(in);

    MaterialPin out;
    out.id = AllocatePinId();
    out.name = "Result";
    out.type = MaterialPinType::Vec3;
    out.isInput = false;
    m_Outputs.push_back(out);
}

std::string NormalizeNode::GenerateHLSL(int) const
{
    std::ostringstream ss;
    ss << "float3 " << GetVarName(0) << " = normalize(DotToFloat3(Vector));";
    return ss.str();
}

DotNode::DotNode() : MaterialNode(MaterialNodeType::Dot, "Dot Product")
{
    MaterialPin a;
    a.id = AllocatePinId();
    a.name = "A";
    a.type = MaterialPinType::Vec3;
    a.isInput = true;
    m_Inputs.push_back(a);
    MaterialPin b;
    b.id = AllocatePinId();
    b.name = "B";
    b.type = MaterialPinType::Vec3;
    b.isInput = true;
    m_Inputs.push_back(b);

    MaterialPin out;
    out.id = AllocatePinId();
    out.name = "Result";
    out.type = MaterialPinType::Float;
    out.isInput = false;
    m_Outputs.push_back(out);
}

std::string DotNode::GenerateHLSL(int) const
{
    std::ostringstream ss;
    ss << "float " << GetVarName(0) << " = dot(DotToFloat3(A), DotToFloat3(B));";
    return ss.str();
}

FresnelNode::FresnelNode() : MaterialNode(MaterialNodeType::Fresnel, "Fresnel")
{
    // Input: Normal (optional, uses worldNormal if not connected)
    MaterialPin normalIn;
    normalIn.id = AllocatePinId();
    normalIn.name = "Normal";
    normalIn.type = MaterialPinType::Vec3;
    normalIn.isInput = true;
    m_Inputs.push_back(normalIn);

    // Input: Power (optional, uses property if not connected)
    MaterialPin power;
    power.id = AllocatePinId();
    power.name = "Power";
    power.type = MaterialPinType::Float;
    power.isInput = true;
    power.defaultFloat = 2.0f;
    m_Inputs.push_back(power);

    // Output
    MaterialPin out;
    out.id = AllocatePinId();
    out.name = "Result";
    out.type = MaterialPinType::Float;
    out.isInput = false;
    m_Outputs.push_back(out);

    // Properties
    m_Properties.push_back(std::make_unique<FloatProperty>("Power", 2.0f, 0.1f, 10.0f));
    m_Properties.push_back(std::make_unique<FloatProperty>("Bias", 0.0f, 0.0f, 1.0f));
}

std::string FresnelNode::GenerateHLSL(int) const
{
    std::ostringstream ss;

    // Get normal (use connected pin or worldNormal)
    std::string normalVar = "worldNormal";
    if (!m_Inputs.empty() && m_Inputs[0].linkedPinId != -1)
        normalVar = "DotToFloat3(pin_" + std::to_string(m_Inputs[0].id) + ")";

    // Get power (use connected pin or property)
    std::string powerVar;
    if (m_Inputs.size() > 1 && m_Inputs[1].linkedPinId != -1)
        powerVar = "DotToFloat(pin_" + std::to_string(m_Inputs[1].id) + ")";
    else
    {
        auto* prop = GetProperty<FloatProperty>();
        powerVar = std::to_string(prop ? prop->value : 2.0f);
    }

    // Get bias from property
    float bias = 0.0f;
    int propIdx = 0;
    for (const auto& prop : m_Properties)
    {
        if (auto* fp = dynamic_cast<FloatProperty*>(prop.get()))
        {
            if (propIdx == 1)
            {
                bias = fp->value;
                break;
            }
            propIdx++;
        }
    }

    // Compute fresnel using view direction from CameraPos uniform
    ss << "float3 " << GetVarName(0) << "_viewDir = normalize(CameraPos - worldPos); ";
    ss << "float " << GetVarName(0) << " = " << bias << " + (1.0 - " << bias << ") * ";
    ss << "pow(1.0 - saturate(dot(" << normalVar << ", " << GetVarName(0) << "_viewDir)), " << powerVar << ");";
    return ss.str();
}

PerlinNode::PerlinNode() : MaterialNode(MaterialNodeType::Perlin, "Perlin Noise")
{
    // Inputs
    MaterialPin uv;
    uv.id = AllocatePinId();
    uv.name = "UV";
    uv.type = MaterialPinType::Vec2;
    uv.isInput = true;
    m_Inputs.push_back(uv);

    // Outputs
    MaterialPin out;
    out.id = AllocatePinId();
    out.name = "Noise";
    out.type = MaterialPinType::Float;
    out.isInput = false;
    m_Outputs.push_back(out);

    MaterialPin outRGB;
    outRGB.id = AllocatePinId();
    outRGB.name = "RGB";
    outRGB.type = MaterialPinType::Vec3;
    outRGB.isInput = false;
    m_Outputs.push_back(outRGB);

    // Properties
    AddProperty<NoiseScaleProperty>();
    AddProperty<NoiseOctavesProperty>();
    AddProperty<NoisePersistenceProperty>();
    AddProperty<NoiseLacunarityProperty>();
    AddProperty<NoiseSeedProperty>();
}

std::string PerlinNode::GenerateHLSL(int outputIndex) const
{
    std::ostringstream ss;

    // UV Input
    std::string uvInput = "UV";
    if (m_Inputs[0].linkedPinId == -1)
        uvInput = "uv"; // Use function parameter, not input struct

    // Properties
    auto* scaleProp = GetProperty<NoiseScaleProperty>();
    auto* octavesProp = GetProperty<NoiseOctavesProperty>();
    auto* persistenceProp = GetProperty<NoisePersistenceProperty>();
    auto* lacunarityProp = GetProperty<NoiseLacunarityProperty>();
    auto* seedProp = GetProperty<NoiseSeedProperty>();

    float scale = scaleProp ? scaleProp->value : 5.0f;
    int octaves = octavesProp ? octavesProp->value : 4;
    float persistence = persistenceProp ? persistenceProp->value : 0.5f;
    float lacunarity = lacunarityProp ? lacunarityProp->value : 2.0f;
    float seed = seedProp ? seedProp->value : 0.0f;

    std::string varName = GetVarName(0);
    std::string rgbVarName = GetVarName(1);

    // Generate noise calculation only for output 0 (scalar)
    if (outputIndex == 0)
    {
        ss << "float " << varName << " = dot_fbm(DotToFloat2(" << uvInput << ") * " << scale << " + " << seed << ", " << octaves
           << ", " << persistence << ", " << lacunarity << ") * 0.5 + 0.5;";
    }
    else if (outputIndex == 1)
    {
        // Output 1 (RGB) just references the scalar version
        ss << "float3 " << rgbVarName << " = float3(" << varName << ", " << varName << ", " << varName << ");";
    }

    return ss.str();
}

// =============================================================================
// Factory
// =============================================================================

std::unique_ptr<MaterialNode> CreateMaterialNode(MaterialNodeType type)
{
    switch (type)
    {
        case MaterialNodeType::PBROutput:
            return std::make_unique<PBROutputNode>();
        case MaterialNodeType::ConstFloat:
            return std::make_unique<ConstFloatNode>();
        case MaterialNodeType::ConstVec3:
            return std::make_unique<ConstVec3Node>();
        case MaterialNodeType::Add:
            return std::make_unique<AddNode>();
        case MaterialNodeType::Subtract:
            return std::make_unique<SubtractNode>();
        case MaterialNodeType::Multiply:
            return std::make_unique<MultiplyNode>();
        case MaterialNodeType::Divide:
            return std::make_unique<DivideNode>();
        case MaterialNodeType::Power:
            return std::make_unique<PowerNode>();
        case MaterialNodeType::Min:
            return std::make_unique<MinNode>();
        case MaterialNodeType::Max:
            return std::make_unique<MaxNode>();
        case MaterialNodeType::Abs:
            return std::make_unique<AbsNode>();
        case MaterialNodeType::Negate:
            return std::make_unique<NegateNode>();
        case MaterialNodeType::OneMinus:
            return std::make_unique<OneMinusNode>();
        case MaterialNodeType::Saturate:
            return std::make_unique<SaturateNode>();
        case MaterialNodeType::Sin:
            return std::make_unique<SinNode>();
        case MaterialNodeType::Cos:
            return std::make_unique<CosNode>();
        case MaterialNodeType::Frac:
            return std::make_unique<FracNode>();
        case MaterialNodeType::Floor:
            return std::make_unique<FloorNode>();
        case MaterialNodeType::Ceil:
            return std::make_unique<CeilNode>();
        case MaterialNodeType::Sqrt:
            return std::make_unique<SqrtNode>();
        case MaterialNodeType::Lerp:
            return std::make_unique<LerpNode>();
        case MaterialNodeType::Clamp:
            return std::make_unique<ClampNode>();
        case MaterialNodeType::Smoothstep:
            return std::make_unique<SmoothstepNode>();
        case MaterialNodeType::SplitVec3:
            return std::make_unique<SplitVec3Node>();
        case MaterialNodeType::MakeVec3:
            return std::make_unique<MakeVec3Node>();
        case MaterialNodeType::Normalize:
            return std::make_unique<NormalizeNode>();
        case MaterialNodeType::Dot:
            return std::make_unique<DotNode>();
        case MaterialNodeType::Fresnel:
            return std::make_unique<FresnelNode>();
        case MaterialNodeType::Texture2D:
            return std::make_unique<Texture2DNode>();
        case MaterialNodeType::TextureCoord:
            return std::make_unique<TextureCoordNode>();
        case MaterialNodeType::Time:
            return std::make_unique<TimeNode>();
        case MaterialNodeType::Panner:
            return std::make_unique<PannerNode>();
        case MaterialNodeType::Perlin:
            return std::make_unique<PerlinNode>();
        default:
            return nullptr;
    }
}

} // namespace Dot
