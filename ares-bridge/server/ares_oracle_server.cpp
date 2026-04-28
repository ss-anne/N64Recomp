/*
 * ares_oracle_server.cpp — standalone TCP-driven Ares oracle.
 *
 * Why a separate exe (not linked into a recomp consumer):
 *
 *   Embedding ares-bridge into a busy host process (e.g. Stadium's
 *   runner with RT64, multiple librecomp engine threads, an audio
 *   thread, and its own sljit usage in LiveRecomp) reliably stalls
 *   ares::Nintendo64::system.run() — Ares' libco cothread state
 *   (thread_local) and sljit allocator collide with the host's
 *   runtime. The standalone smoke produces 85k RSP events in 60
 *   frames; the same code linked into Stadium produces 0 events from
 *   60 frames of step_frame calls.
 *
 *   This server runs Ares in its own process. Recomp consumers
 *   connect over TCP and orchestrate frame stepping + trace-ring
 *   queries. Multiple consumers (Stadium + diff harness) can
 *   coexist without compromising Ares' state.
 *
 * Command surface mirrors what Stadium's debug_server exposes for
 * ares_* (so tools/diff_aspmain.py can switch transports without a
 * payload format change). Wire protocol: line-delimited JSON-ish.
 *
 *   ping
 *     → {"ok":true,"pong":true}
 *   status
 *     → {"ok":true,"is_real":1,"version":"...","trace_count":N,
 *        "trace_enabled":1,"trace_boot_count":M,"rom_path":"..."}
 *   step_frame {n}
 *     → {"ok":true,"frames":N,"status":0,"trace_count":M}
 *   rsp_trace_recent {n}
 *     → {"ok":true,"trace_count":M,"events":[{...},...]}
 *   rsp_trace_boot {start, n}
 *     → same shape
 *   rsp_trace_at_pc {pc, n}
 *     → same shape
 *   rsp_trace_set_enabled {on}
 *     → {"ok":true,"enabled":N}
 *   reset
 *     → {"ok":true,"status":0}
 *   read_memory {addr, len}      addr is hex (0x...) or dec; len <= 4096
 *     → {"ok":true,"addr":"0x...","len":N,"bytes":"hexhexhex..."}
 *   read_pc
 *     → {"ok":true,"pc":"0x..."}
 *   read_gpr {reg}               reg = 0..31 (MIPS R4300 register index)
 *     → {"ok":true,"reg":N,"value":"0x..."}
 *   quit
 *     → exits the server
 *
 * Single-threaded by design: all Ares calls run on the main thread
 * (the OS thread that started the process), so libco's thread_local
 * state is consistent throughout. Connection is single-client; if a
 * second client connects while one is active, the second waits.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>

#include "ares_bridge.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace {

const char* g_rom_path = "";

// ---- Tiny JSON-ish field extractors (same shape as debug_server) -----------

std::string get_str(const std::string& body, const char* key) {
    std::string needle = std::string("\"") + key + "\"";
    size_t k = body.find(needle);
    if (k == std::string::npos) return {};
    size_t colon = body.find(':', k);
    if (colon == std::string::npos) return {};
    size_t qa = body.find('"', colon);
    if (qa == std::string::npos) return {};
    size_t qb = body.find('"', qa + 1);
    if (qb == std::string::npos) return {};
    return body.substr(qa + 1, qb - qa - 1);
}

int get_int(const std::string& body, const char* key, int dflt) {
    std::string needle = std::string("\"") + key + "\"";
    size_t k = body.find(needle);
    if (k == std::string::npos) return dflt;
    size_t colon = body.find(':', k);
    if (colon == std::string::npos) return dflt;
    return std::atoi(body.c_str() + colon + 1);
}

uint32_t get_uint(const std::string& body, const char* key, uint32_t dflt) {
    std::string needle = std::string("\"") + key + "\"";
    size_t k = body.find(needle);
    if (k == std::string::npos) return dflt;
    size_t colon = body.find(':', k);
    if (colon == std::string::npos) return dflt;
    const char* p = body.c_str() + colon + 1;
    while (*p == ' ' || *p == '\t') p++;
    return (uint32_t)std::strtoul(p, nullptr, 0);
}

bool get_bool(const std::string& body, const char* key, bool dflt) {
    std::string needle = std::string("\"") + key + "\"";
    size_t k = body.find(needle);
    if (k == std::string::npos) return dflt;
    size_t colon = body.find(':', k);
    if (colon == std::string::npos) return dflt;
    const char* p = body.c_str() + colon + 1;
    while (*p == ' ' || *p == '\t') p++;
    if (std::strncmp(p, "true", 4) == 0)  return true;
    if (std::strncmp(p, "false", 5) == 0) return false;
    return std::atoi(p) != 0;
}

// ---- Command dispatch ------------------------------------------------------

std::string render_events(const std::vector<ares_rsp_trace_event_t>& events) {
    std::string out = std::string(R"({"ok":true,"trace_count":)") +
                      std::to_string(ares_rsp_trace_count()) +
                      R"(,"events":[)";
    for (size_t i = 0; i < events.size(); i++) {
        const auto& ev = events[i];
        char buf[1024];
        int off = std::snprintf(buf, sizeof(buf),
            "%s{\"seq\":%llu,\"pc\":%u,\"dma_mem_addr\":%u,"
            "\"dma_dram_addr\":%u,\"dma_rd_len\":%u,\"dma_wr_len\":%u,"
            "\"status\":%u,\"gpr\":[",
            (i ? "," : ""),
            (unsigned long long)ev.seq, (unsigned)ev.pc,
            (unsigned)ev.dma_mem_addr, (unsigned)ev.dma_dram_addr,
            (unsigned)ev.dma_rd_len, (unsigned)ev.dma_wr_len,
            (unsigned)ev.status);
        out.append(buf, (size_t)off);
        for (int g = 0; g < 32; g++) {
            char gbuf[16];
            int goff = std::snprintf(gbuf, sizeof(gbuf), "%s%u",
                (g ? "," : ""), (unsigned)ev.gpr[g]);
            out.append(gbuf, (size_t)goff);
        }
        out += "]}";
    }
    out += "]}";
    return out;
}

std::string handle(const std::string& line) {
    auto cmd = get_str(line, "cmd");
    if (cmd.empty()) {
        std::string bare = line;
        while (!bare.empty() && (bare.back() == '\n' ||
                                 bare.back() == '\r' ||
                                 bare.back() == ' ')) bare.pop_back();
        cmd = bare;
    }

    if (cmd == "ping") {
        return R"({"ok":true,"pong":true})";
    }
    if (cmd == "status") {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"is_real\":%d,\"version\":\"%s\","
            "\"trace_count\":%llu,\"trace_enabled\":%d,"
            "\"trace_boot_count\":%u,\"rom_path\":\"%s\"}",
            ares_bridge_is_real(),
            ares_bridge_version() ? ares_bridge_version() : "?",
            (unsigned long long)ares_rsp_trace_count(),
            ares_rsp_trace_is_enabled(),
            (unsigned)ares_rsp_trace_boot_count(),
            g_rom_path);
        return buf;
    }
    if (cmd == "reset") {
        ares_status_t r = ares_reset();
        char buf[96];
        std::snprintf(buf, sizeof(buf),
            "{\"ok\":%s,\"status\":%d}",
            (r == ARES_BRIDGE_OK ? "true" : "false"), (int)r);
        return buf;
    }
    if (cmd == "step_frame") {
        int n = get_int(line, "n", 1);
        if (n < 1) n = 1;
        if (n > 600) n = 600;
        int done = 0;
        ares_status_t last = ARES_BRIDGE_OK;
        for (int i = 0; i < n; i++) {
            last = ares_step_frame();
            if (last != ARES_BRIDGE_OK) break;
            done++;
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "{\"ok\":%s,\"frames\":%d,\"status\":%d,\"trace_count\":%llu}",
            (last == ARES_BRIDGE_OK ? "true" : "false"),
            done, (int)last,
            (unsigned long long)ares_rsp_trace_count());
        return buf;
    }
    if (cmd == "rsp_trace_recent" || cmd == "rsp_trace_boot" ||
        cmd == "rsp_trace_at_pc") {
        int n = get_int(line, "n", 32);
        if (n < 1) n = 1;
        if (n > 1024) n = 1024;
        std::vector<ares_rsp_trace_event_t> events;
        events.reserve((size_t)n);

        if (cmd == "rsp_trace_recent") {
            uint64_t total = ares_rsp_trace_count();
            if ((uint64_t)n > total) n = (int)total;
            for (int i = 0; i < n; i++) {
                uint64_t idx = total - (uint64_t)n + (uint64_t)i;
                ares_rsp_trace_event_t ev{};
                if (ares_rsp_trace_get(idx, &ev)) events.push_back(ev);
            }
        } else if (cmd == "rsp_trace_boot") {
            int start = get_int(line, "start", 0);
            if (start < 0) start = 0;
            uint32_t total = ares_rsp_trace_boot_count();
            for (int i = 0; i < n; i++) {
                uint32_t pos = (uint32_t)start + (uint32_t)i;
                if (pos >= total) break;
                ares_rsp_trace_event_t ev{};
                if (ares_rsp_trace_boot_get(pos, &ev)) events.push_back(ev);
            }
        } else { // rsp_trace_at_pc
            uint32_t want_pc = get_uint(line, "pc", 0) & 0xFFFu;
            uint64_t total = ares_rsp_trace_count();
            int scanned = 0;
            const int kScanCap = 65536;
            for (uint64_t i = total;
                 i-- > 0 && scanned < kScanCap && (int)events.size() < n; ) {
                ares_rsp_trace_event_t ev{};
                if (!ares_rsp_trace_get(i, &ev)) break;
                scanned++;
                if ((ev.pc & 0xFFFu) == want_pc) events.push_back(ev);
            }
            std::reverse(events.begin(), events.end());
        }
        return render_events(events);
    }
    if (cmd == "read_memory") {
        uint32_t addr = get_uint(line, "addr", 0);
        int len = get_int(line, "len", 0);
        if (len < 1 || len > 4096) {
            return R"({"ok":false,"error":"len out of range [1,4096]"})";
        }
        std::vector<uint8_t> bytes((size_t)len, 0);
        ares_status_t r = ares_read_memory(addr, bytes.data(), (size_t)len);
        if (r != ARES_BRIDGE_OK) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "{\"ok\":false,\"error\":\"ares_read_memory status=%d\","
                "\"addr\":\"0x%08x\",\"len\":%d}",
                (int)r, (unsigned)addr, len);
            return buf;
        }
        std::string out = R"({"ok":true,"addr":")";
        char abuf[16];
        std::snprintf(abuf, sizeof(abuf), "0x%08x", (unsigned)addr);
        out += abuf;
        out += R"(","len":)";
        out += std::to_string(len);
        out += R"(,"bytes":")";
        out.reserve(out.size() + (size_t)len * 2 + 8);
        for (int i = 0; i < len; i++) {
            char hb[3];
            std::snprintf(hb, sizeof(hb), "%02x", (unsigned)bytes[(size_t)i]);
            out.append(hb, 2);
        }
        out += R"("})";
        return out;
    }
    if (cmd == "read_pc") {
        uint32_t pc = 0;
        ares_status_t r = ares_read_pc(&pc);
        if (r != ARES_BRIDGE_OK) {
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                "{\"ok\":false,\"error\":\"ares_read_pc status=%d\"}", (int)r);
            return buf;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"pc\":\"0x%08x\"}", (unsigned)pc);
        return buf;
    }
    if (cmd == "read_gpr") {
        int reg = get_int(line, "reg", -1);
        if (reg < 0 || reg > 31) {
            return R"({"ok":false,"error":"reg out of range [0,31]"})";
        }
        uint64_t v = 0;
        ares_status_t r = ares_read_cpu_register(reg, &v);
        if (r != ARES_BRIDGE_OK) {
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                "{\"ok\":false,\"error\":\"ares_read_cpu_register status=%d\"}",
                (int)r);
            return buf;
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"reg\":%d,\"value\":\"0x%016llx\"}",
            reg, (unsigned long long)v);
        return buf;
    }
    if (cmd == "rsp_trace_set_enabled") {
        bool on = get_bool(line, "on", true);
        ares_rsp_trace_set_enabled(on ? 1 : 0);
        return std::string(R"({"ok":true,"enabled":)") +
               std::to_string(ares_rsp_trace_is_enabled()) + "}";
    }
    if (cmd == "quit") {
        ExitProcess(0);
        return R"({"ok":true})";
    }
    return R"({"ok":false,"error":"unknown command"})";
}

void serve(int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "WSAStartup failed\n");
        std::exit(1);
    }
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        std::fprintf(stderr, "socket() failed\n");
        std::exit(1);
    }
    BOOL reuse = TRUE;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
               (char*)&reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
    if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::fprintf(stderr, "bind() failed err=%d\n", WSAGetLastError());
        std::exit(1);
    }
    if (listen(listen_sock, 1) == SOCKET_ERROR) {
        std::fprintf(stderr, "listen() failed\n");
        std::exit(1);
    }
    std::fprintf(stderr,
        "ares_oracle_server: listening on 127.0.0.1:%d\n", port);
    std::fflush(stderr);

    while (true) {
        sockaddr_in caddr{};
        int caddr_len = sizeof(caddr);
        SOCKET client = accept(listen_sock, (sockaddr*)&caddr, &caddr_len);
        if (client == INVALID_SOCKET) continue;

        std::fprintf(stderr,
            "ares_oracle_server: client connected\n");
        std::fflush(stderr);

        std::string buf;
        buf.reserve(1024);
        char chunk[1024];
        while (true) {
            int n = recv(client, chunk, sizeof(chunk), 0);
            if (n <= 0) break;
            buf.append(chunk, chunk + n);
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                std::string resp = handle(line);
                resp += "\n";
                send(client, resp.c_str(), (int)resp.size(), 0);
            }
        }
        closesocket(client);
        std::fprintf(stderr,
            "ares_oracle_server: client disconnected\n");
        std::fflush(stderr);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "usage: %s <rom_path> [port=4372]\n", argv[0]);
        return 1;
    }
    g_rom_path = argv[1];
    int port = (argc >= 3) ? std::atoi(argv[2]) : 4372;

    if (!ares_bridge_is_real()) {
        std::fprintf(stderr,
            "ares_oracle_server: build is placeholder-only "
            "(rebuild with WITH_ARES_BRIDGE=ON)\n");
        return 1;
    }

    std::fprintf(stderr,
        "ares_oracle_server: %s\n", ares_bridge_version());
    std::fflush(stderr);

    ares_status_t s = ares_init(g_rom_path);
    if (s != ARES_BRIDGE_OK) {
        std::fprintf(stderr,
            "ares_oracle_server: ares_init failed (status=%d)\n", (int)s);
        return 1;
    }
    s = ares_reset();
    if (s != ARES_BRIDGE_OK) {
        std::fprintf(stderr,
            "ares_oracle_server: ares_reset failed (status=%d)\n", (int)s);
        return 1;
    }
    std::fprintf(stderr,
        "ares_oracle_server: oracle initialized + reset, "
        "ROM=%s\n", g_rom_path);
    std::fflush(stderr);

    serve(port);
    return 0;
}
