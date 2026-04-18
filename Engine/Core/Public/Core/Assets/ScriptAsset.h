// =============================================================================
// Dot Engine - Script Asset
// =============================================================================

#pragma once

#include "Core/Assets/Asset.h"

#include <string>


namespace Dot
{

class ScriptAsset : public Asset
{
public:
    ScriptAsset(const std::string& path) : Asset(path) {}

    const std::string& GetSource() const { return m_SourceCode; }
    void SetSource(const std::string& source) { m_SourceCode = source; }

private:
    std::string m_SourceCode;
};

using ScriptAssetPtr = std::shared_ptr<ScriptAsset>;

} // namespace Dot
