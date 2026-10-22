#include "stdafx.h"
#include "dx12xrayasset_tests.h"
#include "dx12xrayasset.h"
#include "dx12camera.h"
#include "dx12renderprep.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace smokexray12
{
namespace
{
struct TestState
{
    unsigned checks = 0;
    unsigned failures = 0;

    void Expect(bool condition, const char* name)
    {
        ++checks;
        if (!condition)
        {
            ++failures;
            std::printf("[xray] FAIL: %s\n", name);
        }
    }
};

void AppendU16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void AppendU32(std::vector<uint8_t>& out, uint32_t value)
{
    for (uint32_t i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

void AppendF32(std::vector<uint8_t>& out, float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU32(out, bits);
}

void AppendChunk(std::vector<uint8_t>& file, uint32_t id,
                 const std::vector<uint8_t>& data)
{
    AppendU32(file, id);
    AppendU32(file, static_cast<uint32_t>(data.size()));
    file.insert(file.end(), data.begin(), data.end());
}

std::vector<uint8_t> MakeValidOgf()
{
    std::vector<uint8_t> file;
    std::vector<uint8_t> header;
    header.push_back(4); // format version
    header.push_back(0); // MT_NORMAL
    AppendU16(header, 0); // shader id
    for (int i = 0; i < 3; ++i) AppendF32(header, 0.0f);
    for (int i = 0; i < 3; ++i) AppendF32(header, 1.0f);
    for (int i = 0; i < 3; ++i) AppendF32(header, 0.5f);
    AppendF32(header, 0.75f);
    AppendChunk(file, 1, header);

    std::vector<uint8_t> texture;
    const char textureName[] = "test\\diffuse";
    const char shaderName[] = "default";
    texture.insert(texture.end(), textureName, textureName + sizeof(textureName));
    texture.insert(texture.end(), shaderName, shaderName + sizeof(shaderName));
    AppendChunk(file, 2, texture);

    std::vector<uint8_t> vertices;
    AppendU32(vertices, 0x112);
    AppendU32(vertices, 3);
    const float positions[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const float uvs[3][2] = {{0, 0}, {1, 0}, {0, 1}};
    for (int i = 0; i < 3; ++i)
    {
        for (int c = 0; c < 3; ++c) AppendF32(vertices, positions[i][c]);
        AppendF32(vertices, 0.0f);
        AppendF32(vertices, 0.0f);
        AppendF32(vertices, 1.0f);
        AppendF32(vertices, uvs[i][0]);
        AppendF32(vertices, uvs[i][1]);
    }
    AppendChunk(file, 3, vertices);

    std::vector<uint8_t> indices;
    AppendU32(indices, 3);
    AppendU16(indices, 0);
    AppendU16(indices, 1);
    AppendU16(indices, 2);
    AppendChunk(file, 4, indices);
    return file;
}

std::vector<uint8_t> MakeValidDds()
{
    std::vector<uint8_t> file(128, 0);
    std::memcpy(file.data(), "DDS ", 4);
    const auto put32 = [&](size_t offset, uint32_t value)
    {
        std::memcpy(file.data() + offset, &value, 4);
    };
    put32(4, 124);
    put32(8, 0x1007);
    put32(12, 4);
    put32(16, 4);
    put32(20, 8);
    put32(28, 1);
    put32(76, 32);
    put32(80, 0x4); // DDPF_FOURCC
    std::memcpy(file.data() + 84, "DXT1", 4);
    put32(108, 0x1000); // DDSCAPS_TEXTURE
    file.resize(136, 0);
    return file;
}

bool WriteTemp(const std::wstring& name, const std::vector<uint8_t>& bytes,
               std::wstring& path)
{
    wchar_t directory[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, directory))
        return false;
    path = std::wstring(directory) + name;
    std::ofstream file(std::filesystem::path(path),
                       std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

renderprep12::Object12 MakeAssetObject()
{
    renderprep12::Object12 object{};
    object.id = 1;
    object.firstIndex = 0;
    object.indexCount = 3;
    object.visual = kXrayVisual12;
    object.material = kXrayMaterial12;
    object.pso = kXrayPso12;
    object.minimum = {0, 0, 0};
    object.maximum = {1, 1, 0};
    object.sphere.center = {0.5f, 0.5f, 0.0f};
    object.sphere.radius = 0.75f;
    DirectX::XMStoreFloat4x4(&object.world, DirectX::XMMatrixIdentity());
    return object;
}

renderprep12::OrderedFrame12 MakeOrderedFrame()
{
    renderprep12::OrderedFrame12 frame{};
    frame.frame.generation = 42;
    frame.frame.packets.resize(1);
    frame.frame.packets[0].object = MakeAssetObject();
    frame.frame.packets[0].visible = true;
    frame.categories[0] = {0, 1};
    return frame;
}

void TestSyntheticLoader(TestState& test)
{
    std::wstring ogfPath;
    std::wstring ddsPath;
    test.Expect(WriteTemp(L"f36a_valid.ogf", MakeValidOgf(), ogfPath),
                "synthetic OGF written");
    test.Expect(WriteTemp(L"f36a_valid.dds", MakeValidDds(), ddsPath),
                "synthetic DDS written");

    XrayAssetData12 asset;
    const char* reason = nullptr;
    test.Expect(LoadXrayOgf12(ogfPath, asset, &reason),
                "synthetic OGF accepted");
    test.Expect(asset.vertices.size() == 3 && asset.indices.size() == 3,
                "synthetic OGF counts");
    test.Expect(asset.textureName == "test\\diffuse",
                "synthetic OGF texture name");
    test.Expect(asset.shaderName == "default",
                "synthetic OGF shader name");

    XrayTextureData12 texture;
    test.Expect(LoadXrayDds12(ddsPath, texture, &reason),
                "synthetic DDS accepted");
    test.Expect(texture.width == 4 && texture.height == 4 &&
                texture.mipCount == 1 &&
                texture.format == DXGI_FORMAT_BC1_UNORM,
                "synthetic DDS metadata");

    auto invalid = MakeValidOgf();
    invalid[8] = 3; // unsupported OGF version
    std::wstring invalidPath;
    test.Expect(WriteTemp(L"f36a_bad_version.ogf", invalid, invalidPath),
                "bad version fixture written");
    test.Expect(!LoadXrayOgf12(invalidPath, asset, &reason),
                "bad OGF version rejected");

    invalid = MakeValidOgf();
    invalid[9] = 10; // MT_SKELETON_RIGID
    test.Expect(WriteTemp(L"f36a_bad_type.ogf", invalid, invalidPath),
                "bad type fixture written");
    test.Expect(!LoadXrayOgf12(invalidPath, asset, &reason),
                "non-MT_NORMAL OGF rejected");

    invalid = MakeValidOgf();
    // Mutate the first index to 3, which is outside the 3-vertex range.
    invalid[invalid.size() - 6] = 3;
    test.Expect(WriteTemp(L"f36a_bad_index.ogf", invalid, invalidPath),
                "bad index fixture written");
    test.Expect(!LoadXrayOgf12(invalidPath, asset, &reason),
                "out-of-range OGF index rejected");

    invalid = MakeValidOgf();
    invalid.resize(invalid.size() - 5);
    test.Expect(WriteTemp(L"f36a_truncated.ogf", invalid, invalidPath),
                "truncated fixture written");
    test.Expect(!LoadXrayOgf12(invalidPath, asset, &reason),
                "truncated OGF rejected");

    auto badDds = MakeValidDds();
    std::memcpy(badDds.data() + 84, "DXT5", 4);
    test.Expect(WriteTemp(L"f36a_bad.dds", badDds, ddsPath),
                "bad DDS fixture written");
    test.Expect(!LoadXrayDds12(ddsPath, texture, &reason),
                "non-BC1 DDS rejected");

    badDds = MakeValidDds();
    std::memcpy(badDds.data() + 84, "DX10", 4);
    test.Expect(WriteTemp(L"f36a_dx10.dds", badDds, ddsPath),
                "DX10 DDS fixture written");
    test.Expect(!LoadXrayDds12(ddsPath, texture, &reason),
                "DX10 extended DDS rejected");

    badDds = MakeValidDds();
    const auto put32 = [&](std::vector<uint8_t>& bytes, size_t offset,
                           uint32_t value)
    {
        std::memcpy(bytes.data() + offset, &value, 4);
    };
    put32(badDds, 8, 0); // required header flags removed
    test.Expect(WriteTemp(L"f36a_bad_flags.dds", badDds, ddsPath),
                "bad DDS flags fixture written");
    test.Expect(!LoadXrayDds12(ddsPath, texture, &reason),
                "DDS without required flags rejected");

    badDds = MakeValidDds();
    put32(badDds, 112, 0x200); // DDSCAPS2_CUBEMAP
    test.Expect(WriteTemp(L"f36a_cube.dds", badDds, ddsPath),
                "cube DDS fixture written");
    test.Expect(!LoadXrayDds12(ddsPath, texture, &reason),
                "cube DDS rejected");

    badDds = MakeValidDds();
    put32(badDds, 112, 0x400); // DDSCAPS2_CUBEMAP_POSITIVEX
    test.Expect(WriteTemp(L"f36a_cube_face_x.dds", badDds, ddsPath),
                "single cube face fixture written");
    test.Expect(!LoadXrayDds12(ddsPath, texture, &reason),
                "single cube face flag rejected");

    badDds = MakeValidDds();
    put32(badDds, 112, 0x800); // DDSCAPS2_CUBEMAP_NEGATIVEX
    test.Expect(WriteTemp(L"f36a_cube_face_negx.dds", badDds, ddsPath),
                "second cube face fixture written");
    test.Expect(!LoadXrayDds12(ddsPath, texture, &reason),
                "second cube face flag rejected");

    badDds = MakeValidDds();
    put32(badDds, 112, 0xFE00); // full cubemap mask, including faces
    test.Expect(WriteTemp(L"f36a_cube_full.dds", badDds, ddsPath),
                "full cubemap fixture written");
    test.Expect(!LoadXrayDds12(ddsPath, texture, &reason),
                "full cubemap mask rejected");

    badDds = MakeValidDds();
    put32(badDds, 112, 0x200000); // DDSCAPS2_VOLUME
    test.Expect(WriteTemp(L"f36a_volume.dds", badDds, ddsPath),
                "volume DDS fixture written");
    test.Expect(!LoadXrayDds12(ddsPath, texture, &reason),
                "volume DDS rejected");

    badDds = MakeValidDds();
    put32(badDds, 8, 0x21007); // DDSD_CAPS|HEIGHT|WIDTH|PIXELFORMAT|MIPMAPCOUNT
    put32(badDds, 28, 4); // 4x4 supports at most 3 mips
    test.Expect(WriteTemp(L"f36a_too_many_mips.dds", badDds, ddsPath),
                "too-many-mips fixture written");
    const char* mipReason = nullptr;
    test.Expect(!LoadXrayDds12(ddsPath, texture, &mipReason),
                "excessive DDS mip count rejected");
    test.Expect(mipReason &&
                std::strstr(mipReason, "invalid DDS dimensions/mips"),
                "excessive DDS mip count rejected by mip limit");

    badDds = MakeValidDds();
    badDds.resize(128 + 4); // mip needs 8 bytes
    test.Expect(WriteTemp(L"f36a_truncated.dds", badDds, ddsPath),
                "truncated DDS fixture written");
    test.Expect(!LoadXrayDds12(ddsPath, texture, &reason),
                "truncated DDS mip chain rejected");

    std::wstring unicodePath;
    const std::wstring unicodeName =
        L"f36a_\u0442\u0435\u0441\u0442.ogf";
    const std::wstring unicodeMissing =
        L"f36a_\u0442\u0435\u0441\u0442_missing.ogf";
    test.Expect(WriteTemp(unicodeName, MakeValidOgf(), unicodePath),
                "Unicode OGF fixture written");
    test.Expect(LoadXrayOgf12(unicodePath, asset, &reason),
                "Unicode OGF path accepted");
    std::filesystem::remove(std::filesystem::path(unicodePath));
    test.Expect(!LoadXrayOgf12(unicodeMissing, asset, &reason),
                "missing Unicode OGF path rejected");

    std::wstring spacedPath;
    test.Expect(WriteTemp(L"f36a path with spaces.ogf", MakeValidOgf(), spacedPath),
                "space-path OGF fixture written");
    test.Expect(LoadXrayOgf12(spacedPath, asset, &reason),
                "space-containing OGF path accepted");
    std::filesystem::remove(std::filesystem::path(spacedPath));

    std::filesystem::remove(std::filesystem::path(ogfPath));
    std::filesystem::remove(std::filesystem::path(ddsPath));
    std::filesystem::remove(std::filesystem::path(invalidPath));
}

void TestTextureValidation(TestState& test)
{
    XrayTextureData12 texture;
    texture.width = 4;
    texture.height = 4;
    texture.mipCount = 1;
    texture.format = DXGI_FORMAT_BC1_UNORM;
    texture.data.resize(8);
    XrayTextureMip12 mip{};
    mip.width = 4;
    mip.height = 4;
    mip.rowPitch = 8;
    mip.dataSize = 8;
    mip.dataOffset = 0;
    texture.mips.push_back(mip);
    const char* reason = nullptr;
    test.Expect(ValidateXrayTextureSource(texture, &reason),
                "valid BC1 source metadata accepted");

    XrayTextureData12 bad = texture;
    bad.mips[0].dataOffset = 9;
    test.Expect(!ValidateXrayTextureSource(bad, &reason),
                "source offset beyond actual data rejected");
    bad = texture;
    bad.mips[0].dataSize = 9;
    test.Expect(!ValidateXrayTextureSource(bad, &reason),
                "source size beyond actual data rejected");
    bad = texture;
    bad.mips[0].dataOffset = UINT32_MAX;
    bad.mips[0].dataSize = UINT32_MAX;
    test.Expect(!ValidateXrayTextureSource(bad, &reason),
                "source range overflow rejected");
    bad = texture;
    bad.mips[0].rowPitch = 7;
    test.Expect(!ValidateXrayTextureSource(bad, &reason),
                "source row pitch mismatch rejected");
    bad = texture;
    bad.mips[0].width = 8;
    test.Expect(!ValidateXrayTextureSource(bad, &reason),
                "source mip dimensions mismatch rejected");
    bad = texture;
    bad.data.resize(7);
    test.Expect(!ValidateXrayTextureSource(bad, &reason),
                "truncated source buffer rejected");

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    layout.Offset = 0;
    layout.Footprint.Format = DXGI_FORMAT_BC1_UNORM;
    layout.Footprint.Width = 4;
    layout.Footprint.Height = 4;
    layout.Footprint.Depth = 1;
    layout.Footprint.RowPitch = 256;
    UINT rows = 1;
    UINT64 rowSize = 8;
    test.Expect(ValidateXrayTextureCopy(texture, &layout, &rows, &rowSize,
                                        1, 512, &reason),
                "valid D3D12 copy footprint accepted");
    UINT64 paddedRowSize = 16;
    test.Expect(!ValidateXrayTextureCopy(texture, &layout, &rows, &paddedRowSize,
                                         1, 512, &reason),
                "unpadded destination row size mismatch rejected");

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT badLayout = layout;
    badLayout.Offset = 512;
    test.Expect(!ValidateXrayTextureCopy(texture, &badLayout, &rows, &rowSize,
                                         1, 512, &reason),
                "destination offset beyond upload rejected");
    badLayout = layout;
    badLayout.Footprint.RowPitch = 128;
    test.Expect(!ValidateXrayTextureCopy(texture, &badLayout, &rows, &rowSize,
                                         1, 512, &reason),
                "unaligned destination row pitch rejected");
    badLayout = layout;
    badLayout.Footprint.Width = 8;
    test.Expect(!ValidateXrayTextureCopy(texture, &badLayout, &rows, &rowSize,
                                         1, 512, &reason),
                "destination dimensions mismatch rejected");
    UINT badRows = 0;
    test.Expect(!ValidateXrayTextureCopy(texture, &layout, &badRows, &rowSize,
                                         1, 512, &reason),
                "destination row count mismatch rejected");
    UINT64 badRowSize = 7;
    test.Expect(!ValidateXrayTextureCopy(texture, &layout, &rows, &badRowSize,
                                         1, 512, &reason),
                "destination row size too small rejected");
    test.Expect(!ValidateXrayTextureCopy(texture, &layout, &rows, &rowSize,
                                         1, 0, &reason),
                "empty upload allocation rejected");
}

void TestResolver(TestState& test)
{
    const renderprep12::OrderedFrame12 frame = MakeOrderedFrame();
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = 0x1000;
    vbv.SizeInBytes = 96;
    vbv.StrideInBytes = 32;
    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation = 0x2000;
    ibv.SizeInBytes = 6;
    ibv.Format = DXGI_FORMAT_R16_UINT;

    ResolvedFrame12 resolved;
    const char* reason = nullptr;
    test.Expect(ResolveXrayFrame(frame, 42, vbv, ibv, 3, resolved, &reason),
                "X-Ray frame resolves");
    test.Expect(resolved.draws.size() == 1 &&
                resolved.draws[0].pipeline == 1 &&
                resolved.draws[0].indexCount == 3,
                "X-Ray resolved pipeline/range");

    const ResolvedFrame12 baseline = resolved;
    renderprep12::OrderedFrame12 bad = frame;
    bad.frame.packets[0].object.material = renderprep12::SyntheticResources12[0];
    test.Expect(!ResolveXrayFrame(bad, 42, vbv, ibv, 3, resolved, &reason),
                "stale X-Ray material rejected");
    test.Expect(Equivalent(resolved, baseline),
                "failed X-Ray resolution preserves output");

    bad = frame;
    bad.frame.packets[0].object.visual.id = 0x9999;
    test.Expect(!ResolveXrayFrame(bad, 42, vbv, ibv, 3, resolved, &reason),
                "unknown X-Ray visual rejected");
    test.Expect(Equivalent(resolved, baseline),
                "failed X-Ray visual resolution preserves output");

    bad = frame;
    test.Expect(!ResolveXrayFrame(bad, 41, vbv, ibv, 3, resolved, &reason),
                "stale X-Ray generation rejected");
    test.Expect(Equivalent(resolved, baseline),
                "failed X-Ray generation resolution preserves output");

    bad = frame;
    bad.frame.packets[0].object.indexCount = 6;
    test.Expect(!ResolveXrayFrame(bad, 42, vbv, ibv, 3, resolved, &reason),
                "X-Ray index mismatch rejected");
    test.Expect(Equivalent(resolved, baseline),
                "failed X-Ray index resolution preserves output");
}

void TestPreparation(TestState& test)
{
    SmokeCamera12 camera;
    camera.SetAspect(16.0f / 9.0f);
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 projection;
    camera.CaptureViewProjectionPair(view, projection);
    const std::array<renderprep12::Object12, 1> objects{MakeAssetObject()};
    const renderprep12::RenderSceneSnapshot12 snapshot(
        view, projection, objects, 42);
    const std::array<renderprep12::Resource12, 3> extra{
        kXrayVisual12, kXrayMaterial12, kXrayPso12};
    renderprep12::OrderingTimings12 timing;
    bool threw = false;
    try
    {
        renderprep12::PrepareOrdered(snapshot, 42, renderprep12::Mode::Serial,
                                     nullptr, timing);
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    test.Expect(threw, "asset handles rejected without explicit allow-list");

    const auto ordered = renderprep12::PrepareOrdered(
        snapshot, 42, renderprep12::Mode::Serial, nullptr, timing,
        std::span<const renderprep12::Resource12>(extra));
    test.Expect(ordered.frame.packets.size() == 1,
                "asset handles accepted with allow-list");
}

void TestRealAsset(TestState& test, const wchar_t* ogfPath, const wchar_t* ddsPath)
{
    if (!ogfPath || !*ogfPath)
        return;
    XrayAssetData12 asset;
    const char* reason = nullptr;
    test.Expect(LoadXrayOgf12(std::wstring(ogfPath), asset, &reason),
                "real X-Ray OGF accepted");
    test.Expect(asset.vertices.size() == 1802 && asset.indices.size() == 3300,
                "real X-Ray OGF counts");
    test.Expect(asset.textureName == "ston\\ston_stena_marbl_m_03_back",
                "real X-Ray texture name");
    if (ddsPath && *ddsPath)
    {
        XrayTextureData12 texture;
        test.Expect(LoadXrayDds12(std::wstring(ddsPath), texture, &reason),
                    "real X-Ray DDS accepted");
        test.Expect(texture.width == 512 && texture.height == 512 &&
                    texture.mipCount == 10,
                    "real X-Ray DDS dimensions/mips");
    }
}
} // namespace

bool RunXrayAssetTests(const wchar_t* ogfPath, const wchar_t* ddsPath)
{
    TestState test;
    try
    {
        TestSyntheticLoader(test);
        TestTextureValidation(test);
        TestResolver(test);
        TestPreparation(test);
        TestRealAsset(test, ogfPath, ddsPath);
    }
    catch (const std::exception& error)
    {
        std::printf("[xray] FAIL: exception: %s\n", error.what());
        return false;
    }
    catch (...)
    {
        std::printf("[xray] FAIL: unknown exception\n");
        return false;
    }
    std::printf("[xray] %s checks=%u failures=%u\n",
                test.failures == 0 ? "PASS" : "FAIL",
                test.checks, test.failures);
    return test.failures == 0;
}
} // namespace smokexray12
