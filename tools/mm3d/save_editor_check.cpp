// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/save_editor.h"
#include <cassert>
#include <iostream>
#include <string_view>
#include <unordered_map>
using namespace MM3D::SaveEditor;
namespace SC = MM3D::SimpleCheats;
struct Memory {
    std::unordered_map<U32, U32> words;
    int writes = 0;
    std::optional<U32> Read(U32 a) {
        auto i = words.find(a);
        return i == words.end() ? std::nullopt : std::optional<U32>(i->second);
    }
    void Write(U32 a, U32 v) { assert(words.contains(a)); words[a] = v; ++writes; }
    U32 Byte(U32 a) { return (words.at(a & ~3u) >> (a & 3) * 8) & 0xff; }
    U32 Half(U32 a) { return (words.at(a & ~3u) >> (a & 3) * 8) & 0xffff; }
};
constexpr U32 Game = 0x9100000;
Memory Fixture() {
    Memory m;
    for (auto [a, w] : SC::Guards) m.words[a] = w;
    m.words[0x72f5c0] = 0x9000000; m.words[0x900016c] = Game; m.words[Game + 0x13c] = 0x0402;
    for (U32 a = First; a < End; a += 4) m.words[a] = 0;
    m.words[Bank] = 0xabcd0000 | 1234;
    SC::Set(m, HealthMax, 80, 2); SC::Set(m, Health, 64, 2);
    SC::Set(m, Form, 4, 1);
    SC::Set(m, ButtonB, KokiriSword, 1);
    SC::Set(m, SwordShield, 0x11, 1);
    SC::Set(m, Time, 0x4000, 2);
    m.words[Day] = 1;
    m.writes = 0;
    return m;
}
void Clear() {
    std::scoped_lock lock{mutex};
    queue.clear();
    shared = {};
}
Snapshot Frame(Memory& m) {
    Tick(m);
    return Current();
}
int main() {
    // Field / Bits encode a masked write to the aligned word.
    {
        constexpr Edit a = Field(0x775317, 1, 0x1ff);
        static_assert(a.at == 0x775314 && a.mask == 0xff000000u && a.value == 0xff000000u);
        constexpr Edit b = Field(0x775312, 2, 320);
        static_assert(b.at == 0x775310 && b.mask == 0xffff0000u && b.value == 320u << 16);
        constexpr Edit c = Field(Day, 4, 3);
        static_assert(c.at == Day && c.mask == 0xffffffffu && c.value == 3);
        constexpr Edit d = Masked(Quest, 1u << 12, ~0u);
        static_assert(d.at == Quest && d.mask == 1u << 12 && d.value == 1u << 12);
        constexpr Edit e = Masked(SwordShield, 0xf0, 0x123);
        static_assert(e.at == 0x775350 && e.mask == 0xf00000u && e.value == 0x200000u);
    }
    // Clock conversions round-trip every minute and stay in a day.
    for (U32 minute = 0; minute < 1440; ++minute) {
        assert(TimeOf(minute) < 0x10000);
        assert(Minutes(TimeOf(minute)) == minute);
    }
    static_assert(TimeOf(6 * 60) == 0x4000 && TimeOf(12 * 60) == 0x8000 && Minutes(0xc000) == 18 * 60);

    // Snapshot reads the save, the bank and applies edits locally.
    {
        Clear();
        auto m = Fixture();
        const auto s = Frame(m);
        assert(s.valid && s.Half(HealthMax) == 80 && s.Byte(Form) == 4 && s.Word(Day) == 1);
        assert((s.Word(Bank) & 0xffff) == 1234 && s.Half(Time) == 0x4000);
        assert(m.writes == 0); // reading never writes
    }
    // Queued edits reach guest memory on the next frame, keep neighbouring bytes, and show at once.
    {
        Clear();
        auto m = Fixture();
        SC::Set(m, RazorHits, 77, 2);
        auto s = Frame(m);
        SC::Set(m, PendingRupees, 30, 2);
        Queue(SetRupees(1500));
        assert(Current().Half(Rupees) == 999);
        assert(m.Half(Rupees) == 0);
        s = Frame(m);
        assert(m.Half(Rupees) == 999 && m.Half(PendingRupees) == 0 && m.Half(RazorHits) == 77);
        assert(s.Half(Rupees) == 999 && !Pending());
        Queue(SetBank(9000));
        Frame(m);
        assert(m.words[Bank] == (0xabcd0000u | 5000));
    }
    // Hearts: containers clamp health; health stays at least a quarter heart.
    {
        Clear();
        auto m = Fixture();
        auto s = Frame(m);
        Queue(SetHeartContainers(s, 3));
        s = Frame(m);
        assert(m.Half(HealthMax) == 48 && m.Half(Health) == 48);
        Queue(SetHeartContainers(s, 40));
        s = Frame(m);
        assert(m.Half(HealthMax) == 320 && m.Half(Health) == 48);
        Queue(SetHealth(s, 0));
        Frame(m);
        assert(m.Half(Health) == 1);
    }
    // Sword: nibble, B button in human form, Razor durability; Fierce Deity's B is left alone.
    {
        Clear();
        auto m = Fixture();
        auto s = Frame(m);
        Queue(SetSword(s, 2));
        s = Frame(m);
        assert(m.Byte(SwordShield) == 0x12 && m.Byte(ButtonB) == KokiriSword + 1 && m.Half(RazorHits) == 100);
        Queue(SetSword(s, 0));
        s = Frame(m);
        assert(m.Byte(SwordShield) == 0x10 && m.Byte(ButtonB) == NoItem);
        Queue(SetSword(s, 3));
        s = Frame(m);
        assert(m.Byte(ButtonB) == KokiriSword + 2);
        SC::Set(m, Form, 0, 1);          // wearing Fierce Deity's Mask
        SC::Set(m, ButtonB, 0x99, 1);
        s = Frame(m);
        Queue(SetSword(s, 1));
        s = Frame(m);
        assert(m.Byte(SwordShield) == 0x11 && m.Byte(ButtonB) == 0x99);
        SC::Set(m, Form, 3, 1);          // Deku form: human B still updated
        SC::Set(m, ButtonB, KokiriSword, 1);
        s = Frame(m);
        Queue(SetSword(s, 3));
        Frame(m);
        assert(m.Byte(ButtonB) == KokiriSword + 2);
        s = Frame(m);
        Queue(SetShield(2));
        Frame(m);
        assert(m.Byte(SwordShield) == 0x23);
    }
    // Magic: flags set and the level cleared so the game grows the meter itself.
    {
        Clear();
        auto m = Fixture();
        auto s = Frame(m);
        assert(MagicKind(s) == 0 && MagicCapacity(s) == 0);
        Queue(SetMagicKind(2));
        s = Frame(m);
        assert(m.Byte(MagicAcquired) == 1 && m.Byte(DoubleMagic) == 1 && m.Byte(MagicLevel) == 0 &&
               m.Byte(Magic) == 96 && MagicKind(s) == 2 && MagicCapacity(s) == 96);
        Queue(SetMagic(s, 200));
        s = Frame(m);
        assert(m.Byte(Magic) == 96);
        SC::Set(m, MagicLevel, 2, 1);
        Queue(SetMagicKind(1));
        s = Frame(m);
        assert(m.Byte(DoubleMagic) == 0 && m.Byte(MagicLevel) == 0 && m.Byte(Magic) == 48 && MagicKind(s) == 1);
        Queue(SetMagicKind(0));
        s = Frame(m);
        assert(m.Byte(MagicAcquired) == 0 && m.Byte(Magic) == 0 && MagicKind(s) == 0);
    }
    // Quest bits, heart pieces, upgrades, owls, temple items and masks.
    {
        Clear();
        auto m = Fixture();
        m.words[Quest] = 0x10000001; // Odolwa + 1 piece
        m.words[Upgrades] = 0x1009;  // quiver 1, bomb bag 1, adult wallet
        auto s = Frame(m);
        assert(HeartPieces(s) == 1 && Upgrade(s, WalletBits) == 1 && Upgrade(s, QuiverBits) == 1);
        Queue(SetQuestBit(12, true));
        Queue(SetHeartPieces(3));
        Queue(SetUpgrade(WalletBits, 2));
        Queue(SetUpgrade(QuiverBits, 3));
        Queue(SetUpgrade(BombBagBits, 0));
        s = Frame(m);
        assert(m.words[Quest] == 0x30001001u && m.words[Upgrades] == 0x2003u);
        Queue(SetQuestBit(0, false));
        Queue(SetOwl(4, true));
        Queue(SetOwl(9, true));
        s = Frame(m);
        assert(m.words[Quest] == 0x30001000u && m.Half(OwlStatues) == 0x210);
        Queue(SetOwl(4, false));
        Queue(SetByteBit(DungeonItems + 2, MapBit, true));
        s = Frame(m);
        Queue(SetByteBit(DungeonItems + 2, BossKeyBit, true));
        Queue({Field(SmallKeys + 3, 1, 4), Field(StrayFairies + 1, 1, 15)});
        s = Frame(m);
        assert(m.Half(OwlStatues) == 0x200 && m.Byte(DungeonItems + 2) == (MapBit | BossKeyBit));
        assert(m.Byte(SmallKeys + 3) == 4 && m.Byte(StrayFairies + 1) == 15 && m.Byte(StrayFairies) == 0);
        Queue(SetAllMasks(true));
        s = Frame(m);
        for (U32 i = 0; i < MaskCount; ++i) assert(m.Byte(Masks + i) == FirstMask + i);
        assert(m.Byte(Bottles) == 0 && m.Byte(Items + 23) == 0); // neighbours untouched
        Queue(SetAllMasks(false));
        Frame(m);
        for (U32 i = 0; i < MaskCount; ++i) assert(m.Byte(Masks + i) == NoItem);
    }
    // Clock edits are reported (2Ship's freeze time must accept them); other edits are not.
    {
        Clear();
        auto m = Fixture();
        Frame(m);
        Queue(SetRupees(5));
        assert(!Tick(m));
        Queue(SetTime(7 * 60 + 30));
        assert(Tick(m));
        assert(m.Half(Time) == TimeOf(450));
        Queue(SetDay(9));
        assert(!Tick(m));
        assert(m.words[Day] == 3);
    }
    // Outside gameplay or on another build: nothing is written, queued edits are dropped,
    // and the menu can't queue new ones.
    {
        Clear();
        auto m = Fixture();
        Frame(m);
        Queue(SetRupees(50));
        m.words[Game + 0x13c] = 0x0401; // title screen / file select
        assert(!Tick(m) && m.writes == 0 && !Current().valid && !Pending());
        Queue(SetRupees(60));
        assert(!Pending());
        m.words[Game + 0x13c] = 0x0402;
        assert(Frame(m).valid && m.Half(Rupees) == 0);
        m.words[SC::Guards[0].first] ^= 1; // different code
        Queue(SetRupees(70));
        assert(!Tick(m) && m.Half(Rupees) == 0 && !Current().valid);
    }
    // Names and slot tables line up with the item ids.
    {
        static_assert(ItemNames[FirstMask] == std::string_view("Deku Mask"));
        static_assert(ItemNames[FirstMask + MaskCount - 1] == std::string_view("Giant's Mask"));
        static_assert(ItemNames[0x46] == std::string_view("Bremen Mask"));
        static_assert(ItemNames[0x30] == std::string_view("Pendant of Memories"));
        for (const char* name : ItemNames) assert(name && *name);
        for (U32 slot = 0; slot < ItemSlots.size(); ++slot) {
            const auto& s = ItemSlots[slot];
            assert(s.count >= 1 && s.count <= s.items.size());
            if (s.count == 1) assert(s.items[0] == slot); // the item id matches its slot
        }
        char text[48];
        ItemLabel(NoItem, text, sizeof text); assert(std::string_view(text) == "(none)");
        ItemLabel(0x13, text, sizeof text); assert(std::string_view(text) == "Red Potion");
        ItemLabel(0x90, text, sizeof text); assert(std::string_view(text) == "Item 90");
    }
    std::cout << "save editor checks passed\n";
}
