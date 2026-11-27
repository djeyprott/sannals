// Stellar Annals viewer: a Dear ImGui window onto a running simulation.

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#include <GLFW/glfw3.h>

#include "galaxy_gen.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "sim.h"
#include "viewer.h"

namespace {

void glfwError(int code, const char* description) { std::fprintf(stderr, "GLFW error %d: %s\n", code, description); }

void printUsage() {
  std::fprintf(stderr,
               "Usage: stellar_annals_viewer [--seed N] [--size cluster|small|medium|large|huge]\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::uint64_t seed = 0;
  bool seedGiven = false;
  GalaxySize size = GalaxySize::Small;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--seed" && i + 1 < argc) {
      seed = std::strtoull(argv[++i], nullptr, 10);
      seedGiven = true;
    } else if (arg == "--size" && i + 1 < argc) {
      const auto parsed = parseGalaxySize(argv[++i]);
      if (!parsed) {
        printUsage();
        return 1;
      }
      size = *parsed;
    } else {
      printUsage();
      return 1;
    }
  }
  if (!seedGiven) {
    std::random_device rd;
    seed = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
  }

  glfwSetErrorCallback(glfwError);
  if (!glfwInit()) return 1;

#ifdef __APPLE__
  const char* glslVersion = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
  const char* glslVersion = "#version 330";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

  GLFWwindow* window = glfwCreateWindow(1600, 900, "Stellar Annals " STELLAR_VERSION, nullptr, nullptr);
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
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 4.0f;
  style.Colors[ImGuiCol_WindowBg].w = 0.88f;  // let the map show through a little
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glslVersion);

  World world = generateGalaxy({size, seed});
  Sim sim(world);
  sim.start();

  char title[128];
  std::snprintf(title, sizeof title, "v" STELLAR_VERSION " - seed %llu - %s galaxy - %zu systems", static_cast<unsigned long long>(seed),
                galaxySizeName(size), world.systems.size());
  {
    Viewer viewer(world, sim, title);  // scoped: frees its GL texture before shutdown

    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
      if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
        ImGui_ImplGlfw_Sleep(10);
        continue;
      }

      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      viewer.frame(io.DeltaTime);
      ImGui::Render();

      int width, height;
      glfwGetFramebufferSize(window, &width, &height);
      glViewport(0, 0, width, height);
      glClearColor(0.02f, 0.025f, 0.05f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      glfwSwapBuffers(window);
    }
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
