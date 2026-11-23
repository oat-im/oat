// oat_net: a thin wrapper over SDL3_net.
#include <oat/net.h>

#include "os/os_internal.h"

#include <SDL3_net/SDL_net.h>

namespace oat::net {

using os::detail::fail;

Result<void> init() {
  if (!NET_Init()) return fail(Error::Unknown);
  return {};
}

void shutdown() { NET_Quit(); }

// ---------------------------------------------------------------------------
// Address
// ---------------------------------------------------------------------------

Str Address::text() const {
  const char* s = handle ? NET_GetAddressString(handle) : nullptr;
  return s ? Str(s) : Str();
}

Address Address::retain() const {
  Address a;
  a.handle = handle ? NET_RefAddress(handle) : nullptr;
  return a;
}

void Address::release() {
  if (handle) NET_UnrefAddress(handle);
  handle = nullptr;
}

Result<Address> resolve(Str host) {
  os::detail::CStr chost(host);
  NET_Address* address = NET_ResolveHostname(chost.get());
  if (!address) return fail(Error::Unknown);
  if (NET_WaitUntilResolved(address, -1) != NET_SUCCESS) {
    os::detail::set_error_text(SDL_GetError());
    NET_UnrefAddress(address);
    return err(Error::NotFound);
  }
  Address a;
  a.handle = address;
  return a;
}

// ---------------------------------------------------------------------------
// TCP
// ---------------------------------------------------------------------------

Result<TcpStream> TcpStream::connect(const Address& address, u16 port) {
  OAT_ASSERT(address.handle != nullptr);
  NET_StreamSocket* sock = NET_CreateClient(address.handle, port, 0);
  if (!sock) return fail(Error::Io);
  TcpStream s;
  s.handle = sock;
  return s;
}

Result<void> TcpStream::connect_status() {
  OAT_ASSERT(handle != nullptr);
  switch (NET_GetConnectionStatus(handle)) {
    case NET_SUCCESS: return {};
    case NET_WAITING: return err(Error::WouldBlock);
    default: return fail(Error::Io);
  }
}

Result<u32> TcpStream::read(Span<u8> out) {
  OAT_ASSERT(handle != nullptr);
  int room = out.size() > 0x7fffffff ? 0x7fffffff : static_cast<int>(out.size());
  int n = NET_ReadFromStreamSocket(handle, out.data(), room);
  if (n < 0) return fail(Error::Io);
  if (n == 0) return err(Error::WouldBlock);
  return static_cast<u32>(n);
}

Result<void> TcpStream::write(Span<const u8> bytes) {
  OAT_ASSERT(handle != nullptr);
  const u8* p = bytes.data();
  usize left = bytes.size();
  while (left > 0) { // SDL_net takes an int length
    int chunk = left > 0x40000000 ? 0x40000000 : static_cast<int>(left);
    if (!NET_WriteToStreamSocket(handle, p, chunk)) return fail(Error::Io);
    p += chunk;
    left -= static_cast<usize>(chunk);
  }
  return {};
}

u32 TcpStream::pending_writes() {
  int n = handle ? NET_GetStreamSocketPendingWrites(handle) : 0;
  return n > 0 ? static_cast<u32>(n) : 0;
}

void TcpStream::close() {
  if (handle) NET_DestroyStreamSocket(handle);
  handle = nullptr;
}

Result<TcpListener> TcpListener::listen(u16 port) {
  NET_Server* server = NET_CreateServer(nullptr, port, 0);
  if (!server) return fail(Error::Io);
  TcpListener l;
  l.handle = server;
  return l;
}

Result<TcpStream> TcpListener::accept() {
  OAT_ASSERT(handle != nullptr);
  NET_StreamSocket* client = nullptr;
  if (!NET_AcceptClient(handle, &client)) return fail(Error::Io);
  if (!client) return err(Error::WouldBlock);
  TcpStream s;
  s.handle = client;
  return s;
}

void TcpListener::close() {
  if (handle) NET_DestroyServer(handle);
  handle = nullptr;
}

// ---------------------------------------------------------------------------
// UDP
// ---------------------------------------------------------------------------

Result<UdpSocket> UdpSocket::bind(u16 port) {
  NET_DatagramSocket* sock = NET_CreateDatagramSocket(nullptr, port, 0);
  if (!sock) return fail(Error::Io);
  UdpSocket u;
  u.handle = sock;
  return u;
}

Result<void> UdpSocket::send(const Address& address, u16 port, Span<const u8> bytes) {
  OAT_ASSERT(handle != nullptr && address.handle != nullptr);
  // SDL_net rejects empty datagrams, and nothing larger fits in a UDP packet.
  if (bytes.empty() || bytes.size() > 65535)
    return fail(Error::InvalidArgument, "a datagram must be 1 to 65535 bytes");
  if (!NET_SendDatagram(handle, address.handle, port, bytes.data(), static_cast<int>(bytes.size())))
    return fail(Error::Io);
  return {};
}

Result<Datagram> UdpSocket::receive(Span<u8> buffer) {
  OAT_ASSERT(handle != nullptr);
  NET_Datagram* dgram = nullptr;
  if (!NET_ReceiveDatagram(handle, &dgram)) return fail(Error::Io);
  if (!dgram) return err(Error::WouldBlock);

  // Keep the sender's address alive until the next receive.
  if (last_from) NET_UnrefAddress(last_from);
  last_from = NET_RefAddress(dgram->addr);

  Datagram out;
  out.from.handle = last_from;
  out.port = dgram->port;
  usize size = static_cast<usize>(dgram->buflen);
  out.truncated = size > buffer.size();
  if (out.truncated) size = buffer.size();
  if (size) std::memcpy(buffer.data(), dgram->buf, size);
  out.size = static_cast<u32>(size);
  NET_DestroyDatagram(dgram);
  return out;
}

void UdpSocket::close() {
  if (last_from) NET_UnrefAddress(last_from);
  last_from = nullptr;
  if (handle) NET_DestroyDatagramSocket(handle);
  handle = nullptr;
}

// ---------------------------------------------------------------------------
// Waiting
// ---------------------------------------------------------------------------

Result<u32> wait_for_input(Span<const Socket> sockets, i32 timeout_ms) {
  OAT_CHECK(sockets.size() <= 0x7fffffff);
  // SDL_net wants an array of plain pointers.
  void* small[64];
  void** list = small;
  Allocator h = heap();
  usize bytes = sizeof(void*) * sockets.size();
  if (sockets.size() > array_count(small)) list = static_cast<void**>(h.alloc(bytes, alignof(void*)));
  for (usize i = 0; i < sockets.size(); ++i) list[i] = sockets[i].handle;

  int ready = NET_WaitUntilInputAvailable(list, static_cast<int>(sockets.size()), timeout_ms);
  if (list != small) h.free(list, bytes);
  if (ready < 0) return fail(Error::Io);
  return static_cast<u32>(ready);
}

} // namespace oat::net
