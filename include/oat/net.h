// oat/net.h - oat_net: TCP and UDP sockets and address resolution, over
// SDL3_net. It stops at sockets: reliability, ordering, encryption and
// replication belong to the layer above.
//
// Every call returns at once except resolve(), TcpStream::drain() and
// wait_for_input(), which take a timeout. After a function returns an error,
// oat::os::last_error_text() has the details.
//
// The handle structs here are plain values, like the rest of oat: copying one
// copies the handle, and exactly one copy must be closed or released.
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
  // True if both refer to the same host. Lets a server recognise a known peer
  // without keeping every sender's address.
  bool same_host(const Address& other) const;
  Address retain() const;
  void release();
};

// How many Address references are alive: every resolve(), retain() and
// received Datagram::from adds one, every release() removes one. A number that
// keeps climbing means a release() is missing, most often in a receive loop
// that skips a packet early. Counted when the library is built with
// OAT_DEBUG; always 0 otherwise.
u32 live_address_count();

// Turns a host name or a numeric address into an Address. Blocks until the
// lookup finishes or the timeout passes, so call it from a job: a slow DNS
// server holds the calling thread for up to `timeout_ms` (-1 waits forever).
// NotFound if the name does not resolve, Timeout if no answer came in time.
[[nodiscard]] Result<Address> resolve(Str host, i32 timeout_ms = 10000);

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
  //
  // The queue has no limit. If the other side reads slower than you write,
  // everything you write piles up in memory: backpressure is the caller's
  // job. Check pending_writes() and hold back while it is large.
  [[nodiscard]] Result<void> write(Span<const u8> bytes);

  // Bytes still queued by write().
  u32 pending_writes();

  // Blocks until everything queued by write() has been handed to the system,
  // or the timeout passes (-1 waits forever, 0 only checks). Returns the bytes
  // still queued: 0 means all of it went out. Io if the connection broke.
  [[nodiscard]] Result<u32> drain(i32 timeout_ms);

  // Closes at once and THROWS AWAY anything still queued by write(). To be
  // sure the last write is sent, call drain() first:
  //
  //   (void)stream.write(goodbye);
  //   (void)stream.drain(1000);
  //   stream.close();
  void close();
};

struct TcpListener {
  NET_Server* handle = nullptr;

  // Listens on every local address, or only on `local` if one is given. Pass
  // the loopback address to keep a dev server or tool off the network:
  //
  //   Address loopback = *resolve("127.0.0.1");
  //   TcpListener::listen(8080, &loopback);
  [[nodiscard]] static Result<TcpListener> listen(u16 port, const Address* local = nullptr);

  // The next waiting connection, already connected. WouldBlock if there is none.
  [[nodiscard]] Result<TcpStream> accept();

  void close();
};

struct Datagram {
  Address from; // the sender. Yours: call from.release() when done with it.
  u16 port = 0; // the sender's port
  u32 size = 0; // bytes copied into the caller's buffer
  bool truncated = false; // the packet was larger than the buffer
};

// The largest payload one UDP datagram can carry over IPv4. Stay under about
// 1,200 bytes if a packet should cross the internet without being fragmented.
inline constexpr u32 MAX_DATAGRAM_SIZE = 65507;

struct UdpSocket {
  NET_DatagramSocket* handle = nullptr;

  // Port 0 lets the system pick one. Binds every local address, or only
  // `local` if one is given.
  [[nodiscard]] static Result<UdpSocket> bind(u16 port, const Address* local = nullptr);

  // One packet of 1 to MAX_DATAGRAM_SIZE bytes; InvalidArgument otherwise.
  [[nodiscard]] Result<void> send(const Address& address, u16 port, Span<const u8> bytes);

  // Copies the next packet into `buffer`. WouldBlock if none has arrived.
  // On success the caller owns datagram.from and must release() it.
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
