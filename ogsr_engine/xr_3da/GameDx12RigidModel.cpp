#include "stdafx.h"
#include "GameDx12RigidModel.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "fmesh.h"
#include "LocatorAPI_defs.h"
#include "bone.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>

namespace gameaux12
{
namespace
{
using Bytes = std::span<const uint8_t>;
constexpr size_t maxFileBytes = 32u * 1024u * 1024u;
constexpr size_t maxChildren = 64;
constexpr uint32_t maxIndices = 1024u * 1024u;
constexpr uint32_t maxWindows = 4096;
constexpr size_t maxStringLength = 255;

static_assert(sizeof(ogf_header) == 44);
static_assert(sizeof(Fobb) == 60);
static_assert(sizeof(vertBoned1W) == 60);
static_assert(sizeof(FSlideWindow) == 8);

bool Fail(std::string& reason, const char* message)
{
    reason = message;
    return false;
}

template <typename T>
bool Read(Bytes& bytes, T& value)
{
    if (bytes.size() < sizeof(value))
        return false;
    std::memcpy(&value, bytes.data(), sizeof(value));
    bytes = bytes.subspan(sizeof(value));
    return true;
}

bool ReadString(Bytes& bytes, std::string_view& value)
{
    if (bytes.empty())
        return false;
    const auto* first = reinterpret_cast<const char*>(bytes.data());
    const auto* last = static_cast<const char*>(std::memchr(
        first, '\0', (std::min)(bytes.size(), maxStringLength + 1)));
    if (last == nullptr)
        return false;
    value = std::string_view(first, static_cast<size_t>(last - first));
    bytes = bytes.subspan(value.size() + 1);
    return true;
}

template <size_t Size>
bool ReadChunks(Bytes bytes, std::array<Bytes, Size>& chunks, uint32_t required,
                std::string& reason)
{
    const bool indexed = required == 0;
    while (!bytes.empty())
    {
        uint32_t id = 0;
        uint32_t size = 0;
        if (!Read(bytes, id) || !Read(bytes, size) || size > bytes.size())
            return Fail(reason, "ogf_chunk_bounds");
        const bool compressed = (id & CFS_CompressMark) != 0;
        id &= ~static_cast<uint32_t>(CFS_CompressMark);
        if (id >= chunks.size())
        {
            if (indexed)
                return Fail(reason, "ogf_child_budget");
        }
        else if (indexed || (required & (1u << id)) != 0)
        {
            if (chunks[id].data() != nullptr)
                return Fail(reason, "ogf_duplicate_chunk");
            if (compressed)
                return Fail(reason, "ogf_compressed_chunk");
            chunks[id] = bytes.first(size);
        }
        bytes = bytes.subspan(size);
    }
    return true;
}

char LowerAscii(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool Finite(float value)
{
    return (std::bit_cast<uint32_t>(value) & 0x7F800000u) != 0x7F800000u;
}

bool Finite(const Fvector& vector)
{
    return Finite(vector.x) && Finite(vector.y) && Finite(vector.z);
}

bool ReadHeader(Bytes bytes, ogf_header& header, std::string& reason)
{
    if (!Read(bytes, header) || !bytes.empty() || header.format_version != xrOGF_FormatVersion)
        return Fail(reason, "ogf_header");
    if (!Finite(header.bb.min) || !Finite(header.bb.max) ||
        header.bb.min.x > header.bb.max.x || header.bb.min.y > header.bb.max.y ||
        header.bb.min.z > header.bb.max.z ||
        !Finite(header.bs.c) || !Finite(header.bs.r) || header.bs.r < 0.f)
        return Fail(reason, "ogf_bounds");
    return true;
}

bool ReadBoneCount(Bytes bytes, uint32_t& count, std::string& reason)
{
    if (!Read(bytes, count) || count == 0 || count > kMaxBones)
        return Fail(reason, "ogf_bone_count");
    std::array<std::string, kMaxBones> names;
    std::array<std::string, kMaxBones> parents;
    size_t roots = 0;
    for (uint32_t index = 0; index < count; ++index)
    {
        std::string_view name;
        std::string_view parent;
        Fobb box;
        if (!ReadString(bytes, name) || name.empty() ||
            !ReadString(bytes, parent) || !Read(bytes, box))
            return Fail(reason, "ogf_bone_record");
        names[index].resize(name.size());
        std::transform(name.begin(), name.end(), names[index].begin(), LowerAscii);
        parents[index].resize(parent.size());
        std::transform(parent.begin(), parent.end(), parents[index].begin(), LowerAscii);
        if (std::find(names.begin(), names.begin() + index, names[index]) != names.begin() + index)
            return Fail(reason, "ogf_duplicate_bone_name");
        if (parent.empty())
            ++roots;
    }
    if (roots != 1 || !bytes.empty())
        return Fail(reason, "ogf_bone_hierarchy");
    std::array<uint32_t, kMaxBones> parentIndices{};
    for (uint32_t index = 0; index < count; ++index)
    {
        if (parents[index].empty())
        {
            parentIndices[index] = count;
            continue;
        }
        const auto parent = std::find(names.begin(), names.begin() + count, parents[index]);
        if (parent == names.begin() + count)
            return Fail(reason, "ogf_bone_hierarchy");
        parentIndices[index] = static_cast<uint32_t>(parent - names.begin());
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        uint32_t current = index;
        for (uint32_t depth = 0; depth < count && current != count; ++depth)
            current = parentIndices[current];
        if (current != count)
            return Fail(reason, "ogf_bone_hierarchy");
    }
    return true;
}

std::string NormalizePath(std::string_view source)
{
    std::string result;
    if (source.empty() || source.size() > maxStringLength ||
        source.find("..") != std::string_view::npos)
        return result;
    if (source.front() == '/' || source.front() == '\\')
        return result;
    result.reserve(source.size());
    for (char value : source)
    {
        value = LowerAscii(value);
        if (value == '/')
            value = '\\';
        if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
              value == '_' || value == '-' || value == '.' || value == '\\'))
            return {};
        result.push_back(value);
    }
    if (result.back() == '\\')
        return {};
    return result;
}

bool ReadMaterial(Bytes bytes, Mesh& mesh, std::string& reason)
{
    std::string_view textures;
    std::string_view shader;
    if (!ReadString(bytes, textures) || !ReadString(bytes, shader) ||
        !bytes.empty() || shader.empty())
        return Fail(reason, "ogf_material");
    mesh.texture = NormalizePath(textures.substr(0, textures.find(',')));
    if (mesh.texture.ends_with(".dds"))
        mesh.texture.resize(mesh.texture.size() - 4);
    if (mesh.texture.empty() || mesh.texture.size() > 180)
        return Fail(reason, "ogf_material_texture");
    std::string normalizedShader(shader);
    std::transform(shader.begin(), shader.end(), normalizedShader.begin(), LowerAscii);
    mesh.cutout = normalizedShader.find("aref") != std::string::npos;
    return true;
}

bool ReadVertices(Bytes bytes, uint32_t boneCount, Mesh& mesh, std::string& reason)
{
    uint32_t format = 0;
    uint32_t count = 0;
    if (!Read(bytes, format) || !Read(bytes, count))
        return Fail(reason, "ogf_vertex_header");
    if (format == 2 || format == 3 || format == 4 ||
        format == OGF_VERTEXFORMAT_FVF_2L || format == OGF_VERTEXFORMAT_FVF_3L ||
        format == OGF_VERTEXFORMAT_FVF_4L)
        return Fail(reason, "ogf_weighted_vertices");
    if (format != OGF_VERTEXFORMAT_FVF_1L && format != 1)
        return Fail(reason, "ogf_vertex_format");
    if (count == 0 || count > 65536 || bytes.size() != sizeof(vertBoned1W) * count)
        return Fail(reason, "ogf_vertex_bounds");
    mesh.vertices.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        vertBoned1W vertex;
        if (!Read(bytes, vertex) || vertex.matrix >= boneCount)
            return Fail(reason, "ogf_vertex_bone");
        if (!Finite(vertex.P) || !Finite(vertex.N) ||
            !Finite(vertex.u) || !Finite(vertex.v))
            return Fail(reason, "ogf_vertex_values");
        mesh.vertices.push_back({
            {vertex.P.x, vertex.P.y, vertex.P.z},
            {vertex.N.x, vertex.N.y, vertex.N.z},
            {vertex.u, vertex.v}, vertex.matrix});
    }
    return true;
}

bool ReadIndices(Bytes bytes, Mesh& mesh, std::string& reason)
{
    uint32_t count = 0;
    if (!Read(bytes, count) || count == 0 || count > maxIndices ||
        count % 3 != 0 || bytes.size() != count * sizeof(uint16_t))
        return Fail(reason, "ogf_index_bounds");
    mesh.indices.resize(count);
    std::memcpy(mesh.indices.data(), bytes.data(), bytes.size());
    if (std::any_of(mesh.indices.begin(), mesh.indices.end(),
                    [&mesh](uint16_t index) { return index >= mesh.vertices.size(); }))
        return Fail(reason, "ogf_index_vertex");
    return true;
}

bool SelectHighestLod(Bytes bytes, Mesh& mesh, std::string& reason)
{
    constexpr size_t reservedSize = 4 * sizeof(uint32_t);
    if (bytes.size() < reservedSize)
        return Fail(reason, "ogf_slide_windows");
    bytes = bytes.subspan(reservedSize);
    uint32_t count = 0;
    if (!Read(bytes, count) || count == 0 || count > maxWindows ||
        bytes.size() != count * sizeof(FSlideWindow))
        return Fail(reason, "ogf_slide_windows");
    FSlideWindow selected{};
    for (uint32_t index = 0; index < count; ++index)
    {
        FSlideWindow window{};
        if (!Read(bytes, window) || window.offset > mesh.indices.size() ||
            static_cast<size_t>(window.num_tris) * 3 > mesh.indices.size() - window.offset ||
            window.num_verts > mesh.vertices.size())
            return Fail(reason, "ogf_slide_window_bounds");
        if (index == 0)
            selected = window;
    }
    const size_t size = static_cast<size_t>(selected.num_tris) * 3;
    if (size == 0 || selected.num_verts == 0)
        return Fail(reason, "ogf_empty_slide_window");
    const auto first = mesh.indices.begin() + selected.offset;
    const auto last = first + size;
    if (std::any_of(first, last, [&selected](uint16_t index) { return index >= selected.num_verts; }))
        return Fail(reason, "ogf_slide_window_index");
    mesh.indices.erase(last, mesh.indices.end());
    mesh.indices.erase(mesh.indices.begin(), first);
    mesh.vertices.resize(selected.num_verts);
    return true;
}

using OgfChunks = std::array<Bytes, OGF_S_MOTION_REFS2 + 1>;
constexpr uint32_t skeletonChunks = (1u << OGF_HEADER) | (1u << OGF_CHILDREN) |
                                    (1u << OGF_CHILDREN_L) | (1u << OGF_S_BONE_NAMES);
constexpr uint32_t meshChunks = (1u << OGF_HEADER) | (1u << OGF_TEXTURE) |
                                (1u << OGF_VERTICES) | (1u << OGF_INDICES) |
                                (1u << OGF_SWIDATA) | (1u << OGF_ICONTAINER) |
                                (1u << OGF_GCONTAINER);
}

bool ParseRigidModel(Bytes source, std::vector<Mesh>& meshes, std::string& reason)
{
    meshes.clear();
    reason.clear();
    if (source.empty() || source.size() > maxFileBytes)
        return Fail(reason, "ogf_file_budget");
    OgfChunks root{};
    ogf_header header{};
    if (!ReadChunks(source, root, skeletonChunks, reason) ||
        !ReadHeader(root[OGF_HEADER], header, reason))
        return false;
    if (header.type != MT_SKELETON_RIGID)
        return Fail(reason, "ogf_root_type");
    if (root[OGF_CHILDREN_L].data() != nullptr)
        return Fail(reason, "ogf_linked_children");
    uint32_t boneCount = 0;
    if (!ReadBoneCount(root[OGF_S_BONE_NAMES], boneCount, reason))
        return false;
    std::array<Bytes, maxChildren> children{};
    if (!ReadChunks(root[OGF_CHILDREN], children, 0, reason))
        return false;

    std::vector<Mesh> parsed;
    bool gap = false;
    for (Bytes child : children)
    {
        if (child.data() == nullptr)
        {
            gap = true;
            continue;
        }
        if (gap)
            return Fail(reason, "ogf_child_sequence");
        OgfChunks chunks{};
        if (!ReadChunks(child, chunks, meshChunks, reason) ||
            !ReadHeader(chunks[OGF_HEADER], header, reason))
            return false;
        if (header.type != MT_SKELETON_GEOMDEF_ST && header.type != MT_SKELETON_GEOMDEF_PM)
            return Fail(reason, "ogf_child_type");
        if (chunks[OGF_GCONTAINER].data() != nullptr || chunks[OGF_ICONTAINER].data() != nullptr)
            return Fail(reason, "ogf_shared_geometry");
        Mesh mesh;
        if (!ReadMaterial(chunks[OGF_TEXTURE], mesh, reason) ||
            !ReadVertices(chunks[OGF_VERTICES], boneCount, mesh, reason) ||
            !ReadIndices(chunks[OGF_INDICES], mesh, reason))
            return false;
        if (header.type == MT_SKELETON_GEOMDEF_PM && !SelectHighestLod(chunks[OGF_SWIDATA], mesh, reason))
            return false;
        parsed.push_back(std::move(mesh));
    }
    if (parsed.empty())
        return Fail(reason, "ogf_no_meshes");
    meshes = std::move(parsed);
    return true;
}

bool ReadRigidModel(const char* visualName, std::vector<Mesh>& meshes, std::string& reason)
{
    meshes.clear();
    reason.clear();
    if (!Device.OnMainThread())
        return Fail(reason, "ogf_wrong_thread");
    if (visualName == nullptr)
        return Fail(reason, "ogf_visual_name");
    try
    {
        std::string name = NormalizePath(visualName);
        if (name.empty())
            return Fail(reason, "ogf_visual_name");
        // CModelPool приводит имя к нижнему регистру и удаляет расширение.
        const auto extension = name.find_last_of('.');
        if (extension != std::string::npos)
            name.resize(extension);
        if (name.empty())
            return Fail(reason, "ogf_visual_name");
        name += ".ogf";
        string_path path;
        if (!FS.exist(path, fsgame::level, name.c_str()) &&
            !FS.exist(path, fsgame::game_meshes, name.c_str()))
            return Fail(reason, "ogf_missing");
        const auto close = [](IReader* reader) noexcept
        {
            try
            {
                FS.r_close(reader);
            }
            catch (...)
            {
            }
        };
        const std::unique_ptr<IReader, decltype(close)> reader(FS.r_open(path), close);
        if (reader == nullptr)
            return Fail(reason, "ogf_vfs_open");
        return ParseRigidModel({static_cast<const uint8_t*>(reader->begin()), reader->length()},
                               meshes, reason);
    }
    catch (const std::bad_alloc&)
    {
        return Fail(reason, "ogf_memory");
    }
    catch (...)
    {
        return Fail(reason, "ogf_vfs_exception");
    }
}
}

#endif
