// Serves the real firmware's web page and /api from a PC, with the firmware running against
// the simulated Ninja 300 in real time. For checking the page in a browser:
//   g++ -std=gnu++17 -Imock -o /tmp/webserve webserve.cpp && /tmp/webserve 8080
#include "sim.cpp"
#include "../firmware/obdojo/obdojo_fw.cpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>

static std::string urldec(const std::string &s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+') o += ' ';
    else if (s[i] == '%' && i + 2 < s.size()) { o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
    else o += s[i];
  }
  return o;
}
static uint64_t wallUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char **argv) {
  int port = argc > 1 ? atoi(argv[1]) : 8080;
  bike.rpm = 1350; bike.tps = 215; bike.ect = 78; bike.cur = {{0x13, 0x04}}; bike.sto = {{0x13, 0x04}, {0x56, 0x01}};
  setup();
  int srv = socket(AF_INET, SOCK_STREAM, 0), one = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in a = {};
  a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(srv, (sockaddr *)&a, sizeof(a)) || listen(srv, 8)) { perror("bind"); return 1; }
  printf("serving on http://127.0.0.1:%d\n", port);
  fflush(stdout);
  uint64_t w0 = wallUs(), s0 = g_us;
  for (;;) {
    // keep simulated time in step with the wall clock, and wiggle the bike a little
    uint64_t target = s0 + (wallUs() - w0);
    while (g_us < target) {
      float t = g_us / 1e6f;
      bike.rpm = 1350 + 250 * sinf(t * 0.7f);
      bike.tps = 215 + (int)(40 * (1 + sinf(t * 1.3f)));
      loop();
    }
    pollfd p = {srv, POLLIN, 0};
    if (poll(&p, 1, 5) <= 0) continue;
    int c = accept(srv, nullptr, nullptr);
    if (c < 0) continue;
    std::string req;
    char buf[4096];
    for (int tries = 0; tries < 50; tries++) {
      int n = recv(c, buf, sizeof(buf), 0);
      if (n <= 0) break;
      req.append(buf, n);
      size_t he = req.find("\r\n\r\n");
      if (he != std::string::npos) {
        size_t cl = req.find("Content-Length: ");
        size_t need = cl != std::string::npos && cl < he ? atoi(req.c_str() + cl + 16) : 0;
        if (req.size() >= he + 4 + need) break;
      }
    }
    std::string first = req.substr(0, req.find("\r\n"));
    std::string path = first.substr(first.find(' ') + 1);
    path = path.substr(0, path.find(' '));
    std::string query, body = req.find("\r\n\r\n") != std::string::npos ? req.substr(req.find("\r\n\r\n") + 4) : "";
    if (path.find('?') != std::string::npos) { query = path.substr(path.find('?') + 1); path = path.substr(0, path.find('?')); }
    std::string args = body.size() ? body : query;
    web.curArg = args.rfind("c=", 0) == 0 ? urldec(args.substr(2)) : "";
    web.out.clear();
    auto it = web.routes.find(path);
    if (it != web.routes.end()) it->second(); else if (web.nf) web.nf();
    std::string ct = path == "/" ? "text/html" : path.find(".csv") != std::string::npos ? "text/csv" :
                     path == "/icon.svg" ? "image/svg+xml" : "application/json";
    std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: " + ct + "\r\nContent-Length: " + std::to_string(web.out.size()) +
                       "\r\nConnection: close\r\n\r\n" + web.out;
    send(c, resp.data(), resp.size(), 0);
    close(c);
  }
}
