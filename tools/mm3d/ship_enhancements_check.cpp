// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/ship_enhancements.h"
#include <bit>
#include <cassert>
#include <iostream>
#include <unordered_map>
using namespace MM3D::ShipEnhancements;
namespace SC = MM3D::SimpleCheats;
struct Memory {
    std::unordered_map<U32, U32> words;
    std::optional<U32> Read(U32 a) {
        auto i = words.find(a);
        return i == words.end() ? std::nullopt : std::optional<U32>(i->second);
    }
    void Write(U32 a, U32 v) { assert(words.contains(a)); words[a] = v; }
};
constexpr U32 Game = 0x9100000, Player = 0x9200000, Static = 0x9300000;
Memory Fixture() {
    Memory m;
    for (auto [a, w] : SC::Guards) m.words[a] = w;
    for (auto [a, w] : Guards) m.words[a] = w;
    for (auto h : Hooks) m.words[h.pc] = h.word;
    m.words[0x72f5c0] = 0x9000000; m.words[0x900016c] = Game; m.words[Game + 0x13c] = 0x0402;
    m.words[Game + 0xaac] = 0; m.words[Game + 0x20dc] = Player;
    for (U32 a = 0x7751d8; a <= 0x7753ec; a += 4) m.words[a] = 0;
    SC::Set(m, 0x775312, 80, 2); SC::Set(m, 0x775314, 80, 2);
    m.words[Player] = 0x020000;
    m.words[Player + PlayerSpeedXZ] = std::bit_cast<U32>(0.f);
    m.words[Player + PlayerFlags3] = 0;
    m.words[Player + (PlayerBlastMaskTimer & ~3u)] = 0;
    m.words[StaticContextPtr] = Static;
    m.words[Static + (StaticRunAccel & ~3u)] = 0;
    m.words[Static + (StaticRunSpeed & ~3u)] = 0;
    SC::Set(m, Static + StaticRunSpeed, 550, 2); SC::Set(m, Static + StaticRunAccel, 200, 2);
    return m;
}
void Off() {
    infinite_rupees = unbreakable_razor_sword = freeze_time = time_moves_when_you_move = false;
    keep_inverted_time = fierce_deity_anywhere = goron_rolling_ignores_magic = lower_bank_thresholds = false;
    boss_health_multiplier = 1; run_speed_percent = 100; blast_mask_cooldown_percent = 100;
    Reset();
}
int main() {
    // Hook table: every hook is installed only on its own instruction, and guarded.
    for (U32 i = 0; i < Hooks.size(); ++i) {
        assert(Instruction(Hooks[i].pc, Hooks[i].word) == (0xef000000u | (FirstSVC + i)));
        assert(Instruction(Hooks[i].pc, Hooks[i].word ^ 1) == (Hooks[i].word ^ 1));
        assert(Instruction(Hooks[i].pc + 4, Hooks[i].word) == Hooks[i].word);
    }
    {
        auto m = Fixture();
        for (auto h : Hooks) assert(CodeMatches(m, h.pc));
        assert(!CodeMatches(m, 0x100000));
        m.words[Guards[3].first] ^= 1;
        for (auto h : Hooks) assert(!CodeMatches(m, h.pc));
    }
    // CMP flags
    assert(CompareFlags(0, 5, 5) == 0x60000000u);          // Z C
    assert(CompareFlags(0, 4, 5) == 0x80000000u);          // N
    assert(CompareFlags(0xf00001d3u, 6, 5) == 0x200001d3u); // C, other bits kept
    // Fierce Deity button: off follows the native branch, on always enables
    {
        Off(); auto m = Fixture(); std::array<U32, 16> r{}; U32 cpsr = 0;
        Handle(m, r, cpsr, FierceDeityButton); assert(r[15] == 0x1b0ae0);
        cpsr = 1u << 30; Handle(m, r, cpsr, FierceDeityButton); assert(r[15] == 0x1b0b78);
        cpsr = 0; Handle(m, r, cpsr, FierceDeityButton2); assert(r[15] == 0x1b1374);
        cpsr = 1u << 30; Handle(m, r, cpsr, FierceDeityButton2); assert(r[15] == 0x1b138c);
        fierce_deity_anywhere = true; cpsr = 0;
        Handle(m, r, cpsr, FierceDeityButton); assert(r[15] == 0x1b0b78);
        Handle(m, r, cpsr, FierceDeityButton2); assert(r[15] == 0x1b138c);
    }
    // Actor_ApplyDamage replay
    {
        Off(); auto m = Fixture(); std::array<U32, 16> r{}; U32 cpsr = 0;
        constexpr U32 Actor = 0x9400000;
        for (U32 a = Actor; a < Actor + 0xc0; a += 4) m.words[a] = 0;
        auto hit = [&](U32 id, U32 health, U32 damage) {
            SC::Set(m, Actor, id, 2); SC::Set(m, Actor + 0xbb, health, 1); SC::Set(m, Actor + 0xbc, damage, 1);
            r[0] = Actor; r[14] = 0x123456; Handle(m, r, cpsr, ApplyDamage);
            assert(r[15] == 0x123456 && r[0] == SC::Get(m, Actor + 0xbb, 1));
            return r[0];
        };
        assert(hit(0xcb, 20, 2) == 18);
        assert(hit(0xcb, 2, 5) == 0);
        assert(hit(0x10, 3, 3) == 0);
        boss_health_multiplier = 3;
        assert(hit(0x10, 10, 2) == 8);   // not a boss
        assert(hit(0xcb, 20, 2) == 20);  // 2/3 carried
        assert(hit(0xcb, 20, 2) == 19);  // 4/3 -> 1, 1 carried
        assert(hit(0xcb, 19, 2) == 18);  // 3/3 -> 1
        assert(hit(0x16e, 30, 9) == 27);
        assert(hit(0xd1, 1, 6) == 0);
    }
    // Bank thresholds
    {
        Off(); auto m = Fixture(); std::array<U32, 16> r{}; U32 cpsr = 0;
        r[2] = 600; Handle(m, r, cpsr, BankThreshold1000a);
        assert(r[15] == 0x426ad4 && (cpsr >> 31) == 1); // 600 < 1000: BLO taken (C clear)
        assert(!((cpsr >> 29) & 1));
        Handle(m, r, cpsr, BankThreshold5000); assert(r[12] == 5000 && r[15] == 0x426af8);
        lower_bank_thresholds = true;
        Handle(m, r, cpsr, BankThreshold1000a); assert((cpsr >> 29) & 1); // 600 >= 500
        r[1] = 499; Handle(m, r, cpsr, BankThreshold1000b); assert(!((cpsr >> 29) & 1) && r[15] == 0x426ae4);
        Handle(m, r, cpsr, BankThreshold5000); assert(r[12] == 1000);
    }
    // Per-frame options
    {
        Off(); auto m = Fixture();
        auto before = m.words; Tick(m); assert(m.words == before); // all off: no writes
        infinite_rupees = true; SC::Set(m, SaveRupees, 5, 2);
        m.words[SaveUpgrades] = 1u << 12; Tick(m); assert(SC::Get(m, SaveRupees, 2) == 200);
        m.words[SaveUpgrades] = 2u << 12; Tick(m); assert(SC::Get(m, SaveRupees, 2) == 500);
        unbreakable_razor_sword = true; SC::Set(m, SaveRazorSwordHits, 40, 2);
        SC::Set(m, SaveSwordShield, 0x21, 1); Tick(m); assert(SC::Get(m, SaveRazorSwordHits, 2) == 40);
        SC::Set(m, SaveSwordShield, 0x12, 1); Tick(m); assert(SC::Get(m, SaveRazorSwordHits, 2) == 100);
        // Freeze time keeps normal ticks out but lets big jumps through
        freeze_time = true; SC::Set(m, SaveTime, 0x5000, 2); Tick(m);
        SC::Set(m, SaveTime, 0x5002, 2); Tick(m); assert(SC::Get(m, SaveTime, 2) == 0x5000);
        SC::Set(m, SaveTime, 0x9000, 2); Tick(m); assert(SC::Get(m, SaveTime, 2) == 0x9000);
        SC::Set(m, SaveTime, 0x9003, 2); Tick(m); assert(SC::Get(m, SaveTime, 2) == 0x9000);
        // paused frames do nothing
        m.words[Game + 0xaac] = 1; SC::Set(m, SaveTime, 0x9003, 2); Tick(m); assert(SC::Get(m, SaveTime, 2) == 0x9003);
        m.words[Game + 0xaac] = 0; freeze_time = false;
        // Time moves when you move
        time_moves_when_you_move = true; Tick(m);
        SC::Set(m, SaveTime, 0x9005, 2); Tick(m); assert(SC::Get(m, SaveTime, 2) == 0x9003);
        m.words[Player + PlayerSpeedXZ] = std::bit_cast<U32>(6.f);
        SC::Set(m, SaveTime, 0x9005, 2); Tick(m); assert(SC::Get(m, SaveTime, 2) == 0x9005);
        time_moves_when_you_move = false;
        // Run speed scales and restores
        run_speed_percent = 200; Tick(m);
        assert(SC::Get(m, Static + StaticRunSpeed, 2) == 1100 && SC::Get(m, Static + StaticRunAccel, 2) == 400);
        Tick(m); assert(SC::Get(m, Static + StaticRunSpeed, 2) == 1100); // not compounded
        SC::Set(m, Static + StaticRunSpeed, 350, 2); Tick(m); assert(SC::Get(m, Static + StaticRunSpeed, 2) == 700);
        run_speed_percent = 100; Tick(m); assert(SC::Get(m, Static + StaticRunSpeed, 2) == 350 &&
                                                SC::Get(m, Static + StaticRunAccel, 2) == 200);
        // Goron rolling: magic restored only while spike rolling as a Goron
        goron_rolling_ignores_magic = true; SC::Set(m, SaveMagic, 40, 1); SC::Set(m, SaveForm, 1, 1); Tick(m);
        m.words[Player + PlayerFlags3] = GoronRollingFast; SC::Set(m, SaveMagic, 38, 1); Tick(m);
        assert(SC::Get(m, SaveMagic, 1) == 40);
        m.words[Player + PlayerFlags3] = 0; SC::Set(m, SaveMagic, 30, 1); Tick(m); assert(SC::Get(m, SaveMagic, 1) == 30);
        // Blast Mask cooldown
        blast_mask_cooldown_percent = 50; SC::Set(m, Player + PlayerBlastMaskTimer, 310, 2); Tick(m);
        assert(SC::Get(m, Player + PlayerBlastMaskTimer, 2) == 155);
        SC::Set(m, Player + PlayerBlastMaskTimer, 154, 2); Tick(m); assert(SC::Get(m, Player + PlayerBlastMaskTimer, 2) == 154);
        blast_mask_cooldown_percent = 0; SC::Set(m, Player + PlayerBlastMaskTimer, 310, 2); Tick(m);
        assert(SC::Get(m, Player + PlayerBlastMaskTimer, 2) == 0);
        // Not in gameplay: nothing written
        m.words[Game + 0x13c] = 0x0102; SC::Set(m, SaveRupees, 1, 2); before = m.words; Tick(m); assert(m.words == before);
    }
    // Inverted Song of Time kept through the Song of Time
    {
        Off(); auto m = Fixture(); keep_inverted_time = true;
        auto set = [&](std::int32_t speed, std::int32_t day, U32 time) {
            m.words[SaveExtraTimeSpeed] = std::bit_cast<U32>(speed); m.words[SaveDay] = std::bit_cast<U32>(day);
            SC::Set(m, SaveTime, time, 2); Tick(m);
        };
        set(-2, 3, 0x2000);
        m.words[Game + 0x13c] = 0x0102; Tick(m); m.words[Game + 0x13c] = 0x0402; // reset outside gameplay
        set(0, 1, 0x4000);
        assert(std::bit_cast<std::int32_t>(*m.Read(SaveExtraTimeSpeed)) == -2);
        // Song of Time on the first day also counts as a new cycle
        set(-2, 1, 0x8000); set(0, 1, 0x4000);
        assert(std::bit_cast<std::int32_t>(*m.Read(SaveExtraTimeSpeed)) == -2);
        // Played again to go back to normal speed: stays normal
        for (int i = 0; i < 400; ++i) set(0, 2, 0x6000 + i);
        set(0, 1, 0x4000);
        assert(*m.Read(SaveExtraTimeSpeed) == 0);
        // Option off: never restored
        keep_inverted_time = false; set(-1, 3, 0x2000); set(0, 1, 0x4000);
        assert(*m.Read(SaveExtraTimeSpeed) == 0);
    }
    std::cout << "ship enhancements: ok\n";
    return 0;
}
