// imgui_prose demo: source (read-only) and destination editors side by side, scroll synchronisation, decorations.
// Usage: imgui_prose_demo [file.md] [--width W] [--height H] [--script file]
// Script (one command per line, '#' starts a comment): click X Y [2|3], move X Y, drag X1 Y1 X2 Y2, type TEXT,
// key NAME [ctrl] [shift] [alt], wheel DY, wheel-src DY, wait N, shot FILE.png, dump FILE, log TEXT, set OPTION on|off, quit

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_prose.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <deque>
#include <functional>
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

static char* ReadFile(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = 0;
    fclose(f);
    return buf;
}

static void SaveScreenshot(GLFWwindow* window, const char* path)
{
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    std::vector<unsigned char> pixels((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    stbi_write_png(path, w, h, 4, pixels.data(), w * 4);
}

static ImGuiKey KeyFromName(const std::string& n)
{
    if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z') return (ImGuiKey)(ImGuiKey_A + (n[0] - 'a'));
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return (ImGuiKey)(ImGuiKey_0 + (n[0] - '0'));
    if (n == "enter") return ImGuiKey_Enter;
    if (n == "backspace") return ImGuiKey_Backspace;
    if (n == "delete") return ImGuiKey_Delete;
    if (n == "tab") return ImGuiKey_Tab;
    if (n == "escape") return ImGuiKey_Escape;
    if (n == "left") return ImGuiKey_LeftArrow;
    if (n == "right") return ImGuiKey_RightArrow;
    if (n == "up") return ImGuiKey_UpArrow;
    if (n == "down") return ImGuiKey_DownArrow;
    if (n == "home") return ImGuiKey_Home;
    if (n == "end") return ImGuiKey_End;
    if (n == "pageup") return ImGuiKey_PageUp;
    if (n == "pagedown") return ImGuiKey_PageDown;
    if (n == "space") return ImGuiKey_Space;
    return ImGuiKey_None;
}

// First font file which exists in the list
static ImFont* AddFont(ImGuiIO& io, std::vector<std::string> paths, float size, const ImWchar* ranges)
{
    for (const std::string& path : paths)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f)
            continue;
        fclose(f);
        return io.Fonts->AddFontFromFileTTF(path.c_str(), size, NULL, ranges);
    }
    return NULL;
}

// Demo decorations: paragraphs which differ between the two editors, and a few misspelled words
static void BuildSpans(ImGuiProseEditor* editor, ImGuiProseEditor* other, bool dark)
{
    std::vector<ImGuiProseSpan> spans;
    const int n = ImGuiProse::GetParagraphCount(editor), m = ImGuiProse::GetParagraphCount(other);
    for (int p = 1; p <= n; p++)
    {
        const int line = ImGuiProse::GetLineOfParagraph(editor, p);
        int len = 0, len2 = 0;
        const char* text = ImGuiProse::GetLine(editor, line, &len);
        bool differs = true;
        if (p <= m)
        {
            const char* text2 = ImGuiProse::GetLine(other, ImGuiProse::GetLineOfParagraph(other, p), &len2);
            differs = (len != len2) || memcmp(text, text2, len) != 0;
        }
        if (differs)
            spans.push_back(ImGuiProseSpan(line, -1, -1, dark ? IM_COL32(110, 90, 20, 90) : IM_COL32(255, 235, 150, 140)));
        static const char* bad[] = { "teh", "recieve", "paragrah" };
        for (int b = 0; b < 3; b++)
            for (const char* f = text; (f = strstr(f, bad[b])) != NULL && f < text + len; f++)
                spans.push_back(ImGuiProseSpan(line, (int)(f - text), (int)(f - text) + (int)strlen(bad[b]), IM_COL32(255, 60, 60, 255), ImGuiProseSpanKind_Squiggle));
    }
    ImGuiProse::SetSpans(editor, spans.data(), (int)spans.size());
}

struct Action
{
    std::function<void()> Pre;      // Before the frame is built
    std::function<void()> Post;     // After it was drawn
};

int main(int argc, char** argv)
{
    const char* file = NULL;
    const char* script_path = NULL;
    int win_w = 1200, win_h = 800;
    for (int n = 1; n < argc; n++)
    {
        if (!strcmp(argv[n], "--width") && n + 1 < argc)        win_w = atoi(argv[++n]);
        else if (!strcmp(argv[n], "--height") && n + 1 < argc)  win_h = atoi(argv[++n]);
        else if (!strcmp(argv[n], "--script") && n + 1 < argc)  script_path = argv[++n];
        else                                                    file = argv[n];
    }

    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(win_w, win_h, "imgui_prose demo", NULL, NULL);
    if (!window)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0x2000, 0x206F, 0x20AC, 0x20AC, 0x2190, 0x21FF, 0x2500, 0x25FF, 0 };
    const std::string dir = IMGUI_PROSE_FONT_DIR;
    ImFont* font_text = AddFont(io, { "C:/Windows/Fonts/segoeui.ttf", dir + "/DejaVuSans.ttf" }, 18.0f, ranges);
    ImFont* font_mono = AddFont(io, { dir + "/DejaVuSansMono.ttf" }, 16.0f, ranges);
    io.FontDefault = font_text;

    ImGuiProseEditor* src = ImGuiProse::CreateEditor();
    ImGuiProseEditor* dst = ImGuiProse::CreateEditor();
    char* text = file ? ReadFile(file) : NULL;
    const char* initial = text ? text : "Premier paragraphe : un texte avec teh une faute.\n\nDeuxième paragraphe, un peu plus long pour vérifier le retour à la ligne automatique dans l'éditeur de prose. Il faut que ce texte dépasse la largeur de la colonne.\n\n« Guillemets » et espace insécable : ok ; fin…\n";
    ImGuiProse::SetText(src, initial);
    ImGuiProse::SetText(dst, initial);

    ImGuiProseConfig config_src, config_dst;
    bool sync = true, wrap = true, lines = true, paras = true, nbsp = true, invisible = false, dark = true, mono = false;
    config_src.ReadOnly = true;
    int spans_version = -1;

    // Script
    std::deque<Action> actions;
    bool mouse_scripted = false;
    ImVec2 mouse_pos(0, 0);
    bool quit = false;
    if (script_path)
    {
        char* script = ReadFile(script_path);
        if (!script)
        {
            printf("script not found: %s\n", script_path);
            return 1;
        }
        std::string all = script;
        free(script);
        size_t pos = 0;
        while (pos <= all.size())
        {
            size_t eol = all.find('\n', pos);
            if (eol == std::string::npos)
                eol = all.size();
            std::string line = all.substr(pos, eol - pos);
            pos = eol + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            const size_t sp = line.find(' ');
            const std::string cmd = line.substr(0, sp);
            const std::string rest = sp == std::string::npos ? "" : line.substr(sp + 1);
            float a = 0, b = 0, c = 0, d = 0;
            char word[256] = "";
            if (cmd == "wait")
            {
                for (int k = 0; k < atoi(rest.c_str()); k++)
                    actions.push_back({ nullptr, nullptr });
            }
            else if (cmd == "move" && sscanf(rest.c_str(), "%f %f", &a, &b) == 2)
                actions.push_back({ [&, a, b]() { mouse_scripted = true; mouse_pos = ImVec2(a, b); }, nullptr });
            else if (cmd == "click")
            {
                int count = 1;
                sscanf(rest.c_str(), "%f %f %d", &a, &b, &count);
                for (int k = 0; k < count; k++)
                {
                    actions.push_back({ [&, a, b]() { mouse_scripted = true; mouse_pos = ImVec2(a, b); io.AddMousePosEvent(a, b); io.AddMouseButtonEvent(0, true); }, nullptr });
                    actions.push_back({ [&]() { io.AddMouseButtonEvent(0, false); }, nullptr });
                }
                actions.push_back({ nullptr, nullptr });
            }
            else if (cmd == "drag" && sscanf(rest.c_str(), "%f %f %f %f", &a, &b, &c, &d) == 4)
            {
                actions.push_back({ [&, a, b]() { mouse_scripted = true; mouse_pos = ImVec2(a, b); io.AddMousePosEvent(a, b); io.AddMouseButtonEvent(0, true); }, nullptr });
                actions.push_back({ [&, c, d]() { mouse_pos = ImVec2(c, d); io.AddMousePosEvent(c, d); }, nullptr });
                actions.push_back({ [&]() { io.AddMouseButtonEvent(0, false); }, nullptr });
                actions.push_back({ nullptr, nullptr });
            }
            else if (cmd == "type")
                actions.push_back({ [&, rest]() { io.AddInputCharactersUTF8(rest.c_str()); }, nullptr });
            else if (cmd == "key")
            {
                std::vector<std::string> parts;
                size_t s = 0;
                while (s <= rest.size())
                {
                    size_t e = rest.find(' ', s);
                    if (e == std::string::npos)
                        e = rest.size();
                    parts.push_back(rest.substr(s, e - s));
                    s = e + 1;
                }
                const ImGuiKey key = KeyFromName(parts[0]);
                bool ctrl = false, shift = false, alt = false;
                for (size_t k = 1; k < parts.size(); k++)
                {
                    if (parts[k] == "ctrl") ctrl = true;
                    if (parts[k] == "shift") shift = true;
                    if (parts[k] == "alt") alt = true;
                }
                actions.push_back({ [&, key, ctrl, shift, alt]() {
                    if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
                    if (shift) io.AddKeyEvent(ImGuiMod_Shift, true);
                    if (alt) io.AddKeyEvent(ImGuiMod_Alt, true);
                    io.AddKeyEvent(key, true);
                }, nullptr });
                actions.push_back({ [&, key, ctrl, shift, alt]() {
                    io.AddKeyEvent(key, false);
                    if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
                    if (shift) io.AddKeyEvent(ImGuiMod_Shift, false);
                    if (alt) io.AddKeyEvent(ImGuiMod_Alt, false);
                }, nullptr });
            }
            else if (cmd == "wheel" && sscanf(rest.c_str(), "%f", &a) == 1)
                actions.push_back({ [&, a]() { io.AddMouseWheelEvent(0.0f, a); }, nullptr });
            else if (cmd == "shot")
                actions.push_back({ nullptr, [&, rest]() { SaveScreenshot(window, rest.c_str()); } });
            else if (cmd == "dump")
                actions.push_back({ nullptr, [&, rest]() {
                    ImVector<char> out;
                    ImGuiProse::GetText(dst, &out);
                    FILE* f = fopen(rest.c_str(), "wb");
                    if (f) { fwrite(out.Data, 1, out.Size - 1, f); fclose(f); }
                } });
            else if (cmd == "log")
                actions.push_back({ nullptr, [&, rest]() {
                    ImGuiProsePos c = ImGuiProse::GetCaret(dst);
                    ImGuiProseScrollAnchor sa = ImGuiProse::GetScrollAnchor(src), da = ImGuiProse::GetScrollAnchor(dst);
                    printf("%s | caret %d:%d | src top ¶%d+%.2f | dst top ¶%d+%.2f\n", rest.c_str(), c.Line, c.Column, sa.Paragraph, sa.Fraction, da.Paragraph, da.Fraction);
                } });
            else if (cmd == "set" && sscanf(rest.c_str(), "%255s", word) == 1)
            {
                const bool on = rest.find(" on") != std::string::npos;
                const std::string w = word;
                actions.push_back({ [&, w, on]() {
                    if (w == "wrap") wrap = on; else if (w == "sync") sync = on; else if (w == "lines") lines = on;
                    else if (w == "paras") paras = on; else if (w == "nbsp") nbsp = on; else if (w == "mono") mono = on;
                    else if (w == "srcreadonly") config_src.ReadOnly = on;
                }, nullptr });
            }
            else if (cmd == "quit")
                actions.push_back({ nullptr, [&]() { quit = true; } });
        }
    }

    while (!glfwWindowShouldClose(window) && !quit)
    {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        Action current = { nullptr, nullptr };
        if (!actions.empty())
        {
            current = actions.front();
            actions.pop_front();
        }
        if (mouse_scripted)
            io.AddMousePosEvent(mouse_pos.x, mouse_pos.y);           // After the backend, which reads the real mouse
        if (current.Pre)
            current.Pre();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##main", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Checkbox("wrap", &wrap); ImGui::SameLine();
        ImGui::Checkbox("line numbers", &lines); ImGui::SameLine();
        ImGui::Checkbox("paragraph numbers", &paras); ImGui::SameLine();
        ImGui::Checkbox("no-break spaces", &nbsp); ImGui::SameLine();
        ImGui::Checkbox("invisible characters", &invisible); ImGui::SameLine();
        ImGui::Checkbox("sync scroll", &sync); ImGui::SameLine();
        ImGui::Checkbox("mono", &mono);
        const ImGuiProsePos caret = ImGuiProse::GetCaret(dst);
        ImGui::SameLine();
        ImGui::Text("dst caret %d:%d  paragraph %d/%d", caret.Line, caret.Column, ImGuiProse::GetParagraphOfLine(dst, caret.Line), ImGuiProse::GetParagraphCount(dst));

        for (ImGuiProseConfig* c : { &config_src, &config_dst })
        {
            c->WordWrap = wrap;
            c->ShowLineNumbers = lines;
            c->ShowParagraphNumbers = paras;
            c->ShowNbsp = nbsp;
            c->ShowInvisible = invisible;
            c->Font = mono ? font_mono : font_text;
        }

        // Current paragraph: the one of the focused editor, in both
        ImGuiProseEditor* focused = ImGuiProse::HasFocus(src) ? src : dst;
        const int para = ImGuiProse::GetParagraphOfLine(focused, ImGuiProse::GetCaret(focused).Line);
        config_src.HighlightLine = para > 0 ? ImGuiProse::GetLineOfParagraph(src, para) : 0;
        config_dst.HighlightLine = para > 0 ? ImGuiProse::GetLineOfParagraph(dst, para) : 0;
        config_src.HighlightCaretParagraph = config_dst.HighlightCaretParagraph = false;

        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGuiProse::Render(src, "##src", config_src, ImVec2(half, 0.0f));
        ImGui::SameLine();
        ImGuiProse::Render(dst, "##dst", config_dst, ImVec2(0.0f, 0.0f));

        // Scroll synchronisation: the editor moved by the user drives the other one
        const bool a = ImGuiProse::ScrolledByUser(src), b = ImGuiProse::ScrolledByUser(dst);
        if (sync && a && !b)
            ImGuiProse::SetScrollAnchor(dst, ImGuiProse::GetScrollAnchor(src));
        else if (sync && b && !a)
            ImGuiProse::SetScrollAnchor(src, ImGuiProse::GetScrollAnchor(dst));

        // Decorations when a text changed
        const int version = ImGuiProse::GetVersion(src) * 100003 + ImGuiProse::GetVersion(dst);
        if (version != spans_version)
        {
            spans_version = version;
            BuildSpans(src, dst, dark);
            BuildSpans(dst, src, dark);
        }
        ImGui::End();

        ImGui::Render();
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (current.Post)
            current.Post();
        if (script_path && actions.empty())
            quit = true;
        glfwSwapBuffers(window);
    }

    ImGuiProse::DestroyEditor(src);
    ImGuiProse::DestroyEditor(dst);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    free(text);
    return 0;
}
