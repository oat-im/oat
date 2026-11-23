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

// Polls `step` until it stops returning WouldBlock, for at most `seconds`.
template <class Step>
static auto poll(Step step, u64 seconds = 2) -> decltype(step()) {
  u64 deadline = os::now_ns() + seconds * 1000ull * 1000 * 1000;
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

  // A name that cannot resolve fails, by answer or by running out of time.
  Result<net::Address> bad = net::resolve("no-such-host.invalid", 5000);
  CHECK(!bad && (bad.error() == Error::NotFound || bad.error() == Error::Timeout));

  // same_host compares hosts, not handles.
  net::Address a = *net::resolve("127.0.0.1", 1000);
  net::Address b = *net::resolve("127.0.0.1", 1000);
  CHECK(a.handle != nullptr && a.same_host(b) && b.same_host(a));
  CHECK(!a.same_host(net::Address{}) && net::Address{}.same_host(net::Address{}));
  a.release();
  b.release();
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

  // close() throws away anything still queued, so the last message is
  // drained first. Then it arrives even though the sender is gone.
  CHECK(server->write(as_bytes("goodbye")).has_value());
  Result<u32> left = server->drain(5000);
  CHECK(left && *left == 0);
  server->close();
  CHECK(server->handle == nullptr);
  Array<u8> farewell;
  Result<u32> last = poll([&] { return client->read(buf); });
  while (last) {
    farewell.push_many(Span<const u8>(buf, *last), h);
    last = poll([&] { return client->read(buf); });
  }
  CHECK(farewell.len == 7 && std::memcmp(farewell.data, "goodbye", 7) == 0);
  farewell.free(h);

  // Once the data is read, the closed connection reports itself.
  CHECK(last.error() == Error::Io);
  CHECK(!os::last_error_text().empty());
  client->close();
  CHECK(client->handle == nullptr);

  listener->close();
  addr.release();
}

TEST(net_tcp_listen_on_one_address) {
  // Bound to loopback only: reachable from this machine, not from the network.
  net::Address loopback = *net::resolve("127.0.0.1");
  Result<net::TcpListener> listener = net::TcpListener::listen(47634, &loopback);
  CHECK(listener.has_value());
  if (listener) {
    Result<net::TcpStream> client = net::TcpStream::connect(loopback, 47634);
    CHECK(client.has_value());
    if (client) {
      CHECK(poll([&] { return client->connect_status(); }).has_value());
      Result<net::TcpStream> server = poll([&] { return listener->accept(); });
      CHECK(server.has_value());
      if (server) {
        // Nothing queued: drain has nothing to wait for.
        Result<u32> left = server->drain(0);
        CHECK(left && *left == 0);
        server->close();
      }
      client->close();
    }
    listener->close();
  }
  loopback.release();
}

TEST(net_tcp_connect_refused) {
  net::Address addr = *net::resolve("127.0.0.1");
  // Nothing listens on this port.
  Result<net::TcpStream> client = net::TcpStream::connect(addr, 47639);
  CHECK(client.has_value());
  if (client) {
    // Windows retries a refused connection for a couple of seconds before giving up.
    Result<void> status = poll([&] { return client->connect_status(); }, 20);
    CHECK(!status && status.error() == Error::Io);
    client->close();
  }
  addr.release();
}

TEST(net_udp_loopback) {
  u32 addresses_before = net::live_address_count();
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
    CHECK(got->from.same_host(addr));
    // Reply to whoever sent it. The sender's address is ours to release.
    CHECK(b->send(got->from, got->port, as_bytes("world!")).has_value());
    got->from.release();
  }
  Result<net::Datagram> reply = poll([&] { return a->receive(buf); });
  CHECK(reply && reply->size == 6 && reply->port == UDP_PORT_B && std::memcmp(buf, "world!", 6) == 0);
  if (reply) reply->from.release();

  // A packet larger than the buffer is cut, and says so.
  CHECK(a->send(addr, UDP_PORT_B, as_bytes("0123456789")).has_value());
  u8 tiny[4];
  Result<net::Datagram> cut = poll([&] { return b->receive(tiny); });
  CHECK(cut && cut->size == 4 && cut->truncated && std::memcmp(tiny, "0123", 4) == 0);
  if (cut) {
    // A copy of the socket shares the handle and nothing else, so receiving
    // through either copy is safe.
    net::UdpSocket b_copy = *b;
    CHECK(a->send(addr, UDP_PORT_B, as_bytes("x")).has_value());
    Result<net::Datagram> via_copy = poll([&] { return b_copy.receive(buf); });
    CHECK(via_copy && via_copy->size == 1);
    if (via_copy) via_copy->from.release();
    cut->from.release();
  }

  // Larger than any UDP datagram can be: refused before it reaches the system.
  Allocator h = heap();
  Array<u8> too_big;
  too_big.resize(net::MAX_DATAGRAM_SIZE + 1, h);
  Result<void> oversize = a->send(addr, UDP_PORT_B, too_big.span());
  CHECK(!oversize && oversize.error() == Error::InvalidArgument);
  too_big.free(h);

  // Empty datagrams are rejected up front.
  Result<void> empty = a->send(addr, UDP_PORT_B, std::span<const u8>());
  CHECK(!empty && empty.error() == Error::InvalidArgument);

#if OAT_DEBUG
  // The count makes a forgotten release visible: one received packet that is
  // not released shows up as one extra live address.
  CHECK(net::live_address_count() == addresses_before + 1); // just `addr`
  CHECK(a->send(addr, UDP_PORT_B, as_bytes("leak?")).has_value());
  Result<net::Datagram> held = poll([&] { return b->receive(buf); });
  CHECK(held.has_value());
  CHECK(net::live_address_count() == addresses_before + 2);
  if (held) held->from.release();
  CHECK(net::live_address_count() == addresses_before + 1);
#endif

  a->close();
  b->close();
  b->close(); // harmless
  addr.release();
  CHECK(net::live_address_count() == addresses_before);
}
