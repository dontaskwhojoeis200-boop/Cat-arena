// N3 player controller: the legacy movement model at a fixed 1/120 s tick.
// Statement order and floating-point expression order follow the legacy functions named in each
// comment (legacy/index.html) so that recorded reference trajectories can be replayed.
#include "cat/physics.hpp"
#include "cat/js_math.hpp"
#include <algorithm>
#include <cmath>

namespace cat::physics {
namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kQ3Unit = kPlayerHeight / 56;   // Q3_UNIT: Quake units -> world units
// Q3_VIEW (10457)
constexpr double kBobUp = .005, kBobPitch = .002, kBobRoll = .002, kRunPitch = .002, kRunRoll = .005;
constexpr double kBobRun = .4, kBobSprint = .5, kBobCrouch = .5;
constexpr double kLandDeflect = .150, kLandReturn = .300, kStepTime = .200, kMaxStep = 32, kGunUnit = .09;
double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }   // legacy clamp()
}  // namespace

void PlayerController::spawn(double x, double y, double z, double yaw, double pitch, double now) {
    p_ = PlayerState{};
    p_.x = x; p_.y = y; p_.z = z; p_.a = yaw; p_.pitch = pitch; p_.ground = true;
    t_ = MoveTimers{};
    contacts_ = Contacts{};
    const double scale = q3_.bob_scale;
    q3_ = ViewState{};   // q3Reset()
    q3_.bob_scale = scale;
    keys_ = 0; stick_x_ = stick_y_ = 0; now_ = now; slope_angle_ = 0; fov_scale_ = 1; air_strafe_gained_ = 0; stance_ = Stance::run;
    pad_launches_ = 0; jumps_ = 0;
}

void PlayerController::respawn(double x, double y, double z, double yaw) {   // finishPendingRespawn (12101-12106)
    p_.x = x; p_.y = y; p_.z = z; p_.a = yaw; p_.vx = p_.vy = p_.vz = 0; p_.pitch = 0; p_.ground = true; p_.crouch_offset = 0;
    q3_.bob_cycle = 0; q3_.bobfracsin = 0; q3_.bob_odd = 0; q3_.xyspeed = 0; q3_.crouched = false; q3_.land_change = 0; q3_.land_time = -99;
    q3_.step_change = 0; q3_.step_time = -99; q3_.has_last_z = false; q3_.last_ground = false;   // q3Reset()
    t_.slide_time = 0; t_.slide_cooldown = 0; t_.slide_buffer = 0; t_.jump_buffer = 0; t_.coyote_time = 0;
}

void PlayerController::step(const TickInput& in) {
    body_events_.clear();
    // Key transitions first, in key-bit order, as non-repeating keydown/keyup events would arrive
    // between two simulation ticks; then mouse look; then the tick.
    const std::uint32_t changed = in.keys ^ keys_;
    for(std::uint32_t bit = 1; bit <= key_slide; bit <<= 1)
        if(changed & bit) key_event(static_cast<Key>(bit), (in.keys & bit) != 0);
    if(in.look_dx != 0 || in.look_dy != 0) aim(in.look_dx, in.look_dy);
    stick_x_ = in.stick_x; stick_y_ = in.stick_y;
    tick(kTick);
}

// keydown / keyup handlers (14962-14976), movement part.
void PlayerController::key_event(Key key, bool down) {
    if(!down) { keys_ &= ~static_cast<std::uint32_t>(key); return; }
    if(keys_ & key) return;   // e.repeat
    keys_ |= key;
    if(key == key_slide) begin_slide(false);
    if(key == key_jump) t_.jump_buffer = kJumpBuffer;
    if(key == key_crouch && p_.ground && js_hypot(p_.vx, p_.vy) > kWalkSpeed * 1.05) begin_slide(false);
    if(key == key_crouch && !p_.ground) t_.slide_buffer = kLandingSlideBuffer;
}

// aim() (14945)
void PlayerController::aim(double dx, double dy) {
    p_.a += clamp(dx, -120, 120) * kLookYaw;
    p_.pitch = clamp(p_.pitch + clamp(dy, -90, 90) * kLookPitch, -1.10, kPitchLimit);
}

// Player part of update(dt) (12222-12231).
void PlayerController::tick(double dt) {
    now_ += dt;
    if(p_.downed) { p_.vx = p_.vy = p_.vz = 0; t_.slide_time = 0; }
    const double f = p_.downed ? 0 : (held(key_forward) ? 1 : 0) - (held(key_back) ? 1 : 0) - stick_y_;   // ... - touchStick.y
    const double s = p_.downed ? 0 : (held(key_right) ? 1 : 0) - (held(key_left) ? 1 : 0) + stick_x_;    // ... + touchStick.x
    const double m = js_hypot(f, s), mf = m != 0 ? f / m : 0, ms = m != 0 ? s / m : 0;
    const double ca = jsmath::cos(p_.a), sa = jsmath::sin(p_.a), wx = ca * mf - sa * ms, wy = sa * mf + ca * ms;
    move_marine(dt, wx, wy, m);
    stage4_movement(dt);
    trigger_update(dt);
    post_move(dt);
}

// beginSlide() (14949)
bool PlayerController::begin_slide(bool from_buffer) {
    if(p_.downed || t_.slide_time > 0 || t_.slide_cooldown > 0) return false;
    if(!p_.ground) { if(!from_buffer) t_.slide_buffer = kLandingSlideBuffer; return false; }
    const double speed = js_hypot(p_.vx, p_.vy);
    if(speed < kWalkSpeed * .72) return false;
    note_body(BodySound::slide, std::min(1.2, speed / 10));   // playBodySound('slide', min(1.2, speed / 10)) (14958)
    const double multiplier = std::min(kSlideBoost, kMaxSkillSpeed / speed);
    p_.vx *= multiplier; p_.vy *= multiplier;
    t_.slide_time = kSlideDuration; t_.slide_cooldown = kSlideCooldown;
    return true;
}

// marineFriction() (12124)
void PlayerController::friction(double dt, double amount) {
    const double speed = js_hypot(p_.vx, p_.vy);
    if(speed < .0001) { p_.vx = p_.vy = 0; return; }
    const double drop = std::max(speed, kStopSpeed) * amount * dt;
    const double scale = std::max(0.0, speed - drop) / speed;
    p_.vx *= scale; p_.vy *= scale;
}

// marineAccelerate() (12133): acceleration toward a projected target speed (Quake style).
void PlayerController::accelerate(double dx, double dy, double wish_speed, double accel, double dt, bool airborne) {
    if(wish_speed <= 0) return;
    const double dot = p_.vx * dx + p_.vy * dy;
    const double additional = wish_speed - dot;
    if(additional <= 0) return;
    const double add = std::min(additional, accel * wish_speed * dt);
    const double before = js_hypot(p_.vx, p_.vy);
    p_.vx += dx * add; p_.vy += dy * add;
    if(airborne) {
        const double after = js_hypot(p_.vx, p_.vy);
        const double allowed = std::max(before, kMaxSkillSpeed);
        if(after > allowed) { p_.vx *= allowed / after; p_.vy *= allowed / after; }
        air_strafe_gained_ = std::max(0.0, js_hypot(p_.vx, p_.vy) - kWalkSpeed);
    }
}

// marineSlope() (12114): signed incline along the direction of travel.
double PlayerController::slope(double x, double y, double feet, double dx, double dy) const {
    const double len = js_hypot(dx, dy);
    if(len < .001) return 0;
    const double probe = .66, ux = dx / len, uy = dy / len;
    const double ground = world_->floor_height(x, y, feet);
    const double forward = world_->floor_height(x + ux * probe, y + uy * probe, feet + .32);
    const double rise = forward - ground;
    if(std::fabs(rise) > .48) return 0;
    return jsmath::atan2(rise, probe);
}

// moveMarine() (12150)
void PlayerController::move_marine(double dt, double wx, double wy, double m) {
    if(p_.downed) { p_.vx = p_.vy = 0; return; }
    const bool jump_down = held(key_jump);
    const bool crouching = held(key_crouch);
    const bool sprinting = held(key_sprint) && !crouching;   // sprintRequested(), "hold" mode
    const bool grounded = p_.ground;
    t_.coyote_time = grounded ? kCoyoteTime : std::max(0.0, t_.coyote_time - dt);
    t_.jump_buffer = std::max(0.0, t_.jump_buffer - dt);
    t_.slide_buffer = std::max(0.0, t_.slide_buffer - dt);
    t_.slide_cooldown = std::max(0.0, t_.slide_cooldown - dt);
    if(grounded && t_.slide_buffer > 0 && t_.slide_time <= 0 && !jump_down && t_.jump_buffer <= 0) { if(begin_slide(true)) t_.slide_buffer = 0; }
    const bool has_jump = jump_down || t_.jump_buffer > 0;
    if(has_jump && (grounded || t_.coyote_time > 0)) {
        p_.vz = kJumpImpulse;
        p_.ground = false; t_.coyote_time = 0; t_.jump_buffer = 0; t_.slide_time = 0; t_.slide_buffer = 0;
        ++jumps_;
        note_body(BodySound::jump, 1);   // playBodySound('jump') (12165)
    }
    const bool airborne = !p_.ground;
    t_.slide_time = std::max(0.0, t_.slide_time - dt);
    const bool sliding = t_.slide_time > 0 && !airborne;
    const bool crouch_only = crouching && !sliding;
    const double base_speed = kWalkSpeed * (crouch_only ? kCrouchMult : sprinting ? kSprintMult : 1);
    const bool moving_fast = js_hypot(p_.vx, p_.vy) > .15;
    const double travel_x = moving_fast ? p_.vx : wx, travel_y = moving_fast ? p_.vy : wy;
    slope_angle_ = airborne ? 0 : slope(p_.x, p_.y, p_.z, travel_x, travel_y);
    const double incline = jsmath::sin(slope_angle_);
    const double grade_factor = incline >= 0 ? 1 - kGradePenalty * incline : 1 - kGradeDownhillBonus * incline;
    if(!airborne) {
        friction(dt, sliding ? kSlideFriction : crouch_only ? kCrouchFriction : kGroundFriction);
        if(sliding) {
            if(m > .03) accelerate(wx, wy, kWalkSpeed * .93, 2.25, dt, false);
            const double s = js_hypot(p_.vx, p_.vy);
            if(s > .1 && std::fabs(incline) > .01) {
                const double g = -kGravity * .31 * incline * dt;
                p_.vx += p_.vx / s * g; p_.vy += p_.vy / s * g;
            }
        } else if(m > .03) {
            accelerate(wx, wy, base_speed * grade_factor, kGroundAcceleration, dt, false);
            if(incline > .02) {
                const double uphill = std::max(0.0, 1 - 2.4 * incline * dt);
                p_.vx *= uphill; p_.vy *= uphill;
            }
        }
    } else if(m > .03) {
        accelerate(wx, wy, kWalkSpeed * (sprinting ? kSprintMult : 1), kAirAcceleration, dt, true);
    }
    if(!airborne && crouch_only) {
        const double speed = js_hypot(p_.vx, p_.vy);
        if(speed > base_speed) { const double scale = std::max(base_speed, speed - 14 * dt) / speed; p_.vx *= scale; p_.vy *= scale; }
    }
    p_.crouch_offset += (((crouching || sliding) ? kCrouchOffsetTarget : 0) - p_.crouch_offset) * std::min(1.0, dt * 13);
    const double h_speed = js_hypot(p_.vx, p_.vy);
    const double fast_motion = clamp((h_speed - kWalkSpeed) / (kMaxSkillSpeed - kWalkSpeed), 0, 1);
    const double desired_fov = 1 + (sliding || sprinting ? .05 : 0) + fast_motion * .038;
    fov_scale_ += (desired_fov - fov_scale_) * std::min(1.0, dt * 7);
    stance_ = airborne ? (h_speed > kWalkSpeed * kSprintMult ? Stance::air_momentum : Stance::air_control)
            : sliding ? Stance::slide : crouch_only ? Stance::crouch : sprinting ? Stance::sprint : Stance::run;
}

// stage4Movement() (8574): sub-stepped capsule motion, vertical then horizontal, with wall slide.
void PlayerController::stage4_movement(double dt) {
    if(p_.downed) return;
    const auto& w = *world_;
    const double h = collider_height(p_.crouch_offset), r = kPlayerRadius;
    const double horizontal_dist = js_hypot(p_.vx, p_.vy) * dt, vertical_dist = std::fabs(p_.vz * dt) - .5 * kGravity * dt * dt;
    const double want = std::ceil(std::max(horizontal_dist, std::fabs(vertical_dist)) / kSweepStep);
    const int steps = static_cast<int>(std::min(static_cast<double>(kMaxSubsteps), std::max(1.0, want)));
    const double step_dt = dt / steps;
    bool landed = false;
    double landing_speed = 0;
    if(!w.capsule_clear(p_.x, p_.y, p_.z, h, r)) {   // recover an invalid state without leaving the current floor
        bool repaired = false;
        for(const double shift : {0.0, .11, .24, .38, .56, 1.0}) {
            for(int k = 0; k < 16; ++k) {
                const double a = k * kPi / 8, x = p_.x + shift * jsmath::cos(a), y = p_.y + shift * jsmath::sin(a);
                const auto support = w.support(x, y, p_.z, .55);
                const double z = support && std::fabs(support->z - p_.z) < .55 ? support->z : p_.z;
                if(w.capsule_clear(x, y, z, h, r)) { p_.x = x; p_.y = y; p_.z = z; p_.vx *= .35; p_.vy *= .35; repaired = true; break; }
            }
            if(repaired) break;
        }
        if(repaired) ++contacts_.recoveries;
    }
    struct Candidate { bool ok{}; double x{}, y{}, z{}; };
    auto candidate = [&](double x, double y) {
        double z = p_.z;
        if(p_.ground) {
            const auto s = w.support(x, y, p_.z, kStepHeight);
            if(s && s->z >= p_.z - kGroundSnap) z = s->z;
        }
        if(!w.capsule_clear(x, y, z, h, r)) return Candidate{};
        return Candidate{true, x, y, z};
    };
    auto accept = [&](const Candidate& c) {
        if(!c.ok) return false;
        if(p_.ground && std::fabs(c.z - p_.z) > .0001) ++contacts_.steps;
        p_.x = c.x; p_.y = c.y; p_.z = c.z;
        if(p_.ground) { const auto support = w.support(p_.x, p_.y, p_.z, .09); if(!support || support->z < p_.z - kGroundSnap) p_.ground = false; }
        return true;
    };
    for(int i = 0; i < steps; ++i) {
        ++contacts_.iterations;
        if(!p_.ground) {
            p_.vz -= kGravity * step_dt;
            double next_z = p_.z + p_.vz * step_dt;
            if(p_.vz > 0) {
                const auto ceiling = w.ceiling_plane(p_.x, p_.y, p_.z + h, next_z + h);
                if(ceiling) { next_z = *ceiling - h - .012; p_.vz = 0; ++contacts_.ceiling; }
            }
            if(p_.vz <= 0) {
                const auto landing = w.landing_plane(p_.x, p_.y, p_.z, next_z);
                if(landing) { landing_speed = std::max(landing_speed, -p_.vz); next_z = landing->z; p_.vz = 0; p_.ground = true; landed = true; ++contacts_.landings; }
            }
            p_.z = next_z;
        } else p_.vz = 0;
        const double dx = p_.vx * step_dt, dy = p_.vy * step_dt;
        if(std::fabs(dx) + std::fabs(dy) < 1e-8) {
            if(p_.ground) { const auto support = w.support(p_.x, p_.y, p_.z, .12); if(!support || support->z < p_.z - kGroundSnap) p_.ground = false; }
            continue;
        }
        if(accept(candidate(p_.x + dx, p_.y + dy))) continue;
        ++contacts_.horizontal;
        if(const auto hit = w.nearest_wall(p_.x + dx, p_.y + dy, p_.z, h)) {
            double nx = p_.x + dx - hit->qx, ny = p_.y + dy - hit->qy, nd = js_hypot(nx, ny);
            if(nd < 1e-5) {
                const auto& wall = *hit->wall;
                const double raw = js_hypot(wall.bx - wall.ax, wall.by - wall.ay), len = raw != 0 ? raw : 1;
                nx = -(wall.by - wall.ay) / len; ny = (wall.bx - wall.ax) / len; nd = 1;
                if(nx * dx + ny * dy > 0) { nx = -nx; ny = -ny; }
            }
            nx /= nd; ny /= nd;
            const double normal_motion = dx * nx + dy * ny;
            const double tangent_x = dx - std::min(0.0, normal_motion) * nx;
            const double tangent_y = dy - std::min(0.0, normal_motion) * ny;
            if(accept(candidate(p_.x + tangent_x, p_.y + tangent_y))) {
                const double speed_into = p_.vx * nx + p_.vy * ny;
                if(speed_into < 0) { p_.vx -= speed_into * nx; p_.vy -= speed_into * ny; }
                continue;
            }
        }
        bool moved = false;   // corner fallback: one axis at a time
        if(std::fabs(dx) > .000001) moved = accept(candidate(p_.x + dx, p_.y));
        if(std::fabs(dy) > .000001) moved = accept(candidate(p_.x, p_.y + dy)) || moved;
        if(!moved) { p_.vx *= .20; p_.vy *= .20; }
    }
    if(landed) {
        if(landing_speed > 1.9) note_body(BodySound::land, std::min(1.3, landing_speed / 10));   // (8665)
        crash_land(landing_speed);
    }
}

// worldTriggerUpdate() / worldPadLaunch() (9765-9785): jump pads.
void PlayerController::trigger_update(double dt) {
    t_.pad_cooldown = std::max(0.0, t_.pad_cooldown - dt);
    if(p_.downed || t_.pad_cooldown > 0 || !p_.ground) return;
    for(const auto& pad : world_->pads()) {
        if(std::fabs(p_.z - pad.z) > .35 || js_hypot(p_.x - pad.x, p_.y - pad.y) > pad.r) continue;
        if(pad.has_apex) {   // Quake III trigger_push: the arc's apex is the target
            const double g = kGravity, h = std::max(.5, pad.apex[2] - pad.z), vz = std::sqrt(2 * g * h), t = vz / g;
            p_.vx = (pad.apex[0] - pad.x) / t; p_.vy = (pad.apex[1] - pad.y) / t; p_.vz = vz;
        } else { p_.vx = pad.velocity[0]; p_.vy = pad.velocity[1]; p_.vz = pad.velocity[2]; }
        p_.ground = false;
        t_.pad_cooldown = pad.cooldown + .4;
        ++pad_launches_;
        note_body(BodySound::jump, 1);   // worldTriggerUpdate: playBodySound('jump') (9759)
        break;
    }
}

// q3PostMove() (10474): footstep bob cycle and step-up smoothing.
void PlayerController::post_move(double dt) {
    q3_.xyspeed = js_hypot(p_.vx, p_.vy) / kQ3Unit;
    q3_.crouched = p_.crouch_offset > .25;
    if(q3_.has_last_z && p_.ground && q3_.last_ground) {
        const double dz = (p_.z - q3_.last_z) / kQ3Unit;
        if(dz > 2 && dz < 24) {
            const double td = now_ - q3_.step_time, old = td < kStepTime ? q3_.step_change * (kStepTime - td) / kStepTime : 0;
            q3_.step_change = std::min(kMaxStep, old + dz); q3_.step_time = now_;
        }
    }
    q3_.last_z = p_.z; q3_.has_last_z = true; q3_.last_ground = p_.ground;
    const bool moving = std::fabs((held(key_forward) ? 1.0 : 0.0) - (held(key_back) ? 1.0 : 0.0) - stick_y_) > .01 ||
                        std::fabs((held(key_right) ? 1.0 : 0.0) - (held(key_left) ? 1.0 : 0.0) + stick_x_) > .01;
    const bool sliding = t_.slide_time > 0;
    const bool sprinting = held(key_sprint) && !q3_.crouched;
    if(p_.downed) q3_.bob_cycle = 0;
    else if(!p_.ground || sliding) { /* airborne or sliding: the cycle holds its phase */ }
    else if(!moving) { if(q3_.xyspeed < 5) q3_.bob_cycle = 0; }
    else {
        const double bobmove = q3_.crouched ? kBobCrouch : sprinting ? kBobSprint : kBobRun;
        const auto old = static_cast<int>(std::floor(q3_.bob_cycle));
        q3_.bob_cycle = std::fmod(q3_.bob_cycle + bobmove * dt * 1000, 256.0);
        const auto cur = static_cast<int>(std::floor(q3_.bob_cycle));
        if(!q3_.crouched && (((old + 64) ^ (cur + 64)) & 128)) { ++q3_.footsteps; note_body(BodySound::step, sprinting ? .6 : .48); }   // (10497)
    }
    const auto c = static_cast<int>(std::floor(q3_.bob_cycle));
    q3_.bobfracsin = std::fabs(jsmath::sin((c & 127) / 127.0 * kPi));
    q3_.bob_odd = (c & 128) >> 7;
}

// q3CrashLand() (10505): landing view dip tiers. No fall damage in the legacy game.
void PlayerController::crash_land(double impact) {
    const double q = impact / kQ3Unit, delta = q * q * .0001;
    q3_.last_land_delta = delta; ++q3_.landings;
    if(delta < 1) return;
    q3_.bob_cycle = 0;
    if(delta > 4) { q3_.land_change = delta > 45 ? -24 : delta > 25 ? -16 : -8; q3_.land_time = now_; }
}

// q3ShotOrigin() (10513)
ViewCamera PlayerController::shot_origin() const {
    ViewCamera c;
    c.x = p_.x; c.y = p_.y; c.z = p_.z + (p_.downed ? kDownedEye : kEyeHeight) - (p_.downed ? 0 : p_.crouch_offset);
    const double ca = jsmath::cos(p_.a), sa = jsmath::sin(p_.a), cp = jsmath::cos(p_.pitch), sp = jsmath::sin(p_.pitch);
    c.forward = {ca * cp, sa * cp, -sp}; c.right = {-sa, ca, 0}; c.up = {ca * sp, sa * sp, cp};
    return c;
}

// q3ViewCamera() (10518): CG_OffsetFirstPersonView.
ViewCamera PlayerController::view_camera() const {
    const double S = q3_.bob_scale;
    ViewCamera c = shot_origin();
    double z = c.z, pitch = p_.pitch, roll = 0;
    if(!p_.downed) {
        double bob = q3_.bobfracsin * q3_.xyspeed * kBobUp;
        if(bob > 6) bob = 6;
        z += bob * kQ3Unit * S;
        const double lt = now_ - q3_.land_time;
        if(lt >= 0 && lt < kLandDeflect) z += q3_.land_change * kQ3Unit * S * (lt / kLandDeflect);
        else if(lt >= 0 && lt < kLandDeflect + kLandReturn) z += q3_.land_change * kQ3Unit * S * (1 - (lt - kLandDeflect) / kLandReturn);
        const double st = now_ - q3_.step_time;
        if(st >= 0 && st < kStepTime) z -= q3_.step_change * kQ3Unit * (kStepTime - st) / kStepTime;
        const double fx = jsmath::cos(p_.a), fy = jsmath::sin(p_.a);
        const double vf = (p_.vx * fx + p_.vy * fy) / kQ3Unit, vr = (p_.vx * -fy + p_.vy * fx) / kQ3Unit;
        const double dp = vf * kRunPitch, dr = -vr * kRunRoll;   // degrees
        const double speed = q3_.xyspeed > 200 ? q3_.xyspeed : 200;
        double bp = q3_.bobfracsin * kBobPitch * speed, br = q3_.bobfracsin * kBobRoll * speed;
        if(q3_.crouched) { bp *= 3; br *= 3; }
        if(q3_.bob_odd) br = -br;
        pitch += (dp + bp) * S * kPi / 180; roll = (dr + br) * S * kPi / 180;
    }
    const double ca = jsmath::cos(p_.a), sa = jsmath::sin(p_.a), cp = jsmath::cos(pitch), sp = jsmath::sin(pitch), cr = jsmath::cos(roll), sr = jsmath::sin(roll);
    const std::array<double, 3> r0{-sa, ca, 0}, u0{ca * sp, sa * sp, cp};
    c.z = z;
    c.forward = {ca * cp, sa * cp, -sp};
    c.right = {r0[0] * cr - u0[0] * sr, r0[1] * cr - u0[1] * sr, r0[2] * cr - u0[2] * sr};   // positive roll lowers the right side
    c.up = {u0[0] * cr + r0[0] * sr, u0[1] * cr + r0[1] * sr, u0[2] * cr + r0[2] * sr};
    c.roll = roll;
    return c;
}

// q3WeaponMotion() (10544): CG_CalculateWeaponPosition.
WeaponMotion PlayerController::weapon_motion() const {
    const double S = q3_.bob_scale, xy = q3_.xyspeed, scale = q3_.bob_odd ? -xy : xy;
    double roll = scale * q3_.bobfracsin * .005, yaw = scale * q3_.bobfracsin * .01, pitch = xy * q3_.bobfracsin * .005;
    double drop = 0;
    const double lt = now_ - q3_.land_time;
    if(lt >= 0 && lt < kLandDeflect) drop = q3_.land_change * .25 * lt / kLandDeflect;
    else if(lt >= 0 && lt < kLandDeflect + kLandReturn) drop = q3_.land_change * .25 * (kLandDeflect + kLandReturn - lt) / kLandReturn;
    const double drift = (xy + 40) * jsmath::sin(now_) * .01;
    roll += drift; yaw += drift; pitch += drift;
    const double k = kPi / 180 * S;
    return {pitch * k, yaw * k, roll * k, drop * kGunUnit * S};
}
}  // namespace cat::physics
