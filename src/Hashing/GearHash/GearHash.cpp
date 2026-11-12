//
// Created by user on 2026/9/30.
//

#include "GearHash.h"

// 将 length 个字节依次滚入 hash；hash 由调用者持有，便于跨调用累加
//将一段字节依次滚入 Gear 指纹
//输入：data/length：字节地址和长度，非空输入须可读；hash：调用者持有的初始状态。
//输出：返回最终指纹，并通过 hash 引用回写；长度为 0 时保持原状态。
auto updateGearHash(const std::uint8_t *data, const std::size_t length, std::uint64_t &hash) -> std::uint64_t {
    for (std::size_t i = 0; i < length; ++i) {
        hash = gearRoll(hash, data[i]);
    }
    return hash;
}

