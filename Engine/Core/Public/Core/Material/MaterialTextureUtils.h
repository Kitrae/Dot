// =============================================================================
// Dot Engine - Material Texture Utilities
// =============================================================================

#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace Dot
{

enum class TextureSampleType : int
{
    Color = 0,
    Normal = 1,
    Mask = 2,
};

enum class TextureSemantic : int
{
    Auto = 0,
    Color,
    Normal,
    Mask,
    Data,
    HdrColor,
    HdrCubemap,
};

inline std::string MaterialTextureToLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

inline std::vector<std::string> MaterialTextureTokenizeStem(const std::string& path)
{
    const std::string stem = MaterialTextureToLowerAscii(std::filesystem::path(path).stem().string());
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : stem)
    {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
        {
            current.push_back(ch);
        }
        else if (!current.empty())
        {
            tokens.push_back(current);
            current.clear();
        }
    }
    if (!current.empty())
        tokens.push_back(current);
    return tokens;
}

inline bool IsLikelyNormalMapPath(const std::string& path)
{
    if (path.empty())
        return false;

    const std::vector<std::string> tokens = MaterialTextureTokenizeStem(path);
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const std::string& token = tokens[i];
        if (token == "normal" || token == "normals" || token == "norm" || token == "nrm" || token == "nm")
            return true;
        if (token == "n" && i == tokens.size() - 1)
            return true;
    }

    const std::string stem = MaterialTextureToLowerAscii(std::filesystem::path(path).stem().string());
    auto endsWith = [&](const char* suffix) -> bool
    {
        const size_t suffixLen = std::char_traits<char>::length(suffix);
        return stem.size() >= suffixLen && stem.compare(stem.size() - suffixLen, suffixLen, suffix) == 0;
    };
    return endsWith("_normal") || endsWith("_norm") || endsWith("_nrm") || endsWith("_nm") || endsWith("_n");
}

inline bool IsLikelyMaskMapPath(const std::string& path)
{
    if (path.empty())
        return false;

    const std::vector<std::string> tokens = MaterialTextureTokenizeStem(path);
    bool hasAo = false;
    bool hasRoughness = false;
    bool hasMetallic = false;
    for (const std::string& token : tokens)
    {
        if (token == "orm" || token == "arm" || token == "rma" || token == "mra" || token == "mask" ||
            token == "masks")
            return true;
        if (token == "ao" || token == "occlusion")
            hasAo = true;
        if (token == "roughness" || token == "rough")
            hasRoughness = true;
        if (token == "metallic" || token == "metal" || token == "met")
            hasMetallic = true;
    }

    const std::string stem = MaterialTextureToLowerAscii(std::filesystem::path(path).stem().string());
    auto endsWith = [&](const char* suffix) -> bool
    {
        const size_t suffixLen = std::char_traits<char>::length(suffix);
        return stem.size() >= suffixLen && stem.compare(stem.size() - suffixLen, suffixLen, suffix) == 0;
    };

    return (hasAo && hasRoughness) || (hasRoughness && hasMetallic) || endsWith("_orm") || endsWith("_arm") ||
           endsWith("_rma") || endsWith("_mra") || endsWith("_mask") || endsWith("_masks");
}

inline TextureSampleType GuessTextureSampleTypeFromPath(const std::string& path)
{
    if (IsLikelyNormalMapPath(path))
        return TextureSampleType::Normal;
    if (IsLikelyMaskMapPath(path))
        return TextureSampleType::Mask;
    return TextureSampleType::Color;
}

inline bool IsLikelyHdrTexturePath(const std::string& path)
{
    if (path.empty())
        return false;

    const std::string ext = MaterialTextureToLowerAscii(std::filesystem::path(path).extension().string());
    return ext == ".hdr" || ext == ".exr";
}

inline bool IsLikelyCubemapPath(const std::string& path)
{
    if (path.empty())
        return false;

    const std::string loweredPath = MaterialTextureToLowerAscii(path);
    if (loweredPath.find("cubemap") != std::string::npos || loweredPath.find("skybox") != std::string::npos ||
        loweredPath.find("reflectionprobe") != std::string::npos)
    {
        return true;
    }

    return std::filesystem::path(path).has_extension() == false;
}

inline TextureSemantic TextureSemanticFromSampleType(TextureSampleType sampleType)
{
    switch (sampleType)
    {
        case TextureSampleType::Normal:
            return TextureSemantic::Normal;
        case TextureSampleType::Mask:
            return TextureSemantic::Mask;
        case TextureSampleType::Color:
        default:
            return TextureSemantic::Color;
    }
}

inline TextureSemantic GuessTextureSemanticFromPath(const std::string& path)
{
    if (IsLikelyHdrTexturePath(path))
        return IsLikelyCubemapPath(path) ? TextureSemantic::HdrCubemap : TextureSemantic::HdrColor;

    switch (GuessTextureSampleTypeFromPath(path))
    {
        case TextureSampleType::Normal:
            return TextureSemantic::Normal;
        case TextureSampleType::Mask:
            return TextureSemantic::Mask;
        case TextureSampleType::Color:
        default:
            return TextureSemantic::Color;
    }
}

inline bool TextureSemanticUsesSRGB(TextureSemantic semantic)
{
    return semantic == TextureSemantic::Color;
}

inline bool TextureSemanticIsHdr(TextureSemantic semantic)
{
    return semantic == TextureSemantic::HdrColor || semantic == TextureSemantic::HdrCubemap;
}

inline const char* GetTextureSemanticName(TextureSemantic semantic)
{
    switch (semantic)
    {
        case TextureSemantic::Color:
            return "Color";
        case TextureSemantic::Normal:
            return "Normal";
        case TextureSemantic::Mask:
            return "Mask";
        case TextureSemantic::Data:
            return "Data";
        case TextureSemantic::HdrColor:
            return "HdrColor";
        case TextureSemantic::HdrCubemap:
            return "HdrCubemap";
        case TextureSemantic::Auto:
        default:
            return "Auto";
    }
}

inline std::string BuildTextureSemanticCacheKey(const std::string& path, TextureSemantic semantic)
{
    return path + "|semantic=" + GetTextureSemanticName(semantic);
}

} // namespace Dot
