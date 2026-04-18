// =============================================================================
// Dot Engine - Texture Asset
// =============================================================================

#pragma once

#include "Core/Assets/Asset.h"
#include "Core/Material/MaterialTextureUtils.h"
#include "RHI/RHITypes.h"

#include <memory>


namespace Dot
{

class RHITexture;
using RHITexturePtr = std::shared_ptr<RHITexture>;

class TextureAsset : public Asset
{
public:
    TextureAsset(const std::string& path) : Asset(path) {}

    RHITexturePtr GetTexture() const { return m_Texture; }
    void SetTexture(RHITexturePtr texture) { m_Texture = texture; }
    TextureSemantic GetSemantic() const { return m_Semantic; }
    void SetSemantic(TextureSemantic semantic) { m_Semantic = semantic; }
    RHIFormat GetFormat() const { return m_Format; }
    void SetFormat(RHIFormat format) { m_Format = format; }
    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }
    void SetDimensions(uint32_t width, uint32_t height)
    {
        m_Width = width;
        m_Height = height;
    }

private:
    RHITexturePtr m_Texture;
    TextureSemantic m_Semantic = TextureSemantic::Color;
    RHIFormat m_Format = RHIFormat::Unknown;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
};

using TextureAssetPtr = std::shared_ptr<TextureAsset>;

} // namespace Dot
