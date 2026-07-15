// X32 → REAPER Mirror
// Thin cross-platform (winsock2 / BSD) RAII UDP socket. One socket is used for
// both send and receive so the console replies to our source IP:port. Supports
// a large receive buffer (scene-load bursts) and broadcast (/xinfo discovery).
//
// No REAPER dependencies; used by the socket thread and by the headless
// integration test.
#ifndef X32MIRROR_UDP_SOCKET_H_
#define X32MIRROR_UDP_SOCKET_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace x32 {

// Process-wide winsock init/teardown (no-op on POSIX). Construct one instance
// for the lifetime of the plugin.
struct NetInit {
  NetInit();
  ~NetInit();
  NetInit(const NetInit&) = delete;
  NetInit& operator=(const NetInit&) = delete;
};

struct Endpoint {
  std::string ip;   // dotted-quad
  uint16_t port = 0;
};

class UdpSocket {
 public:
  UdpSocket() = default;
  ~UdpSocket();
  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  // Open an unconnected UDP socket bound to an ephemeral local port. Sets a
  // 256 KB receive buffer and non-blocking mode. Returns false on failure.
  bool Open();
  void Close();
  bool IsOpen() const { return fd_ >= 0; }

  // Enable SO_BROADCAST (for /xinfo discovery). Returns false on failure.
  bool EnableBroadcast(bool on);

  // Send a datagram to ip:port. Returns bytes sent, or -1 on error.
  int SendTo(const std::string& ip, uint16_t port, const uint8_t* data,
             size_t len);
  int SendTo(const Endpoint& ep, const std::vector<uint8_t>& data) {
    return SendTo(ep.ip, ep.port, data.data(), data.size());
  }

  // Receive one datagram (non-blocking). Returns bytes read (>=0), 0 if a
  // zero-length datagram, or -1 if there is nothing to read / on error. On
  // success *from holds the sender endpoint.
  int RecvFrom(uint8_t* buf, size_t buflen, Endpoint* from);

  // Wait until the socket is readable or timeout_ms elapses. Returns true if
  // readable, false on timeout/error. Uses select().
  bool WaitReadable(int timeout_ms);

  // The local port the socket is bound to (0 if unknown).
  uint16_t LocalPort() const;

 private:
  int fd_ = -1;
};

// Resolve the local subnet broadcast candidates for discovery. Always includes
// 255.255.255.255; may include per-interface directed broadcasts. Best-effort.
std::vector<std::string> BroadcastAddresses();

}  // namespace x32

#endif  // X32MIRROR_UDP_SOCKET_H_
