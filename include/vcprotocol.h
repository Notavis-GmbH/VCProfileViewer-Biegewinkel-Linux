/****************************************************************************
** vcprotocol.h
** TCP/IP Protocol for VC nano 3D Z Laser Scanner
** Based on Vision Components official SDK
****************************************************************************/
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "types.h"  // ProfilePoint

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

// -----------------------------------------------------------------------
// Protocol constants
// -----------------------------------------------------------------------
static constexpr uint32_t VC_SYNC_BYTE        = 0xAA0F55F0u;
static constexpr int      VC_GLOBAL_HDR_SIZE  = 32;
static constexpr int      VC_META_HDR_SIZE    = 16;

// Command IDs (Parameter IDs)
static constexpr int CMD_STOP                 =  0;
static constexpr int CMD_LOAD_PRODUCT_FILE    = 38;
static constexpr int CMD_RECEIVE_PRODUCT_DATA = 40;
static constexpr int CMD_SEND_PRODUCT_DATA    = 41;
static constexpr int CMD_LOAD_SENSOR_FILE     = 42;
static constexpr int CMD_RECEIVE_SENSOR_DATA  = 43;
static constexpr int CMD_SEND_SENSOR_DATA     = 44;

// DataMode values returned by sensor
static constexpr int DATAMODE_IMAGE           =  1;  // greyscale image
static constexpr int DATAMODE_PROFILE_PIX     =  2;  // profile in pixel
static constexpr int DATAMODE_PROFILE_MM      =  4;  // profile in mm  <-- we use this
static constexpr int DATAMODE_PRODUCT         =  8;  // product result string

// CmdType in response
static constexpr int CMDTYPE_STRING           =  0;
static constexpr int CMDTYPE_DATA             = 100;

// -----------------------------------------------------------------------
// Wire structures (packed, little-endian)
// -----------------------------------------------------------------------
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
    uint32_t type;       // 0 = string cmd/response, 100 = data response
    uint32_t id;         // parameter ID
    uint32_t hostCnt;    // host counter
    uint32_t length;     // payload length in bytes
};
#pragma pack(pop)

// ProfilePoint is defined in types.h (included above)

// -----------------------------------------------------------------------
// VcProtocol – thin wrapper around a blocking TCP socket
// -----------------------------------------------------------------------
class VcProtocol
{
public:
    VcProtocol();
    ~VcProtocol();

    bool    connect(const std::string &ip, uint16_t port);
    void    disconnect();
    bool    isConnected() const { return m_connected; }

    // Send a string command (e.g. "LaserMode=1\nDataMode=4\nNbrLines=-1\n")
    // Returns true on success
    bool    sendCommand(int paramId, const std::string &payload, uint32_t hostCmdId = 0);

    // Wait for a string-type response that matches paramId / hostCmdId
    // out_response receives the ASCII text the sensor echoes back
    bool    readStringResponse(int paramId, uint32_t hostCmdId,
                               std::string &out_response,
                               int timeoutMs = 2000);

    // Receive one data frame (DataMode=4 profile or DataMode=8 product)
    // Returns true when a CMDTYPE_DATA packet arrived
    // out_dataMode, out_resultCnt, out_timestamp filled from meta header
    bool    readDataFrame(int &out_dataMode, int &out_resultCnt,
                         uint64_t &out_timestamp,
                         std::vector<uint8_t> &out_payload,
                         int timeoutMs = 500);

private:
    bool    recvAll(void *buf, int size, int timeoutMs);
    bool    findSync(int timeoutMs);
    bool    recvGlobalHeader(VcGlobalHeader &gh, int timeoutMs);

    vc_socket_t m_sock;
    bool        m_connected;
    uint32_t    m_globalCounter;
    uint32_t    m_metaCounter;
};
