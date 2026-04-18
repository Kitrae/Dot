// =============================================================================
// Dot Engine - RHI Shader
// =============================================================================
// Abstract GPU shader interface.
// =============================================================================

#pragma once

#include "RHI/RHITypes.h"

#include <memory>
#include <string>
#include <vector>


namespace Dot
{

/// Shader bytecode
struct RHIShaderBytecode
{
    std::vector<uint8> data;
    RHIShaderStage stage = RHIShaderStage::Vertex;
    std::string entryPoint = "main";
};

/// Abstract GPU shader module
class RHIShader
{
public:
    virtual ~RHIShader() = default;

    virtual RHIShaderStage GetStage() const = 0;

protected:
    RHIShader() = default;
};

using RHIShaderPtr = std::shared_ptr<RHIShader>;

/// Shader program (linked shaders)
class RHIShaderProgram
{
public:
    virtual ~RHIShaderProgram() = default;

    virtual RHIShader* GetVertexShader() const = 0;
    virtual RHIShader* GetFragmentShader() const = 0;

protected:
    RHIShaderProgram() = default;
};

using RHIShaderProgramPtr = std::shared_ptr<RHIShaderProgram>;

} // namespace Dot
