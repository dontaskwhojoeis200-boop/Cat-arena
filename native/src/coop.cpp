// N8: see include/cat/coop.hpp. Line numbers refer to legacy/index.html.
#include "cat/coop.hpp"

#include "cat/js_math.hpp"
#include "cat/relay.hpp"

#include <algorithm>
#include <cmath>
#include <regex>

namespace cat::net {
namespace {
constexpr double kMapSize = 117;   // SIZE
double clampd(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
// netSafeNumber(v, def, lo, hi)
double safe(const json::Value& v, double def, double lo = -9999, double hi = 9999) { return v.finite_number() ? clampd(v.as_number(), lo, hi) : def; }
double q3(double v) { return std::isfinite(v) ? std::floor(v * 1000 + .5) / 1000 : v; }   // netQ: Math.round(v * 1000) / 1000
double q2(double v) { return std::isfinite(v) ? std::floor(v * 100 + .5) / 100 : v; }
std::string clean_name(const std::string& raw, std::size_t limit) {   // .replace(/[^\w .-]/g, '')
    std::string s;
    for(char c : raw) if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ' ' || c == '.' || c == '-') s += c;
    if(s.size() > limit) s.resize(limit);
    return s;
}
std::string trim(std::string s) {
    while(!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
    while(!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}
int type_from(const json::Value& v) {
    if(!v.is_string()) return -1;
    return combat::enemy_type_index(v.as_string().c_str());
}
const char* type_name(int type) { return type >= 0 && type < static_cast<int>(combat::kEnemyTypeCount) ? combat::enemy_types()[static_cast<std::size_t>(type)].name : "grunt"; }
}  // namespace

Coop::Coop() { startup(); }
Coop::~Coop() { leave(); }

// ---- lobby -----------------------------------------------------------------------------------------------
void Coop::host(const std::string& relay_typed, const std::string& name, const std::string& title, const std::string& map_id) { connect("host", relay_typed, {}, name, title, map_id); }
void Coop::join(const std::string& relay_typed, const std::string& code, const std::string& name, const std::string& map_id) { connect("join", relay_typed, code, name, {}, map_id); }
void Coop::connect(const std::string& role, const std::string& relay_typed, const std::string& code_in, const std::string& name, const std::string& title, const std::string& map_id) {   // netConnect (15591)
    if(busy()) return;
    const auto addr = relay_address(relay_typed);
    if(!addr.ws) { set_status(addr.error, true); return; }
    std::string code;
    for(char c : code_in) code += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
    if(role == "join" && !std::regex_match(code, std::regex("^[A-HJ-NP-Z2-9]{6}$"))) { set_status("Select an active server from the list.", true); return; }
    name_ = clean_name(trim(name).substr(0, 20), 20);
    if(name_.empty()) name_ = "Marine Cat";
    std::string t = trim(title);
    title_ = clean_name(t.empty() ? name_ + "'s Server" : t, 32);
    role_wanted_ = role; pending_code_ = code; map_id_ = map_id;
    conn_ = net::connect(*addr.ws, 18000);
    mode_ = Mode::connecting;
    stats_ = {};
    set_status(role == "host" ? "Creating your server..." : "Joining selected server...");
}
void Coop::leave(const std::string& why) {   // netDisconnectPopup
    if(conn_) { conn_->close(); conn_.reset(); }
    const bool was = in_room();
    mode_ = Mode::solo;
    players_.clear(); room_.clear(); peer_id_.clear(); slot_ = 0; loading_queue_.clear();
    revive_for_.clear(); revive_time_ = 0; wipe_ = 0; you_downed_ = false; last_world_ = -1;
    if(arena_) { arena_->set_role(combat::NetRole::solo); for(const auto& m : std::vector<combat::Teammate>(arena_->team())) arena_->remove_teammate(m.key); }
    if(horde_) horde_->set_puppet(false);
    arena_ = nullptr; horde_ = nullptr;
    if(!why.empty()) { set_status(why, true); if(was) events_.push_back({CoopEvent::Kind::disconnected, why}); }
}
std::vector<std::string> Coop::roster() const {
    std::vector<std::string> list{name_ + " (you)"};
    for(const auto& p : players_) list.push_back(p.name.empty() ? "Marine Cat" : p.name);
    return list;
}
RemotePlayer* Coop::player(const std::string& id) { for(auto& p : players_) if(p.id == id) return &p; return nullptr; }
void Coop::send(const json::Value& v) {
    if(!conn_) return;
    const std::string text = json::dump(v);
    if(conn_->send(text)) stats_.bytes_sent += text.size();
}
void Coop::relay(const json::Value& data, const std::string& to) {
    json::Value m; m.set("t", "relay");
    if(!to.empty()) m.set("to", to);
    m.set("data", data);
    send(m);
}

void Coop::apply_roster(const json::Array& list) {   // netApplyRoster (15542)
    std::vector<std::string> ids;
    for(const auto& peer : list) {
        if(!peer.is_object() || !peer["id"].is_string() || peer["id"].as_string() == self_id_) continue;
        const std::string id = peer["id"].as_string();
        ids.push_back(id);
        RemotePlayer* p = player(id);
        if(!p) { players_.push_back({}); p = &players_.back(); p->id = id; p->name = "Marine Cat"; }
        if(peer["name"].is_string()) p->name = peer["name"].as_string().substr(0, 20);
        p->is_host = id == peer_id_;
        if(mode_ == Mode::host && arena_ && !arena_->teammate(p->key)) p->key = arena_->add_teammate(p->id, p->name).key;
    }
    for(std::size_t i = players_.size(); i-- > 0;) {
        if(std::find(ids.begin(), ids.end(), players_[i].id) != ids.end()) continue;
        if(mode_ == Mode::host && arena_ && players_[i].key) arena_->remove_teammate(players_[i].key);
        players_.erase(players_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    if(mode_ == Mode::guest) if(const RemotePlayer* h = player(peer_id_)) peer_name_ = h->name;
}

void Coop::handle(const json::Value& packet, double now_ms) {   // socket.onmessage (15612)
    const std::string t = packet["t"].as_string();
    if(t == "error") {
        const std::string message = packet["message"].is_string() ? packet["message"].as_string() : "Server refused the request.";
        const bool was = in_room();
        if(conn_) { conn_->close(); conn_.reset(); }
        if(was) leave(message); else { mode_ = Mode::solo; set_status(message, true); }
        return;
    }
    if(t == "ready") {
        mode_ = packet["role"].as_string() == "host" ? Mode::host : Mode::guest;
        room_ = packet["code"].as_string(); self_id_ = packet["id"].as_string();
        slot_ = static_cast<int>(std::max(0.0, std::min(3.0, packet["slot"].as_number())));
        peer_id_ = packet["peer"]["id"].as_string(); peer_name_ = packet["peer"]["name"].is_string() ? packet["peer"]["name"].as_string() : "Partner";
        json::Array list = packet["players"].as_array();
        if(list.empty() && packet["peer"].is_object()) list.push_back(packet["peer"]);
        apply_roster(list);
        set_status(mode_ == Mode::host ? "Server is online. Friends can find it in the server browser." : "Server joined. Loading arena...");
        CoopEvent e{CoopEvent::Kind::ready};
        e.map_id = map_id_;
        events_.push_back(e);
        last_world_ = now_ms;
        return;
    }
    if(t == "roster") { apply_roster(packet["players"].as_array()); return; }
    if(t == "peer") {
        const std::string event = packet["event"].as_string();
        const json::Value& p = packet["peer"];
        if(event == "guest_joined" && p["id"].is_string() && p["id"].as_string() != self_id_ && in_room()) {
            RemotePlayer* r = player(p["id"].as_string());
            if(!r) { players_.push_back({}); r = &players_.back(); r->id = p["id"].as_string(); }
            r->name = p["name"].is_string() ? p["name"].as_string() : "Marine Cat";
            if(mode_ == Mode::host) {
                if(arena_ && !arena_->teammate(r->key)) r->key = arena_->add_teammate(r->id, r->name).key;
                json::Value m; m.set("type", "map"); m.set("map", 0); m.set("host", self_id_);   // netSendMapToGuest
                relay(m, r->id);
            }
            say(r->name + " JOINED THE ARENA", true);
        } else if(event == "guest_left") {
            if(p["id"].is_string()) {
                if(RemotePlayer* r = player(p["id"].as_string())) {
                    if(mode_ == Mode::host && arena_ && r->key) arena_->remove_teammate(r->key);
                    players_.erase(players_.begin() + (r - players_.data()));
                }
            }
            say("TEAMMATE LEFT · ROOM STILL OPEN");
        } else if(event == "host_left" && mode_ == Mode::guest) {
            leave("Host disconnected. This server is closed. Select another from the server browser.");
        }
        return;
    }
    if(t == "relay" && packet["data"].is_object()) {
        if(!arena_) {   // loadingNetRelayQueue
            if(loading_queue_.size() >= 60) loading_queue_.pop_front();
            loading_queue_.emplace_back(packet["from"].as_string(), packet["data"]);
        } else on_relay(packet["from"].as_string(), packet["data"], now_ms);
    }
}

void Coop::attach(combat::Arena* arena, combat::Horde* horde) {
    arena_ = arena; horde_ = horde;
    if(!arena_) return;
    arena_->set_role(mode_ == Mode::host ? combat::NetRole::host : mode_ == Mode::guest ? combat::NetRole::guest : combat::NetRole::solo);
    if(horde_) horde_->set_puppet(mode_ == Mode::guest);
    if(mode_ == Mode::host) for(auto& p : players_) if(!arena_->teammate(p.key)) p.key = arena_->add_teammate(p.id, p.name).key;
    if(mode_ == Mode::guest) {   // the host's world replaces anything the local wave start spawned (no death animations for those)
        revive_for_.clear(); revive_time_ = 0; wipe_ = 0; you_downed_ = false;
        arena_->clear_targets();
    }
    while(!loading_queue_.empty()) { auto [from, data] = loading_queue_.front(); loading_queue_.pop_front(); on_relay(from, data, now_ms_); }
}

// ---- the game packets ------------------------------------------------------------------------------------------
json::Value Coop::pose_message() const {   // netPose
    json::Value m;
    const auto& p = arena_->player().state();
    m.set("type", "pose"); m.set("x", p.x); m.set("y", p.y); m.set("z", p.z); m.set("a", p.a); m.set("pitch", p.pitch);
    m.set("vx", p.vx); m.set("vy", p.vy); m.set("c", p.crouch_offset); m.set("step", arena_->player().view().bob_cycle);
    m.set("holdingRevive", arena_->holding_revive());
    return m;
}
json::Value Coop::hero_local() const {   // netHero(player)
    const auto& p = arena_->player().state();
    json::Value h;
    h.set("x", q3(p.x)); h.set("y", q3(p.y)); h.set("z", q3(p.z)); h.set("a", q3(p.a)); h.set("pitch", q3(p.pitch)); h.set("vx", q3(p.vx)); h.set("vy", q3(p.vy));
    h.set("hp", q2(arena_->health())); h.set("downed", p.downed); h.set("step", q3(arena_->player().view().bob_cycle));
    return h;
}
json::Value Coop::hero(const combat::Teammate& t) const {
    json::Value h;
    h.set("x", q3(t.x)); h.set("y", q3(t.y)); h.set("z", q3(t.z)); h.set("a", q3(t.a)); h.set("pitch", q3(t.pitch)); h.set("vx", q3(t.vx)); h.set("vy", q3(t.vy));
    h.set("hp", q2(t.hp)); h.set("downed", t.downed); h.set("step", q3(t.step));
    return h;
}
std::string Coop::world_message(const std::string& guest_id) const {   // netWorldShared + netWorldGuestPart + netSnapshotMessage (15903-15931)
    json::Value d;
    d.set("type", "world"); d.set("map", 0);
    d.set("wave", horde_ ? horde_->wave_number() : 1); d.set("total", horde_ ? horde_->wave_total() : 0);
    d.set("kills", horde_ ? horde_->wave_kills() : 0); d.set("spawned", horde_ ? horde_->wave_spawned() : 0);
    d.set("cooldown", q3(horde_ ? horde_->wave_cooldown() : -1));
    json::Array pickups;
    for(const auto& k : arena_->pickups()) { if(pickups.size() >= 64) break; json::Value e; e.set("x", q3(k.x)); e.set("y", q3(k.y)); e.set("z", q3(k.z)); e.set("cool", q2(k.cool)); pickups.push_back(e); }
    d.set("pickups", pickups);
    json::Array bots;
    for(const auto& b : arena_->targets()) {
        if(b.type < 0) continue;
        if(bots.size() >= 40) break;
        json::Value e;
        e.set("u", b.uid); e.set("t", type_name(b.type)); e.set("x", q3(b.x)); e.set("y", q3(b.y)); e.set("z", q3(b.z)); e.set("a", q3(b.a));
        e.set("h", q2(b.hp)); e.set("maxHp", q2(b.max_hp)); e.set("phase", q3(b.anim_phase)); e.set("walk", q3(b.walk_amount)); e.set("hit", q3(b.hit_flash));
        e.set("wind", q3(b.attack_wind)); e.set("totalWind", q3(b.attack_total)); e.set("recover", q3(b.attack_recover)); e.set("totalRecover", q3(b.attack_recover_total));
        bots.push_back(e);
    }
    d.set("bots", bots);
    json::Array shots;
    if(horde_) for(const auto& s : horde_->shots()) {
        if(shots.size() >= 100) break;
        json::Value e;
        e.set("x", q3(s.x)); e.set("y", q3(s.y)); e.set("z", q3(s.z)); e.set("vx", q3(s.vx)); e.set("vy", q3(s.vy)); e.set("vz", q3(s.vz)); e.set("life", q3(s.life));
        e.set("kind", combat::shot_kind_name(s.kind)); e.set("e", type_name(s.type));
        shots.push_back(e);
    }
    d.set("shots", shots);
    json::Array players;
    {
        json::Value me = hero_local();
        json::Value h; h.set("id", self_id_); h.set("name", name_);
        for(const auto& kv : me.as_object()) h.set(kv.first, kv.second);
        players.push_back(h);
    }
    const combat::Teammate* viewer = nullptr;
    for(const auto& t : arena_->team()) {
        if(t.id == guest_id) { viewer = &t; continue; }
        json::Value h; h.set("id", t.id); h.set("name", t.name);
        const json::Value body = hero(t);
        for(const auto& kv : body.as_object()) h.set(kv.first, kv.second);
        players.push_back(h);
    }
    d.set("players", players);
    d.set("you", viewer ? hero(*viewer) : json::Value());
    // reviveFor: the relay id of the player being revived ('' none)
    std::string revive_for;
    const int target = arena_->revive_target();
    if(target == 0) revive_for = self_id_;
    else if(target > 0) for(const auto& t : arena_->team()) if(static_cast<int>(t.key) == target) revive_for = t.id;
    d.set("reviveFor", revive_for); d.set("reviveTime", q3(arena_->revive_seconds())); d.set("wipe", q3(arena_->wipe_clock()));
    json::Value m; m.set("t", "relay"); m.set("to", guest_id); m.set("data", d);
    return json::dump(m);
}

void Coop::on_relay(const std::string& from, const json::Value& d, double now_ms) {   // netOnRelay (15679)
    if(!arena_) return;
    const std::string type = d["type"].as_string();
    if(mode_ == Mode::host) {
        RemotePlayer* r = player(from);
        combat::Teammate* m = r ? arena_->teammate(r->key) : nullptr;
        if(!m) return;
        if(type == "pose") {
            ++stats_.poses;
            const double x = safe(d["x"], m->x, 0, kMapSize), y = safe(d["y"], m->y, 0, kMapSize), rz = safe(d["z"], m->z, -4.5, 28);
            const double crouch = safe(d["c"], 0, 0, .6);
            const double valid_height = std::max(physics::kPlayerCrouchHeight, physics::kPlayerHeight - crouch * .60);
            const double since = now_ms - (m->pose_ms >= 0 ? m->pose_ms : now_ms);
            const double max_move = m->pose_ms >= 0 ? std::max(3.0, std::min(22.0, since * .027 + 2)) : INFINITY;
            const double delta = std::hypot(std::hypot(x - m->x, y - m->y), rz - m->z);
            if(!m->downed && delta <= max_move && arena_->world().capsule_clear(x, y, rz, valid_height, physics::kPlayerRadius)) {
                m->pose_ms = now_ms;
                m->x = x; m->y = y; m->z = rz; m->crouch = crouch;
                m->vx = safe(d["vx"], 0, -25, 25); m->vy = safe(d["vy"], 0, -25, 25);
            }
            m->a = safe(d["a"], m->a, -1e4, 1e4); m->pitch = safe(d["pitch"], 0, -1.11, 1.11);
            m->holding_revive = d["holdingRevive"].truthy(); m->step = safe(d["step"], 0, 0, 30000);
            return;
        }
        if(type == "fire") arena_->queue_remote_fire(m->key, static_cast<int>(d["weapon"].as_number(-1)));   // netHostRemoteFire, at the next tick
        return;
    }
    if(mode_ != Mode::guest || from != peer_id_) return;   // guests take game state from the host only
    if(type == "map") return;   // the room's map is fixed (the legacy index 0..2 is a layout variant of the old engine)
    if(type == "world") {
        last_world_ = now_ms;
        std::vector<combat::Pickup> pickups;
        for(const auto& p : d["pickups"].as_array()) {
            if(pickups.size() >= 64) break;
            if(!p["x"].finite_number() || !p["y"].finite_number()) continue;
            combat::Pickup k;
            k.x = p["x"].as_number(); k.y = p["y"].as_number();
            k.z = safe(p["z"], arena_->world().floor_height(k.x, k.y, 0), -5, 22);
            k.cool = std::max(0.0, p["cool"].finite_number() ? p["cool"].as_number() : 0.0);
            pickups.push_back(k);
        }
        arena_->set_pickups(std::move(pickups));
        combat::WorldState w;
        w.wave = static_cast<int>(d["wave"].as_number()); w.total = static_cast<int>(d["total"].as_number());
        w.kills = static_cast<int>(d["kills"].as_number()); w.spawned = static_cast<int>(d["spawned"].as_number());
        w.cooldown = safe(d["cooldown"], -1);
        for(const auto& p : d["bots"].as_array()) {
            if(w.bots.size() >= 40) break;
            combat::PuppetBot b;
            b.uid = static_cast<std::uint32_t>(p["u"].as_number());
            const int type_index = type_from(p["t"]);
            b.type = type_index >= 0 ? type_index : 0;
            b.x = safe(p["x"], 0); b.y = safe(p["y"], 0); b.z = safe(p["z"], 0); b.a = safe(p["a"], 0);
            b.hp = safe(p["h"], 100); b.max_hp = p["maxHp"].finite_number() ? p["maxHp"].as_number() : 100;
            b.phase = safe(p["phase"], 0); b.walk = safe(p["walk"], 0); b.hit = safe(p["hit"], 0);
            b.wind = safe(p["wind"], 0); b.total_wind = safe(p["totalWind"], 0); b.recover = safe(p["recover"], 0); b.total_recover = safe(p["totalRecover"], 0);
            w.bots.push_back(b);
        }
        for(const auto& s : d["shots"].as_array()) {
            if(w.shots.size() >= 100) break;
            combat::PuppetShot e;
            e.x = s["x"].as_number(); e.y = s["y"].as_number(); e.z = s["z"].as_number();
            e.vx = s["vx"].as_number(); e.vy = s["vy"].as_number(); e.vz = s["vz"].as_number(); e.life = s["life"].as_number();
            e.kind = combat::shot_kind_from(s["kind"].as_string()).value_or(combat::ShotKind::shot);
            e.type = type_from(s["e"]);
            w.shots.push_back(e);
        }
        if(horde_) horde_->queue_world(std::move(w));
        // other players (the host first)
        std::vector<std::string> keep;
        for(const auto& info : d["players"].as_array()) {
            if(!info["id"].is_string() || info["id"].as_string() == self_id_) continue;
            const std::string id = info["id"].as_string();
            keep.push_back(id);
            RemotePlayer* r = player(id);
            if(!r) { players_.push_back({}); r = &players_.back(); r->id = id; }
            if(info["name"].is_string()) r->name = info["name"].as_string().substr(0, 20);
            r->x = safe(info["x"], r->x); r->y = safe(info["y"], r->y); r->z = safe(info["z"], r->z); r->a = safe(info["a"], r->a);
            r->pitch = safe(info["pitch"], 0); r->vx = safe(info["vx"], 0); r->vy = safe(info["vy"], 0); r->hp = safe(info["hp"], r->hp, 0, 100);
            r->downed = info["downed"].truthy(); r->step = safe(info["step"], 0);
            r->placed = r->x != 0 || r->y != 0;
            r->is_host = id == peer_id_;
            if(r->is_host) { r->color = {.82F, 1.F, .87F}; r->name = peer_name_; }
        }
        players_.erase(std::remove_if(players_.begin(), players_.end(), [&](const RemotePlayer& p) { return std::find(keep.begin(), keep.end(), p.id) == keep.end(); }), players_.end());
        if(d["you"].is_object()) {
            const bool was = arena_->player().state().downed;
            arena_->set_health(safe(d["you"]["hp"], arena_->health(), 0, 100));
            const bool down = d["you"]["downed"].truthy();
            arena_->set_local_downed(down);
            if(down && !was) say("YOU ARE DOWN · WAIT FOR REVIVE");
            if(!down && was) say("REVIVED! BACK IN THE FIGHT", true);
            you_downed_ = down;
        }
        revive_for_ = d["reviveFor"].as_string(); revive_time_ = safe(d["reviveTime"], 0, 0, 4); wipe_ = safe(d["wipe"], 0, 0, 5);
        return;
    }
    if(type == "restart") {
        events_.push_back({CoopEvent::Kind::restart});
        say("SQUAD WIPED · ROUND 1 RESTART");
        return;
    }
    if(type == "frag") { arena_->queue_frag(d["head"].truthy()); return; }
    if(type == "shotFx") {
        const std::string shooter = d["shooter"].as_string();
        if(!shooter.empty() && shooter == self_id_) return;
        RemotePlayer* r = shooter.empty() ? player(peer_id_) : player(shooter);   // a host volley names no shooter in the legacy client
        CoopEvent e{CoopEvent::Kind::remote_volley};
        e.weapon = static_cast<int>(d["weapon"].as_number(-1));
        if(r) { r->shoot_until = arena_->now() + .14; e.x = r->x; e.y = r->y; e.z = r->z; }
        events_.push_back(e);
    }
}

// ---- per frame / per tick ---------------------------------------------------------------------------------------
void Coop::poll(double now_ms) {
    now_ms_ = now_ms;
    if(!conn_) return;
    std::vector<std::string> in;
    conn_->poll(in);
    stats_.bytes_received = conn_->bytes_received();
    if(mode_ == Mode::connecting && conn_->state() == Connection::State::open && !role_wanted_.empty()) {   // socket.onopen
        set_status(std::string("Server connected. ") + (role_wanted_ == "host" ? "Creating a 4-player server..." : "Joining server..."));
        json::Value m;
        m.set("t", role_wanted_ == "host" ? "create" : "join"); m.set("code", pending_code_); m.set("name", name_); m.set("title", title_);
        m.set("build", kBuildId); m.set("mapId", map_id_);
        role_wanted_.clear();
        send(m);
    }
    for(const auto& text : in) if(auto v = json::try_parse(text)) handle(*v, now_ms);
    if(!conn_) return;
    if(conn_->state() == Connection::State::closed) {   // socket.onclose / onerror
        const std::string why = conn_->error();
        if(in_room()) leave("Disconnected from the multiplayer service. Rooms are reset if the relay restarts." + (why.empty() ? std::string() : " (" + why + ")"));
        else { conn_.reset(); mode_ = Mode::solo; set_status("Cannot reach the relay: " + (why.empty() ? std::string("connection closed") : why), true); }
        return;
    }
    if(!arena_) return;
    if(mode_ == Mode::guest) {   // networkTick (15935)
        if(now_ms - last_pose_ > kPoseIntervalMs) { last_pose_ = now_ms; relay(pose_message()); }
        if(last_world_ >= 0 && now_ms - last_world_ > kSnapshotStallMs) set_status("Host snapshots delayed. Verify all players are in the same room.", true);
    } else if(mode_ == Mode::host && !arena_->team().empty() && now_ms - last_world_sent_ > kWorldIntervalMs) {
        last_world_sent_ = now_ms;
        for(const auto& t : arena_->team()) {
            const std::string msg = world_message(t.id);
            if(conn_->send(msg)) { stats_.bytes_sent += msg.size(); stats_.snapshot_bytes += msg.size(); }
        }
        ++stats_.snapshots;
    }
    sync_host_players();
}
void Coop::sync_host_players() {   // host: display state of the other players from the simulation
    if(mode_ != Mode::host || !arena_) return;
    for(auto& p : players_) if(const combat::Teammate* t = arena_->teammate(p.key)) {
        p.x = t->x; p.y = t->y; p.z = t->z; p.a = t->a; p.pitch = t->pitch; p.vx = t->vx; p.vy = t->vy; p.step = t->step; p.hp = t->hp; p.downed = t->downed;
        p.shoot_until = t->shoot_until; p.placed = t->pose_ms >= 0;
    }
}
void Coop::after_step() {
    if(!arena_ || !in_room()) return;
    const auto& e = arena_->events();
    if(e.fired_weapon >= 0) {
        json::Value m;
        if(mode_ == Mode::guest) { m.set("type", "fire"); m.set("weapon", e.fired_weapon); relay(m); }   // fireWeapon (12062)
        else { m.set("type", "shotFx"); m.set("from", "host"); m.set("weapon", e.fired_weapon); m.set("shooter", self_id_); relay(m); }   // (12063) + shooter
    }
    if(mode_ != Mode::host) return;
    auto name_of = [&](std::uint32_t key) -> std::string { if(const auto* t = arena_->teammate(key)) return t->name; return "Marine Cat"; };
    for(const auto& f : e.remote_frags) if(const auto* t = arena_->teammate(f.key)) { json::Value m; m.set("type", "frag"); m.set("head", f.head); relay(m, t->id); }
    for(const auto& s : e.remote_shots) if(const auto* t = arena_->teammate(s.key)) { json::Value m; m.set("type", "shotFx"); m.set("weapon", s.weapon); m.set("shooter", t->id); relay(m); }
    for(const auto key : e.downed) say(key == 0 ? "YOU ARE DOWN · TEAMMATE MUST REVIVE YOU" : name_of(key) + " IS DOWN!");
    for(const auto key : e.revived) say((key == 0 ? std::string("YOU") : name_of(key)) + " REVIVED!", true);
    sync_host_players();
}
void Coop::round_restarted() {
    if(mode_ != Mode::host) return;
    json::Value m; m.set("type", "restart"); m.set("map", 0);
    relay(m);
    say("SQUAD ELIMINATED · WAVE 1 RESTART");
}

std::array<double, 3> Coop::find_spawn(const physics::CollisionWorld& world, double bx, double by, double bz, int slot) const {   // netFindValidSpawn (15523)
    const double offset = slot * 1.55;
    for(int ring = 0; ring < 9; ++ring) for(int spoke = 0; spoke < 16; ++spoke) {
        const double a = spoke * 3.141592653589793 / 8 + offset, r = ring * 1.45 + 1.8;
        const double x = bx + jsmath::cos(a) * r, y = by + jsmath::sin(a) * r;
        const auto sf = world.support(x, y, bz + .55, .6);   // a floor of the base's deck (levelFloorSurfacesAt … |z| < .6)
        if(!sf || std::fabs(sf->z - bz) >= .6) continue;
        if(world.capsule_clear(x, y, sf->z, physics::kPlayerHeight, physics::kPlayerRadius)) return {x, y, sf->z};
    }
    return {bx, by, bz};
}

std::string Coop::revive_status() const {   // the coopReviveStatus text (15950-15964)
    if(!arena_ || !in_room()) return {};
    const auto& me = arena_->player().state();
    const double seconds = mode_ == Mode::host ? arena_->revive_seconds() : revive_time_;
    const double wipe = mode_ == Mode::host ? arena_->wipe_clock() : wipe_;
    std::string target_id;
    if(mode_ == Mode::host) {
        const int t = arena_->revive_target();
        if(t == 0) target_id = self_id_;
        else if(t > 0) for(const auto& m : arena_->team()) if(static_cast<int>(m.key) == t) target_id = m.id;
    } else target_id = revive_for_;
    const RemotePlayer* mate = nullptr;
    double best = INFINITY;
    for(const auto& p : players_) if(p.downed && arena_->near_revive(me.x, me.y, me.z, me.downed, p.x, p.y, p.z, p.downed)) {
        const double d = std::hypot(p.x - me.x, p.y - me.y);
        if(d < best) { best = d; mate = &p; }
    }
    const int pct = static_cast<int>(std::floor(seconds / combat::kReviveSeconds * 100 + .5));
    if(wipe > 0) return "SQUAD ELIMINATED · RESTARTING ROUND";
    if(me.downed) return target_id == self_id_ ? "BEING REVIVED · " + std::to_string(pct) + "%" : "YOU ARE DOWN · WAIT FOR ANY TEAMMATE TO HOLD F FOR 4 SECONDS";
    if(mate) return target_id == mate->id ? "REVIVING " + mate->name + " · " + std::to_string(pct) + "%" : "HOLD F TO REVIVE " + mate->name + " · 4 SECONDS";
    return {};
}
std::string Coop::badge() const {   // netSetBadge
    if(!in_room()) return {};
    return std::string(mode_ == Mode::host ? "HOSTING" : "CONNECTED") + " · " + std::to_string(std::min(kRoomSize, players_in_room())) + "/" + std::to_string(kRoomSize) + " PLAYERS";
}
Coop::Stats Coop::stats() const {
    Stats s = stats_;
    s.last_world_age_ms = last_world_ >= 0 ? now_ms_ - last_world_ : -1;
    return s;
}

// ---- server list ----------------------------------------------------------------------------------------------------
ServerList fetch_servers(const std::string& relay_typed, int timeout_ms) {   // netBrowseServers (15997)
    ServerList out;
    const auto addr = relay_address(relay_typed);
    if(!addr.rooms) { out.status = addr.error; return out; }
    const auto r = http_get(*addr.rooms, timeout_ms);
    if(!r.error.empty()) { out.status = "Cannot reach the server directory: " + r.error; return out; }
    if(r.status != 200) { out.status = "Server directory unavailable (HTTP " + std::to_string(r.status) + ")."; return out; }
    const auto data = json::try_parse(r.body);
    if(!data || (*data)["version"].as_number() != 1 || !(*data)["rooms"].is_array() || (*data)["build"].as_string() != kBuildId) { out.status = "The server is running an incompatible version of Cat Arena."; return out; }
    int players = 0;
    for(const auto& e : (*data)["rooms"].as_array()) {
        if(out.rooms.size() >= 100) break;
        if(!e["id"].is_string() || !std::regex_match(e["id"].as_string(), std::regex("^[A-HJ-NP-Z2-9]{6}$"))) continue;
        ServerEntry s;
        s.id = e["id"].as_string();
        s.title = e["title"].is_string() ? e["title"].as_string().substr(0, 50) : "Unnamed server";
        s.host = e["host"].is_string() ? e["host"].as_string().substr(0, 20) : "Marine Cat";
        s.players = static_cast<int>(std::max(0.0, std::min(4.0, e["players"].as_number())));
        s.open = e["open"].truthy();
        s.map_id = e["mapId"].as_string();
        players += s.players;
        out.rooms.push_back(s);
    }
    out.ok = true;
    out.status = std::to_string(out.rooms.size()) + " LIVE SERVER" + (out.rooms.size() == 1 ? "" : "S") + " · " + std::to_string(players) + " PLAYERS ONLINE";
    return out;
}

}  // namespace cat::net
