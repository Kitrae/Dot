// =============================================================================
// Dot Engine - Shader Compiler Implementation
// =============================================================================

#include "ShaderCompiler.h"

#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

namespace Dot
{

ShaderCompiler& ShaderCompiler::Get()
{
    static ShaderCompiler instance;
    return instance;
}

void ShaderCompiler::Initialize(const std::string& shaderRootPath)
{
    m_ShaderRootPath = shaderRootPath;

    // Ensure path ends with separator
    if (!m_ShaderRootPath.empty() && m_ShaderRootPath.back() != '/' && m_ShaderRootPath.back() != '\\')
    {
        m_ShaderRootPath += '/';
    }

    m_Initialized = true;
}

void ShaderCompiler::Shutdown()
{
    ClearCache();
    m_Initialized = false;
}

const char* ShaderCompiler::GetShaderTarget(ShaderType type)
{
    switch (type)
    {
        case ShaderType::Vertex:
            return "vs_5_0";
        case ShaderType::Pixel:
            return "ps_5_0";
        case ShaderType::Compute:
            return "cs_5_0";
        case ShaderType::Geometry:
            return "gs_5_0";
        case ShaderType::Hull:
            return "hs_5_0";
        case ShaderType::Domain:
            return "ds_5_0";
        default:
            return "ps_5_0";
    }
}

bool ShaderCompiler::ReadFile(const std::string& path, std::string& outContent)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        m_LastError = "Failed to open file: " + path;
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    outContent.resize(static_cast<size_t>(size));
    if (!file.read(outContent.data(), size))
    {
        m_LastError = "Failed to read file: " + path;
        return false;
    }

    return true;
}

uint64_t ShaderCompiler::ComputeHash(const std::string& source)
{
    // Simple FNV-1a hash
    uint64_t hash = 14695981039346656037ULL;
    for (char c : source)
    {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool ShaderCompiler::CompileInternal(const std::string& source, const std::string& name, const std::string& entryPoint,
                                     ShaderType type, const ShaderCompileOptions& options, CompiledShader& outShader)
{
    UINT compileFlags = 0;

    if (options.debug)
    {
        compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    }
    else if (options.optimizationLevel3)
    {
        compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
    }

    // Build defines
    std::vector<D3D_SHADER_MACRO> macros;
    for (const auto& def : options.defines)
    {
        size_t eqPos = def.find('=');
        if (eqPos != std::string::npos)
        {
            // Store in temporary strings that live until compile
            macros.push_back({def.substr(0, eqPos).c_str(), def.substr(eqPos + 1).c_str()});
        }
        else
        {
            macros.push_back({def.c_str(), "1"});
        }
    }
    macros.push_back({nullptr, nullptr}); // Terminator

    const char* target = GetShaderTarget(type);

    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    // Custom include handler for #include support
    class IncludeHandler : public ID3DInclude
    {
    public:
        IncludeHandler(const std::string& basePath) : m_BasePath(basePath) {}

        HRESULT __stdcall Open(D3D_INCLUDE_TYPE includeType, LPCSTR fileName, LPCVOID parentData, LPCVOID* data,
                               UINT* bytes) override
        {
            (void)includeType;
            (void)parentData;

            std::string fullPath = m_BasePath + fileName;
            std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                return E_FAIL;
            }

            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);

            char* buffer = new char[static_cast<size_t>(size)];
            file.read(buffer, size);

            *data = buffer;
            *bytes = static_cast<UINT>(size);
            return S_OK;
        }

        HRESULT __stdcall Close(LPCVOID data) override
        {
            delete[] static_cast<const char*>(data);
            return S_OK;
        }

    private:
        std::string m_BasePath;
    };

    IncludeHandler includeHandler(m_ShaderRootPath);

    HRESULT hr = D3DCompile(source.c_str(), source.length(), name.c_str(), macros.data(), &includeHandler,
                            entryPoint.c_str(), target, compileFlags, 0, &shaderBlob, &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            m_LastError =
                std::string(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
            errorBlob->Release();
        }
        else
        {
            m_LastError = "Unknown shader compilation error";
        }

        if (shaderBlob)
            shaderBlob->Release();
        return false;
    }

    if (errorBlob)
    {
        // Warnings (compilation succeeded)
        errorBlob->Release();
    }

    // Copy bytecode
    outShader.bytecode.resize(shaderBlob->GetBufferSize());
    memcpy(outShader.bytecode.data(), shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
    outShader.name = name;
    outShader.entryPoint = entryPoint;
    outShader.type = type;
    outShader.sourceHash = ComputeHash(source);
    outShader.valid = true;

    shaderBlob->Release();
    return true;
}

const CompiledShader* ShaderCompiler::CompileFromFile(const std::string& relativePath, const std::string& entryPoint,
                                                      ShaderType type, const ShaderCompileOptions& options)
{
    std::string fullPath = m_ShaderRootPath + relativePath;
    std::string source;

    if (!ReadFile(fullPath, source))
    {
        return nullptr;
    }

    // Use relative path as cache key
    std::string cacheKey = relativePath + ":" + entryPoint;

    CompiledShader shader;
    if (!CompileInternal(source, relativePath, entryPoint, type, options, shader))
    {
        return nullptr;
    }

    // Cache and return
    m_ShaderCache[cacheKey] = std::move(shader);

    // Store file timestamp for hot-reload
    if (m_HotReloadEnabled)
    {
        try
        {
            auto ftime = std::filesystem::last_write_time(fullPath);
            m_FileTimestamps[relativePath] = ftime.time_since_epoch().count();
        }
        catch (...)
        {
        }
    }

    return &m_ShaderCache[cacheKey];
}

const CompiledShader* ShaderCompiler::CompileFromSource(const std::string& source, const std::string& name,
                                                        const std::string& entryPoint, ShaderType type,
                                                        const ShaderCompileOptions& options)
{
    std::string cacheKey = name + ":" + entryPoint;

    CompiledShader shader;
    if (!CompileInternal(source, name, entryPoint, type, options, shader))
    {
        return nullptr;
    }

    m_ShaderCache[cacheKey] = std::move(shader);
    return &m_ShaderCache[cacheKey];
}

const CompiledShader* ShaderCompiler::GetShader(const std::string& name) const
{
    auto it = m_ShaderCache.find(name);
    if (it != m_ShaderCache.end())
    {
        return &it->second;
    }
    return nullptr;
}

void ShaderCompiler::CheckForChanges(std::function<void(const std::string& shaderName)> callback)
{
    if (!m_HotReloadEnabled)
        return;

    for (auto& [path, timestamp] : m_FileTimestamps)
    {
        std::string fullPath = m_ShaderRootPath + path;

        try
        {
            auto ftime = std::filesystem::last_write_time(fullPath);
            uint64_t newTimestamp = ftime.time_since_epoch().count();

            if (newTimestamp != timestamp)
            {
                timestamp = newTimestamp;

                // Find all shaders using this file and recompile
                for (auto& [key, shader] : m_ShaderCache)
                {
                    if (key.find(path) == 0)
                    {
                        std::string source;
                        if (ReadFile(fullPath, source))
                        {
                            ShaderCompileOptions opts;
                            if (CompileInternal(source, shader.name, shader.entryPoint, shader.type, opts, shader))
                            {
                                if (callback)
                                    callback(key);
                            }
                        }
                    }
                }
            }
        }
        catch (...)
        {
        }
    }
}

void ShaderCompiler::ClearCache()
{
    m_ShaderCache.clear();
    m_FileTimestamps.clear();
    m_LastError.clear();
}

} // namespace Dot
