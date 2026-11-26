#ifndef GUIAPP_HPP
#define GUIAPP_HPP

#include <string>

#include "BenchmarkView.hpp"
#include "GraphView.hpp"
#include "GuiTask.hpp"

class GuiApp
{
private:
    int tab = 0;
    bool closing = false;
    std::string failure;
    GraphView network{true};
    GraphView generator{false};
    BenchmarkView performance;
    GuiTask job;

    void DrawHeader();
    void DrawTaskStatus();

public:
    void Poll();
    void Draw();
    void RequestClose();
    bool ShouldClose() const { return closing && !job.Busy(); }
    static void ApplyTheme();
};

#endif
