#pragma once

// F4.0-B1: CPU-only reader статической геометрии уровня X-Ray.
//
// Модуль намеренно не зависит от engine, R4, DX11, DX12, CHW12 и VFS.
// Входные байты передаёт вызывающий код; reader не владеет ими и не сохраняет
// указатели. Вся опубликованная память принадлежит output.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace xraylevel12
{
constexpr uint32_t kOgfVersion = 4;
constexpr uint32_t kOgfHeader = 1;
constexpr uint32_t kOgfGcContainer = 21;
constexpr uint32_t kOgfFastPath = 22;
constexpr uint32_t kMtNormal = 0;

constexpr uint32_t kLevelVisualsChunk = 3;
constexpr uint32_t kLevelVertexChunk = 9;
constexpr uint32_t kLevelIndexChunk = 10;

// Старший бит chunk id — CFS_CompressMark. B1 не умеет декомпрессию,
// поэтому сжатые chunk'и отклоняются до интерпретации payload.
constexpr uint32_t kCompressMark = 0x80000000u;

constexpr uint32_t kMaxDeclarationElements = 17; // MAXD3DDECLLENGTH + terminator объявления
constexpr uint32_t kD3dDeclTypeUnused = 17;
constexpr uint32_t kMaxVertexBuffers = 4096;
constexpr uint32_t kMaxIndexBuffers = 4096;
constexpr uint32_t kMaxVertexCount = 8u * 1024u * 1024u;
constexpr uint32_t kMaxIndexCount = 16u * 1024u * 1024u;

// Header AABB — только охватывающая граница исходника, а не требование
// побайтового равенства. Вычисленный AABB принимается, если он входит в
// header AABB с этим абсолютным допуском.
constexpr float kHeaderBoundsTolerance = 2.0e-3f;

// Allocation-free статус отказа. Reason остаётся best-effort текстом.
enum class ReadError : uint8_t
{
    None = 0,
    EmptyLevelInput,
    EmptyGeomInput,
    LevelInputTooLarge,
    GeomInputTooLarge,
    InputOutputAliasing,
    CompressedChunk,
    TruncatedChunkHeader,
    ChunkExceedsInput,
    DuplicateMandatoryChunk,
    MissingMandatoryChunk,
    TruncatedVisualChunkHeader,
    VisualChunkExceedsContainer,
    DuplicateVisualId,
    VisualNotFound,
    TruncatedVisualSubchunkHeader,
    VisualSubchunkExceedsVisual,
    UnsupportedOgfVersion,
    UnsupportedVisualType,
    InvalidOgfHeaderSize,
    TruncatedOgfHeader,
    TruncatedOgfGcContainer,
    NonFiniteOgfHeaderBounds,
    InvalidOgfHeaderBounds,
    DuplicateOgfHeader,
    MissingOgfHeader,
    DuplicateOgfGcContainer,
    MissingOgfGcContainer,
    InvalidOgfGcContainerSize,
    DuplicateOgfFastPath,
    UnsupportedOgfSubchunk,
    TruncatedVbContainer,
    InvalidVbContainerCount,
    TruncatedVbCount,
    InvalidVbCountOrStride,
    VbPayloadExceedsContainer,
    TrailingVbBytes,
    TruncatedIbContainer,
    InvalidIbContainerCount,
    TruncatedIbCount,
    InvalidIbCount,
    IbPayloadExceedsContainer,
    TrailingIbBytes,
    InvalidSourceVbId,
    InvalidSourceIbId,
    UnsupportedDeclaration,
    TruncatedDeclaration,
    InvalidDeclarationTerminator,
    InvalidDeclarationTerminatorFields,
    UnsupportedDeclarationElement,
    VertexElementExceedsStride,
    InvalidVisualCounts,
    VertexRangeOutsideVb,
    IndexRangeOutsideIb,
    OutputBudgetExceeded,
    VertexRangeOutsideGeom,
    IndexRangeOutsideGeom,
    TruncatedPosition,
    NonFinitePosition,
    ComputedBoundsOutsideHeader,
    InvalidComputedSphere,
    TruncatedIndex,
    IndexOutsideVertexRange,
    AllocationFailure,
    UnexpectedException
};

// Текст ошибки статический, не требует allocation и стабилен для диагностики.
const char* ReadErrorMessage(ReadError error) noexcept;

// Выбранный Jupiter visual использует ровно это объявление. Неизвестные
// форматы отклоняются, а не интерпретируются как другая раскладка.
struct DeclarationElement
{
    uint16_t stream = 0;
    uint16_t offset = 0;
    uint8_t type = 0;
    uint8_t method = 0;
    uint8_t usage = 0;
    uint8_t usage_index = 0;
};

struct Bounds
{
    float minimum[3] = {0.0f, 0.0f, 0.0f};
    float maximum[3] = {0.0f, 0.0f, 0.0f};
};

struct Sphere
{
    float center[3] = {0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
};

struct Limits
{
    uint64_t maxLevelBytes = 16ull * 1024ull * 1024ull;
    uint64_t maxGeomBytes = 128ull * 1024ull * 1024ull;
    uint64_t maxOutputBytes = 16ull * 1024ull * 1024ull;
};

struct Geometry
{
    // Происхождение visual. source_visual_offset — смещение payload
    // запрошенного внутреннего visual-чанка в переданном span level.
    uint32_t visual_id = 0;
    uint32_t shader_id = 0;
    uint64_t source_visual_offset = 0;
    uint32_t source_visual_size = 0;

    // Происхождение общих source VB/IB. Это не compact draw offsets.
    // Байтовые смещения абсолютны внутри переданного span level.geom.
    uint32_t source_vb_id = 0;
    uint32_t source_vbase = 0;
    uint32_t source_vcount = 0;
    uint32_t source_ib_id = 0;
    uint32_t source_ibase = 0;
    uint32_t source_icount = 0;
    uint64_t source_vertex_byte_offset = 0;
    uint64_t source_index_byte_offset = 0;

    // Compact draw contract. В B1 firstIndex и baseVertex всегда 0;
    // скопированные индексы остаются base-relative и не пересчитываются.
    uint32_t first_index = 0;
    uint32_t base_vertex = 0;
    uint32_t index_count = 0;
    uint32_t triangle_count = 0;

    uint32_t vertex_stride = 0;
    uint32_t vertex_count = 0;
    uint32_t declaration_count = 0; // включает канонический D3DDECL_END
    std::array<DeclarationElement, kMaxDeclarationElements> declaration{};

    // Сырые compact-строки (по vertex_stride байт) и uint16-индексы.
    // D3DCOLOR/SHORT2 не распаковываются, материалы и шейдеры не трогаются.
    std::vector<uint8_t> vertex_bytes;
    std::vector<uint16_t> indices;

    Bounds header_bounds{};
    Bounds bounds{};
    Sphere sphere{};

    bool empty() const noexcept { return vertex_bytes.empty() && indices.empty(); }
    void clear() noexcept;
};

// Читает один shared-container MT_NORMAL visual из переданных байтов level
// и level.geom.
//
// B1 не является runtime-интеграцией и не привязан к глобальному game generation.
// Будущий owner-side adapter обязан сам инвалидировать/пересоздавать Geometry
// при смене уровня; reader не читает и не хранит такой счётчик.
//
// Контракт:
// - предыдущий output очищается до разбора;
// - разбор транзакционный, output публикуется только после всех проверок;
// - при успехе возвращается ReadError::None, output непуст, reason пуст;
// - при отказе возвращается ненулевой ReadError, output пуст;
// - reason — best-effort текст; при OOM он может быть пустым;
// - input spans валидны только на время вызова и не сохраняются;
// - input spans не должны пересекаться с output/reason; такие пересечения
//   обнаруживаются до очистки и возвращают ReadError::InputOutputAliasing;
// - сжатые chunks (CFS_CompressMark) не поддерживаются и отклоняются;
// - GPU/engine/VFS объекты не создаются, не читаются и не возвращаются.
ReadError ReadVisual(std::span<const uint8_t> level_bytes,
                     std::span<const uint8_t> geom_bytes,
                     uint32_t visual_id,
                     Geometry& output,
                     std::string& reason,
                     const Limits& limits = Limits{}) noexcept;
} // namespace xraylevel12
