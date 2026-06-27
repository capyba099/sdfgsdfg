#include <cstdio>
#include <cstring>
#include <string>
#include "Game.h"
#include "Net.h"

// ---------------------------------------------------------------------------
// Networking integration: a simple authoritative TCP client-server.
//
// The host runs the full simulation and broadcasts state snapshots ~30 Hz.
// Each connected client takes over one bot slot, sending its input ~60 Hz.
// Works directly on a LAN by IP; for play across the internet the host must
// forward the TCP port (or use a VPN such as Tailscale/Hamachi/Radmin).
// ---------------------------------------------------------------------------

namespace {
constexpr uint8_t MSG_WELCOME = 1;
constexpr uint8_t MSG_INPUT = 2;
constexpr uint8_t MSG_STATE = 3;

struct W {
    std::string b;
    void u8(uint8_t v) { b.push_back((char)v); }
    void i16(int16_t v) { b.append((const char*)&v, 2); }
    void i32(int32_t v) { b.append((const char*)&v, 4); }
    void f32(float v) { b.append((const char*)&v, 4); }
};

struct R {
    const char* p;
    int len;
    int off = 0;
    bool ok(int n) const { return off + n <= len; }
    uint8_t u8() { return (uint8_t)p[off++]; }
    int16_t i16() { int16_t v; std::memcpy(&v, p + off, 2); off += 2; return v; }
    int32_t i32() { int32_t v; std::memcpy(&v, p + off, 4); off += 4; return v; }
    float f32() { float v; std::memcpy(&v, p + off, 4); off += 4; return v; }
};

int sendFrame(net::Socket s, const std::string& payload) {
    uint16_t len = (uint16_t)payload.size();
    char hdr[2];
    std::memcpy(hdr, &len, 2);
    if (net::sendAll(s, hdr, 2) < 0) return -1;
    return net::sendAll(s, payload.data(), (int)payload.size());
}

// Pull one complete length-prefixed frame out of a receive buffer.
bool nextFrame(std::string& buf, std::string& out) {
    if (buf.size() < 2) return false;
    uint16_t len;
    std::memcpy(&len, buf.data(), 2);
    if (buf.size() < (size_t)2 + len) return false;
    out.assign(buf.data() + 2, len);
    buf.erase(0, (size_t)2 + len);
    return true;
}

Audio::Sfx shotSfxFor(WeaponId w) {
    switch (w) {
        case WeaponId::Pistol: return Audio::ShotPistol;
        case WeaponId::Smg: return Audio::ShotSmg;
        case WeaponId::Sniper: return Audio::ShotSniper;
        case WeaponId::Knife: return Audio::Knife;
        default: return Audio::ShotRifle;
    }
}
}  // namespace

struct Game::NetState {
    net::Socket listenSock = net::INVALID;
    struct Client { net::Socket sock; int actor; std::string rx; };
    std::vector<Client> clients;
    net::Socket sock = net::INVALID;   // client side
    std::string rx;                    // client side receive buffer
    float sendTimer = 0.0f;
    bool prevMuzzle[64] = {};
    int prevHp[64] = {};
    int snaps = 0;                     // snapshots applied (debug)
};

void Game::startHost() {
    shutdownNet();
    if (!net::init()) { netStatus_ = "NET INIT FAILED"; return; }
    net::Socket l = net::listenOn((uint16_t)joinPort_);
    if (l == net::INVALID) {
        netStatus_ = "CANNOT BIND PORT (ALREADY IN USE?)";
        net::cleanup();
        return;
    }
    netMode_ = NetMode::Host;
    localIndex_ = 0;
    spectIdx_ = 0;
    startMatch();
    net_ = new NetState();
    net_->listenSock = l;
    netStatus_.clear();
}

void Game::startClient(const std::string& ip, int port) {
    shutdownNet();
    if (!net::init()) { netStatus_ = "NET INIT FAILED"; return; }
    net::Socket s = net::connectTo(ip.c_str(), (uint16_t)port, 3000);
    if (s == net::INVALID) {
        netStatus_ = "CONNECT FAILED - CHECK IP / PORT";
        net::cleanup();
        return;
    }

    // Wait briefly for the server's welcome message.
    std::string buf, frame;
    int gotIndex = -1, gotN = 0;
    Uint32 start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 3000) {
        char tmp[512];
        int n = net::recvSome(s, tmp, sizeof(tmp));
        if (n > 0) buf.append(tmp, n);
        else if (n == 0) break;  // closed
        while (nextFrame(buf, frame)) {
            R r{frame.data(), (int)frame.size()};
            if (r.u8() == MSG_WELCOME && r.ok(8)) {
                gotIndex = r.i32();
                gotN = r.i32();
            }
        }
        if (gotIndex >= 0) break;
        SDL_Delay(10);
    }
    if (gotIndex < 0) {
        net::closeSock(s);
        net::cleanup();
        netStatus_ = "NO RESPONSE FROM HOST";
        return;
    }

    netMode_ = NetMode::Client;
    startMatch();  // allocate matching actor array + initial state
    (void)gotN;
    localIndex_ = (gotIndex >= 0 && gotIndex < (int)actors_.size()) ? gotIndex : 0;
    spectIdx_ = localIndex_;
    net_ = new NetState();
    net_->sock = s;
    net_->rx = buf;  // keep any bytes already received
    phase_ = Phase::Freeze;
    phaseTimer_ = cfg::FREEZE_TIME;
    netStatus_.clear();
}

void Game::shutdownNet() {
    if (net_) {
        if (net_->listenSock != net::INVALID) net::closeSock(net_->listenSock);
        for (auto& cl : net_->clients) net::closeSock(cl.sock);
        if (net_->sock != net::INVALID) net::closeSock(net_->sock);
        delete net_;
        net_ = nullptr;
        net::cleanup();
    }
    netMode_ = NetMode::Single;
    for (auto& a : actors_) { a.remote = false; a.connected = false; }
}

std::string Game::netBuildSnapshot() const {
    W w;
    w.u8(MSG_STATE);
    w.u8((uint8_t)phase_);
    w.f32(phaseTimer_);
    w.i16((int16_t)ctScore_);
    w.i16((int16_t)tScore_);
    uint8_t bf = (bombPlanted_ ? 1 : 0) | (bombDefused_ ? 2 : 0) |
                 (bombCarried_ ? 4 : 0);
    w.u8(bf);
    w.f32(bombPos_.x);
    w.f32(bombPos_.y);
    w.f32(bombTimer_);
    w.u8((uint8_t)actors_.size());
    for (const auto& a : actors_) {
        w.f32(a.pos.x);
        w.f32(a.pos.y);
        w.f32(a.aim);
        w.i16((int16_t)a.hp);
        w.u8(a.team == Team::T ? 1 : 0);
        uint8_t fl = (a.alive ? 1 : 0) | (a.hasBomb ? 2 : 0) |
                     (a.muzzleFlash > 0 ? 4 : 0);
        w.u8(fl);
        w.u8((uint8_t)a.weapon);
        w.i16((int16_t)a.kills);
        w.i16((int16_t)a.deaths);
        w.i32(a.money);
    }
    return w.b;
}

void Game::netApplyInput(int idx, const char* data, int len) {
    R r{data, len};
    if (r.u8() != MSG_INPUT || !r.ok(12)) return;
    float aim = r.f32();
    int btn = r.i32();
    int wsel = r.i32();
    if (idx >= 0 && idx < (int)actors_.size()) {
        actors_[idx].aim = aim;
        actors_[idx].netButtons = btn;
        actors_[idx].netWeaponSel = wsel;
    }
}

void Game::netApplySnapshot(const char* data, int len) {
    R r{data, len};
    if (r.u8() != MSG_STATE) return;
    phase_ = (Phase)r.u8();
    phaseTimer_ = r.f32();
    ctScore_ = r.i16();
    tScore_ = r.i16();
    uint8_t bf = r.u8();
    bombPlanted_ = bf & 1;
    bombDefused_ = bf & 2;
    bombCarried_ = bf & 4;
    bombPos_.x = r.f32();
    bombPos_.y = r.f32();
    bombTimer_ = r.f32();
    int n = r.u8();
    if ((int)actors_.size() < n) actors_.resize(n);
    for (int i = 0; i < n; ++i) {
        if (!r.ok(27)) break;
        float x = r.f32(), y = r.f32(), aim = r.f32();
        int16_t hp = r.i16();
        uint8_t team = r.u8();
        uint8_t fl = r.u8();
        uint8_t wp = r.u8();
        int16_t k = r.i16();
        int16_t d = r.i16();
        int32_t money = r.i32();
        Actor& a = actors_[i];
        Vec2 newPos{x, y};
        Vec2 delta = newPos - a.pos;
        a.vel = delta * 60.0f;          // approximate, drives footsteps/anim
        a.anim += delta.length() * 0.06f;
        a.pos = newPos;
        if (i != localIndex_) a.aim = aim;   // keep local aim responsive
        a.team = team ? Team::T : Team::CT;
        a.hasBomb = (fl & 2) != 0;
        a.alive = (fl & 1) != 0;
        bool muzzle = (fl & 4) != 0;
        a.weapon = (WeaponId)wp;
        a.kills = k;
        a.deaths = d;
        a.money = money;
        if (i < 64 && net_) {
            if (muzzle && !net_->prevMuzzle[i]) {
                a.muzzleFlash = 0.06f;
                playAt(shotSfxFor(a.weapon), a.pos, 1.0f);
            }
            net_->prevMuzzle[i] = muzzle;
            if (hp < net_->prevHp[i] && i == localIndex_) damageFlash_ = 0.55f;
            net_->prevHp[i] = hp;
        }
        a.hp = hp;
    }
}

void Game::netHostTick(float dt) {
    if (!net_) return;

    // Accept new clients, assigning each to a free bot slot.
    net::Socket c;
    while ((c = net::acceptOne(net_->listenSock)) != net::INVALID) {
        int slot = -1;
        for (size_t i = 0; i < actors_.size(); ++i)
            if ((int)i != localIndex_ && !actors_[i].connected) {
                slot = (int)i;
                break;
            }
        if (slot < 0) { net::closeSock(c); continue; }  // server full
        actors_[slot].remote = true;
        actors_[slot].connected = true;
        actors_[slot].netButtons = 0;
        actors_[slot].netWeaponSel = 0;
        W w;
        w.u8(MSG_WELCOME);
        w.i32(slot);
        w.i32((int)actors_.size());
        sendFrame(c, w.b);
        net_->clients.push_back({c, slot, std::string()});
        netStatus_ = "PLAYER JOINED";
        if (headless_)
            std::printf("[host] client joined -> actor slot %d (%d clients)\n",
                        slot, (int)net_->clients.size());
    }

    // Read inputs from each client.
    for (size_t ci = 0; ci < net_->clients.size();) {
        auto& cl = net_->clients[ci];
        char tmp[4096];
        int n;
        bool closed = false;
        while ((n = net::recvSome(cl.sock, tmp, sizeof(tmp))) > 0)
            cl.rx.append(tmp, n);
        if (n == 0) closed = true;
        std::string frame;
        while (nextFrame(cl.rx, frame))
            if (!frame.empty() && (uint8_t)frame[0] == MSG_INPUT)
                netApplyInput(cl.actor, frame.data(), (int)frame.size());
        if (closed) {
            int a = cl.actor;
            if (a >= 0 && a < (int)actors_.size()) {
                actors_[a].connected = false;
                actors_[a].remote = false;
                actors_[a].netButtons = 0;
            }
            net::closeSock(cl.sock);
            net_->clients.erase(net_->clients.begin() + ci);
            continue;
        }
        ++ci;
    }

    // Broadcast a snapshot ~30 Hz.
    net_->sendTimer -= dt;
    if (net_->sendTimer <= 0) {
        net_->sendTimer = 1.0f / 30.0f;
        std::string snap = netBuildSnapshot();
        for (auto& cl : net_->clients) sendFrame(cl.sock, snap);
    }
}

void Game::netClientTick(float dt) {
    if (!net_) return;

    // Send local input ~60 Hz.
    net_->sendTimer -= dt;
    if (net_->sendTimer <= 0) {
        net_->sendTimer = 1.0f / 60.0f;
        W w;
        w.u8(MSG_INPUT);
        w.f32(player().aim);
        w.i32(player().alive ? gatherLocalButtons() : 0);
        w.i32(0);
        if (sendFrame(net_->sock, w.b) < 0) {
            netStatus_ = "DISCONNECTED";
            shutdownNet();
            phase_ = Phase::MainMenu;
            menuScreen_ = MenuScreen::Main;
            return;
        }
    }

    // Receive and apply snapshots.
    char tmp[8192];
    int n;
    bool closed = false;
    while ((n = net::recvSome(net_->sock, tmp, sizeof(tmp))) > 0)
        net_->rx.append(tmp, n);
    if (n == 0) closed = true;
    std::string frame;
    while (nextFrame(net_->rx, frame))
        if (!frame.empty() && (uint8_t)frame[0] == MSG_STATE) {
            netApplySnapshot(frame.data(), (int)frame.size());
            if (headless_ && (++net_->snaps % 60) == 1) {
                const Actor& e = actors_[localIndex_ == 0 ? 1 : 0];
                std::printf("[client] snap#%d phase=%d CT %d:%d T  me=(%.0f,%.0f) other=(%.0f,%.0f) alive=%d\n",
                            net_->snaps, (int)phase_, ctScore_, tScore_,
                            player().pos.x, player().pos.y, e.pos.x, e.pos.y,
                            (int)player().alive);
            }
        }

    if (closed) {
        netStatus_ = "HOST CLOSED THE GAME";
        shutdownNet();
        phase_ = Phase::MainMenu;
        menuScreen_ = MenuScreen::Main;
    }
}
