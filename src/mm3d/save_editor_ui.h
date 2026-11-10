// SPDX-License-Identifier: GPL-2.0-or-later
// The Save Editor tab of the Dekui settings menu (see save_editor.h). Only ImGui and the
// snapshot/queue from save_editor.h are used here, so the tab can be drawn in a test.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <imgui.h>

#include "mm3d/save_editor.h"

namespace MM3D::SaveEditor {

// `help` shows a tooltip for the last item (the overlay's hover_help).
template <class Help> void Draw(Help&& help) {
    Snapshot s = Current();
    ImGui::TextWrapped("Edits the save you're playing. Changes go into the game straight away; "
                       "save in the game (owl statue or Song of Time) to keep them.");
    if (!s.valid) {
        ImGui::TextWrapped("Load a save and start playing to edit it.");
        return;
    }
    const auto put = [&](const Edits& edits) {
        Queue(edits);
        for (const auto& e : edits) s.Apply(e); // later widgets this frame see the change
    };
    const auto number = [&](const char* label, int value, int low, int high, auto&& apply) {
        int v = value;
        if (ImGui::InputInt(label, &v) && std::clamp(v, low, high) != value)
            apply(std::clamp(v, low, high));
    };
    const auto check = [&](const char* label, bool on, auto&& apply) {
        bool v = on;
        if (ImGui::Checkbox(label, &v)) apply(v);
    };
    const auto choice = [&](const char* label, U32 value, const auto& names, auto&& apply) {
        char other[24];
        std::snprintf(other, sizeof other, "Other (%u)", unsigned(value));
        if (ImGui::BeginCombo(label, value < names.size() ? names[value] : other)) {
            for (U32 i = 0; i < names.size(); ++i)
                if (ImGui::Selectable(names[i], i == value) && i != value) apply(i);
            ImGui::EndCombo();
        }
    };
    const auto item_choice = [&](const char* label, U32 at, const U8* items, U32 count) {
        const U8 current = U8(s.Byte(at));
        char preview[48];
        ItemLabel(current, preview, sizeof preview);
        if (ImGui::BeginCombo(label, preview)) {
            const auto option = [&](U8 id) {
                char text[48];
                ItemLabel(id, text, sizeof text);
                ImGui::PushID(id);
                if (ImGui::Selectable(text, id == current) && id != current) put({Field(at, 1, id)});
                ImGui::PopID();
            };
            option(NoItem);
            for (U32 i = 0; i < count; ++i) option(items[i]);
            ImGui::EndCombo();
        }
    };
    const auto columns = [&](const char* id, U32 count, int n, auto&& cell) {
        if (ImGui::BeginTable(id, n)) {
            for (U32 i = 0; i < count; ++i) {
                ImGui::TableNextColumn();
                ImGui::PushID(int(i));
                cell(i);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    };

    if (ImGui::BeginTabBar("Save editor sections")) {
        if (ImGui::BeginTabItem("General")) {
            ImGui::SeparatorText("Rupees");
            number("Rupees", int(s.Half(Rupees)), 0, 999, [&](int v) { put(SetRupees(U32(v))); });
            help("Your wallet holds 99, 200 or 500. More is allowed here, but picking up rupees caps it again.");
            number("Bank balance", int(s.Word(Bank) & 0xffff), 0, 5000, [&](int v) { put(SetBank(U32(v))); });
            help("Rupees deposited at the bank in West Clock Town, up to 5,000. It survives the Song of Time.");

            ImGui::SeparatorText("Health");
            number("Heart containers", int(s.Half(HealthMax) / 16), 1, 20,
                   [&](int v) { put(SetHeartContainers(s, U32(v))); });
            const float most = std::max(0.25f, float(s.Half(HealthMax)) / 16.f);
            float hearts = std::clamp(float(s.Half(Health)) / 16.f, 0.25f, most);
            if (ImGui::SliderFloat("Health", &hearts, 0.25f, most, "%.2f hearts", ImGuiSliderFlags_AlwaysClamp))
                put(SetHealth(s, U32(std::lround(hearts * 4.f)) * 4));
            number("Pieces of heart", int(HeartPieces(s)), 0, 3, [&](int v) { put(SetHeartPieces(U32(v))); });
            help("Pieces towards the next heart container (0 to 3).");
            check("Enhanced defense", s.Byte(DoubleDefense) != 0,
                  [&](bool v) { put({Field(DoubleDefense, 1, v)}); });
            help("Halves the damage you take, like the Great Fairy of Ikana's gift. "
                 "The heart outlines may only change after you enter a new area.");

            ImGui::SeparatorText("Magic");
            choice("Magic", U32(MagicKind(s)), MagicNames, [&](U32 v) { put(SetMagicKind(int(v))); });
            help("The meter grows into place, as when a Great Fairy gives it to you.");
            if (MagicKind(s)) {
                int magic = int(s.Byte(Magic));
                if (ImGui::SliderInt("Magic points", &magic, 0, int(MagicCapacity(s)), "%d",
                                     ImGuiSliderFlags_AlwaysClamp))
                    put(SetMagic(s, U32(magic)));
            }

            ImGui::SeparatorText("Day and time");
            int day = std::clamp(int(s.Word(Day)), 1, 3);
            if (ImGui::SliderInt("Day", &day, 1, 3, "Day %d", ImGuiSliderFlags_AlwaysClamp))
                put(SetDay(U32(day)));
            const U32 minutes = Minutes(s.Half(Time));
            int hour = int(minutes / 60), minute = int(minutes % 60);
            bool clock = ImGui::SliderInt("Hour", &hour, 0, 23, "%d:00", ImGuiSliderFlags_AlwaysClamp);
            clock |= ImGui::SliderInt("Minute", &minute, 0, 59, ":%02d", ImGuiSliderFlags_AlwaysClamp);
            if (clock) put(SetTime(U32(hour * 60 + minute)));
            ImGui::Text("Clock: %d:%02d %s", (hour + 11) % 12 + 1, minute, hour < 12 ? "a.m." : "p.m.");
            help("Each day starts at 6:00 a.m.; midnight to 6:00 a.m. is the end of that day's night. "
                 "People and events catch up when you next enter an area.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Items")) {
            ImGui::SeparatorText("Items");
            std::array<U32, ItemSlots.size()> single{};
            U32 singles = 0;
            for (U32 i = 0; i < ItemSlots.size(); ++i)
                if (ItemSlots[i].count == 1) single[singles++] = i;
            columns("Save items", singles, 2, [&](U32 k) {
                const U32 i = single[k];
                const auto& slot = ItemSlots[i];
                check(slot.label, s.Byte(Items + i) != NoItem,
                      [&](bool v) { put({Field(Items + i, 1, v ? slot.items[0] : NoItem)}); });
            });
            ImGui::SeparatorText("Trade items");
            for (U32 i = 0; i < ItemSlots.size(); ++i) {
                const auto& slot = ItemSlots[i];
                if (slot.count != 1) item_choice(slot.label, Items + i, slot.items.data(), slot.count);
            }
            ImGui::SeparatorText("Ammo");
            ImGui::PushID("Ammo");
            for (const auto& a : AmmoFields)
                number(a.label, int(s.Byte(Ammo + a.slot)), 0, a.max,
                       [&](int v) { put({Field(Ammo + a.slot, 1, U32(v))}); });
            ImGui::PopID();
            help("How much you're carrying. Your bags, quiver and the game's own limits still apply "
                 "when you next pick some up.");
            ImGui::SeparatorText("Bottles");
            std::array<U8, 0x28 - 0x12> contents{};
            for (U32 i = 0; i < contents.size(); ++i) contents[i] = U8(0x12 + i);
            for (U32 i = 0; i < BottleCount; ++i) {
                char label[16];
                std::snprintf(label, sizeof label, "Bottle %u", unsigned(i + 1));
                item_choice(label, Bottles + i, contents.data(), U32(contents.size()));
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Masks")) {
            if (ImGui::Button("Give all masks")) put(SetAllMasks(true));
            columns("Save masks", MaskCount, 3, [&](U32 i) {
                const U8 id = U8(FirstMask + i);
                check(ItemNames[id], s.Byte(Masks + i) != NoItem,
                      [&](bool v) { put({Field(Masks + i, 1, v ? id : NoItem)}); });
            });
            ImGui::TextWrapped("Removing the mask Link is wearing takes effect when he next changes masks.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Equipment")) {
            ImGui::SeparatorText("Sword and shield");
            choice("Sword", s.Byte(SwordShield) & 15, SwordNames, [&](U32 v) { put(SetSword(s, v)); });
            if ((s.Byte(SwordShield) & 15) == 2) {
                number("Razor Sword uses left", int(s.Half(RazorHits)), 0, 100,
                       [&](int v) { put({Field(RazorHits, 2, U32(v))}); });
                help("The Razor Sword dulls back into the Kokiri Sword after 100 hits.");
            }
            choice("Shield", s.Byte(SwordShield) >> 4, ShieldNames, [&](U32 v) { put(SetShield(v)); });
            ImGui::TextWrapped("Reopen the gear screen to see a new sword or shield there.");
            ImGui::SeparatorText("Upgrades");
            choice("Wallet", Upgrade(s, WalletBits), WalletNames, [&](U32 v) { put(SetUpgrade(WalletBits, v)); });
            choice("Quiver", Upgrade(s, QuiverBits), QuiverNames, [&](U32 v) { put(SetUpgrade(QuiverBits, v)); });
            choice("Bomb bag", Upgrade(s, BombBagBits), BombBagNames,
                   [&](U32 v) { put(SetUpgrade(BombBagBits, v)); });
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Quest")) {
            const U32 quest = s.Word(Quest);
            ImGui::SeparatorText("Boss remains");
            columns("Save remains", U32(Remains.size()), 2, [&](U32 i) {
                check(Remains[i].label, quest & (1u << Remains[i].bit),
                      [&](bool v) { put(SetQuestBit(Remains[i].bit, v)); });
            });
            ImGui::SeparatorText("Songs");
            columns("Save songs", U32(Songs.size()), 2, [&](U32 i) {
                check(Songs[i].label, quest & (1u << Songs[i].bit),
                      [&](bool v) { put(SetQuestBit(Songs[i].bit, v)); });
            });
            ImGui::SeparatorText("Other");
            check("Bombers' Notebook", quest & (1u << NotebookBit),
                  [&](bool v) { put(SetQuestBit(NotebookBit, v)); });
            ImGui::SeparatorText("Owl statues");
            const U32 owls = s.Half(OwlStatues);
            columns("Save owls", U32(OwlNames.size()), 2, [&](U32 i) {
                check(OwlNames[i], owls & (1u << i), [&](bool v) { put(SetOwl(i, v)); });
            });
            help("Owl statues you've activated, for the Song of Soaring.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Dungeons")) {
            for (U32 t = 0; t < TempleCount; ++t) {
                ImGui::PushID(int(t));
                ImGui::SeparatorText(TempleNames[t]);
                const U32 items = s.Byte(DungeonItems + t);
                check("Map", items & MapBit, [&](bool v) { put(SetByteBit(DungeonItems + t, MapBit, v)); });
                ImGui::SameLine();
                check("Compass", items & CompassBit,
                      [&](bool v) { put(SetByteBit(DungeonItems + t, CompassBit, v)); });
                ImGui::SameLine();
                check("Boss Key", items & BossKeyBit,
                      [&](bool v) { put(SetByteBit(DungeonItems + t, BossKeyBit, v)); });
                number("Small keys", std::max(0, int(std::int8_t(s.Byte(SmallKeys + t)))), 0, 99,
                       [&](int v) { put({Field(SmallKeys + t, 1, U32(v))}); });
                int fairies = std::clamp(int(std::int8_t(s.Byte(StrayFairies + t))), 0, 15);
                if (ImGui::SliderInt("Stray fairies", &fairies, 0, 15, "%d of 15", ImGuiSliderFlags_AlwaysClamp))
                    put({Field(StrayFairies + t, 1, U32(fairies))});
                help("Stray fairies you're holding for this temple's Great Fairy. Bring all 15 to her fountain.");
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (Pending()) ImGui::TextUnformatted("Waiting for the game to take the changes...");
}
} // namespace MM3D::SaveEditor
