#include "DeltaCodec.h"

#include <xdelta3.h>

#include <algorithm>
#include <cerrno>
#include <limits>
#include <stdexcept>

//检查传给 xdelta 的只读字节视图
//输入：view：字节地址和长度，调用期间必须有效。
//输出：无返回值；非零长度空地址或长度超出 xdelta 类型时抛异常。
static auto validateView(const ChunkView view) -> void {
    if (view.length > 0 && view.data == nullptr) {
        throw std::invalid_argument("null delta codec input with nonzero length");
    }
    if (view.length > std::numeric_limits<usize_t>::max()) {
        throw std::length_error("chunk exceeds xdelta window type");
    }
}

//以基块 B 为来源，将目标 A 编码成真正的 VCDIFF
//输入：base：B 的只读视图；target：A 的只读视图，均在调用期间有效。
//输出：返回拥有 Delta 字节的 string；固定 level6、Adler32、无二级压缩，非法输入或编码失败抛异常。
auto encodeDelta(const ChunkView base, const ChunkView target) -> std::string {
    validateView(base);
    validateView(target);
    std::string output;
    if (target.length > output.max_size() - 256) {
        throw std::length_error("delta encode output too large");
    }
    std::size_t capacity = std::max<std::size_t>(256, target.length + 256);
    const std::uint8_t empty = 0;
    const auto *targetData = target.length == 0 ? &empty : target.data;
    const auto *baseData = base.length == 0 ? nullptr : base.data;
    // 不指定任何 XD3_SEC_TYPE：无二级压缩。SEC_NOALL 只能配合已选 compressor 使用。
    constexpr int FLAGS = XD3_COMPLEVEL_6 | XD3_ADLER32;
    for (;;) {
        output.resize(capacity);
        usize_t produced = 0;
        const int result = xd3_encode_memory(targetData, target.length, baseData, base.length,
            reinterpret_cast<std::uint8_t *>(output.data()), &produced, output.size(), FLAGS);
        if (result == 0) {
            if (produced > output.size()) {
                throw std::runtime_error("xdelta encoded beyond output buffer");
            }
            output.resize(static_cast<std::size_t>(produced));
            return output;
        }
        if (result != ENOSPC) {
            throw std::runtime_error("xdelta encode failed, code=" + std::to_string(result));
        }
        if (capacity > output.max_size() / 2) {
            throw std::length_error("xdelta encode output capacity overflow");
        }
        capacity *= 2;
    }
}

//使用基块 B 和 VCDIFF 恢复目标 A
//输入：base：B 的只读视图；delta：VCDIFF 字节；expectedLength：必须恢复出的长度。
//输出：返回拥有恢复字节的 string；长度不符、Adler32 校验或解码失败时抛异常，调用者另核对目标内容。
auto decodeDelta(const ChunkView base, const std::string_view delta, const std::size_t expectedLength) -> std::string {
    validateView(base);
    if (delta.empty()) {
        throw std::invalid_argument("empty VCDIFF stream");
    }
    if (delta.size() > std::numeric_limits<usize_t>::max()
        || expectedLength > std::numeric_limits<usize_t>::max()) {
        throw std::length_error("delta decode size exceeds xdelta type");
    }
    std::string output(expectedLength, '\0');
    std::uint8_t empty = 0;
    auto *outputData = expectedLength == 0 ? &empty : reinterpret_cast<std::uint8_t *>(output.data());
    const auto *baseData = base.length == 0 ? nullptr : base.data;
    usize_t produced = 0;
    const int result = xd3_decode_memory(reinterpret_cast<const std::uint8_t *>(delta.data()), delta.size(),
        baseData, base.length, outputData, &produced, expectedLength, 0);
    if (result != 0) {
        throw std::runtime_error("xdelta decode failed, code=" + std::to_string(result));
    }
    if (produced != expectedLength) {
        throw std::runtime_error("xdelta decoded length differs from original length");
    }
    return output;
}
