/****************************************************************************
** vcprotocol.h
** TCP/IP Protocol for VC nano 3D Z Laser Scanner
**
** NOTICE: The full protocol implementation is maintained in a separate
** internal repository (VCProfileViewer-Protocol-Internal) and is not
** distributed as part of this open-source release.
**
** The VC nano 3D Z communication protocol is proprietary to
** Vision Components GmbH (© Vision Components, All rights reserved).
**
** To build this project, copy vcprotocol.cpp and vcprotocol.h from the
** internal repository into src/ and include/ respectively.
**
** Contact: info@notavis.com
****************************************************************************/
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "types.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "Ws2_32.lib")
   typedef SOCKET vc_socket_t;
#  define VC_INVALID_SOCKET INVALID_SOCKET
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
   typedef int vc_socket_t;
#  define VC_INVALID_SOCKET (-1)
#endif

// Protocol structs and constants – see internal repo for full implementation.

#pragma pack(push,1)
struct VcGlobalHeader {
    uint32_t syncByte;
    uint32_t checksumHead;
    uint32_t checksumData;
    uint32_t counter;
    int32_t  timeMs;
    int32_t  reserved;
    int32_t  error;
    uint32_t dataLength;
};
struct VcMetaHeader {
    uint32_t type;
    uint32_t id;
    uint32_t hostCnt;
    uint32_t length;
};
#pragma pack(pop)

static constexpr int CMDTYPE_STRING = 0;
static constexpr int CMDTYPE_DATA   = 100;

// Command parameter IDs (stub definitions)
static constexpr int CMD_STOP                 = 3;
static constexpr int CMD_RECEIVE_SENSOR_DATA  = 10;
static constexpr int CMD_RECEIVE_PRODUCT_DATA = 11;

class VcProtocol {
public:
    VcProtocol();
    ~VcProtocol();
    bool connect(const std::string &ip, uint16_t port);
    void disconnect();
    bool isConnected() const { return m_connected; }
    bool sendCommand(int paramId, const std::string &payload, uint32_t hostCmdId = 0);
    bool readStringResponse(int paramId, uint32_t hostCmdId,
                            std::string &out_response, int timeoutMs = 2000);
    bool readDataFrame(int &out_dataMode, int &out_resultCnt,
                       uint64_t &out_timestamp,
                       std::vector<uint8_t> &out_payload,
                       int timeoutMs = 500);
private:
    bool recvAll(void *buf, int size, int timeoutMs);
    bool findSync(int timeoutMs);
    bool recvGlobalHeader(VcGlobalHeader &gh, int timeoutMs);
    vc_socket_t m_sock;
    bool        m_connected;
    uint32_t    m_globalCounter;
    uint32_t    m_metaCounter;
};
