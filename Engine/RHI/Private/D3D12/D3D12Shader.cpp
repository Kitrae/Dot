// =============================================================================
// Dot Engine - D3D12 Shader Implementation
// =============================================================================

#include "D3D12Shader.h"

#include "D3D12Device.h"


#ifdef DOT_PLATFORM_WINDOWS

namespace Dot
{

D3D12Shader::D3D12Shader(D3D12Device* device, const RHIShaderBytecode& bytecode)
    : m_Bytecode(bytecode.data), m_Stage(bytecode.stage)
{
    (void)device;
    // Bytecode is already compiled DXBC/DXIL, just store it
}

D3D12ShaderProgram::D3D12ShaderProgram(RHIShaderPtr vertex, RHIShaderPtr fragment)
    : m_VertexShader(std::move(vertex)), m_FragmentShader(std::move(fragment))
{
}

} // namespace Dot

#endif // DOT_PLATFORM_WINDOWS
