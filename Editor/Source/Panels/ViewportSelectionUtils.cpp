// =============================================================================
// Dot Engine - Viewport Selection Utilities
// =============================================================================

#include "ViewportSelectionUtils.h"

#include <algorithm>
#include <unordered_set>

namespace Dot
{

namespace
{

bool ProjectWorldPointToScreen(const Camera& camera, const Vec3& point, float viewportX, float viewportY, float screenWidth,
                               float screenHeight, ImVec2& outScreenPosition)
{
    const float* viewProjection = camera.GetViewProjectionMatrix();
    const float clipX = viewProjection[0] * point.x + viewProjection[4] * point.y + viewProjection[8] * point.z +
                        viewProjection[12];
    const float clipY = viewProjection[1] * point.x + viewProjection[5] * point.y + viewProjection[9] * point.z +
                        viewProjection[13];
    const float clipW = viewProjection[3] * point.x + viewProjection[7] * point.y + viewProjection[11] * point.z +
                        viewProjection[15];

    if (clipW <= 0.0001f)
        return false;

    const float ndcX = clipX / clipW;
    const float ndcY = clipY / clipW;
    outScreenPosition.x = viewportX + ((ndcX * 0.5f + 0.5f) * screenWidth);
    outScreenPosition.y = viewportY + ((1.0f - (ndcY * 0.5f + 0.5f)) * screenHeight);
    return true;
}

bool ExpandScreenBoundsForVertices(const Camera& camera, const std::vector<Vec3>& vertices, const std::vector<uint32>* indices,
                                   float viewportX, float viewportY, float screenWidth, float screenHeight,
                                   ImVec2& outMin, ImVec2& outMax)
{
    bool projectedAny = false;
    outMin = ImVec2(screenWidth, screenHeight);
    outMax = ImVec2(0.0f, 0.0f);

    auto projectVertex = [&](const Vec3& point)
    {
        ImVec2 screenPoint;
        if (!ProjectWorldPointToScreen(camera, point, viewportX, viewportY, screenWidth, screenHeight, screenPoint))
            return;

        projectedAny = true;
        outMin.x = std::min(outMin.x, screenPoint.x);
        outMin.y = std::min(outMin.y, screenPoint.y);
        outMax.x = std::max(outMax.x, screenPoint.x);
        outMax.y = std::max(outMax.y, screenPoint.y);
    };

    if (indices)
    {
        for (uint32 index : *indices)
        {
            if (index < vertices.size())
                projectVertex(vertices[index]);
        }
    }
    else
    {
        for (const Vec3& vertex : vertices)
            projectVertex(vertex);
    }

    return projectedAny;
}

bool RectanglesOverlap(const ImVec2& minA, const ImVec2& maxA, const ImVec2& minB, const ImVec2& maxB)
{
    return minA.x <= maxB.x && maxA.x >= minB.x && minA.y <= maxB.y && maxA.y >= minB.y;
}

bool MapSelectionsEqual(const MapSelection& lhs, const MapSelection& rhs)
{
    return lhs.brushId == rhs.brushId && lhs.faceIndex == rhs.faceIndex && lhs.vertexIndex == rhs.vertexIndex &&
           lhs.edgeVertexA == rhs.edgeVertexA && lhs.edgeVertexB == rhs.edgeVertexB;
}

} // namespace

bool ScreenPointToCameraPlaneWorld(const Camera& camera, float screenX, float screenY, float viewportX, float viewportY,
                                   float viewportWidth, float viewportHeight, float planeDepth, Vec3& outPoint)
{
    if (viewportWidth <= 1.0f || viewportHeight <= 1.0f)
        return false;

    const float normalizedX = ((screenX - viewportX) / viewportWidth) * 2.0f - 1.0f;
    const float normalizedY = 1.0f - (((screenY - viewportY) / viewportHeight) * 2.0f);

    float originX, originY, originZ;
    float rightX, rightY, rightZ;
    float upX, upY, upZ;
    float forwardX, forwardY, forwardZ;
    camera.GetPosition(originX, originY, originZ);
    camera.GetRight(rightX, rightY, rightZ);
    camera.GetUp(upX, upY, upZ);
    camera.GetForward(forwardX, forwardY, forwardZ);

    const float tanHalfVerticalFov = std::tan(camera.GetFOV() * 0.5f);
    const float tanHalfHorizontalFov = tanHalfVerticalFov * camera.GetAspect();
    const float cameraPlaneX = normalizedX * tanHalfHorizontalFov * planeDepth;
    const float cameraPlaneY = normalizedY * tanHalfVerticalFov * planeDepth;

    outPoint.x = originX + forwardX * planeDepth + rightX * cameraPlaneX + upX * cameraPlaneY;
    outPoint.y = originY + forwardY * planeDepth + rightY * cameraPlaneX + upY * cameraPlaneY;
    outPoint.z = originZ + forwardZ * planeDepth + rightZ * cameraPlaneX + upZ * cameraPlaneY;
    return true;
}

std::vector<MapSelection> CollectMapSelectionsInScreenRect(const Camera& camera, const MapDocument& document, const ImVec2& start,
                                                           const ImVec2& end, float viewportX, float viewportY,
                                                           float screenWidth, float screenHeight)
{
    std::vector<MapSelection> selections;
    const ImVec2 rectMin(std::min(start.x, end.x), std::min(start.y, end.y));
    const ImVec2 rectMax(std::max(start.x, end.x), std::max(start.y, end.y));
    const MapAsset& asset = document.GetAsset();
    const MapSelectionMode selectionMode = document.GetSelectionMode();
    const std::unordered_set<uint32>& hiddenBrushIds = document.GetHiddenBrushIds();

    auto containsEquivalentSelection = [&selections](const MapSelection& candidate) -> bool
    {
        return std::find_if(selections.begin(), selections.end(),
                            [&candidate](const MapSelection& existing)
                            {
                                return MapSelectionsEqual(existing, candidate);
                            }) != selections.end();
    };

    for (const MapBrush& brush : asset.brushes)
    {
        if (hiddenBrushIds.find(brush.brushId) != hiddenBrushIds.end())
            continue;

        if (selectionMode == MapSelectionMode::Brush)
        {
            ImVec2 boundsMin;
            ImVec2 boundsMax;
            if (ExpandScreenBoundsForVertices(camera, brush.vertices, nullptr, viewportX, viewportY, screenWidth,
                                              screenHeight, boundsMin, boundsMax) &&
                RectanglesOverlap(boundsMin, boundsMax, rectMin, rectMax))
            {
                selections.push_back(MapSelection{brush.brushId, -1, -1, -1, -1});
            }
        }
        else if (selectionMode == MapSelectionMode::Face)
        {
            for (size_t faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex)
            {
                const MapFace& face = brush.faces[faceIndex];
                std::vector<Vec3> faceVertices;
                faceVertices.reserve(face.vertexIndices.size());
                for (uint32 vertexIndex : face.vertexIndices)
                {
                    if (vertexIndex < brush.vertices.size())
                        faceVertices.push_back(brush.vertices[vertexIndex]);
                }

                ImVec2 boundsMin;
                ImVec2 boundsMax;
                if (!faceVertices.empty() &&
                    ExpandScreenBoundsForVertices(camera, faceVertices, nullptr, viewportX, viewportY, screenWidth,
                                                  screenHeight, boundsMin, boundsMax) &&
                    RectanglesOverlap(boundsMin, boundsMax, rectMin, rectMax))
                {
                    selections.push_back(MapSelection{brush.brushId, static_cast<int>(faceIndex), -1, -1, -1});
                }
            }
        }
        else if (selectionMode == MapSelectionMode::Edge)
        {
            for (size_t faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex)
            {
                const MapFace& face = brush.faces[faceIndex];
                if (face.vertexIndices.size() < 2)
                    continue;

                for (size_t edgeIndex = 0; edgeIndex < face.vertexIndices.size(); ++edgeIndex)
                {
                    const uint32 rawA = face.vertexIndices[edgeIndex];
                    const uint32 rawB = face.vertexIndices[(edgeIndex + 1) % face.vertexIndices.size()];
                    if (rawA >= brush.vertices.size() || rawB >= brush.vertices.size())
                        continue;

                    ImVec2 screenA;
                    ImVec2 screenB;
                    if (!ProjectWorldPointToScreen(camera, brush.vertices[rawA], viewportX, viewportY, screenWidth,
                                                   screenHeight, screenA) ||
                        !ProjectWorldPointToScreen(camera, brush.vertices[rawB], viewportX, viewportY, screenWidth,
                                                   screenHeight, screenB))
                    {
                        continue;
                    }

                    const ImVec2 edgeMin(std::min(screenA.x, screenB.x), std::min(screenA.y, screenB.y));
                    const ImVec2 edgeMax(std::max(screenA.x, screenB.x), std::max(screenA.y, screenB.y));
                    if (!RectanglesOverlap(edgeMin, edgeMax, rectMin, rectMax))
                        continue;

                    MapSelection selection;
                    selection.brushId = brush.brushId;
                    selection.edgeVertexA = static_cast<int>(std::min(rawA, rawB));
                    selection.edgeVertexB = static_cast<int>(std::max(rawA, rawB));
                    if (!containsEquivalentSelection(selection))
                        selections.push_back(selection);
                }
            }
        }
        else if (selectionMode == MapSelectionMode::Vertex)
        {
            for (size_t vertexIndex = 0; vertexIndex < brush.vertices.size(); ++vertexIndex)
            {
                ImVec2 screenPoint;
                if (!ProjectWorldPointToScreen(camera, brush.vertices[vertexIndex], viewportX, viewportY, screenWidth,
                                               screenHeight, screenPoint))
                {
                    continue;
                }

                constexpr float kVertexScreenRadius = 6.0f;
                const ImVec2 vertexMin(screenPoint.x - kVertexScreenRadius, screenPoint.y - kVertexScreenRadius);
                const ImVec2 vertexMax(screenPoint.x + kVertexScreenRadius, screenPoint.y + kVertexScreenRadius);
                if (!RectanglesOverlap(vertexMin, vertexMax, rectMin, rectMax))
                    continue;

                MapSelection selection;
                selection.brushId = brush.brushId;
                selection.vertexIndex = static_cast<int>(vertexIndex);
                selections.push_back(selection);
            }
        }
    }

    return selections;
}

std::vector<Entity> CollectEntitiesInScreenRect(const Camera& camera, World& world, const ImVec2& start, const ImVec2& end,
                                                float viewportX, float viewportY, float screenWidth, float screenHeight)
{
    std::vector<Entity> entities;
    const ImVec2 rectMin(std::min(start.x, end.x), std::min(start.y, end.y));
    const ImVec2 rectMax(std::max(start.x, end.x), std::max(start.y, end.y));

    world.Each<TransformComponent>(
        [&](Entity entity, TransformComponent& transform)
        {
            const Vec3 worldPosition = transform.worldMatrix.GetTranslation();
            const Vec3 worldScale = transform.worldMatrix.GetScale();
            const Vec3 halfExtents(std::max(0.5f, std::abs(worldScale.x) * 0.5f),
                                   std::max(0.5f, std::abs(worldScale.y) * 0.5f),
                                   std::max(0.5f, std::abs(worldScale.z) * 0.5f));

            const std::vector<Vec3> boundsCorners = {
                worldPosition + Vec3(-halfExtents.x, -halfExtents.y, -halfExtents.z),
                worldPosition + Vec3(halfExtents.x, -halfExtents.y, -halfExtents.z),
                worldPosition + Vec3(-halfExtents.x, halfExtents.y, -halfExtents.z),
                worldPosition + Vec3(halfExtents.x, halfExtents.y, -halfExtents.z),
                worldPosition + Vec3(-halfExtents.x, -halfExtents.y, halfExtents.z),
                worldPosition + Vec3(halfExtents.x, -halfExtents.y, halfExtents.z),
                worldPosition + Vec3(-halfExtents.x, halfExtents.y, halfExtents.z),
                worldPosition + Vec3(halfExtents.x, halfExtents.y, halfExtents.z),
            };

            ImVec2 boundsMin;
            ImVec2 boundsMax;
            if (ExpandScreenBoundsForVertices(camera, boundsCorners, nullptr, viewportX, viewportY, screenWidth,
                                              screenHeight, boundsMin, boundsMax) &&
                RectanglesOverlap(boundsMin, boundsMax, rectMin, rectMax))
            {
                entities.push_back(entity);
            }
        });

    return entities;
}

} // namespace Dot
