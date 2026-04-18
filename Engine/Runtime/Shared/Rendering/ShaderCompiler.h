// =============================================================================
// Dot Engine - Shader Compiler
// =============================================================================
// Centralized shader compilation, caching, and hot-reload support.
// =============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Dot
{

// Shader type for determining compilation target
enum class ShaderType : uint8_t
{
    Vertex,
    Pixel,
    Compute,
    Geometry,
    Hull,
    Domain
};

// Compiled shader data
struct CompiledShader
{
    std::vector<uint8_t> bytecode;
    std::string name;
    std::string entryPoint;
    ShaderType type;
    uint64_t sourceHash = 0; // For hot-reload detection
    bool valid = false;
};

// Shader compilation options
struct ShaderCompileOptions
{
    bool debug = false;                    // Include debug info
    bool optimizationLevel3 = true;        // Maximum optimization
    std::vector<std::string> defines;      // Preprocessor defines
    std::vector<std::string> includePaths; // Additional include directories
};

/// Centralized shader compilation and management
class ShaderCompiler
{
public:
    /// Get singleton instance
    static ShaderCompiler& Get();

    /// Initialize the compiler with shader root directory
    /// @param shaderRootPath Path to shader directory (e.g., "Shaders")
    void Initialize(const std::string& shaderRootPath);

    /// Shutdown and release resources
    void Shutdown();

    /// Compile a shader from file
    /// @param relativePath Path relative to shader root (e.g., "PostProcess/GTAO.hlsl")
    /// @param entryPoint Shader entry function name (e.g., "main" or "PSMain")
    /// @param type Shader type (Vertex, Pixel, Compute, etc.)
    /// @param options Optional compilation settings
    /// @return Pointer to compiled shader, or nullptr on failure
    const CompiledShader* CompileFromFile(const std::string& relativePath, const std::string& entryPoint,
                                          ShaderType type, const ShaderCompileOptions& options = {});

    /// Compile a shader from source string (for legacy/inline shaders)
    /// @param source HLSL source code
    /// @param name Debug name for the shader
    /// @param entryPoint Shader entry function name
    /// @param type Shader type
    /// @param options Optional compilation settings
    /// @return Pointer to compiled shader, or nullptr on failure
    const CompiledShader* CompileFromSource(const std::string& source, const std::string& name,
                                            const std::string& entryPoint, ShaderType type,
                                            const ShaderCompileOptions& options = {});

    /// Get a previously compiled shader by name
    /// @param name Shader identifier (file path or custom name)
    /// @return Pointer to compiled shader, or nullptr if not found
    const CompiledShader* GetShader(const std::string& name) const;

    /// Check for modified shaders and recompile
    /// @param callback Called for each recompiled shader
    void CheckForChanges(std::function<void(const std::string& shaderName)> callback = nullptr);

    /// Enable/disable hot-reload watching
    void SetHotReloadEnabled(bool enabled) { m_HotReloadEnabled = enabled; }
    bool IsHotReloadEnabled() const { return m_HotReloadEnabled; }

    /// Get the shader root path
    const std::string& GetShaderRootPath() const { return m_ShaderRootPath; }

    /// Clear all cached shaders
    void ClearCache();

    /// Get compilation error for last failed shader
    const std::string& GetLastError() const { return m_LastError; }

private:
    ShaderCompiler() = default;
    ~ShaderCompiler() = default;
    ShaderCompiler(const ShaderCompiler&) = delete;
    ShaderCompiler& operator=(const ShaderCompiler&) = delete;

    /// Get shader model target string (e.g., "ps_5_0")
    static const char* GetShaderTarget(ShaderType type);

    /// Read file contents
    bool ReadFile(const std::string& path, std::string& outContent);

    /// Compute hash of source for change detection
    uint64_t ComputeHash(const std::string& source);

    /// Internal compilation using D3DCompile
    bool CompileInternal(const std::string& source, const std::string& name, const std::string& entryPoint,
                         ShaderType type, const ShaderCompileOptions& options, CompiledShader& outShader);

    std::string m_ShaderRootPath;
    std::unordered_map<std::string, CompiledShader> m_ShaderCache;
    std::unordered_map<std::string, uint64_t> m_FileTimestamps; // For hot-reload
    std::string m_LastError;
    bool m_HotReloadEnabled = true;
    bool m_Initialized = false;
};

} // namespace Dot
