// Headless integration test: drives the real X32Connection state machine
// against a live x32sim.py on loopback. Exercises SYNC → LIVE, a live push,
// and a LIVE → LOST → LIVE reconnect cycle. POSIX-only (spawns python3); the
// CMake target is gated to non-Windows accordingly.
//
// Usage: integration_x32conn <path-to-x32sim.py> [port]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "event_queue.h"
#include "state_cache.h"
#include "x32_addresses.h"
#include "x32_connection.h"

using namespace x32;

namespace {

int g_fail = 0;
#define IEXPECT(cond, msg)                                        \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL: %s\n", msg);                             \
      ++g_fail;                                                   \
    } else {                                                      \
      std::printf("ok:   %s\n", msg);                             \
    }                                                             \
  } while (0)

void SleepMs(int ms) { usleep(ms * 1000); }

// A running sim child, with a pipe to its stdin so we can inject stimuli.
struct SimProc {
  pid_t pid = -1;
  int stdin_fd = -1;
};

SimProc SpawnSim(const std::string& script, int port) {
  int pipefd[2];
  if (pipe(pipefd) != 0) return {};
  pid_t pid = fork();
  if (pid == 0) {
    // Child: wire read end to stdin, exec python.
    dup2(pipefd[0], STDIN_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    char portbuf[16];
    std::snprintf(portbuf, sizeof(portbuf), "%d", port);
    execlp("python3", "python3", script.c_str(), "--port", portbuf, "--listen",
           "127.0.0.1", "--quiet", (char*)nullptr);
    // If exec fails:
    std::perror("execlp python3");
    _exit(127);
  }
  close(pipefd[0]);
  SimProc s;
  s.pid = pid;
  s.stdin_fd = pipefd[1];
  return s;
}

void SimSend(SimProc& s, const std::string& line) {
  if (s.stdin_fd < 0) return;
  std::string l = line + "\n";
  ssize_t n = write(s.stdin_fd, l.data(), l.size());
  (void)n;
}

void KillSim(SimProc& s) {
  if (s.pid > 0) {
    kill(s.pid, SIGKILL);
    int status = 0;
    waitpid(s.pid, &status, 0);
    s.pid = -1;
  }
  if (s.stdin_fd >= 0) {
    close(s.stdin_fd);
    s.stdin_fd = -1;
  }
}

// Ask the sim to exit cleanly and assert it reports success (exit code 0).
// The sim exits nonzero if it ever observed a value-carrying set message, so
// this is what actually enforces the plan's automated one-way guarantee.
void QuitSimAndCheck(SimProc& s) {
  SimSend(s, "quit");
  int status = 0;
  pid_t pid = s.pid;
  s.pid = -1;
  waitpid(pid, &status, 0);
  if (s.stdin_fd >= 0) {
    close(s.stdin_fd);
    s.stdin_fd = -1;
  }
  IEXPECT(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "sim exited 0 on quit (one-way guarantee never violated)");
}

bool WaitForState(X32Connection& conn, ConnState want, int timeout_ms) {
  int waited = 0;
  while (waited < timeout_ms) {
    if (conn.state() == want) return true;
    SleepMs(50);
    waited += 50;
  }
  return conn.state() == want;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: %s <x32sim.py> [port]\n", argv[0]);
    return 2;
  }
  std::string script = argv[1];
  int port = argc >= 3 ? std::atoi(argv[2]) : 10023;

  // Tightened timings so a full LIVE→LOST→LIVE cycle runs in a few seconds.
  ConnConfig cfg;
  cfg.info_timeout_ms = 1500;
  cfg.xremote_interval_ms = 1000;
  cfg.live_probe_after_ms = 600;
  cfg.live_lost_after_ms = 1500;
  cfg.sync_pace_us = 200;
  cfg.backoff_ms = {400, 400, 800};

  StateCache cache;
  CoalescingQueue queue;
  X32Connection conn(&cache, &queue, cfg);

  SimProc sim = SpawnSim(script, port);
  IEXPECT(sim.pid > 0, "spawned x32sim.py");
  SleepMs(400);  // let it bind

  conn.Start();
  conn.Connect("127.0.0.1", static_cast<uint16_t>(port));

  IEXPECT(WaitForState(conn, ConnState::Live, 6000), "reached LIVE after sync");

  // The sync sweep should have populated the cache from the sim's defaults.
  double v = -1;
  IEXPECT(cache.Get({StripType::CH, 1}, Param::Fader, &v) && v > 0.7 && v < 0.8,
          "cache seeded /ch/01 fader (~0.75) from sync");

  // Drain the initial burst, then inject a live change and observe it.
  std::vector<StripEvent> evs;
  queue.Drain(&evs);
  SimSend(sim, "set /ch/01/mix/fader 0.5");

  bool saw_change = false;
  for (int i = 0; i < 40 && !saw_change; ++i) {
    SleepMs(50);
    queue.Drain(&evs);
    for (auto& e : evs) {
      if (e.id == StripId{StripType::CH, 1} && e.param == Param::Fader &&
          e.value > 0.49 && e.value < 0.51) {
        saw_change = true;
      }
    }
  }
  IEXPECT(saw_change, "received live fader push (0.5) via /xremote");

  // Mute push (X32: 0 = muted).
  SimSend(sim, "mute /ch/02/mix/on 0");
  bool saw_mute = false;
  for (int i = 0; i < 40 && !saw_mute; ++i) {
    SleepMs(50);
    queue.Drain(&evs);
    for (auto& e : evs) {
      if (e.id == StripId{StripType::CH, 2} && e.param == Param::Mute &&
          e.value < 0.5) {
        saw_mute = true;
      }
    }
  }
  IEXPECT(saw_mute, "received live mute push (on=0) via /xremote");

  // Force a disconnect: kill the sim; the connection must detect silence.
  KillSim(sim);
  IEXPECT(WaitForState(conn, ConnState::Lost, 5000), "detected LOST after sim died");

  // Bring the sim back; the connection must reconnect on its own.
  SleepMs(300);
  sim = SpawnSim(script, port);
  IEXPECT(sim.pid > 0, "respawned x32sim.py");
  IEXPECT(WaitForState(conn, ConnState::Live, 8000), "auto-reconnected to LIVE");

  conn.Stop();
  QuitSimAndCheck(sim);

  std::printf("\n%s (%d failures)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail);
  return g_fail == 0 ? 0 : 1;
}
