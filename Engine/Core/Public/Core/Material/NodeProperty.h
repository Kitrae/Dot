// =============================================================================
// Dot Engine - Node Property System
// =============================================================================
// Reusable, composable property types for material nodes.
// Define once, use in any node.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/Material/MaterialTextureUtils.h"
#include "Core/Math/Vec3.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace Dot
{

// =============================================================================
// Property Type Enum
// =============================================================================

enum class PropertyType : uint8_t
{
    Float,
    Int,
    Vec3,
    Bool,
    FilePath,
    Enum, // For dropdowns like FilterMode, WrapMode
};

// =============================================================================
// Base Property Class
// =============================================================================

class DOT_CORE_API NodeProperty
{
public:
    NodeProperty(const std::string& name, PropertyType type) : m_Name(name), m_Type(type) {}
    virtual ~NodeProperty() = default;

    const std::string& GetName() const { return m_Name; }
    PropertyType GetType() const { return m_Type; }

    // Override these for custom behavior
    virtual void OnGUI() {} // Default: no UI (rendered by panel)

    // Serialization
    virtual std::string Serialize() const = 0;
    virtual void Deserialize(const std::string& data) = 0;

    // HLSL code generation (returns empty if not relevant)
    virtual std::string GetHLSLValue() const { return ""; }

protected:
    std::string m_Name;
    PropertyType m_Type;
};

// =============================================================================
// Concrete Property Types
// =============================================================================

/// Float property with optional min/max
class DOT_CORE_API FloatProperty : public NodeProperty
{
public:
    // Default constructor for AddProperty<>()
    FloatProperty() : NodeProperty("Value", PropertyType::Float), value(0.0f), minValue(0.0f), maxValue(1.0f) {}

    FloatProperty(const std::string& name, float defaultVal = 0.0f, float minVal = 0.0f, float maxVal = 1.0f)
        : NodeProperty(name, PropertyType::Float), value(defaultVal), minValue(minVal), maxValue(maxVal)
    {
    }

    float value;
    float minValue;
    float maxValue;

    std::string Serialize() const override
    {
        std::ostringstream ss;
        ss << value;
        return ss.str();
    }

    void Deserialize(const std::string& data) override { value = std::stof(data); }

    std::string GetHLSLValue() const override
    {
        std::ostringstream ss;
        ss << value << "f";
        return ss.str();
    }
};

/// Integer property
class DOT_CORE_API IntProperty : public NodeProperty
{
public:
    // Default constructor for AddProperty<>()
    IntProperty() : NodeProperty("Value", PropertyType::Int), value(0), minValue(0), maxValue(100) {}

    IntProperty(const std::string& name, int defaultVal = 0, int minVal = 0, int maxVal = 100)
        : NodeProperty(name, PropertyType::Int), value(defaultVal), minValue(minVal), maxValue(maxVal)
    {
    }

    int value;
    int minValue;
    int maxValue;

    std::string Serialize() const override { return std::to_string(value); }

    void Deserialize(const std::string& data) override { value = std::stoi(data); }

    std::string GetHLSLValue() const override { return std::to_string(value); }
};

/// Vec3/Color property
class DOT_CORE_API Vec3Property : public NodeProperty
{
public:
    // Default constructor for AddProperty<>()
    Vec3Property() : NodeProperty("Color", PropertyType::Vec3), value(1, 1, 1) {}

    Vec3Property(const std::string& name, Vec3 defaultVal = Vec3(1, 1, 1))
        : NodeProperty(name, PropertyType::Vec3), value(defaultVal)
    {
    }

    Vec3 value;

    std::string Serialize() const override
    {
        std::ostringstream ss;
        ss << value.x << " " << value.y << " " << value.z;
        return ss.str();
    }

    void Deserialize(const std::string& data) override
    {
        std::istringstream ss(data);
        ss >> value.x >> value.y >> value.z;
    }

    std::string GetHLSLValue() const override
    {
        std::ostringstream ss;
        ss << "float3(" << value.x << ", " << value.y << ", " << value.z << ")";
        return ss.str();
    }
};

/// Boolean property
class DOT_CORE_API BoolProperty : public NodeProperty
{
public:
    BoolProperty(const std::string& name, bool defaultVal = false)
        : NodeProperty(name, PropertyType::Bool), value(defaultVal)
    {
    }

    bool value;

    std::string Serialize() const override { return value ? "1" : "0"; }

    void Deserialize(const std::string& data) override { value = (data == "1" || data == "true"); }

    std::string GetHLSLValue() const override { return value ? "true" : "false"; }
};

/// File path property (for textures, etc.)
class DOT_CORE_API FilePathProperty : public NodeProperty
{
public:
    // Default constructor for AddProperty<>()
    FilePathProperty() : NodeProperty("File Path", PropertyType::FilePath) {}

    FilePathProperty(const std::string& name, const std::string& filter = "All Files\0*.*\0")
        : NodeProperty(name, PropertyType::FilePath), fileFilter(filter)
    {
    }

    std::string path;
    std::string fileFilter;

    std::string Serialize() const override { return path; }

    void Deserialize(const std::string& data) override { path = data; }
};

/// Specialized texture path property (common reuse case)
class DOT_CORE_API TexturePathProperty : public FilePathProperty
{
public:
    TexturePathProperty() : FilePathProperty("Texture", "Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.dds\0") {}
};

/// Enum property (for dropdowns) - REUSABLE base for FilterMode, WrapMode, etc.
class DOT_CORE_API EnumProperty : public NodeProperty
{
public:
    EnumProperty(const std::string& name, const std::vector<std::string>& options, int defaultIndex = 0)
        : NodeProperty(name, PropertyType::Enum), m_Options(options), value(defaultIndex)
    {
    }

    int value;
    const std::vector<std::string>& GetOptions() const { return m_Options; }

    std::string Serialize() const override { return std::to_string(value); }

    void Deserialize(const std::string& data) override { value = std::stoi(data); }

    std::string GetHLSLValue() const override { return std::to_string(value); }

protected:
    std::vector<std::string> m_Options;
};

// =============================================================================
// SHARED REUSABLE PROPERTIES
// These are predefined properties that any node can use
// =============================================================================

/// Filter mode for texture sampling (Nearest, Bilinear, Trilinear)
class DOT_CORE_API FilterModeProperty : public EnumProperty
{
public:
    FilterModeProperty() : EnumProperty("Filter Mode", {"Nearest", "Bilinear", "Trilinear"}, 1) {}
};

/// Wrap mode for texture sampling (Repeat, Clamp, Mirror)
class DOT_CORE_API WrapModeProperty : public EnumProperty
{
public:
    WrapModeProperty() : EnumProperty("Wrap Mode", {"Repeat", "Clamp", "Mirror"}, 0) {}
};

/// Texture register slot property (0-7)
class DOT_CORE_API TextureSlotProperty : public IntProperty
{
public:
    TextureSlotProperty() : IntProperty("Texture Slot", 0, 0, 3) {}

    void Deserialize(const std::string& data) override
    {
        IntProperty::Deserialize(data);
        value = std::clamp(value, minValue, maxValue);
    }
};

/// Texture sample interpretation for graph-driven materials
class DOT_CORE_API TextureSampleTypeProperty : public EnumProperty
{
public:
    TextureSampleTypeProperty()
        : EnumProperty("Sample Type", {"Color", "Normal", "Packed Mask"}, static_cast<int>(TextureSampleType::Color))
    {
    }
};

/// Tiling property (UV scale)
class DOT_CORE_API TilingProperty : public NodeProperty
{
public:
    TilingProperty() : NodeProperty("Tiling", PropertyType::Vec3), tilingU(1.0f), tilingV(1.0f) {}

    float tilingU;
    float tilingV;

    std::string Serialize() const override
    {
        std::ostringstream ss;
        ss << tilingU << " " << tilingV;
        return ss.str();
    }

    void Deserialize(const std::string& data) override
    {
        std::istringstream ss(data);
        ss >> tilingU >> tilingV;
    }

    std::string GetHLSLValue() const override
    {
        std::ostringstream ss;
        ss << "float2(" << tilingU << ", " << tilingV << ")";
        return ss.str();
    }
};

/// Offset property (UV offset)
class DOT_CORE_API OffsetProperty : public NodeProperty
{
public:
    OffsetProperty() : NodeProperty("Offset", PropertyType::Vec3), offsetU(0.0f), offsetV(0.0f) {}

    float offsetU;
    float offsetV;

    std::string Serialize() const override
    {
        std::ostringstream ss;
        ss << offsetU << " " << offsetV;
        return ss.str();
    }

    void Deserialize(const std::string& data) override
    {
        std::istringstream ss(data);
        ss >> offsetU >> offsetV;
    }

    std::string GetHLSLValue() const override
    {
        std::ostringstream ss;
        ss << "float2(" << offsetU << ", " << offsetV << ")";
        return ss.str();
    }
};

/// Mipmap enable property (enables mipmap generation for textures)
class DOT_CORE_API MipmapProperty : public BoolProperty
{
public:
    MipmapProperty() : BoolProperty("Mipmaps", true) {} // Default enabled for better quality
};

/// Panner speed property (UV scroll speed)
class DOT_CORE_API PannerSpeedProperty : public NodeProperty
{
public:
    PannerSpeedProperty() : NodeProperty("Speed", PropertyType::Vec3), speedU(0.5f), speedV(0.0f) {}

    float speedU;
    float speedV;

    std::string Serialize() const override
    {
        std::ostringstream ss;
        ss << speedU << " " << speedV;
        return ss.str();
    }

    void Deserialize(const std::string& data) override
    {
        std::istringstream ss(data);
        ss >> speedU >> speedV;
    }

    std::string GetHLSLValue() const override
    {
        std::ostringstream ss;
        ss << "float2(" << speedU << ", " << speedV << ")";
        return ss.str();
    }
};

/// Panner method (Linear, Sine, ZigZag, Rotate)
class DOT_CORE_API PannerMethodProperty : public EnumProperty
{
public:
    PannerMethodProperty() : EnumProperty("Method", {"Linear", "Sine", "ZigZag", "Rotate"}, 0) {}
};

/// Panner Link UV property (Forces U and V speeds to be identical)
class DOT_CORE_API PannerLinkProperty : public BoolProperty
{
public:
    PannerLinkProperty() : BoolProperty("Link UV", false) {}
};

// =============================================================================
// NOISE PROPERTIES
// =============================================================================

class DOT_CORE_API NoiseScaleProperty : public FloatProperty
{
public:
    NoiseScaleProperty() : FloatProperty("Scale", 5.0f, 0.01f, 100.0f) {}
};

class DOT_CORE_API NoiseOctavesProperty : public IntProperty
{
public:
    NoiseOctavesProperty() : IntProperty("Octaves", 4, 1, 8) {}
};

class DOT_CORE_API NoisePersistenceProperty : public FloatProperty
{
public:
    NoisePersistenceProperty() : FloatProperty("Persistence", 0.5f, 0.0f, 1.0f) {}
};

class DOT_CORE_API NoiseLacunarityProperty : public FloatProperty
{
public:
    NoiseLacunarityProperty() : FloatProperty("Lacunarity", 2.0f, 1.0f, 4.0f) {}
};

class DOT_CORE_API NoiseSeedProperty : public FloatProperty
{
public:
    NoiseSeedProperty() : FloatProperty("Seed", 0.0f, 0.0f, 1000.0f) {}
};

// =============================================================================
// Property Factory
// =============================================================================

/// Create a property by cloning (for composable nodes)
template <typename T> std::unique_ptr<NodeProperty> MakeProperty()
{
    return std::make_unique<T>();
}

} // namespace Dot
