#ifndef GUISTYLE_HPP
#define GUISTYLE_HPP

#include <cstdint>

#include "imgui.h"

namespace Theme
{
    inline constexpr std::uint32_t Background = 0x080f1d;
    inline constexpr std::uint32_t Panel = 0x101a2b;
    inline constexpr std::uint32_t Inset = 0x0c1524;
    inline constexpr std::uint32_t Border = 0x263852;
    inline constexpr std::uint32_t Text = 0xe6eefb;
    inline constexpr std::uint32_t Muted = 0x9fb2ca;
    inline constexpr std::uint32_t Blue = 0x62c9ff;
    inline constexpr std::uint32_t Purple = 0xc4a5ff;
    inline constexpr std::uint32_t Green = 0x54e5a9;
    inline constexpr std::uint32_t Red = 0xff798d;

    inline ImU32 Color(std::uint32_t rgb, unsigned int alpha = 255)
    {
        return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, alpha);
    }

    inline ImVec4 Tint(std::uint32_t rgb, unsigned int alpha = 255)
    {
        return ImGui::ColorConvertU32ToFloat4(Color(rgb, alpha));
    }

    inline void Heading(const char* text, float size = 20.0f)
    {
        ImGui::PushFont(nullptr, size);
        ImGui::TextUnformatted(text);
        ImGui::PopFont();
    }

    inline void Note(const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Tint(Muted));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    inline void Overline(const char* text)
    {
        ImGui::PushFont(nullptr, 13.0f);
        ImGui::TextColored(Tint(Blue), "%s", text);
        ImGui::PopFont();
    }

    inline bool Primary(const char* label, ImVec2 size = {})
    {
        ImGui::PushStyleColor(ImGuiCol_Button, Tint(0x17629a));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Tint(0x237fbc));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Tint(0x155481));
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(3);
        return pressed;
    }
}

#endif
