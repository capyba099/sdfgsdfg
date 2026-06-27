#pragma once
#include <cstdint>

// Minimal cross-platform (POSIX + Winsock) non-blocking TCP socket helpers.
namespace net {

#ifdef _WIN32
using Socket = unsigned long long;  // matches SOCKET (UINT_PTR) on Win64
#else
using Socket = int;
#endif

extern const Socket INVALID;

bool init();      // WSAStartup on Windows; no-op elsewhere
void cleanup();

// Create a non-blocking listening socket bound to all interfaces on `port`.
Socket listenOn(uint16_t port);

// Accept a pending connection (non-blocking). Returns INVALID if none waiting.
Socket acceptOne(Socket listener);

// Blocking connect with a timeout; the returned socket is left non-blocking.
Socket connectTo(const char* host, uint16_t port, int timeoutMs);

void setNonBlocking(Socket s);

// Returns >0 bytes received, 0 if the peer closed, -1 if no data / would block.
int recvSome(Socket s, char* buf, int len);

// Sends all bytes. Returns len on success, -1 on error / disconnect.
int sendAll(Socket s, const char* buf, int len);

void closeSock(Socket s);

}  // namespace net
