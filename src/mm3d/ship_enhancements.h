// SPDX-License-Identifier: GPL-2.0-or-later
// Options modelled on 2 Ship 2 Harkinian's enhancements and cheats, for the USA Rev 1 game
// with Project Restoration (the code layout is v1.0 + Restoration; addresses as in simple_cheats.h).
//
// Two mechanisms, like the rest of Dekui:
//  * Instruction hooks: a game instruction is replaced by an SVC whose handler replays the
//    original instruction when the option is off. Hooks stay installed, so options can be
//    toggled at any time without restarting.
//  * A per-frame Tick, called from Restoration's pad-update hook (the same place as moon jump),
//    for options that only touch save data, the player actor or StaticContext.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

#include "mm3d/simple_cheats.h"

namespace MM3D::ShipEnhancements {
using U32 = std::uint32_t;

// ---- options (written by the F1 menu, read on the emulation thread)
inline std::atomic<bool> infinite_rupees{false};
inline std::atomic<bool> unbreakable_razor_sword{false};
inline std::atomic<bool> freeze_time{false};
inline std::atomic<bool> time_moves_when_you_move{false};
inline std::atomic<bool> keep_inverted_time{false};       // "Do not reset time speed"
inline std::atomic<bool> fierce_deity_anywhere{false};
inline std::atomic<bool> goron_rolling_ignores_magic{false};
inline std::atomic<bool> lower_bank_thresholds{false};
inline std::atomic<int> boss_health_multiplier{1};       // 1..5
inline std::atomic<int> run_speed_percent{100};          // 50..300
inline std::atomic<int> blast_mask_cooldown_percent{100}; // 0..100

// ---- addresses
constexpr U32 Common = 0x7751d8;           // CommonData; save data starts at +8
constexpr U32 SaveExtraTimeSpeed = Common + 0x10; // s32, Inverted Song of Time: negative
constexpr U32 SaveDay = Common + 0x14;     // s32
constexpr U32 SaveTime = Common + 0x20;    // u16, 0x4000 = 6:00
constexpr U32 SaveForm = 0x7751fe;         // u8: 0 Fierce Deity, 1 Goron, 2 Zora, 3 Deku, 4 Human
constexpr U32 SaveMagic = 0x775317;        // u8
constexpr U32 SaveRupees = 0x775318;       // u16
constexpr U32 SaveRazorSwordHits = 0x77531a; // u16, 100 when new
constexpr U32 SaveSwordShield = 0x775352;  // u8: low nibble sword (2 = Razor)
constexpr U32 SaveUpgrades = 0x7753cc;     // u32: wallet in bits 12-13
constexpr U32 StaticContextPtr = 0x66889c;
constexpr U32 StaticRunAccel = 0x3a, StaticRunSpeed = 0x6e; // u16 copies of the form's FormParam
constexpr U32 PlayerSpeedXZ = 0x70;        // float (Actor::vel_xz)
constexpr U32 PlayerFlags3 = 0x11db8, GoronRollingFast = 0x80000;
constexpr U32 PlayerBlastMaskTimer = 0x11ec8; // u16, set to 310 when the mask explodes

// ---- instruction hooks
constexpr U32 FirstSVC = 0xfff700;
struct Hook { U32 pc, word; };
enum HookIndex : U32 { FierceDeityButton, FierceDeityButton2, ApplyDamage,
    BankThreshold1000a, BankThreshold1000b, BankThreshold5000, HookCount };
inline constexpr std::array<Hook, HookCount> Hooks{{
    // Item-button update: Fierce Deity's Mask is enabled only in the four boss rooms and on
    // the Moon (scenes 0x1f, 0x44, 0x5f, 0x36, 0x0b). Two passes over the C buttons.
    {0x1b0b74, 0x1affffd9}, // BNE 0x1b0ae0 (not a boss room: native dimming checks)
    {0x1b1370, 0x0a000005}, // BEQ 0x1b138c (boss room: enable)
    // Actor_ApplyDamage(actor): health (u8 +0xbb) -= damage (u8 +0xbc), clamped at 0
    {0x2287b0, 0xe5d020bb}, // LDRB r2,[r0,#0xbb]
    // En_Ginko_Man deposit rewards at 200 / 1000 / 5000 rupees
    {0x426ad0, 0xe3520ffa}, // CMP r2,#1000 (balance)
    {0x426ae0, 0xe3510ffa}, // CMP r1,#1000 (balance before the deposit)
    {0x426af4, 0xe59fc590}, // LDR r12,=5000
}};
inline constexpr std::array<std::pair<U32, U32>, 40> Guards{{
    {0x1b0b58,0xe1da04f8},{0x1b0b5c,0xe350001f},{0x1b0b60,0x13500044},{0x1b0b64,0x0a000003},
    {0x1b0b68,0xe350005f},{0x1b0b6c,0x13500036},{0x1b0b70,0x1350000b},{0x1b0b78,0xe0870008},
    {0x1b1354,0xe1da24f8},{0x1b1358,0xe352001f},{0x1b135c,0x13520044},{0x1b1360,0x0a000009},
    {0x1b1364,0xe352005f},{0x1b1368,0x13520036},{0x1b136c,0x1352000b},
    {0x2287b4,0xe5d010bc},{0x2287b8,0xe1520001},{0x2287bc,0x93a01000},{0x2287c0,0x80421001},
    {0x2287c4,0xe5c010bb},{0x2287c8,0xe20100ff},{0x2287cc,0xe12fff1e},
    {0x426aa4,0xe5903230},{0x426aa8,0xe6ff2073},{0x426aac,0xe35200c8},{0x426ab0,0x3a000006},
    {0x426ab8,0xe1d711fe},{0x426abc,0xe35100c8},{0x426ad4,0x3a000046},{0x426adc,0xe1d711fe},
    {0x426ae4,0xaa000002},{0x426af8,0xe152000c},{0x426afc,0x3a00003c},{0x426b00,0xe151000c},
    {0x427088,0x007760d8},{0x42708c,5000},
    {0x1f1190,0xe1c343ba},{0x1f11e8,0xe1c346be},{0x1f1260,StaticContextPtr},{0x1f1268,0x11db0},
}};
inline U32 Instruction(U32 pc, U32 word) {
    for (U32 i = 0; i < Hooks.size(); ++i)
        if (pc == Hooks[i].pc && word == Hooks[i].word) return 0xef000000u | (FirstSVC + i);
    return word;
}
template <class M> bool CodeMatches(M& m, U32 pc) {
    bool known = false;
    for (const auto& hook : Hooks) known |= hook.pc == pc;
    if (!known) return false;
    for (const auto& [at, word] : Guards)
        if (m.Read(at) != std::optional<U32>(word)) return false;
    return true;
}

// NZCV of CMP a,b, merged into cpsr.
constexpr U32 CompareFlags(U32 cpsr, U32 a, U32 b) {
    const U32 r = a - b;
    const U32 n = r >> 31, z = r == 0, c = a >= b, v = ((a ^ b) & (a ^ r)) >> 31;
    return (cpsr & 0x0fffffffu) | (n << 31) | (z << 30) | (c << 29) | (v << 28);
}
constexpr bool ZeroFlag(U32 cpsr) { return (cpsr >> 30) & 1; }

// Main bosses: Odolwa, Twinmold, Gyorg, Wart (Boss_04), Majora, Goht.
constexpr bool IsBoss(U32 id) {
    return id == 0xcb || id == 0xcc || id == 0xcd || id == 0xce || id == 0xd1 || id == 0x16e;
}
// Damage that is not yet big enough to remove a whole unit of a multiplied boss's health,
// per actor. Health is a byte, so the multiplier divides the damage instead of raising it.
struct DamageCarry {
    std::array<std::pair<U32, U32>, 16> slots{};
    std::size_t next = 0;
    U32 Take(U32 actor, U32 damage, U32 multiplier) {
        std::pair<U32, U32>* slot = nullptr;
        for (auto& s : slots)
            if (s.first == actor) slot = &s;
        if (!slot) {
            slot = &slots[next];
            next = (next + 1) % slots.size();
            *slot = {actor, 0};
        }
        const U32 total = slot->second + damage;
        slot->second = total % multiplier;
        return total / multiplier;
    }
};
inline DamageCarry damage_carry;

template <class M> std::optional<U32> Get(M& m, U32 at, unsigned bytes) { return SimpleCheats::Get(m, at, bytes); }
template <class M> void Set(M& m, U32 at, U32 value, unsigned bytes) { SimpleCheats::Set(m, at, value, bytes); }

// The SVC handler. regs: r0-r15; cpsr in/out.
template <class M, class Registers>
void Handle(M& m, Registers& r, U32& cpsr, U32 index) {
    switch (index) {
    case FierceDeityButton: // BNE 0x1b0ae0
        r[15] = fierce_deity_anywhere || ZeroFlag(cpsr) ? 0x1b0b78 : 0x1b0ae0;
        return;
    case FierceDeityButton2: // BEQ 0x1b138c
        r[15] = fierce_deity_anywhere || ZeroFlag(cpsr) ? 0x1b138c : 0x1b1374;
        return;
    case ApplyDamage: {
        // Replays the whole native routine, then returns to the caller.
        const U32 actor = r[0];
        const U32 health = Get(m, actor + 0xbb, 1).value_or(0);
        U32 damage = Get(m, actor + 0xbc, 1).value_or(0);
        const int multiplier = std::clamp(boss_health_multiplier.load(), 1, 5);
        if (multiplier > 1 && damage > 0 && IsBoss(Get(m, actor, 2).value_or(0)))
            damage = damage_carry.Take(actor, damage, U32(multiplier));
        const U32 result = health > damage ? health - damage : 0;
        Set(m, actor + 0xbb, result, 1);
        cpsr = CompareFlags(cpsr, health, damage);
        r[1] = result;
        r[2] = health;
        r[0] = result & 255;
        r[15] = r[14];
        return;
    }
    case BankThreshold1000a:
        cpsr = CompareFlags(cpsr, r[2], lower_bank_thresholds ? 500 : 1000);
        r[15] = Hooks[index].pc + 4;
        return;
    case BankThreshold1000b:
        cpsr = CompareFlags(cpsr, r[1], lower_bank_thresholds ? 500 : 1000);
        r[15] = Hooks[index].pc + 4;
        return;
    case BankThreshold5000:
        r[12] = lower_bank_thresholds ? 1000 : 5000;
        r[15] = Hooks[index].pc + 4;
        return;
    default:
        return;
    }
}

// ---- per-frame options
struct FrameState {
    std::optional<U32> last_time;
    bool time_held = false;
    std::int32_t inverted_speed = 0;  // last Inverted Song of Time speed seen in this play session
    U32 normal_frames = 0;            // gameplay frames at normal speed since it was seen
    std::int32_t last_day = 0;
    U32 last_cycle_time = 0;
    std::chrono::steady_clock::time_point last_seen{};
    std::optional<U32> last_magic;
    U32 last_blast_timer = 0;
    // StaticContext run speed: native value and the value last written
    U32 native_speed = 0, written_speed = 0, native_accel = 0, written_accel = 0;
};
inline FrameState frame;

constexpr U32 WalletCapacity(U32 upgrades) {
    switch ((upgrades >> 12) & 3) {
    case 0: return 99;
    case 1: return 200;
    default: return 500;
    }
}

// One run speed value (StaticContext u16): keep the game's own value as the reference and
// write the scaled one; restore it when the option is set back to 100%.
template <class M>
void ScaleStatic(M& m, U32 at, U32& native, U32& written, int percent) {
    const auto current = Get(m, at, 2);
    if (!current) return;
    if (*current != written || written == 0) native = *current; // the game set a new value
    const U32 want = percent == 100 ? native : std::min<U32>(0x7fff, native * U32(percent) / 100);
    if (want != *current) Set(m, at, want, 2);
    written = want;
}

template <class M> void Tick(M& m) {
    using namespace std::chrono;
    if (!SimpleCheats::CodeMatches(m) || !SimpleCheats::InGame(m)) {
        frame.last_time.reset();
        frame.last_magic.reset();
        return;
    }
    const auto context = m.Read(0x72f5c0), game = m.Read(*context + 0x16c);
    const auto pause = Get(m, *game + 0xaac, 2);
    const auto player = m.Read(*game + 0x20dc);
    const bool paused = !pause || (*pause & 3);
    const bool player_ok = player && *player >= 0x100000 && *player <= 0x3ffe0000 && !(*player & 3) &&
                           (m.Read(*player).value_or(0) & 0xffffff) == 0x020000;
    const auto now = steady_clock::now();

    // Wallet / Razor Sword
    if (infinite_rupees) {
        const U32 cap = WalletCapacity(m.Read(SaveUpgrades).value_or(0));
        if (Get(m, SaveRupees, 2).value_or(cap) < cap) Set(m, SaveRupees, cap, 2);
    }
    if (unbreakable_razor_sword && (Get(m, SaveSwordShield, 1).value_or(0) & 15) == 2 &&
        Get(m, SaveRazorSwordHits, 2).value_or(100) < 100)
        Set(m, SaveRazorSwordHits, 100, 2);

    // Time: undo the normal clock tick (a small forward step); Song of Time / Double Time,
    // sleeping and scripted time changes are bigger jumps and are kept.
    const auto time = Get(m, SaveTime, 2);
    bool hold = freeze_time.load();
    if (!hold && time_moves_when_you_move && player_ok) {
        const float speed = std::bit_cast<float>(m.Read(*player + PlayerSpeedXZ).value_or(0));
        hold = std::isfinite(speed) && std::fabs(speed) < 0.5f;
    }
    if (time) {
        if (hold && frame.last_time && !paused) {
            const U32 step = (*time - *frame.last_time) & 0xffff;
            if (step != 0 && step <= 0x100) Set(m, SaveTime, *frame.last_time, 2);
            else frame.last_time = time;
        } else {
            frame.last_time = time;
        }
    }

    // Keep the Inverted Song of Time through the Song of Time: when a new cycle starts
    // (dawn of the first day) shortly after a play session that had it, put it back.
    // The cycle reset itself happens outside gameplay; the song played again to return to normal
    // speed shows up as several seconds of gameplay at normal speed.
    const auto extra = m.Read(SaveExtraTimeSpeed);
    const auto day = m.Read(SaveDay);
    if (extra && day && time) {
        const auto speed = std::bit_cast<std::int32_t>(*extra);
        const auto d = std::bit_cast<std::int32_t>(*day);
        const bool recent = now - frame.last_seen < seconds(60);
        const bool dawn = *time >= 0x3f00 && *time <= 0x4800;
        const bool new_cycle = recent && d == 1 && dawn &&
            (frame.last_day > 1 || (frame.last_day == 1 && frame.last_cycle_time > *time + 0x400));
        if (speed < 0) {
            frame.inverted_speed = speed;
            frame.normal_frames = 0;
        } else if (speed == 0 && frame.inverted_speed < 0) {
            if (keep_inverted_time && new_cycle) {
                m.Write(SaveExtraTimeSpeed, std::bit_cast<U32>(frame.inverted_speed));
                frame.normal_frames = 0;
            } else if (!paused && ++frame.normal_frames > 300) {
                frame.inverted_speed = 0;
            }
        }
        frame.last_day = d;
        frame.last_cycle_time = *time;
    }
    frame.last_seen = now;

    // Run speed: StaticContext copies of the current form's movement parameters
    if (const auto sc = m.Read(StaticContextPtr); sc && *sc >= 0x100000 && !(*sc & 1)) {
        const int percent = std::clamp(run_speed_percent.load(), 50, 300);
        ScaleStatic(m, *sc + StaticRunSpeed, frame.native_speed, frame.written_speed, percent);
        ScaleStatic(m, *sc + StaticRunAccel, frame.native_accel, frame.written_accel, percent);
    }

    if (!player_ok) return;

    // Goron spike rolling without draining magic
    const auto magic = Get(m, SaveMagic, 1);
    const auto flags3 = m.Read(*player + PlayerFlags3);
    if (magic && flags3) {
        const bool rolling = Get(m, SaveForm, 1) == 1u && (*flags3 & GoronRollingFast);
        if (goron_rolling_ignores_magic && rolling && frame.last_magic && *magic < *frame.last_magic)
            Set(m, SaveMagic, *frame.last_magic, 1);
        else
            frame.last_magic = magic;
    }

    // Blast Mask cooldown (310 frames natively)
    if (const auto timer = Get(m, *player + PlayerBlastMaskTimer, 2)) {
        const int percent = std::clamp(blast_mask_cooldown_percent.load(), 0, 100);
        U32 value = *timer;
        if (percent < 100 && value > frame.last_blast_timer + 1) { // just set by the mask
            value = value * U32(percent) / 100;
            Set(m, *player + PlayerBlastMaskTimer, value, 2);
        }
        frame.last_blast_timer = value;
    }
}

inline void Reset() {
    frame = FrameState{};
    damage_carry = DamageCarry{};
}
} // namespace MM3D::ShipEnhancements
