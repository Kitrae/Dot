#pragma once

#include "Core/Map/MapCompiler.h"
#include "Core/Map/MapSerializer.h"
#include "Core/Map/StaticWorldGeometry.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace Dot
{

enum class MapSelectionMode : uint8
{
    Brush = 0,
    Face,
    Edge,
    Vertex
};

struct MapSelection
{
    uint32 brushId = 0;
    int faceIndex = -1;
    int vertexIndex = -1;
    int edgeVertexA = -1;
    int edgeVertexB = -1;

    void Clear()
    {
        brushId = 0;
        faceIndex = -1;
        vertexIndex = -1;
        edgeVertexA = -1;
        edgeVertexB = -1;
    }
};

class MapDocument
{
public:
    MapDocument();

    void New();
    bool Load(const std::filesystem::path& path);
    bool Save();
    bool SaveAs(const std::filesystem::path& path);
    void SetPath(const std::filesystem::path& path) { m_Path = path; }

    const std::filesystem::path& GetPath() const { return m_Path; }
    bool HasPath() const { return !m_Path.empty(); }
    bool IsDirty() const { return m_Dirty; }
    const std::string& GetLastError() const { return m_LastError; }

    const MapAsset& GetAsset() const { return m_Asset; }
    MapAsset& GetAsset() { return m_Asset; }
    const MapCompiledData& GetCompiledData() const { return m_Compiled; }
    std::shared_ptr<StaticWorldGeometry> GetStaticWorldGeometry() const { return m_StaticWorldGeometry; }
    uint64 GetRevision() const { return m_Revision; }

    MapSelectionMode GetSelectionMode() const { return m_SelectionMode; }
    void SetSelectionMode(MapSelectionMode selectionMode) { m_SelectionMode = selectionMode; }

    const MapSelection& GetSelection() const { return m_Selection; }
    const std::vector<MapSelection>& GetSelections() const { return m_Selections; }
    void SetSelection(const MapSelection& selection);
    void SetSelections(const std::vector<MapSelection>& selections);
    bool AddSelection(const MapSelection& selection);
    bool ToggleSelection(const MapSelection& selection);
    bool IsSelectionSelected(const MapSelection& selection) const;
    void ClearSelection();
    bool IsBrushHidden(uint32 brushId) const;
    bool IsBrushLocked(uint32 brushId) const;
    const std::unordered_set<uint32>& GetHiddenBrushIds() const { return m_HiddenBrushIds; }
    const std::unordered_set<uint32>& GetLockedBrushIds() const { return m_LockedBrushIds; }
    bool SetBrushHidden(uint32 brushId, bool hidden);
    bool SetBrushLocked(uint32 brushId, bool locked);
    bool HideSelectedBrushes();
    bool HideUnselectedBrushes();
    bool UnhideAllBrushes();
    bool LockSelectedBrushes();
    bool UnlockAllBrushes();
    bool IsolateSelectedBrushes();

    MapBrush* GetSelectedBrush();
    const MapBrush* GetSelectedBrush() const;
    MapFace* GetSelectedFace();
    const MapFace* GetSelectedFace() const;
    Vec3 GetSelectedBrushCenter() const;
    Vec3 GetSelectionPivot() const;

    uint32 CreateBoxBrush(const Vec3& center, const Vec3& halfExtents, const std::string& defaultMaterialPath = {});
    uint32 PasteBrush(const MapBrush& brush, const Vec3& offset = Vec3(1.0f, 0.0f, 0.0f), bool appendCopySuffix = true);
    bool DeleteSelectedBrush();
    bool DuplicateSelectedBrush();
    bool TranslateSelectedBrush(const Vec3& delta, bool rebuildStaticWorld = true);
    bool HollowSelectedBrush(float wallThickness);
    bool BuildSelectedBrushHollowPreview(float wallThickness, std::vector<MapBrush>& outBrushes) const;
    bool TranslateSelectedFace(float distance, bool rebuildStaticWorld = true);
    bool ExtrudeSelectedFace(float distance);
    bool ClipSelectedBrush(float planeOffset, bool flipPlane);
    bool BuildSelectedBrushClipPreview(float planeOffset, bool flipPlane, std::vector<Vec3>& outPolygon,
                                       Vec3& outPlaneNormal) const;
    bool BuildSelectedFaceExtrudePreview(float distance, std::vector<Vec3>& outPolygon, Vec3& outFaceNormal) const;
    bool TranslateSelectedEdge(const Vec3& delta, bool rebuildStaticWorld = true);
    bool TranslateSelectedVertex(const Vec3& delta, bool rebuildStaticWorld = true);
    bool SetSelectedFaceMaterial(const std::string& materialPath);
    bool SetSelectedBrushMaterial(const std::string& materialPath);
    bool SetSelectedFaceSurface(const std::string& materialPath, const MapFaceUV& uv);
    bool SetSelectedFaceUV(const MapFaceUV& uv);
    bool FitSelectedFaceUV();
    bool NudgeSelectedFaceUV(float deltaU, float deltaV);
    bool RotateSelectedFaceUV(float deltaDegrees);
    bool FlipSelectedFaceUV(bool flipU, bool flipV);
    bool SelectAllCoplanarFaces();
    bool SelectFacesWithSameMaterial();
    bool SelectLinkedBrushFaces();
    bool GrowFaceSelection();
    bool ShrinkFaceSelection();
    bool RenameSelectedBrush(const std::string& name);

    bool Pick(const Ray& ray, MapSelection& outSelection, float* outDistance = nullptr) const;

    void ApplySnapshot(const MapAsset& asset, const std::vector<MapSelection>& selections, const MapSelection& selection,
                       const std::unordered_set<uint32>& hiddenBrushIds, const std::unordered_set<uint32>& lockedBrushIds,
                       bool dirty);
    void RebuildCompiledData(bool rebuildStaticWorld = true);
    void MarkDirty(bool rebuildCompiledData = true, bool rebuildStaticWorld = true);

private:
    bool PickBrush(const Ray& ray, MapSelection& outSelection, float* outDistance) const;
    bool PickFace(const Ray& ray, MapSelection& outSelection, float* outDistance) const;
    bool PickVertex(const Ray& ray, MapSelection& outSelection, float* outDistance) const;
    bool PickEdge(const Ray& ray, MapSelection& outSelection, float* outDistance) const;

    std::filesystem::path m_Path;
    MapAsset m_Asset;
    MapCompiledData m_Compiled;
    std::shared_ptr<StaticWorldGeometry> m_StaticWorldGeometry;
    MapSelectionMode m_SelectionMode = MapSelectionMode::Brush;
    MapSelection m_Selection;
    std::vector<MapSelection> m_Selections;
    std::unordered_set<uint32> m_HiddenBrushIds;
    std::unordered_set<uint32> m_LockedBrushIds;
    MapSerializer m_Serializer;
    std::string m_LastError;
    uint64 m_Revision = 1;
    bool m_Dirty = false;
};

} // namespace Dot
