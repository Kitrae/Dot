#include "Core/Map/MapSerializer.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Dot
{

namespace
{

bool ReadExactPrefix(std::istream& input, const char* expected)
{
    std::string line;
    std::getline(input, line);
    return line == expected;
}

std::string Unquote(std::string value)
{
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        return value.substr(1, value.size() - 2);
    return value;
}

} // namespace

bool MapSerializer::Save(const MapAsset& asset, const std::string& path)
{
    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open())
    {
        m_LastError = "Failed to open map for writing: " + path;
        return false;
    }

    output << "DOTMAP 2\n";
    output << "NEXT_BRUSH_ID " << asset.nextBrushId << "\n";
    output << "BRUSH_COUNT " << asset.brushes.size() << "\n";

    for (const MapBrush& brush : asset.brushes)
    {
        output << "BRUSH " << brush.brushId << " " << std::quoted(brush.name) << "\n";
        output << "BRUSH_BAKE " << brush.bakedLighting.participateInBake << " " << brush.bakedLighting.receiveBakedLighting
               << " " << brush.bakedLighting.castBakedShadows << " " << brush.bakedLighting.resolutionScale << "\n";
        output << "VERTEX_COUNT " << brush.vertices.size() << "\n";
        for (const Vec3& vertex : brush.vertices)
        {
            output << "VERTEX " << vertex.x << " " << vertex.y << " " << vertex.z << "\n";
        }

        output << "FACE_COUNT " << brush.faces.size() << "\n";
        for (const MapFace& face : brush.faces)
        {
            output << "FACE " << face.vertexIndices.size();
            for (uint32 vertexIndex : face.vertexIndices)
                output << " " << vertexIndex;
            output << "\n";
            output << "MATERIAL " << std::quoted(face.materialPath) << "\n";
            output << "UV " << static_cast<uint32>(face.uv.projectionMode) << " " << face.uv.scaleU << " "
                   << face.uv.scaleV << " " << face.uv.offsetU << " " << face.uv.offsetV << " " << face.uv.rotationDeg
                   << "\n";
            output << "FACE_BAKE " << face.bakedLighting.useBakedLighting << " " << face.bakedLighting.lightmapIntensity
                   << " " << face.bakedLighting.bakeValid << " " << face.bakedLighting.bakeStale << " "
                   << face.bakedLighting.lightmapScaleU << " " << face.bakedLighting.lightmapScaleV << " "
                   << face.bakedLighting.lightmapOffsetU << " " << face.bakedLighting.lightmapOffsetV << "\n";
            output << "LIGHTMAP_TEXTURE " << std::quoted(face.bakedLighting.lightmapTexturePath) << "\n";
            output << "LIGHTMAP_SIDECAR " << std::quoted(face.bakedLighting.lightmapSidecarPath) << "\n";
            output << "BAKE_SIGNATURE " << std::quoted(face.bakedLighting.bakeSignature) << "\n";
        }
        output << "END_BRUSH\n";
    }

    m_LastError.clear();
    return true;
}

bool MapSerializer::Load(MapAsset& asset, const std::string& path)
{
    std::ifstream input(path);
    if (!input.is_open())
    {
        m_LastError = "Failed to open map for reading: " + path;
        return false;
    }

    asset.Clear();

    std::string header;
    std::getline(input, header);
    if (header != "DOTMAP 1" && header != "DOTMAP 2")
    {
        m_LastError = "Invalid map header";
        return false;
    }
    const uint32 fileVersion = (header == "DOTMAP 2") ? 2u : 1u;
    asset.version = fileVersion;

    std::string token;
    size_t brushCount = 0;

    while (input >> token)
    {
        if (token == "NEXT_BRUSH_ID")
        {
            input >> asset.nextBrushId;
        }
        else if (token == "BRUSH_COUNT")
        {
            input >> brushCount;
            asset.brushes.reserve(brushCount);
        }
        else if (token == "BRUSH")
        {
            MapBrush brush;
            input >> brush.brushId;
            input >> std::ws;

            std::getline(input, brush.name);
            brush.name = Unquote(brush.name);

            input >> token;
            if (fileVersion >= 2 && token == "BRUSH_BAKE")
            {
                input >> brush.bakedLighting.participateInBake >> brush.bakedLighting.receiveBakedLighting >>
                    brush.bakedLighting.castBakedShadows >> brush.bakedLighting.resolutionScale;
                input >> token;
            }

            if (token != "VERTEX_COUNT")
            {
                m_LastError = "Expected VERTEX_COUNT";
                return false;
            }
            size_t vertexCount = 0;
            size_t faceCount = 0;
            input >> vertexCount;

            brush.vertices.reserve(vertexCount);
            for (size_t i = 0; i < vertexCount; ++i)
            {
                Vec3 vertex;
                input >> token >> vertex.x >> vertex.y >> vertex.z;
                if (token != "VERTEX")
                {
                    m_LastError = "Expected VERTEX";
                    return false;
                }
                brush.vertices.push_back(vertex);
            }

            input >> token >> faceCount;
            if (token != "FACE_COUNT")
            {
                m_LastError = "Expected FACE_COUNT";
                return false;
            }

            brush.faces.reserve(faceCount);
            for (size_t faceIndex = 0; faceIndex < faceCount; ++faceIndex)
            {
                MapFace face;
                size_t faceVertexCount = 0;
                input >> token >> faceVertexCount;
                if (token != "FACE")
                {
                    m_LastError = "Expected FACE";
                    return false;
                }

                face.vertexIndices.resize(faceVertexCount);
                for (size_t vertexIndex = 0; vertexIndex < faceVertexCount; ++vertexIndex)
                {
                    input >> face.vertexIndices[vertexIndex];
                }

                input >> token >> std::ws;
                if (token != "MATERIAL")
                {
                    m_LastError = "Expected MATERIAL";
                    return false;
                }

                std::getline(input, face.materialPath);
                face.materialPath = Unquote(face.materialPath);

                uint32 projectionMode = 0;
                input >> token >> projectionMode >> face.uv.scaleU >> face.uv.scaleV >> face.uv.offsetU >> face.uv.offsetV >>
                    face.uv.rotationDeg;
                if (token != "UV")
                {
                    m_LastError = "Expected UV";
                    return false;
                }
                face.uv.projectionMode = static_cast<MapProjectionMode>(projectionMode);

                if (fileVersion >= 2)
                {
                    input >> token;
                    if (token != "FACE_BAKE")
                    {
                        m_LastError = "Expected FACE_BAKE";
                        return false;
                    }
                    input >> face.bakedLighting.useBakedLighting >> face.bakedLighting.lightmapIntensity >>
                        face.bakedLighting.bakeValid >> face.bakedLighting.bakeStale >> face.bakedLighting.lightmapScaleU >>
                        face.bakedLighting.lightmapScaleV >> face.bakedLighting.lightmapOffsetU >>
                        face.bakedLighting.lightmapOffsetV;

                    input >> token >> std::ws;
                    if (token != "LIGHTMAP_TEXTURE")
                    {
                        m_LastError = "Expected LIGHTMAP_TEXTURE";
                        return false;
                    }
                    std::getline(input, face.bakedLighting.lightmapTexturePath);
                    face.bakedLighting.lightmapTexturePath = Unquote(face.bakedLighting.lightmapTexturePath);

                    input >> token >> std::ws;
                    if (token != "LIGHTMAP_SIDECAR")
                    {
                        m_LastError = "Expected LIGHTMAP_SIDECAR";
                        return false;
                    }
                    std::getline(input, face.bakedLighting.lightmapSidecarPath);
                    face.bakedLighting.lightmapSidecarPath = Unquote(face.bakedLighting.lightmapSidecarPath);

                    input >> token >> std::ws;
                    if (token != "BAKE_SIGNATURE")
                    {
                        m_LastError = "Expected BAKE_SIGNATURE";
                        return false;
                    }
                    std::getline(input, face.bakedLighting.bakeSignature);
                    face.bakedLighting.bakeSignature = Unquote(face.bakedLighting.bakeSignature);
                }

                brush.faces.push_back(std::move(face));
            }

            input >> token;
            if (token != "END_BRUSH")
            {
                m_LastError = "Expected END_BRUSH";
                return false;
            }

            asset.brushes.push_back(std::move(brush));
        }
    }

    if (asset.nextBrushId == 0)
        asset.nextBrushId = 1;
    for (const MapBrush& brush : asset.brushes)
    {
        if (brush.brushId >= asset.nextBrushId)
            asset.nextBrushId = brush.brushId + 1;
    }

    m_LastError.clear();
    return true;
}

} // namespace Dot
