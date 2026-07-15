#include "x32_connection.h"

#include <chrono>

#include "osc.h"
#include "x32_addresses.h"

namespace x32 {

namespace {
// Non-strip control addresses the connection is permitted to transmit. Every
// one of these is a no-argument message. Strip addresses are allowed too, but
// only via a valid ParseAddress round-trip (also no-arg).
bool IsAllowedControlAddress(const std::string& a) {
  return a == "/xremote" || a == "/info" || a == "/xinfo";
}

// Families synced on connect / resync (the shipping scope). Adding a family to
// the sweep is a one-line change here once its UI is wired up.
const StripType kSyncFamilies[] = {StripType::CH, StripType::BUS,
                                    StripType::DCA};
}  // namespace

const char* ConnStateName(ConnState s) {
  switch (s) {
    case ConnState::Disconnected: return "Disconnected";
    case ConnState::Connecting:   return "Connecting";
    case ConnState::Syncing:      return "Syncing";
    case ConnState::Live:         return "Live";
    case ConnState::Lost:         return "Lost";
  }
  return "?";
}

X32Connection::X32Connection(StateCache* cache, CoalescingQueue* queue,
                             ConnConfig cfg)
    : cache_(cache), queue_(queue), cfg_(std::move(cfg)) {}

X32Connection::~X32Connection() { Stop(); }

int64_t X32Connection::NowMs() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void X32Connection::Start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread(&X32Connection::ThreadMain, this);
}

void X32Connection::Stop() {
  if (!running_.exchange(false)) return;
  cmd_cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

Endpoint X32Connection::peer() const {
  std::lock_guard<std::mutex> lock(peer_mu_);
  return peer_;
}

// --- Command / control plumbing --------------------------------------------

void X32Connection::Connect(const std::string& ip, uint16_t port) {
  {
    std::lock_guard<std::mutex> lock(cmd_mu_);
    cmds_.push_back({CmdType::Connect, ip, port});
  }
  cmd_cv_.notify_all();
}
void X32Connection::Disconnect() {
  {
    std::lock_guard<std::mutex> lock(cmd_mu_);
    cmds_.push_back({CmdType::Disconnect, "", 0});
  }
  cmd_cv_.notify_all();
}
void X32Connection::Resync() {
  {
    std::lock_guard<std::mutex> lock(cmd_mu_);
    cmds_.push_back({CmdType::Resync, "", 0});
  }
  cmd_cv_.notify_all();
}
void X32Connection::Discover(uint16_t port) {
  {
    std::lock_guard<std::mutex> lock(cmd_mu_);
    cmds_.push_back({CmdType::Discover, "", port});
  }
  cmd_cv_.notify_all();
}

bool X32Connection::PopCommand(Command* out) {
  std::lock_guard<std::mutex> lock(cmd_mu_);
  if (cmds_.empty()) return false;
  *out = cmds_.front();
  cmds_.pop_front();
  return true;
}

void X32Connection::PushControl(ControlEvent ev) {
  std::lock_guard<std::mutex> lock(ctl_mu_);
  ctls_.push_back(std::move(ev));
}

bool X32Connection::PollControl(ControlEvent* out) {
  std::lock_guard<std::mutex> lock(ctl_mu_);
  if (ctls_.empty()) return false;
  *out = std::move(ctls_.front());
  ctls_.pop_front();
  return true;
}

void X32Connection::SetState(ConnState s, const std::string& status) {
  state_.store(s);
  if (s == ConnState::Lost) {
    int idx = backoff_idx_;
    if (idx >= static_cast<int>(cfg_.backoff_ms.size()))
      idx = static_cast<int>(cfg_.backoff_ms.size()) - 1;
    int wait = cfg_.backoff_ms.empty() ? 1000 : cfg_.backoff_ms[idx < 0 ? 0 : idx];
    backoff_deadline_ = NowMs() + wait;
  }
  ControlEvent ev;
  ev.type = ControlEvent::Type::StateChanged;
  ev.state = s;
  ev.status_text = status;
  ev.peer = peer();
  PushControl(std::move(ev));
}

// --- Transmit (one-way guarantee lives here) --------------------------------

bool X32Connection::SendQueryTo(const std::string& ip, uint16_t port,
                                const std::string& address) {
  // Guard: only control-plane addresses or valid strip addresses, and always
  // as no-argument queries. There is deliberately no value-carrying send.
  if (!IsAllowedControlAddress(address)) {
    StripId sid;
    Param sp;
    if (!ParseAddress(address, &sid, &sp)) return false;
  }
  std::vector<uint8_t> pkt = OscEncodeQuery(address);
  return sock_.SendTo(ip, port, pkt.data(), pkt.size()) >= 0;
}

bool X32Connection::SendQuery(const std::string& address) {
  Endpoint p = peer();
  if (p.ip.empty() || p.port == 0) return false;
  return SendQueryTo(p.ip, p.port, address);
}

void X32Connection::SendSyncSweep() {
  Endpoint p = peer();
  if (p.ip.empty()) return;
  for (StripType fam : kSyncFamilies) {
    int count = StripCount(fam);
    for (int i = 1; i <= count; ++i) {
      if (!running_.load()) return;
      StripId id{fam, i};
      SendQueryTo(p.ip, p.port, AddressFor(id, Param::Fader));
      if (cfg_.sync_pace_us > 0)
        std::this_thread::sleep_for(std::chrono::microseconds(cfg_.sync_pace_us));
      SendQueryTo(p.ip, p.port, AddressFor(id, Param::Mute));
      if (cfg_.sync_pace_us > 0)
        std::this_thread::sleep_for(std::chrono::microseconds(cfg_.sync_pace_us));
      // Drain opportunistically so the socket buffer does not overflow during
      // a long sweep.
      DrainIncoming();
    }
  }
}

// --- Receive ----------------------------------------------------------------

int X32Connection::DrainIncoming() {
  int emitted = 0;
  uint8_t buf[4096];
  Endpoint from;
  for (;;) {
    int n = sock_.RecvFrom(buf, sizeof(buf), &from);
    if (n < 0) break;  // nothing more (EWOULDBLOCK) or error
    last_rx_ = NowMs();
    OscMessage msg;
    if (n == 0 || !OscDecode(buf, static_cast<size_t>(n), &msg)) continue;

    // Discovery reply?
    if (msg.address == "/xinfo" && msg.args.size() >= 4) {
      ControlEvent ev;
      ev.type = ControlEvent::Type::Discovered;
      ev.disc_ip = msg.args[0].s;
      ev.disc_name = msg.args[1].s;
      ev.disc_model = msg.args[2].s;
      ev.disc_firmware = msg.args[3].s;
      ev.peer = from;
      PushControl(std::move(ev));
      continue;
    }

    // Strip value?
    StripId id;
    Param p;
    if (!ParseAddress(msg.address, &id, &p)) continue;
    if (msg.args.empty()) continue;  // a bare address echo carries no value
    double value;
    if (p == Param::Fader) {
      if (msg.args[0].type != OscArg::Type::Float) continue;
      value = static_cast<double>(msg.args[0].f);
    } else {  // mute / on: integer 1=ON/unmuted, 0=muted (X32 convention)
      if (msg.args[0].type == OscArg::Type::Int) {
        value = static_cast<double>(msg.args[0].i);
      } else if (msg.args[0].type == OscArg::Type::Float) {
        value = msg.args[0].f >= 0.5 ? 1.0 : 0.0;
      } else {
        continue;
      }
    }
    cache_->Set(id, p, value);
    StripEvent se{id, p, value};
    queue_->Push(se);
    ++emitted;
  }
  return emitted;
}

// --- State handlers ---------------------------------------------------------

void X32Connection::RunConnecting() {
  // Probe with a no-arg /info and wait for any reply.
  int64_t start = NowMs();
  last_rx_ = 0;  // reset; a reply after `start` means the console is alive
  SendQuery("/info");
  bool reprobed = false;
  while (running_.load()) {
    // Bail if a command arrived (handled by the outer loop).
    {
      std::lock_guard<std::mutex> lock(cmd_mu_);
      if (!cmds_.empty()) return;
    }
    int remaining = cfg_.info_timeout_ms - static_cast<int>(NowMs() - start);
    if (remaining <= 0) break;
    if (sock_.WaitReadable(remaining > 200 ? 200 : remaining)) {
      DrainIncoming();
      if (last_rx_ >= start) {
        backoff_idx_ = 0;
        SetState(ConnState::Syncing, "Connected — syncing");
        return;
      }
    }
    // Re-probe once, midway through the window, in case the first was lost.
    if (!reprobed && NowMs() - start > cfg_.info_timeout_ms / 2) {
      reprobed = true;
      SendQuery("/info");
    }
  }
  // Timed out. SetState(Lost, ...) below computes the backoff deadline from
  // backoff_idx_ as-is; do not increment here too (see SetState).
  SetState(ConnState::Lost, "No response — retrying");
}

void X32Connection::RunSyncing() {
  SendSyncSweep();
  if (!running_.load()) return;
  // Register for live pushes and enter LIVE.
  SendQuery("/xremote");
  last_xremote_ = NowMs();
  last_rx_ = NowMs();  // sweep replies count as activity
  last_repoll_ = NowMs();
  SetState(ConnState::Live, "Live");
}

void X32Connection::RunLive() {
  int64_t now = NowMs();

  // Keepalive.
  if (now - last_xremote_ >= cfg_.xremote_interval_ms) {
    SendQuery("/xremote");
    last_xremote_ = now;
  }

  // Optional periodic re-poll to recover from UDP loss.
  if (cfg_.repoll_interval_ms > 0 &&
      now - last_repoll_ >= cfg_.repoll_interval_ms) {
    SendSyncSweep();
    last_repoll_ = now;
  }

  // Receive.
  sock_.WaitReadable(200);
  DrainIncoming();

  now = NowMs();
  int64_t gap = now - last_rx_;
  if (gap > cfg_.live_lost_after_ms) {
    SetState(ConnState::Lost, "Connection lost — reconnecting");
    return;
  }
  if (gap > cfg_.live_probe_after_ms && now - last_probe_ >= 1000) {
    SendQuery("/info");  // nudge; a live console replies and resets the gap
    last_probe_ = now;
  }
}

void X32Connection::RunLost() {
  if (NowMs() >= backoff_deadline_) {
    if (backoff_idx_ < static_cast<int>(cfg_.backoff_ms.size()) - 1)
      ++backoff_idx_;
    SetState(ConnState::Connecting, "Reconnecting");
    return;
  }
  // Sleep in small slices so commands stay responsive.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void X32Connection::RunDiscover(uint16_t port) {
  // Broadcast /xinfo and let replies flow into DrainIncoming as Discovered
  // control events. This does not alter the connection state.
  if (!sock_.IsOpen()) return;
  sock_.EnableBroadcast(true);
  std::vector<uint8_t> pkt = OscEncodeQuery("/xinfo");
  for (const std::string& b : BroadcastAddresses()) {
    sock_.SendTo(b, port, pkt.data(), pkt.size());
  }
  sock_.EnableBroadcast(false);
  // Collect replies for a short window.
  int64_t start = NowMs();
  while (running_.load() && NowMs() - start < 1500) {
    if (sock_.WaitReadable(200)) DrainIncoming();
  }
}

void X32Connection::ThreadMain() {
  if (!sock_.Open()) {
    SetState(ConnState::Disconnected, "Socket open failed");
    running_.store(false);
    return;
  }

  while (running_.load()) {
    // Process pending commands first.
    Command c;
    while (PopCommand(&c)) {
      switch (c.type) {
        case CmdType::Connect: {
          {
            std::lock_guard<std::mutex> lock(peer_mu_);
            peer_.ip = c.ip;
            peer_.port = c.port;
          }
          backoff_idx_ = 0;
          SetState(ConnState::Connecting, "Connecting");
          break;
        }
        case CmdType::Disconnect:
          SetState(ConnState::Disconnected, "Disconnected");
          break;
        case CmdType::Resync:
          if (state_.load() == ConnState::Live ||
              state_.load() == ConnState::Syncing)
            SetState(ConnState::Syncing, "Resyncing");
          break;
        case CmdType::Discover:
          RunDiscover(c.port);
          break;
      }
    }

    switch (state_.load()) {
      case ConnState::Disconnected: {
        std::unique_lock<std::mutex> lock(cmd_mu_);
        if (cmds_.empty())
          cmd_cv_.wait_for(lock, std::chrono::milliseconds(200));
        break;
      }
      case ConnState::Connecting: RunConnecting(); break;
      case ConnState::Syncing:    RunSyncing();    break;
      case ConnState::Live:       RunLive();       break;
      case ConnState::Lost:       RunLost();       break;
    }
  }
  sock_.Close();
}

}  // namespace x32
