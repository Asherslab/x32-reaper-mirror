#include "udp_socket.h"

#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
static int close_socket(int fd) { return closesocket(fd); }
static int last_err() { return WSAGetLastError(); }
#define X32_EWOULDBLOCK WSAEWOULDBLOCK
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
static int close_socket(int fd) { return ::close(fd); }
static int last_err() { return errno; }
#define X32_EWOULDBLOCK EWOULDBLOCK
#endif

namespace x32 {

NetInit::NetInit() {
#if defined(_WIN32)
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}
NetInit::~NetInit() {
#if defined(_WIN32)
  WSACleanup();
#endif
}

UdpSocket::~UdpSocket() { Close(); }

bool UdpSocket::Open() {
  Close();
  fd_ = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  if (fd_ < 0) return false;

  // Bind to an ephemeral port on all interfaces so the console can reply.
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = 0;
  if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    Close();
    return false;
  }

  // Large receive buffer: scene loads produce a burst of parameter pushes.
  int rcvbuf = 256 * 1024;
  ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));

  // Non-blocking.
#if defined(_WIN32)
  u_long nb = 1;
  ioctlsocket(fd_, FIONBIO, &nb);
#else
  int flags = fcntl(fd_, F_GETFL, 0);
  fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
#endif
  return true;
}

void UdpSocket::Close() {
  if (fd_ >= 0) {
    close_socket(fd_);
    fd_ = -1;
  }
}

bool UdpSocket::EnableBroadcast(bool on) {
  if (fd_ < 0) return false;
  int v = on ? 1 : 0;
  return ::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST,
                      reinterpret_cast<const char*>(&v), sizeof(v)) == 0;
}

int UdpSocket::SendTo(const std::string& ip, uint16_t port, const uint8_t* data,
                      size_t len) {
  if (fd_ < 0) return -1;
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) return -1;
  int n = static_cast<int>(::sendto(fd_, reinterpret_cast<const char*>(data),
                                    static_cast<int>(len), 0,
                                    reinterpret_cast<sockaddr*>(&addr),
                                    sizeof(addr)));
  return n;
}

int UdpSocket::RecvFrom(uint8_t* buf, size_t buflen, Endpoint* from) {
  if (fd_ < 0) return -1;
  sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  std::memset(&addr, 0, sizeof(addr));
  int n = static_cast<int>(::recvfrom(fd_, reinterpret_cast<char*>(buf),
                                      static_cast<int>(buflen), 0,
                                      reinterpret_cast<sockaddr*>(&addr),
                                      &addrlen));
  if (n < 0) {
    if (last_err() == X32_EWOULDBLOCK) return -1;
    return -1;
  }
  if (from) {
    char ipbuf[64] = {0};
    ::inet_ntop(AF_INET, &addr.sin_addr, ipbuf, sizeof(ipbuf));
    from->ip = ipbuf;
    from->port = ntohs(addr.sin_port);
  }
  return n;
}

bool UdpSocket::WaitReadable(int timeout_ms) {
  if (fd_ < 0) return false;
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(fd_, &rfds);
  timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  int r = ::select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
  return r > 0 && FD_ISSET(fd_, &rfds);
}

uint16_t UdpSocket::LocalPort() const {
  if (fd_ < 0) return 0;
  sockaddr_in addr;
  socklen_t len = sizeof(addr);
  std::memset(&addr, 0, sizeof(addr));
  if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0)
    return 0;
  return ntohs(addr.sin_port);
}

std::vector<std::string> BroadcastAddresses() {
  std::vector<std::string> out;
  out.push_back("255.255.255.255");
#if !defined(_WIN32)
  ifaddrs* ifap = nullptr;
  if (getifaddrs(&ifap) == 0) {
    for (ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
      if (!(ifa->ifa_flags & IFF_BROADCAST)) continue;
      if (!ifa->ifa_broadaddr) continue;
      auto* b = reinterpret_cast<sockaddr_in*>(ifa->ifa_broadaddr);
      char ip[64] = {0};
      inet_ntop(AF_INET, &b->sin_addr, ip, sizeof(ip));
      if (ip[0] && std::strcmp(ip, "0.0.0.0") != 0) out.push_back(ip);
    }
    freeifaddrs(ifap);
  }
#endif
  return out;
}

}  // namespace x32
