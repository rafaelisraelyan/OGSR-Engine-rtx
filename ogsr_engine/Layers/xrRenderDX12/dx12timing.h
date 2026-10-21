#pragma once

#include <cstdint>

namespace dx12timing12
{
enum class ConsumeResult { Empty, MapFailed, Consumed };

// Внутренний helper: caller уже подтвердил slot fence либо общий drain.
// Снимаем привязку ДО Map: отказ readback не должен переносить индекс
// старого CPU-сэмпла на следующий кадр при исчерпании массива метрик.
template<class Frame, class ReadTicks, class Publish>
ConsumeResult Consume(Frame& frame, uint64_t frequency, ReadTicks read, Publish publish)
{
    const bool pending = frame.timestampPending;
    const bool sample = frame.timestampSample;
    const int32_t index = frame.timestampSampleIndex;
    frame.timestampPending = false;
    frame.timestampSample = false;
    frame.timestampSampleIndex = -1;
    if (!pending || !frequency)
        return ConsumeResult::Empty;

    uint64_t ticks[2]{};
    if (!read(ticks))
        return ConsumeResult::MapFailed;
    if (sample && ticks[1] >= ticks[0])
        publish(index, double(ticks[1] - ticks[0]) * 1000.0 / double(frequency));
    return ConsumeResult::Consumed;
}
}
