#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "GuiApp.hpp"
#include "imgui_impl_glfw.h"
#include "imgui_impl_metal.h"

std::optional<std::filesystem::path> ChooseBenchmarkCsv()
{
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.title = @"Экспорт результатов замеров";
    panel.nameFieldStringValue = @"benchmark.csv";
    panel.canCreateDirectories = YES;
    panel.allowedContentTypes = @[UTTypeCommaSeparatedText];
    if ([panel runModal] != NSModalResponseOK) { return std::nullopt; }
    const char* path = panel.URL.fileSystemRepresentation;
    if (path == nullptr) { return std::nullopt; }
    return std::filesystem::path(path);
}

namespace
{
    void GlfwError(int code, const char* description)
    {
        std::fprintf(stderr, "GLFW (%d): %s\n", code, description);
    }

    void StartupError(const char* error)
    {
        std::fprintf(stderr, "%s\n", error);
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Не удалось запустить Interplanetary Network";
        alert.informativeText = [NSString stringWithUTF8String:error];
        [alert runModal];
    }

    std::filesystem::path FontPath(const char* executable)
    {
        NSBundle* bundle = [NSBundle mainBundle];
        NSString* path = [bundle pathForResource:@"DroidSans" ofType:@"ttf"];
        if (path != nil) { return std::filesystem::path(path.fileSystemRepresentation); }
        auto binary = std::filesystem::canonical(executable);
        return binary.parent_path().parent_path() / "gui" / "assets" / "DroidSans.ttf";
    }

    void RunFrames(GLFWwindow* window, CAMetalLayer* layer, id<MTLCommandQueue> queue)
    {
        GuiApp app;
        while (!app.ShouldClose())
        {
            @autoreleasepool
            {
                glfwPollEvents();
                if (glfwWindowShouldClose(window))
                {
                    app.RequestClose();
                    glfwSetWindowShouldClose(window, GLFW_FALSE);
                }
                app.Poll();
                if (app.ShouldClose()) { break; }

                int width = 0, height = 0;
                glfwGetFramebufferSize(window, &width, &height);
                if (width == 0 || height == 0 || glfwGetWindowAttrib(window, GLFW_ICONIFIED))
                {
                    glfwWaitEventsTimeout(0.055);
                    continue;
                }

                layer.drawableSize = CGSizeMake(width, height);
                id<CAMetalDrawable> drawable = [layer nextDrawable];
                if (drawable == nil) { glfwWaitEventsTimeout(0.04); continue; }
                id<MTLCommandBuffer> command = [queue commandBuffer];
                if (command == nil) { throw std::runtime_error("Metal не создал буфер команд."); }

                MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture = drawable.texture;
                pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                pass.colorAttachments[0].clearColor = MTLClearColorMake(0.031, 0.059, 0.114, 1.0);

                id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
                if (encoder == nil) { throw std::runtime_error("Metal не создал кодировщик кадра."); }
                ImGui_ImplMetal_NewFrame(pass);
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                app.Draw();
                ImGui::Render();
                ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), command, encoder);
                [encoder endEncoding];
                [command presentDrawable:drawable];
                [command commit];
            }
        }
    }
}

int main(int, char** argv)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        glfwSetErrorCallback(GlfwError);

        if (!glfwInit())
        {
            StartupError("GLFW не смог открыть графическую сессию.");
            return 1;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        int width = 1370;
        int height = 848;
        if (auto* monitor = glfwGetPrimaryMonitor())
        {
            int x = 0, y = 0, monitorWidth = 0, monitorHeight = 0;
            glfwGetMonitorWorkarea(monitor, &x, &y, &monitorWidth, &monitorHeight);
            if (monitorWidth > 0) { width = std::min(width, std::max(840, monitorWidth - 84)); }
            if (monitorHeight > 0) { height = std::min(height, std::max(625, monitorHeight - 70)); }
        }
        GLFWwindow* window = glfwCreateWindow(width, height, "Interplanetary Network", nullptr, nullptr);
        if (window == nullptr)
        {
            glfwTerminate();
            StartupError("GLFW не смог создать окно.");
            return 1;
        }
        glfwSetWindowSizeLimits(window, 850, 610, GLFW_DONT_CARE, GLFW_DONT_CARE);

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (device == nil || queue == nil)
        {
            glfwDestroyWindow(window);
            glfwTerminate();
            StartupError("Для запуска требуется доступ к Metal.");
            return 1;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;
        GuiApp::ApplyTheme();

        bool glfwReady = ImGui_ImplGlfw_InitForOther(window, true);
        bool metalReady = glfwReady && ImGui_ImplMetal_Init(device);
        int status = 0;
        try
        {
            if (!metalReady) { throw std::runtime_error("Не удалось подключить платформу GLFW и рендерер Metal."); }
            auto font = FontPath(argv[0]);
            if (!std::filesystem::exists(font) || io.Fonts->AddFontFromFileTTF(font.c_str(), 15.0f) == nullptr)
            {
                throw std::runtime_error("Не удалось загрузить шрифт Droid Sans.");
            }

            NSWindow* nativeWindow = glfwGetCocoaWindow(window);
            CAMetalLayer* layer = [CAMetalLayer layer];
            layer.device = device;
            layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            nativeWindow.contentView.wantsLayer = YES;
            nativeWindow.contentView.layer = layer;
            glfwShowWindow(window);
            glfwFocusWindow(window);
            [NSApp activateIgnoringOtherApps:YES];
            RunFrames(window, layer, queue);
        }
        catch (const std::exception& error)
        {
            StartupError(error.what());
            status = 1;
        }
        if (metalReady) { ImGui_ImplMetal_Shutdown(); }
        if (glfwReady) { ImGui_ImplGlfw_Shutdown(); }
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return status;
    }
}
