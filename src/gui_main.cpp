#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

#include <portable-file-dialogs.h>

#include "apex/apxc.hpp"

#if __has_include("apex_compress_version.hpp")
#include "apex_compress_version.hpp"
#else
#define APEX_COMPRESS_VERSION "0.1.0-dev"
#endif

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#endif
#include <GLFW/glfw3.h>

namespace {

void glfwErrorCallback(int error, const char* description) {
  (void)error;
  (void)description;
}

std::mutex gStatusMutex;
enum class StatusKind { Info, Success, Error };
StatusKind gStatusKind = StatusKind::Info;
std::string gStatus = "Pick Input, adjust Output if needed, then Run.";

std::atomic<bool> gJobRunning{false};

enum class JobKind { Compress, Decompress };

void setStatus(StatusKind kind, std::string msg) {
  std::scoped_lock lk(gStatusMutex);
  gStatusKind = kind;
  gStatus = std::move(msg);
}

[[nodiscard]] bool preflight(JobKind kind, const std::filesystem::path& inPath,
                             const std::filesystem::path& outPath, std::string& err) {
  std::error_code ec;
  if (inPath.empty() || outPath.empty()) {
    err = "Choose input and output paths.";
    return false;
  }
  if (!std::filesystem::exists(inPath, ec) || ec) {
    err = "Input does not exist.";
    return false;
  }
  if (!std::filesystem::is_regular_file(inPath, ec) || ec) {
    err = "Input must be a file.";
    return false;
  }
  if (std::filesystem::equivalent(inPath, outPath, ec) && !ec) {
    err = "Input and output must differ.";
    return false;
  }
  if (outPath.has_parent_path()) {
    std::filesystem::create_directories(outPath.parent_path(), ec);
    (void)ec;
  }
  if (kind == JobKind::Decompress && inPath.extension() != ".apxc") {
    err = "Pick a .apxc file.";
    return false;
  }
  return true;
}

void runJobAsync(JobKind kind, const std::filesystem::path& inPath,
                 const std::filesystem::path& outPath, apex::ApexCompressOptions opt) {
  if (gJobRunning.exchange(true)) return;

  std::thread([kind, inPath, outPath, opt]() {
    try {
      if (kind == JobKind::Compress) {
        apex::compressFile(inPath, outPath, opt);
      } else {
        apex::decompressFile(inPath, outPath);
      }
      setStatus(StatusKind::Success, std::string("Saved: ") + outPath.string());
    } catch (const std::exception& e) {
      setStatus(StatusKind::Error, std::string("Error: ") + e.what());
    }
    gJobRunning = false;
  }).detach();
}

void applyMinimalStyle() {
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowPadding = ImVec2(16, 14);
  s.FramePadding = ImVec2(10, 7);
  s.ItemSpacing = ImVec2(10, 8);
  s.ItemInnerSpacing = ImVec2(8, 6);
  s.ScrollbarSize = 11.0f;
  s.WindowRounding = 0.0f;
  s.ChildRounding = 6.0f;
  s.FrameRounding = 6.0f;
  s.PopupRounding = 6.0f;
  s.TabRounding = 6.0f;
}

[[nodiscard]] std::filesystem::path toFsPath(const std::string& p) {
  return std::filesystem::path(p);
}

template <typename Fn>
void pathRow(const char* pushId, const char* caption, const std::string& path, const char* buttonText,
             Fn&& onClick) {
  ImGui::PushID(pushId);
  ImGui::TextUnformatted(caption);
  if (ImGui::Button(buttonText)) {
    std::forward<Fn>(onClick)();
  }
  ImGui::SameLine();
  const float wrapRight = ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x;
  ImGui::PushStyleColor(ImGuiCol_Text,
                        path.empty() ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                                     : ImGui::GetStyleColorVec4(ImGuiCol_Text));
  ImGui::PushTextWrapPos(wrapRight);
  ImGui::TextWrapped("%s", path.empty() ? "Not set" : path.c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
  ImGui::PopID();
}

} // namespace

int main() {
  glfwSetErrorCallback(glfwErrorCallback);
  if (!glfwInit()) return 1;

#if defined(__APPLE__)
  const char* glslVersion = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
  const char* glslVersion = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

  std::string title = std::string("Apex Compress ") + APEX_COMPRESS_VERSION;
  GLFWwindow* window = glfwCreateWindow(640, 560, title.c_str(), nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();
  applyMinimalStyle();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glslVersion);

  int mode = 0;
  std::string inPath;
  std::string outPath;
  int lastTabMode = -1;
  constexpr apex::ApexCompressOptions kGuiCompressOpts{};

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    constexpr ImGuiWindowFlags kWin =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##apex_main", nullptr, kWin);

    ImGui::TextUnformatted("Apex Compress");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", APEX_COMPRESS_VERSION);
    ImGui::Spacing();
    ImGui::TextDisabled("Compressed format is .apxc (binary). Works with any file type.");

    if (ImGui::BeginTabBar("##modes", ImGuiTabBarFlags_None)) {
      if (ImGui::BeginTabItem("Compress")) {
        mode = 0;
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Decompress")) {
        mode = 1;
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();

      if (lastTabMode >= 0 && mode != lastTabMode) {
        inPath.clear();
        outPath.clear();
        setStatus(StatusKind::Info, mode == 0 ? "Compress: choose files." : "Decompress: choose .apxc.");
      }
      lastTabMode = mode;
    }

    std::string statusCopy;
    StatusKind statusKind = StatusKind::Info;
    {
      std::scoped_lock lk(gStatusMutex);
      statusCopy = gStatus;
      statusKind = gStatusKind;
    }

    const ImVec4 errCol(0.95f, 0.50f, 0.52f, 1.00f);
    const ImVec4 okCol(0.50f, 0.84f, 0.58f, 1.00f);
    const ImVec4 infCol(0.78f, 0.80f, 0.84f, 1.00f);
    ImGui::Separator();
    ImGui::Spacing();

    constexpr float footerH = 74.0f;
    ImGui::BeginChild("##body", ImVec2(0.0f, -footerH), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImGui::PushStyleColor(ImGuiCol_Text,
                         statusKind == StatusKind::Error   ? errCol
                         : statusKind == StatusKind::Success ? okCol
                                                             : infCol);
    ImGui::TextWrapped("%s", statusCopy.c_str());
    ImGui::PopStyleColor();

    if (gJobRunning.load()) {
      const float u = static_cast<float>(
                         std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count() %
                         1300) /
                     1300.0f;
      ImGui::ProgressBar(u, ImVec2(-1.0f, 0.0f));
    }

    ImGui::Spacing();

    auto pickInput = [&]() {
      if (mode == 0) {
        auto paths = pfd::open_file("File to compress", "", {"All files", "*"}).result();
        if (!paths.empty()) {
          inPath = paths.front();
          outPath = inPath + ".apxc";
          setStatus(StatusKind::Info, "Ready.");
        }
      } else {
        auto paths =
            pfd::open_file("Open .apxc", "", {"APXC", "*.apxc", "All files", "*"}).result();
        if (!paths.empty()) {
          inPath = paths.front();
          std::filesystem::path p = toFsPath(inPath);
          if (p.has_extension() && p.extension() == ".apxc")
            p.replace_extension();
          else
            p += ".out";
          outPath = p.string();
          setStatus(StatusKind::Info, "Ready.");
        }
      }
    };

    auto pickOutput = [&]() {
      auto path = pfd::save_file("Output path", outPath.empty() ? "." : outPath, {"All files", "*"},
                                 pfd::opt::force_overwrite)
                      .result();
      if (!path.empty()) outPath = path;
    };

    pathRow("in_row", mode == 0 ? "Input" : "Input (.apxc)", inPath, "Choose file…##in_btn", pickInput);

    ImGui::Spacing();
    pathRow("out_row", "Output", outPath, "Save as…##out", pickOutput);

    ImGui::EndChild();

    ImGui::Separator();
    const bool blocked = gJobRunning.load();
    const bool canRun = !inPath.empty() && !outPath.empty();
    bool ok = false;
    if (blocked || !canRun) ImGui::BeginDisabled();

    ImVec2 sz(-1.0f, ImGui::GetFrameHeightWithSpacing() * 1.35f);
    if (mode == 0) {
      ok = ImGui::Button("Compress##run", sz);
    } else {
      ok = ImGui::Button("Decompress##run", sz);
    }

    if (blocked || !canRun) ImGui::EndDisabled();

    if (ok && !blocked) {
      const JobKind kind = (mode == 0) ? JobKind::Compress : JobKind::Decompress;
      std::filesystem::path inFs = toFsPath(inPath);
      std::filesystem::path outFs = toFsPath(outPath);
      std::string err;
      if (!preflight(kind, inFs, outFs, err)) {
        setStatus(StatusKind::Error, err);
      } else {
        setStatus(StatusKind::Info, (mode == 0) ? "Compressing…" : "Decompressing…");
        runJobAsync(kind, inFs, outFs, kGuiCompressOpts);
      }
    }

    ImGui::End();

    ImGui::Render();
    int displayW = 0;
    int displayH = 0;
    glfwGetFramebufferSize(window, &displayW, &displayH);
    glViewport(0, 0, displayW, displayH);
    const ImVec4 clear = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
    glClearColor(clear.x * clear.w, clear.y * clear.w, clear.z * clear.w, clear.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
