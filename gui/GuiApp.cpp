#include "GuiApp.hpp"

#include <array>

#include "GuiStyle.hpp"

void GuiApp::ApplyTheme()
{
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {18, 14};
    style.WindowRounding = 0;
    style.ChildRounding = 9;
    style.PopupRounding = 9;
    style.FrameRounding = 6;
    style.GrabRounding = 5;
    style.ScrollbarRounding = 5;
    style.ScrollbarSize = 10;
    style.FramePadding = {9, 5};
    style.ItemSpacing = {11, 8};
    style.ItemInnerSpacing = {8, 6};
    style.CellPadding = {8, 5};
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.TabRounding = 5;
    style.IndentSpacing = 12;

    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = Theme::Tint(Theme::Text);
    colors[ImGuiCol_TextDisabled] = Theme::Tint(Theme::Muted);
    colors[ImGuiCol_WindowBg] = Theme::Tint(Theme::Background);
    colors[ImGuiCol_ChildBg] = Theme::Tint(Theme::Panel);
    colors[ImGuiCol_PopupBg] = Theme::Tint(0x132238);
    colors[ImGuiCol_Border] = Theme::Tint(Theme::Border);
    colors[ImGuiCol_FrameBg] = Theme::Tint(0x18273c);
    colors[ImGuiCol_FrameBgHovered] = Theme::Tint(0x213954);
    colors[ImGuiCol_FrameBgActive] = Theme::Tint(0x274568);
    colors[ImGuiCol_Button] = Theme::Tint(0x1c3048);
    colors[ImGuiCol_ButtonHovered] = Theme::Tint(0x2d4c6b);
    colors[ImGuiCol_ButtonActive] = Theme::Tint(0x23415e);
    colors[ImGuiCol_Header] = Theme::Tint(0x1b2e45);
    colors[ImGuiCol_HeaderHovered] = Theme::Tint(0x25415c);
    colors[ImGuiCol_HeaderActive] = Theme::Tint(0x2a5073);
    colors[ImGuiCol_CheckMark] = Theme::Tint(Theme::Blue);
    colors[ImGuiCol_SliderGrab] = Theme::Tint(Theme::Blue);
    colors[ImGuiCol_SliderGrabActive] = Theme::Tint(0x98e2ff);
    colors[ImGuiCol_Separator] = Theme::Tint(Theme::Border);
    colors[ImGuiCol_PlotHistogram] = Theme::Tint(0x3baee8);
    colors[ImGuiCol_Tab] = Theme::Tint(0x182a40);
    colors[ImGuiCol_TabHovered] = Theme::Tint(0x2c5274);
    colors[ImGuiCol_TabSelected] = Theme::Tint(0x254765);
    colors[ImGuiCol_TabDimmedSelected] = Theme::Tint(0x25415c);
    colors[ImGuiCol_TableHeaderBg] = Theme::Tint(0x172a40);
    colors[ImGuiCol_TableBorderLight] = Theme::Tint(0x22324b);
    colors[ImGuiCol_TableBorderStrong] = Theme::Tint(Theme::Border);
    colors[ImGuiCol_TableRowBgAlt] = Theme::Tint(0x162238);
}

void GuiApp::RequestClose()
{
    closing = true;
    job.RequestStop();
}

void GuiApp::Poll()
{
    try { job.Poll(); }
    catch (const std::bad_alloc&) { failure = "Недостаточно памяти для выполнения операции."; }
    catch (const std::exception& error) { failure = error.what(); }
}

void GuiApp::DrawHeader()
{
    const auto point = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    ImGui::Dummy({35, 34});
    ImVec2 center{point.x + 16.0f, point.y + 17.0f};
    draw->AddCircleFilled(center, 15.0f, Theme::Color(0x153c5a), 36);
    draw->AddEllipse(center, {13.5f, 6.8f}, Theme::Color(Theme::Blue, 207), -0.62f, 36, 1.2f);
    draw->AddCircleFilled(center, 4.5f, Theme::Color(Theme::Blue), 24);
    draw->AddCircleFilled({center.x + 10.0f, center.y - 7.6f}, 2.55f, Theme::Color(0xbde4ff), 13);
    ImGui::SameLine();
    Theme::Heading("Interplanetary Network", 27.0f);

    constexpr std::array<const char*, 3> tabs{"Межпланетная сеть", "Генератор графов", "Производительность"};
    if (ImGui::GetWindowWidth() >= 965)
    {
        ImGui::SameLine(0.0f, 26.0f);
    }
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i)
    {
        if (i > 0) { ImGui::SameLine(0.0f, 7.0f); }
        if (i == tab)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Tint(0x17446a));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Tint(0xc7ecff));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Tint(Theme::Background));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Tint(Theme::Muted));
        }
        if (ImGui::Button(tabs[static_cast<std::size_t>(i)], {0.0f, 33.0f})) { tab = i; }
        ImGui::PopStyleColor(2);
    }
    ImGui::Separator();
}

void GuiApp::DrawTaskStatus()
{
    if (job.Busy())
    {
        const char* progress = job.Stopping() ? "Остановка…" :
            (job.stage.load() == GuiTask::Stage::Preparing ? "Подготовка" :
             (job.stage.load() == GuiTask::Stage::Saving ? "Сохранение" : "Замеры"));
        ImGui::TextColored(Theme::Tint(Theme::Blue), "%s · %s", job.Title().c_str(), progress);
        if (job.Total() != 0)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%zu / %zu", job.completed.load(), job.Total());
        }
        if (!job.Stopping() && job.stage.load() != GuiTask::Stage::Saving)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Остановить##Task")) { job.RequestStop(); }
        }
    }
    if (!failure.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Tint(Theme::Red));
        ImGui::TextWrapped("Ошибка — %s", failure.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Закрыть##Error")) { failure.clear(); }
    }
}

void GuiApp::Draw()
{
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    if (ImGui::Begin("Interplanetary Network", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        DrawHeader();
        DrawTaskStatus();
        ImGui::BeginDisabled(closing);
        switch (tab)
        {
            case 0: network.Draw(job); break;
            case 1: generator.Draw(job, &network); break;
            case 2: performance.Draw(job, network, generator); break;
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
}
