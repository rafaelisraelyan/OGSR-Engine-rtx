#pragma once

namespace j2a_r6_fault
{
enum class Point
{
    None,
    BeforePacketIdsReserve,
    AtPacketIdsInsert,
    BeforeCommit,
};

void MaybeThrow(Point point);
} // namespace j2a_r6_fault
