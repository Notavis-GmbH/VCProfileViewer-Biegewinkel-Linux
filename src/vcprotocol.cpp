/****************************************************************************
** vcprotocol.cpp
** TCP/IP Protocol implementation for VC nano 3D Z Laser Scanner
**
** NOTICE: This file is intentionally left as a stub in the public
** repository. The full implementation is maintained in the internal
** repository VCProfileViewer-Protocol-Internal.
**
** The VC nano 3D Z communication protocol is proprietary to
** Vision Components GmbH (© Vision Components, All rights reserved,
** confidential to Vision Components).
**
** To build with live sensor support, copy the full vcprotocol.cpp from
** the internal repository. Contact: info@notavis.com
****************************************************************************/

#include "vcprotocol.h"
#include <stdexcept>

VcProtocol::VcProtocol()
    : m_sock(VC_INVALID_SOCKET), m_connected(false),
      m_globalCounter(0), m_metaCounter(0) {}

VcProtocol::~VcProtocol() { disconnect(); }

bool VcProtocol::connect(const std::string&, uint16_t) {
    throw std::runtime_error(
        "VcProtocol: full implementation not included in open-source build. "
        "See VCProfileViewer-Protocol-Internal repository.");
}
void VcProtocol::disconnect() { m_connected = false; }
bool VcProtocol::sendCommand(int, const std::string&, uint32_t) { return false; }
bool VcProtocol::readStringResponse(int, uint32_t, std::string&, int) { return false; }
bool VcProtocol::readDataFrame(int&, int&, uint64_t&, std::vector<uint8_t>&, int) { return false; }
bool VcProtocol::recvAll(void*, int, int) { return false; }
bool VcProtocol::findSync(int) { return false; }
bool VcProtocol::recvGlobalHeader(VcGlobalHeader&, int) { return false; }
