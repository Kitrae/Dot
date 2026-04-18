#pragma once

#include "Command.h"

#include "../Map/MapDocument.h"

#include <string>
#include <unordered_set>

namespace Dot
{

class MapSnapshotCommand : public Command
{
public:
    MapSnapshotCommand(MapDocument* document, std::string name, const MapAsset& beforeAsset,
                       const std::vector<MapSelection>& beforeSelections, const MapSelection& beforeSelection,
                       const std::unordered_set<uint32>& beforeHiddenBrushIds,
                       const std::unordered_set<uint32>& beforeLockedBrushIds, bool beforeDirty,
                       const MapAsset& afterAsset, const std::vector<MapSelection>& afterSelections,
                       const MapSelection& afterSelection, const std::unordered_set<uint32>& afterHiddenBrushIds,
                       const std::unordered_set<uint32>& afterLockedBrushIds, bool afterDirty)
        : m_Document(document), m_Name(std::move(name)), m_BeforeAsset(beforeAsset),
          m_BeforeSelections(beforeSelections), m_BeforeSelection(beforeSelection),
          m_BeforeHiddenBrushIds(beforeHiddenBrushIds), m_BeforeLockedBrushIds(beforeLockedBrushIds),
          m_BeforeDirty(beforeDirty), m_AfterAsset(afterAsset), m_AfterSelections(afterSelections),
          m_AfterSelection(afterSelection), m_AfterHiddenBrushIds(afterHiddenBrushIds),
          m_AfterLockedBrushIds(afterLockedBrushIds), m_AfterDirty(afterDirty)
    {
    }

    void Execute() override
    {
        if (m_Document)
            m_Document->ApplySnapshot(m_AfterAsset, m_AfterSelections, m_AfterSelection, m_AfterHiddenBrushIds,
                                      m_AfterLockedBrushIds, m_AfterDirty);
    }

    void Undo() override
    {
        if (m_Document)
            m_Document->ApplySnapshot(m_BeforeAsset, m_BeforeSelections, m_BeforeSelection, m_BeforeHiddenBrushIds,
                                      m_BeforeLockedBrushIds, m_BeforeDirty);
    }

    bool CanUndo() const override { return true; }
    const char* GetName() const override { return m_Name.c_str(); }

private:
    MapDocument* m_Document = nullptr;
    std::string m_Name;
    MapAsset m_BeforeAsset;
    std::vector<MapSelection> m_BeforeSelections;
    MapSelection m_BeforeSelection;
    std::unordered_set<uint32> m_BeforeHiddenBrushIds;
    std::unordered_set<uint32> m_BeforeLockedBrushIds;
    bool m_BeforeDirty = false;
    MapAsset m_AfterAsset;
    std::vector<MapSelection> m_AfterSelections;
    MapSelection m_AfterSelection;
    std::unordered_set<uint32> m_AfterHiddenBrushIds;
    std::unordered_set<uint32> m_AfterLockedBrushIds;
    bool m_AfterDirty = false;
};

} // namespace Dot
