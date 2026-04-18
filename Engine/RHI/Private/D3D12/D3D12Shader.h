// =============================================================================
// Dot Engine - D3D12 Shader
// =============================================================================
// D3D12 implementation of RHIShader.
// =============================================================================

#pragma once

#include "D3D12Common.h"
#include "RHI/RHIShader.h"


#ifdef DOT_PLATFORM_WINDOWS

namespace Dot
{

class D3D12Device;

class D3D12Shader : public RHIShader
{
public:
    D3D12Shader(D3D12Device* device, const RHIShaderBytecode& bytecode);
    ~D3D12Shader() override = default;

    RHIShaderStage GetStage() const override { return m_Stage; }

    const void* GetBytecode() const { return m_Bytecode.data(); }
    size_t GetBytecodeSize() const { return m_Bytecode.size(); }

private:
    std::vector<uint8> m_Bytecode;
    RHIShaderStage m_Stage;
};

class D3D12ShaderProgram : public RHIShaderProgram
{
public:
    D3D12ShaderProgram(RHIShaderPtr vertex, RHIShaderPtr fragment);
    ~D3D12ShaderProgram() override = default;

    RHIShader* GetVertexShader() const override { return m_VertexShader.get(); }
    RHIShader* GetFragmentShader() const override { return m_FragmentShader.get(); }

private:
    RHIShaderPtr m_VertexShader;
    RHIShaderPtr m_FragmentShader;
};

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
