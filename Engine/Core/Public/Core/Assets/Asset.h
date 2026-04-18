// =============================================================================
// Dot Engine - Base Asset Class
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <atomic>
#include <memory>
#include <string>


namespace Dot
{

enum class AssetState
{
    Unloaded,
    Loading,
    Ready,
    Failed
};

class Asset
{
public:
    Asset(const std::string& path) : m_Path(path) {}
    virtual ~Asset() = default;

    const std::string& GetPath() const { return m_Path; }
    AssetState GetState() const { return m_State.load(); }

    bool IsReady() const { return GetState() == AssetState::Ready; }
    bool IsLoading() const { return GetState() == AssetState::Loading; }
    bool HasFailed() const { return GetState() == AssetState::Failed; }

protected:
    friend class AssetManager;
    void SetState(AssetState state) { m_State.store(state); }

    std::string m_Path;
    std::atomic<AssetState> m_State{AssetState::Unloaded};
};

using AssetPtr = std::shared_ptr<Asset>;

template <typename T> class AssetHandle
{
public:
    AssetHandle() = default;
    AssetHandle(std::shared_ptr<T> asset) : m_Asset(asset) {}

    T* operator->() { return m_Asset.get(); }
    const T* operator->() const { return m_Asset.get(); }

    T& operator*() { return *m_Asset; }
    const T& operator*() const { return *m_Asset; }

    bool IsValid() const { return m_Asset != nullptr; }
    bool IsReady() const { return IsValid() && m_Asset->IsReady(); }

    std::shared_ptr<T> GetInternal() const { return m_Asset; }

private:
    std::shared_ptr<T> m_Asset;
};

} // namespace Dot
