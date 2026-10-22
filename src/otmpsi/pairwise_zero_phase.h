#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

volePSI::Matrix<volePSI::u8> MakeGmwInput(const std::vector<uint128_t> &shares,
                                          size_t bit_width = 128);
size_t PeqtBitWidth(const OtmpsiParams &params);
size_t PeqtTripleBatchSize(const OtmpsiParams &params);
std::vector<uint8_t>
DecodeBitShareOutput(const volePSI::Matrix<volePSI::u8> &out, size_t row_count);
ShareWithStats PairwiseZeroSide(const std::vector<uint128_t> &shares,
                                size_t party_idx, int rank,
                                const OtmpsiParams &params,
                                const std::string &tag);

} // namespace examples::otmpsi
