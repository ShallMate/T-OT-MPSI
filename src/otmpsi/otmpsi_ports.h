#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

std::vector<std::string> ProtocolSocketTags(const OtmpsiParams &params);
bool CanBindTcpPort(uint16_t port);
uint16_t FindAvailableProtocolPort(uint16_t start, std::vector<uint8_t> &used);
void GeneratePortMapFiles(const std::string &dir, const OtmpsiParams &params);

} // namespace examples::otmpsi
