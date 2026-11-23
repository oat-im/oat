// oat/net.h - oat_net: TCP and UDP sockets and address resolution, over
// SDL3_net. It stops at sockets: reliability, ordering, encryption and
// replication belong to the layer above.
//
// Every call returns at once except resolve() and wait_for_input(). After a
// function returns an error, oat::os::last_error_text() has the details.
#pragma once

#include "base.h"

struct NET_Address;
struct NET_StreamSocket;
struct NET_Server;
struct NET_DatagramSocket;

namespace oat::net {

[[nodiscard]] Result<void> init();
void shutdown();

// A resolved address. It is a counted reference: call release() when done,
// and retain() to keep a copy alive on its own.
struct Address {
  NET_Address* handle = nullptr;

  // Numeric form, such as "127.0.0.1". Valid while the address is alive.
  Str text() const;
  Address retain() const;
  void release();
};

// Turns a host name or a numeric address into an Address. Blocks until the
// lookup finishes, so call it from a job. NotFound if the name does not resolve.
[[nodiscard]] Result<Address> resolve(Str host);

struct TcpStream {
  NET_StreamSocket* handle = nullptr;

  // Starts connecting and returns straight away. Poll connect_status() (or
  // sleep in wait_for_input) before reading or writing.
  [[nodiscard]] static Result<TcpStream> connect(const Address& address, u16 port);

  // Success once connected, WouldBlock while still connecting, Io if the
  // connection was refused or failed.
  [[nodiscard]] Result<void> connect_status();

  // How many bytes were read, at least 1. WouldBlock if nothing has arrived.
  // Io means the connection is closed or broken: close() the stream.
  [[nodiscard]] Result<u32> read(Span<u8> out);

  // Sends, or queues for sending; never blocks. Io means the connection is
  // closed or broken.
  [[nodiscard]] Result<void> write(Span<const u8> bytes);

  // Bytes still queued by write().
  u32 pending_writes();

  void close();
};

struct TcpListener {
  NET_Server* handle = nullptr;

  // Listens on every local address.
  [[nodiscard]] static Result<TcpListener> listen(u16 port);

  // The next waiting connection, already connected. WouldBlock if there is none.
  [[nodiscard]] Result<TcpStream> accept();

  void close();
};

struct Datagram {
  Address from; // owned by the socket: valid until its next receive() or close()
  u16 port = 0; // the sender's port
  u32 size = 0; // bytes copied into the caller's buffer
  bool truncated = false; // the packet was larger than the buffer
};

struct UdpSocket {
  NET_DatagramSocket* handle = nullptr;
  NET_Address* last_from = nullptr;

  // Port 0 lets the system pick one.
  [[nodiscard]] static Result<UdpSocket> bind(u16 port);

  // One packet of 1 to 65535 bytes; InvalidArgument otherwise.
  [[nodiscard]] Result<void> send(const Address& address, u16 port, Span<const u8> bytes);

  // Copies the next packet into `buffer`. WouldBlock if none has arrived.
  [[nodiscard]] Result<Datagram> receive(Span<u8> buffer);

  void close();
};

// Anything wait_for_input can sleep on.
struct Socket {
  void* handle = nullptr;

  Socket() = default;
  Socket(const TcpStream& s) : handle(s.handle) {}
  Socket(const TcpListener& l) : handle(l.handle) {}
  Socket(const UdpSocket& u) : handle(u.handle) {}
};

// Sleeps until at least one socket has something to read or accept (or a
// pending connect finishes), or until the timeout passes. A timeout of -1
// waits forever, 0 only checks. Returns how many sockets are ready, which is 0
// on timeout; it does not say which, so try each of them.
[[nodiscard]] Result<u32> wait_for_input(Span<const Socket> sockets, i32 timeout_ms);

} // namespace oat::net
