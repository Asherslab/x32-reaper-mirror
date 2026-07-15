// X32 → REAPER Mirror
// X32Connection: owns the UDP socket, the connection state machine and the
// background socket thread. This is the ONLY place that ever transmits to the
// console, and by construction it transmits nothing but no-argument OSC
// messages (queries, /xremote, /info, /xinfo). There is no code path here that
// encodes a value — that is the one-way guarantee, enforced structurally.
//
// Threading contract: the socket thread touches only the socket, the OSC
// codec, the StateCache, the CoalescingQueue and its own command/control
// queues. It never calls a REAPER API. The main thread communicates purely
// through the thread-safe command and control queues.
#ifndef X32MIRROR_X32_CONNECTION_H_
#define X32MIRROR_X32_CONNECTION_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "event_queue.h"
#include "state_cache.h"
#include "udp_socket.h"

namespace x32 {

enum class ConnState : int {
  Disconnected = 0,
  Connecting,
  Syncing,
  Live,
  Lost,
};

const char* ConnStateName(ConnState s);

// Tunable timings. Defaults match the plan; the integration test tightens them
// so a LIVE→LOST→LIVE cycle happens in seconds rather than tens of seconds.
struct ConnConfig {
  int info_timeout_ms = 2000;        // CONNECTING: wait for first reply
  int xremote_interval_ms = 5000;    // LIVE: re-send /xremote cadence
  int live_probe_after_ms = 3000;    // LIVE: start /info probes after silence
  int live_lost_after_ms = 9000;     // LIVE: declare LOST after this silence
  int sync_pace_us = 2000;           // SYNCING: gap between no-arg queries
  int repoll_interval_ms = 0;        // LIVE: optional periodic full re-poll (0=off)
  std::vector<int> backoff_ms = {1000, 2000, 4000, 8000, 15000};
};

class X32Connection {
 public:
  X32Connection(StateCache* cache, CoalescingQueue* queue,
                ConnConfig cfg = ConnConfig());
  ~X32Connection();

  X32Connection(const X32Connection&) = delete;
  X32Connection& operator=(const X32Connection&) = delete;

  // Launch / stop the socket thread. Stop signals and joins within ~500 ms.
  void Start();
  void Stop();

  // --- Commands (callable from the main thread) ---------------------------
  void Connect(const std::string& ip, uint16_t port);
  void Disconnect();
  void Resync();                       // re-run the paced sync sweep
  void Discover(uint16_t port = 10023);  // /xinfo broadcast scan

  // --- Control events drained on the main thread --------------------------
  struct ControlEvent {
    enum class Type { StateChanged, Discovered } type = Type::StateChanged;
    ConnState state = ConnState::Disconnected;
    std::string status_text;
    Endpoint peer;
    // Discovery payload (Type::Discovered): /xinfo reply fields.
    std::string disc_ip, disc_name, disc_model, disc_firmware;
  };
  // Pop one control event; returns false if none pending.
  bool PollControl(ControlEvent* out);

  ConnState state() const { return state_.load(); }
  Endpoint peer() const;

 private:
  enum class CmdType { Connect, Disconnect, Resync, Discover };
  struct Command {
    CmdType type;
    std::string ip;
    uint16_t port = 0;
  };

  void ThreadMain();

  // State handlers (socket thread only). Each returns the next state.
  void RunConnecting();
  void RunSyncing();
  void RunLive();
  void RunLost();
  void RunDiscover(uint16_t port);

  // The ONLY transmit primitive. Sends a no-argument OSC message to the given
  // address, restricted to an allowlist. Never encodes a value → one-way.
  bool SendQuery(const std::string& address);
  bool SendQueryTo(const std::string& ip, uint16_t port,
                   const std::string& address);

  // Drain all readable datagrams, decode, update caches/queue. Returns the
  // number of strip events emitted. Updates last_rx_ on any datagram.
  int DrainIncoming();

  // Emit the paced no-arg query sweep for every in-scope strip parameter.
  void SendSyncSweep();

  void SetState(ConnState s, const std::string& status);
  void PushControl(ControlEvent ev);
  bool PopCommand(Command* out);

  int64_t NowMs() const;

  StateCache* cache_;
  CoalescingQueue* queue_;
  ConnConfig cfg_;

  UdpSocket sock_;
  NetInit net_;

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<ConnState> state_{ConnState::Disconnected};

  // Target endpoint (guarded by peer_mu_).
  mutable std::mutex peer_mu_;
  Endpoint peer_;

  // Command queue (main → socket).
  std::mutex cmd_mu_;
  std::deque<Command> cmds_;
  std::condition_variable cmd_cv_;

  // Control queue (socket → main).
  std::mutex ctl_mu_;
  std::deque<ControlEvent> ctls_;

  int64_t last_rx_ = 0;         // steady clock ms of last datagram
  int64_t last_xremote_ = 0;    // last /xremote keepalive
  int64_t last_probe_ = 0;      // last /info silence probe
  int64_t last_repoll_ = 0;     // last optional full re-poll
  int64_t backoff_deadline_ = 0;
  int backoff_idx_ = 0;
};

}  // namespace x32

#endif  // X32MIRROR_X32_CONNECTION_H_
