#include "test.h"

#include <oat/net.h>
#include <oat/oat.h>
#include <oat/os.h>

using namespace oat;

// Loopback only; nothing here leaves the machine.
static constexpr u16 TCP_PORT = 47631;
static constexpr u16 UDP_PORT_A = 47632;
static constexpr u16 UDP_PORT_B = 47633;

static const bool g_ready = os::init().has_value() && net::init().has_value();

static std::span<const u8> as_bytes(std::string_view s) {
  return std::span<const u8>(reinterpret_cast<const u8*>(s.data()), s.size());
}

// Polls `step` until it stops returning WouldBlock, for at most two seconds.
template <class Step>
static auto poll(Step step) -> decltype(step()) {
  u64 deadline = os::now_ns() + 2000ull * 1000 * 1000;
  for (;;) {
    auto r = step();
    if (r || r.error() != Error::WouldBlock || os::now_ns() > deadline) return r;
    os::sleep_ns(1000 * 1000);
  }
}

TEST(net_init) { CHECK(g_ready); }

TEST(net_resolve) {
  Result<net::Address> loopback = net::resolve("127.0.0.1");
  CHECK(loopback.has_value());
  if (loopback) {
    CHECK(loopback->text() == "127.0.0.1");
    net::Address copy = loopback->retain();
    loopback->release();
    CHECK(loopback->handle == nullptr);
    CHECK(copy.text() == "127.0.0.1"); // the retained copy outlives the original
    copy.release();
    copy.release(); // harmless
  }
  Result<net::Address> named = net::resolve("localhost");
  CHECK(named.has_value());
  if (named) named->release();

  Result<net::Address> bad = net::resolve("no-such-host.invalid");
  CHECK(!bad && bad.error() == Error::NotFound);
}

TEST(net_tcp_loopback) {
  net::Address addr = *net::resolve("127.0.0.1");
  Result<net::TcpListener> listener = net::TcpListener::listen(TCP_PORT);
  CHECK(listener.has_value());
  if (!listener) return;

  // Nothing is connecting yet.
  Result<net::TcpStream> no_client = listener->accept();
  CHECK(!no_client && no_client.error() == Error::WouldBlock);
  net::Socket only_listener[] = {*listener};
  Result<u32> idle = net::wait_for_input(only_listener, 10);
  CHECK(idle && *idle == 0);

  Result<net::TcpStream> client = net::TcpStream::connect(addr, TCP_PORT);
  CHECK(client.has_value());
  if (!client) return;
  CHECK(poll([&] { return client->connect_status(); }).has_value());

  Result<u32> ready = net::wait_for_input(only_listener, 2000);
  CHECK(ready && *ready == 1);
  Result<net::TcpStream> server = poll([&] { return listener->accept(); });
  CHECK(server.has_value());
  if (!server) return;

  // Nothing sent yet: read does not block.
  u8 buf[64];
  Result<u32> nothing = server->read(buf);
  CHECK(!nothing && nothing.error() == Error::WouldBlock);

  // Client to server.
  CHECK(client->write(as_bytes("ping")).has_value());
  net::Socket server_side[] = {*server};
  CHECK(net::wait_for_input(server_side, 2000).has_value());
  Result<u32> n = poll([&] { return server->read(buf); });
  CHECK(n && *n == 4 && std::memcmp(buf, "ping", 4) == 0);

  // Server to client, a payload large enough to arrive in pieces.
  Allocator h = heap();
  Array<u8> payload, received;
  Rng rng = Rng::seeded(60);
  payload.resize_uninitialized(200000, h);
  for (u8& b : payload) b = static_cast<u8>(rng.next_u64());
  CHECK(server->write(payload.span()).has_value());
  u64 deadline = os::now_ns() + 5000ull * 1000 * 1000;
  while (received.len < payload.len && os::now_ns() < deadline) {
    u8 chunk[16384];
    Result<u32> got = client->read(chunk);
    if (got) received.push_many(std::span<const u8>(chunk, *got), h);
    else if (got.error() == Error::WouldBlock) os::sleep_ns(200 * 1000);
    else break;
  }
  CHECK(received.len == payload.len);
  CHECK(received.len == payload.len && std::memcmp(received.data, payload.data, payload.len) == 0);
  CHECK(server->pending_writes() == 0);
  payload.free(h);
  received.free(h);

  // When one side closes, the other side's read reports it.
  client->close();
  CHECK(client->handle == nullptr);
  Result<u32> after_close = poll([&] { return server->read(buf); });
  CHECK(!after_close && after_close.error() == Error::Io);
  CHECK(!os::last_error_text().empty());

  server->close();
  listener->close();
  addr.release();
}

TEST(net_tcp_connect_refused) {
  net::Address addr = *net::resolve("127.0.0.1");
  // Nothing listens on this port.
  Result<net::TcpStream> client = net::TcpStream::connect(addr, 47639);
  CHECK(client.has_value());
  if (client) {
    Result<void> status = poll([&] { return client->connect_status(); });
    CHECK(!status && status.error() == Error::Io);
    client->close();
  }
  addr.release();
}

TEST(net_udp_loopback) {
  net::Address addr = *net::resolve("127.0.0.1");
  Result<net::UdpSocket> a = net::UdpSocket::bind(UDP_PORT_A);
  Result<net::UdpSocket> b = net::UdpSocket::bind(UDP_PORT_B);
  CHECK(a.has_value() && b.has_value());
  if (!a || !b) return;

  u8 buf[32];
  Result<net::Datagram> no_packet = b->receive(buf);
  CHECK(!no_packet && no_packet.error() == Error::WouldBlock);
  net::Socket both[] = {*a, *b};
  Result<u32> idle = net::wait_for_input(both, 10);
  CHECK(idle && *idle == 0);

  CHECK(a->send(addr, UDP_PORT_B, as_bytes("hello")).has_value());
  CHECK(net::wait_for_input(both, 2000).has_value());
  Result<net::Datagram> got = poll([&] { return b->receive(buf); });
  CHECK(got.has_value());
  if (got) {
    CHECK(got->size == 5 && !got->truncated && std::memcmp(buf, "hello", 5) == 0);
    CHECK(got->port == UDP_PORT_A);
    CHECK(got->from.text() == "127.0.0.1");
    // Reply to whoever sent it.
    CHECK(b->send(got->from, got->port, as_bytes("world!")).has_value());
  }
  Result<net::Datagram> reply = poll([&] { return a->receive(buf); });
  CHECK(reply && reply->size == 6 && reply->port == UDP_PORT_B && std::memcmp(buf, "world!", 6) == 0);

  // A packet larger than the buffer is cut, and says so.
  CHECK(a->send(addr, UDP_PORT_B, as_bytes("0123456789")).has_value());
  u8 tiny[4];
  Result<net::Datagram> cut = poll([&] { return b->receive(tiny); });
  CHECK(cut && cut->size == 4 && cut->truncated && std::memcmp(tiny, "0123", 4) == 0);

  // Empty datagrams are rejected up front.
  Result<void> empty = a->send(addr, UDP_PORT_B, std::span<const u8>());
  CHECK(!empty && empty.error() == Error::InvalidArgument);

  a->close();
  b->close();
  b->close(); // harmless
  addr.release();
}
