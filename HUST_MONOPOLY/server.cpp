// The game engine as a small local HTTP server: Godot (or anything else) sends the JSON
// requests of PROTOCOL.md and gets the JSON responses back.
//
//   Linux / macOS:  g++ -std=c++17 -O2 server.cpp protocol.cpp GameLogic.cpp -o server -pthread
//   Windows MinGW:  g++ -std=c++17 -O2 server.cpp protocol.cpp GameLogic.cpp -o server -lws2_32 -lwsock32
//
//   ./server                       listens on http://127.0.0.1:8080
//   ./server --port 9000
//   ./server --host 0.0.0.0        also reachable from your LAN (see the note below)
//   ./server --dev                 lets new_game take a "seed" (reproducible games; dev only)
//
// Endpoints:  POST /api  (body = one JSON request, Content-Type: application/json)
//             GET  /health
//
// Safety, because a server on localhost is reachable by every web page you open:
//  * POST /api only accepts Content-Type: application/json. A web page on another site
//    cannot send that without a CORS preflight, and this server never answers preflights.
//  * While bound to the loopback address, requests whose Host header is not localhost /
//    127.0.0.1 / [::1] are refused (this blocks DNS-rebinding attacks).
//  * Bodies over 4096 bytes are refused; one request at a time touches the engine.
//  * There are no accounts or seats yet: with --host 0.0.0.0 anyone on your network can send
//    actions for the current player. Fine for a game night, not for the internet.
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>

#include "protocol.h"
#include "third_party/httplib.h"

namespace {

const char* kForbiddenHost = R"({"ok":false,"error":"forbidden_host","message":"Host header must be localhost"})";
const char* kBadMedia = R"({"ok":false,"error":"unsupported_media_type","message":"use Content-Type: application/json"})";

// "localhost:8080", "127.0.0.1", "[::1]:8080" ...
bool hostIsLocal(const std::string& header) {
  std::string h = header;
  if (!h.empty() && h[0] == '[') {
    const size_t end = h.find(']');
    h = (end == std::string::npos) ? h : h.substr(0, end + 1);
  } else {
    const size_t colon = h.find(':');
    if (colon != std::string::npos) h = h.substr(0, colon);
  }
  return h == "127.0.0.1" || h == "localhost" || h == "[::1]";
}

bool isJsonContentType(const std::string& contentType) {
  return contentType.rfind("application/json", 0) == 0;  // allows "; charset=utf-8"
}

std::string errorBody(const std::string& code, const std::string& message) {
  return std::string(R"({"ok":false,"error":")") + code + R"(","message":")" + message + R"("})";
}

}  // namespace

int main(int argc, char** argv) {
  std::string host = "127.0.0.1";
  int port = 8080;
  bool dev = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--host" && i + 1 < argc) host = argv[++i];
    else if (arg == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (arg == "--dev") dev = true;
    else {
      std::cerr << "unknown argument: " << arg << "\n";
      return 2;
    }
  }
  if (port < 1 || port > 65535) {
    std::cerr << "port must be 1..65535\n";
    return 2;
  }

  Engine engine;
  engine.allowSeed = dev;
  std::mutex engineMutex;  // the engine is not thread-safe; requests take turns

  const bool loopbackOnly = (host == "127.0.0.1" || host == "localhost" || host == "::1");

  httplib::Server svr;
  svr.set_payload_max_length(4096);
  svr.set_read_timeout(5, 0);
  svr.set_write_timeout(5, 0);

  svr.set_pre_routing_handler([loopbackOnly](const httplib::Request& req, httplib::Response& res) {
    if (loopbackOnly && !hostIsLocal(req.get_header_value("Host"))) {
      res.status = 403;
      res.set_content(kForbiddenHost, "application/json");
      return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(R"({"ok":true})", "application/json");
  });

  svr.Post("/api", [&engine, &engineMutex](const httplib::Request& req, httplib::Response& res) {
    if (!isJsonContentType(req.get_header_value("Content-Type"))) {
      res.status = 415;
      res.set_content(kBadMedia, "application/json");
      return;
    }
    std::string reply;
    {
      std::lock_guard<std::mutex> lock(engineMutex);
      reply = handleRequest(engine, req.body);  // game-level errors come back as ok:false, HTTP 200
    }
    res.set_content(reply, "application/json");
  });

  // 404, 413 (body too large), 405 ... all answer in the same JSON shape.
  svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
    const char* code = res.status == 404 ? "not_found" : res.status == 413 ? "request_too_large" : "http_error";
    res.set_content(errorBody(code, "HTTP " + std::to_string(res.status)), "application/json");
  });

  std::cout << "HUST Monopoly server on http://" << host << ":" << port << "  (POST /api, GET /health)\n";
  if (dev) std::cout << "DEV mode: new_game accepts a seed.\n";
  if (!loopbackOnly) std::cout << "WARNING: reachable from other machines, and there is no login.\n";
  std::cout << "Press Ctrl+C to stop." << std::endl;

  if (!svr.listen(host, port)) {
    std::cerr << "could not listen on " << host << ":" << port << " (port already in use?)\n";
    return 1;
  }
  return 0;
}
