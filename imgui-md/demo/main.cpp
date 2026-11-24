// imgui_md demo : GLFW + OpenGL3 window showing a Markdown file.
// Usage: imgui_md_demo [file.md] [--shot out.png] [--frames N] [--line N] [--width W --height H]

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_md.h"
#include "imgui_md_diagram.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <map>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
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

// Images are loaded on demand next to the Markdown file and kept as OpenGL textures
struct ImageCache
{
    std::string Dir;
    ImGuiMdDiagramCache* Diagrams = NULL;
    struct Tex { GLuint Id; int W, H; };
    std::map<std::string, Tex> Map;
};

static ImTextureID OnImage(const char* url, ImVec2* out_size, void* user_data)
{
    ImageCache* cache = (ImageCache*)user_data;
    auto it = cache->Map.find(url);
    if (it == cache->Map.end())
    {
        ImageCache::Tex tex = { 0, 0, 0 };
        std::string path = cache->Dir + "/" + url;
        int channels;
        unsigned char* data = strncmp(url, "http", 4) ? stbi_load(path.c_str(), &tex.W, &tex.H, &channels, 4) : NULL;
        if (data)
        {
            glGenTextures(1, &tex.Id);
            glBindTexture(GL_TEXTURE_2D, tex.Id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.W, tex.H, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
        it = cache->Map.insert(std::make_pair(std::string(url), tex)).first;
    }
    if (it->second.Id == 0)
        return 0;
    *out_size = ImVec2((float)it->second.W, (float)it->second.H);
    return (ImTextureID)(intptr_t)it->second.Id;
}

static float OnCodeBlock(const char* language, const char* text, int text_length, float width, bool draw, ImVec2 pos, void* user_data)
{
    return ImGuiMdDiagrams::CodeBlock(((ImageCache*)user_data)->Diagrams, language, text, text_length, width, draw, pos);
}

static bool g_bundled_fonts_only = false;      // --bundled-fonts: ignore the fonts of the system

// First font file which exists in the list (NULL if none)
static ImFont* AddFont(ImGuiIO& io, std::vector<std::string> paths, float size, const ImFontConfig* config, const ImWchar* ranges)
{
    for (const std::string& path : paths)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f)
            continue;
        fclose(f);
        return io.Fonts->AddFontFromFileTTF(path.c_str(), size, config, ranges);
    }
    return NULL;
}
static void OnLink(const char* url, void*)
{
    printf("link clicked: %s\n", url);
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

int main(int argc, char** argv)
{
    const char* file = NULL;
    const char* shot = NULL;
    int frames = 0, line = 0, win_w = 1000, win_h = 800;
    int mouse_x = -1, mouse_y = -1;
    bool click = false;
    for (int n = 1; n < argc; n++)
    {
        if (!strcmp(argv[n], "--shot") && n + 1 < argc)         shot = argv[++n];
        else if (!strcmp(argv[n], "--frames") && n + 1 < argc)  frames = atoi(argv[++n]);
        else if (!strcmp(argv[n], "--line") && n + 1 < argc)    line = atoi(argv[++n]);
        else if (!strcmp(argv[n], "--mouse") && n + 2 < argc)   { mouse_x = atoi(argv[n + 1]); mouse_y = atoi(argv[n + 2]); n += 2; }
        else if (!strcmp(argv[n], "--bundled-fonts"))           g_bundled_fonts_only = true;
        else if (!strcmp(argv[n], "--click"))                   click = true;
        else if (!strcmp(argv[n], "--width") && n + 1 < argc)   win_w = atoi(argv[++n]);
        else if (!strcmp(argv[n], "--height") && n + 1 < argc)  win_h = atoi(argv[++n]);
        else                                                    file = argv[n];
    }

    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(win_w, win_h, "imgui_md demo", NULL, NULL);
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

    // The fonts bundled with the original sample are subsets: no U+202F, no arrows, no box drawing. The demo takes the fonts of the system when it
    // finds them (Windows, then DejaVu on Linux) and falls back to the bundled ones.
    const ImWchar* ranges = ImGuiMd::GetGlyphRanges();
    const ImWchar* symbols = ImGuiMd::GetSymbolGlyphRanges();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    ImFontConfig merge = cfg;
    merge.MergeMode = true;
    const char* dejavu = "/usr/share/fonts/truetype/dejavu/";
    const std::string d = dejavu;
    ImGuiMdConfig md;
    md.Fonts.Regular = AddFont(io, { "C:/Windows/Fonts/segoeui.ttf", d + "DejaVuSans.ttf", IMGUI_MD_FONT_DIR "/DejaVuSans.ttf" }, 18.0f, &cfg, ranges);
    AddFont(io, { "C:/Windows/Fonts/seguisym.ttf", d + "DejaVuSans.ttf", IMGUI_MD_FONT_DIR "/DejaVuSans.ttf" }, 16.0f, &merge, symbols);
    md.Fonts.Bold = AddFont(io, { "C:/Windows/Fonts/segoeuib.ttf", d + "DejaVuSans-Bold.ttf", IMGUI_MD_FONT_DIR "/DejaVuSans-Bold.ttf" }, 18.0f, &cfg, ranges);
    AddFont(io, { "C:/Windows/Fonts/seguisym.ttf", d + "DejaVuSans-Bold.ttf", IMGUI_MD_FONT_DIR "/DejaVuSans-Bold.ttf" }, 16.0f, &merge, symbols);
    md.Fonts.Italic = AddFont(io, { "C:/Windows/Fonts/segoeuii.ttf", d + "DejaVuSans-Oblique.ttf", IMGUI_MD_FONT_DIR "/DejaVuSans-Oblique.ttf" }, 18.0f, &cfg, ranges);
    md.Fonts.BoldItalic = AddFont(io, { "C:/Windows/Fonts/segoeuiz.ttf", d + "DejaVuSans-BoldOblique.ttf", IMGUI_MD_FONT_DIR "/DejaVuSans-BoldOblique.ttf" }, 18.0f, &cfg, ranges);
    md.Fonts.Code = AddFont(io, { "C:/Windows/Fonts/consola.ttf", d + "DejaVuSansMono.ttf", IMGUI_MD_FONT_DIR "/DejaVuSansMono.ttf" }, 16.0f, &cfg, ranges);    io.FontDefault = md.Fonts.Regular;
    md.LinkCallback = OnLink;
    ImageCache images;
    if (file)
    {
        images.Dir = file;
        size_t slash = images.Dir.find_last_of("/\\");
        images.Dir = (slash == std::string::npos) ? "." : images.Dir.substr(0, slash);
    }
    md.ImageCallback = OnImage;
    images.Diagrams = ImGuiMdDiagrams::CreateCache();
    md.CodeBlockCallback = OnCodeBlock;
    md.UserData = &images;

    ImGuiMdDocument* doc = ImGuiMd::CreateDocument();
    char* text = file ? ReadFile(file) : NULL;
    ImGuiMd::SetText(doc, text ? text : "# imgui_md\n\nPass a Markdown file on the command line.\n");

    ImGuiMdState state;
    state.HighlightLine = line;
    state.ScrollToLine = line;
    int frame = 0;
    int last_clicked = 0;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        if (mouse_x >= 0 && frame >= 3)             // Scripted mouse, after the backend which reads the real one
        {
            io.AddMousePosEvent((float)mouse_x, (float)mouse_y);
            if (click && frame == 4)
                io.AddMouseButtonEvent(0, true);
            if (click && frame == 5)
                io.AddMouseButtonEvent(0, false);
        }
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##main", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("highlight line", &state.HighlightLine);
        ImGui::SameLine();
        if (ImGui::Button("scroll to"))
            state.ScrollToLine = state.HighlightLine;
        ImGui::SameLine();
        ImGui::Checkbox("soft break = new line", &md.SoftBreakAsNewLine);
        ImGui::SameLine();
        ImGui::Text("clicked line: %d", last_clicked);
        ImGui::Separator();
        ImGui::BeginChild("##preview", ImVec2(0, 0), false);
        ImGuiMd::Render(doc, md, &state);
        if (state.ClickedLine > 0)
            last_clicked = state.ClickedLine;
        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        frame++;
        if (shot && frame == (frames > 0 ? frames : 5))
        {
            SaveScreenshot(window, shot);
            break;
        }
        glfwSwapBuffers(window);
    }

    ImGuiMd::DestroyDocument(doc);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    free(text);
    return 0;
}
