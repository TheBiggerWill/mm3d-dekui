// SPDX-License-Identifier: GPL-2.0-or-later
// Save editor for the Dekui settings menu, modelled on 2 Ship 2 Harkinian's.
//
// The menu runs on the UI thread and never touches guest memory. Once per game frame (from
// Restoration's pad-update hook, like the 2Ship options) Tick() applies the edits the menu
// queued and copies the save fields into a snapshot that the menu draws from.
//
// Addresses are for the USA Rev 1 game with Project Restoration (see simple_cheats.h). Beyond
// the ones the cheats already use, they were checked against the game code: the magic grant and
// meter set-up at 0x171084, the bank at 0x426aa0 and the stray fairy pick-up at 0x25a6ec.
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

#include "mm3d/simple_cheats.h"

namespace MM3D::SaveEditor {
using U32 = std::uint32_t;
using U8 = std::uint8_t;

// ---- addresses
constexpr U32 Common = 0x7751d8;
constexpr U32 Day = Common + 0x14;          // s32, 1..3
constexpr U32 Time = Common + 0x20;         // u16, 0x4000 = 6:00, 0x10000 = one day
constexpr U32 PendingRupees = 0x7751fc;     // s16, rupees still being counted in or out
constexpr U32 Form = 0x7751fe;              // u8: 0 Fierce Deity, 1 Goron, 2 Zora, 3 Deku, 4 Human
constexpr U32 HealthMax = 0x775312, Health = 0x775314;  // u16, 16 per heart
constexpr U32 MagicLevel = 0x775316, Magic = 0x775317;  // u8
constexpr U32 Rupees = 0x775318, RazorHits = 0x77531a;  // u16
constexpr U32 MagicAcquired = 0x77531e, DoubleMagic = 0x77531f, DoubleDefense = 0x775320; // u8
constexpr U32 OwlStatues = 0x775324;        // u16, one bit per statue
constexpr U32 ButtonB = 0x77532a;           // u8, item on B for human form (Fierce Deity shares it)
constexpr U32 SwordShield = 0x775352;       // u8, sword in bits 0-3, shield in bits 4-7
constexpr U32 Items = 0x775354, Masks = 0x77536c, Bottles = 0x775384; // u8 item ids
constexpr U32 Ammo = 0x775390;              // u8 per item slot
constexpr U32 Upgrades = 0x7753cc, Quest = 0x7753d0;    // u32 bit fields
constexpr U32 DungeonItems = 0x7753d4, SmallKeys = 0x7753de, StrayFairies = 0x7753e8; // u8 per temple
constexpr U32 Bank = 0x776408;              // u32, balance in the low 16 bits
constexpr U32 First = Common, End = 0x775400; // the snapshot covers [First, End) and Bank

constexpr U8 NoItem = 0xff;
constexpr U32 MaskCount = 24, BottleCount = 7, TempleCount = 4;
constexpr U32 QuiverBits = 7u << 0, BombBagBits = 7u << 3, WalletBits = 3u << 12;
constexpr U32 NotebookBit = 18, HeartPieceShift = 28;
constexpr U32 BossKeyBit = 1, CompassBit = 2, MapBit = 4;
constexpr U8 FirstMask = 0x32, KokiriSword = 0x4d;

// ---- names
constexpr std::array<const char*, 0x4a> ItemNames{
    "Ocarina of Time", "Hero's Bow", "Fire Arrow", "Ice Arrow", "Light Arrow", "Fairy Ocarina",
    "Bombs", "Bombchus", "Deku Sticks", "Deku Nuts", "Magic Beans", "Fairy Slingshot",
    "Powder Keg", "Pictograph Box", "Lens of Truth", "Hookshot", "Great Fairy's Sword", "Longshot",
    "Empty Bottle", "Red Potion", "Green Potion", "Blue Potion", "Fairy", "Deku Princess",
    "Milk", "Milk (half)", "Fish", "Bug", "Blue Fire", "Poe", "Big Poe", "Spring Water",
    "Hot Spring Water", "Zora Egg", "Gold Dust", "Magical Mushroom", "Seahorse", "Chateau Romani",
    "Mystery Milk", "Mystery Milk (spoiled)", "Moon's Tear", "Land Title Deed",
    "Swamp Title Deed", "Mountain Title Deed", "Ocean Title Deed", "Room Key",
    "Express Mail to Mama", "Letter to Kafei", "Pendant of Memories", "Map",
    "Deku Mask", "Goron Mask", "Zora Mask", "Fierce Deity's Mask", "Mask of Truth", "Kafei's Mask",
    "All-Night Mask", "Bunny Hood", "Keaton Mask", "Garo's Mask", "Romani's Mask",
    "Circus Leader's Mask", "Postman's Hat", "Couple's Mask", "Great Fairy's Mask", "Gibdo Mask",
    "Don Gero's Mask", "Kamaro's Mask", "Captain's Hat", "Stone Mask", "Bremen Mask", "Blast Mask",
    "Mask of Scents", "Giant's Mask"};

inline void ItemLabel(U8 id, char* out, std::size_t size) {
    if (id == NoItem) std::snprintf(out, size, "(none)");
    else if (id < ItemNames.size()) std::snprintf(out, size, "%s", ItemNames[id]);
    else std::snprintf(out, size, "Item %02X", unsigned(id));
}

// Item screen slots 0-17. Each holds one item, except the three trade slots. Slots 18-23 are
// not used by the 3DS game (bottles have their own array).
struct Slot { const char* label; std::array<U8, 5> items; U32 count; };
constexpr std::array<Slot, 18> ItemSlots{{
    {"Ocarina of Time", {0x00}, 1}, {"Hero's Bow", {0x01}, 1}, {"Fire Arrow", {0x02}, 1},
    {"Ice Arrow", {0x03}, 1}, {"Light Arrow", {0x04}, 1},
    {"Moon's Tear / title deeds", {0x28, 0x29, 0x2a, 0x2b, 0x2c}, 5},
    {"Bombs", {0x06}, 1}, {"Bombchus", {0x07}, 1}, {"Deku Sticks", {0x08}, 1},
    {"Deku Nuts", {0x09}, 1}, {"Magic Beans", {0x0a}, 1},
    {"Room Key / Express Mail", {0x2d, 0x2e}, 2},
    {"Powder Keg", {0x0c}, 1}, {"Pictograph Box", {0x0d}, 1}, {"Lens of Truth", {0x0e}, 1},
    {"Hookshot", {0x0f}, 1}, {"Great Fairy's Sword", {0x10}, 1},
    {"Letter to Kafei / Pendant", {0x2f, 0x30}, 2},
}};
struct AmmoField { const char* label; U32 slot; int max; };
constexpr std::array<AmmoField, 7> AmmoFields{{
    {"Arrows", 1, 99}, {"Bombs", 6, 99}, {"Bombchus", 7, 99}, {"Deku Sticks", 8, 99},
    {"Deku Nuts", 9, 99}, {"Magic Beans", 10, 99}, {"Powder Kegs", 12, 99}}};
struct Flag { const char* label; U32 bit; };
constexpr std::array<Flag, 4> Remains{{
    {"Odolwa's Remains", 0}, {"Goht's Remains", 1}, {"Gyorg's Remains", 2}, {"Twinmold's Remains", 3}}};
constexpr std::array<Flag, 11> Songs{{
    {"Song of Time", 12}, {"Song of Healing", 13}, {"Epona's Song", 14}, {"Song of Soaring", 15},
    {"Song of Storms", 16}, {"Sonata of Awakening", 6}, {"Goron Lullaby", 7},
    {"Lullaby Intro", 24}, {"New Wave Bossa Nova", 8}, {"Elegy of Emptiness", 9},
    {"Oath to Order", 10}}};
// Owl statue bits are the statues' actor parameters (Restoration's owl fix uses the same bits).
constexpr std::array<const char*, 10> OwlNames{
    "Great Bay Coast", "Zora Cape", "Snowhead", "Mountain Village", "Clock Town", "Milk Road",
    "Woodfall", "Southern Swamp", "Ikana Canyon", "Stone Tower"};
constexpr std::array<const char*, TempleCount> TempleNames{
    "Woodfall Temple", "Snowhead Temple", "Great Bay Temple", "Stone Tower Temple"};
constexpr std::array<const char*, 4> SwordNames{"None", "Kokiri Sword", "Razor Sword", "Gilded Sword"};
constexpr std::array<const char*, 3> ShieldNames{"None", "Hero's Shield", "Mirror Shield"};
constexpr std::array<const char*, 3> WalletNames{
    "Child's Wallet (99)", "Adult's Wallet (200)", "Giant's Wallet (500)"};
constexpr std::array<const char*, 4> QuiverNames{
    "None", "Quiver (30)", "Big Quiver (40)", "Biggest Quiver (50)"};
constexpr std::array<const char*, 4> BombBagNames{
    "None", "Bomb Bag (20)", "Big Bomb Bag (30)", "Biggest Bomb Bag (40)"};
constexpr std::array<const char*, 3> MagicNames{"None", "Magic meter", "Double magic"};

// ---- edits: a masked write to one aligned word
struct Edit { U32 at, mask, value; };
using Edits = std::vector<Edit>;
// Bits `mask` of the field at byte address `at` (any alignment within its word).
constexpr Edit Masked(U32 at, U32 mask, U32 value) {
    const U32 shift = (at & 3) * 8;
    return {at & ~3u, mask << shift, (value << shift) & (mask << shift)};
}
constexpr Edit Field(U32 at, unsigned bytes, U32 value) {
    return Masked(at, bytes == 1 ? 0xffu : bytes == 2 ? 0xffffu : 0xffffffffu, value);
}

struct Snapshot {
    bool valid = false;
    std::array<U8, End - First> bytes{};
    U32 bank = 0;
    U32 Byte(U32 at) const { return at >= First && at < End ? bytes[at - First] : 0; }
    U32 Half(U32 at) const { return Byte(at) | Byte(at + 1) << 8; }
    U32 Word(U32 at) const { return at == Bank ? bank : Half(at) | Half(at + 2) << 16; }
    void Apply(const Edit& e) {
        if (e.at == Bank) {
            bank = (bank & ~e.mask) | (e.value & e.mask);
            return;
        }
        for (U32 i = 0; i < 4; ++i) {
            const U32 at = e.at + i, m = (e.mask >> (8 * i)) & 0xff;
            if (m && at >= First && at < End)
                bytes[at - First] = U8((bytes[at - First] & ~m) | ((e.value >> (8 * i)) & m));
        }
    }
};

// ---- what the menu edits
constexpr U32 Minutes(U32 time) { return (time & 0xffff) * 1440 / 0x10000; }
constexpr U32 TimeOf(U32 minutes) { return ((minutes % 1440) * 0x10000 + 1439) / 1440; }
inline U32 HeartPieces(const Snapshot& s) { return (s.Word(Quest) >> HeartPieceShift) & 3; }
inline int MagicKind(const Snapshot& s) {
    return !s.Byte(MagicAcquired) ? 0 : s.Byte(DoubleMagic) ? 2 : 1;
}
inline U32 MagicCapacity(const Snapshot& s) { return MagicKind(s) * 48u; }

inline Edits SetRupees(U32 rupees) {
    return {Field(Rupees, 2, std::min<U32>(rupees, 999)), Field(PendingRupees, 2, 0)};
}
inline Edits SetBank(U32 rupees) { return {Masked(Bank, 0xffff, std::min<U32>(rupees, 5000))}; }
inline Edits SetHeartContainers(const Snapshot& s, U32 hearts) {
    const U32 max = std::clamp<U32>(hearts, 1, 20) * 16;
    Edits e{Field(HealthMax, 2, max)};
    if (s.Half(Health) > max) e.push_back(Field(Health, 2, max));
    return e;
}
inline Edits SetHealth(const Snapshot& s, U32 sixteenths) {
    return {Field(Health, 2, std::clamp<U32>(sixteenths, 1, s.Half(HealthMax)))};
}
// When magic is acquired and the meter level is 0, the game sets the level from the double magic
// flag and grows the meter to the current magic (0x1710a4), as it does for the Great Fairy.
inline Edits SetMagicKind(int kind) {
    kind = std::clamp(kind, 0, 2);
    return {Field(MagicAcquired, 1, kind > 0), Field(DoubleMagic, 1, kind == 2),
            Field(MagicLevel, 1, 0), Field(Magic, 1, U32(kind) * 48)};
}
inline Edits SetMagic(const Snapshot& s, U32 magic) {
    return {Field(Magic, 1, std::min<U32>(magic, MagicCapacity(s)))};
}
inline Edits SetSword(const Snapshot& s, U32 sword) {
    sword = std::min<U32>(sword, 3);
    Edits e{Masked(SwordShield, 0x0f, sword)};
    // B shows the sword in human form. Fierce Deity shares those buttons with its own sword
    // and the game puts the human sword back when the mask comes off, so leave B alone then.
    const U32 b = s.Byte(ButtonB);
    if (s.Byte(Form) != 0 && (b == NoItem || (b >= KokiriSword && b < KokiriSword + 3)))
        e.push_back(Field(ButtonB, 1, sword ? KokiriSword + sword - 1 : NoItem));
    if (sword == 2) e.push_back(Field(RazorHits, 2, 100));
    return e;
}
inline Edits SetShield(U32 shield) { return {Masked(SwordShield, 0xf0, std::min<U32>(shield, 2) << 4)}; }
inline Edits SetTime(U32 minutes) { return {Field(Time, 2, TimeOf(minutes))}; }
inline Edits SetDay(U32 day) { return {Field(Day, 4, std::clamp<U32>(day, 1, 3))}; }
inline Edits SetQuestBit(U32 bit, bool on) { return {Masked(Quest, 1u << bit, on ? ~0u : 0)}; }
inline Edits SetHeartPieces(U32 pieces) {
    return {Masked(Quest, 15u << HeartPieceShift, std::min<U32>(pieces, 3) << HeartPieceShift)};
}
inline Edits SetUpgrade(U32 bits, U32 value) {
    const U32 shift = U32(std::countr_zero(bits));
    return {Masked(Upgrades, bits, value << shift)};
}
inline U32 Upgrade(const Snapshot& s, U32 bits) {
    return (s.Word(Upgrades) & bits) >> U32(std::countr_zero(bits));
}
inline Edits SetByteBit(U32 at, U32 bit, bool on) { return {Masked(at, bit & 0xff, on ? bit : 0)}; }
inline Edits SetOwl(U32 index, bool on) { return {Masked(OwlStatues, 1u << index, on ? ~0u : 0)}; }
inline Edits SetAllMasks(bool on) {
    Edits e;
    for (U32 i = 0; i < MaskCount; ++i) e.push_back(Field(Masks + i, 1, on ? FirstMask + i : NoItem));
    return e;
}

// ---- shared between the menu and the emulation thread
inline std::mutex mutex;
inline Snapshot shared;
inline std::vector<Edit> queue;

inline Snapshot Current() {
    std::scoped_lock lock{mutex};
    return shared;
}
inline void Queue(const Edits& edits) {
    std::scoped_lock lock{mutex};
    if (!shared.valid) return;
    for (const auto& e : edits) {
        queue.push_back(e);
        shared.Apply(e); // shown straight away, before the game has the change
    }
}
inline bool Pending() {
    std::scoped_lock lock{mutex};
    return !queue.empty();
}

// Emulation thread, once per game frame. Returns true when an edit set the clock (the 2Ship
// time options keep the previous time and would otherwise treat a small change as a tick).
template <class M> bool Tick(M& m) {
    std::scoped_lock lock{mutex};
    if (!SimpleCheats::CodeMatches(m) || !SimpleCheats::InGame(m)) {
        queue.clear(); // never carry edits into another save
        shared.valid = false;
        return false;
    }
    bool clock = false;
    for (const auto& e : queue) {
        if (const auto word = m.Read(e.at)) {
            m.Write(e.at, (*word & ~e.mask) | (e.value & e.mask));
            clock |= e.at == (Time & ~3u) && (e.mask & (0xffffu << (Time & 3) * 8));
        }
    }
    queue.clear();
    Snapshot s;
    for (U32 at = First; at < End; at += 4) {
        const auto word = m.Read(at);
        if (!word) {
            shared.valid = false;
            return clock;
        }
        for (U32 i = 0; i < 4; ++i) s.bytes[at - First + i] = U8(*word >> (8 * i));
    }
    const auto bank = m.Read(Bank);
    if (!bank) {
        shared.valid = false;
        return clock;
    }
    s.bank = *bank;
    s.valid = true;
    shared = s;
    return clock;
}
} // namespace MM3D::SaveEditor
