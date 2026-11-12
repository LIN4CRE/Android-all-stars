// The preview tool: draws the app's screens (the hub, the menus, the
// on-screen controls) on the host and saves them as pictures, so that the
// layout can be looked at without a phone. It runs the real application code
// (app/*.cpp) with the tests' stand-in for Android and a software canvas
// (host/soft_canvas.h). No game is involved: where the game's picture would
// be, a placeholder is drawn.
//
//   smas_preview <output folder> [body.ttf bold.ttf]
//
// writes one .ppm per screen (half size); tools/ppm2png.py makes PNGs of them.
// These are renderings for checking layout, not screenshots of a device: the
// phone draws text with its own font.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "app/sys.h"
#include "core/fileio.h"
#include "host/fake_platform.h"
#include "host/soft_canvas.h"

#ifndef SMAS_REPO_DIR
#define SMAS_REPO_DIR "."
#endif

namespace {
struct Scene {
  const char* name;
  int w, h;
  float insets[4];
  float density;
  bool gameFile;
  std::function<void(fake::Session&)> setup;
};

std::wstring wide(const std::string& s) { return widenUtf8(s); }

void page(fake::Session& s, std::shared_ptr<MenuPage> p) { s.app->pushPage(std::move(p)); }

// (the hub as it looks once a game file is in place and a game has been played)
void played(fake::Session& s) {
  s.app->romInfo.verdict = RomCheck::Verified;  // (a picture of the usual case; the stand-in file is not the game)
  s.app->launchGame(GameId::Menu);
  s.frames(30);
  s.app->returnToHub();
  s.app->refreshHub();
  s.frames(300);  // (until the game's first-run hint has gone)
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: smas_preview <output folder> [body.ttf bold.ttf]\n");
    return 2;
  }
  const std::string out = argv[1];
  const std::string body = argc > 3 ? argv[2] : "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
  const std::string bold = argc > 3 ? argv[3] : "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
  const std::string title = std::string(SMAS_REPO_DIR) + "/third_party/fonts/LuckiestGuy-Regular.ttf";
  std::filesystem::create_directories(out);

  const std::vector<Scene> scenes = {
      {"hub-first-run", 2400, 1080, {0, 0, 0, 0}, 2.625f, false, [](fake::Session&) {}},
      {"hub", 2400, 1080, {0, 0, 0, 0}, 2.625f, true, [](fake::Session& s) { played(s); }},
      {"hub-16x9-cutout", 1920, 1080, {96, 0, 0, 0}, 2.625f, true, [](fake::Session& s) { played(s); }},
      {"hub-tablet", 2560, 1600, {0, 0, 0, 0}, 2.0f, true, [](fake::Session& s) { played(s); }},
      {"hub-720p", 1600, 720, {0, 0, 0, 0}, 1.75f, true, [](fake::Session& s) { played(s); }},
      {"play-touch", 2400, 1080, {0, 0, 0, 0}, 2.625f, true,
       [](fake::Session& s) {
         s.app->cfg.achievements = false;  // (so that rewind and fast forward show)
         s.app->launchGame(GameId::Menu);
       }},
      {"play-touch-16x9", 1920, 1080, {0, 0, 0, 0}, 2.625f, true, [](fake::Session& s) { s.app->launchGame(GameId::Menu); }},
      {"play-touch-large", 2400, 1080, {110, 0, 0, 0}, 2.625f, true,
       [](fake::Session& s) {
         s.app->cfg.touchScale = 130;
         s.app->cfg.achievements = false;
         s.app->launchGame(GameId::Menu);
       }},
      {"pause-menu", 2400, 1080, {0, 0, 0, 0}, 2.625f, true,
       [](fake::Session& s) {
         s.app->launchGame(GameId::Menu);
         s.frames(10);
         s.app->openMenu();
       }},
      {"save-management", 2400, 1080, {0, 0, 0, 0}, 2.625f, true,
       [](fake::Session& s) {
         played(s);
         page(s, s.app->pageSaves());
       }},
      {"game-file-first-run", 2400, 1080, {0, 0, 0, 0}, 2.625f, false, [](fake::Session& s) { page(s, s.app->pageGameFile()); }},
      {"import-refused", 2400, 1080, {0, 0, 0, 0}, 2.625f, false,
       [](fake::Session& s) {
         fake::sys().document.assign(300000, 0x5a);
         fake::sys().document[0] = 'P', fake::sys().document[1] = 'K', fake::sys().document[2] = 3, fake::sys().document[3] = 4;
         fake::sys().documentName = "Super Mario All-Stars (USA).zip";
         s.app->pickGameFile();
       }},
      {"help-saving", 2400, 1080, {0, 0, 0, 0}, 2.625f, true,
       [](fake::Session& s) {
         played(s);
         page(s, s.app->pageHelp());
         page(s, s.app->pageHelpTopic(1));
       }},
      {"display-settings", 2400, 1080, {0, 0, 0, 0}, 2.625f, true,
       [](fake::Session& s) {
         played(s);
         page(s, s.app->pageDisplay());
       }},
      {"controls", 1920, 1080, {0, 0, 0, 0}, 2.625f, true,
       [](fake::Session& s) {
         played(s);
         page(s, s.app->pageControls());
       }},
      {"about", 2400, 1080, {0, 0, 0, 0}, 2.625f, true,
       [](fake::Session& s) {
         played(s);
         page(s, s.app->pageAbout());
       }},
      {"touch-editor", 2400, 1080, {0, 0, 0, 0}, 2.625f, true,
       [](fake::Session& s) {
         played(s);
         s.app->openTouchEditor();
       }},
      {"menu-controller-tv", 1920, 1080, {0, 0, 0, 0}, 1.0f, true,
       [](fake::Session& s) {
         played(s);
         fake::sys().pad = true;
         s.app->touchActive = false;
         page(s, s.app->pageDisplay());
       }},
  };

  int failed = 0;
  for (const Scene& sc : scenes) {
    char tmpl[] = "/tmp/smas-preview-XXXXXX";
    const char* dir = mkdtemp(tmpl);
    if (!dir) return 1;
    const std::wstring data = wide(std::string(dir) + "/data"), shared = wide(std::string(dir) + "/shared");
    makeDir(data);
    makeDir(shared);
    fake::reset();
    fake::Platform& sys = fake::sys();
    sys.width = sc.w;
    sys.height = sc.h;
    for (int i = 0; i < 4; i++) sys.insets[i] = sc.insets[i];
    sys.density = sc.density;
    SoftCanvas canvas(sc.w, sc.h, body, bold, title);
    if (!canvas.fontsOk()) {
      std::fprintf(stderr, "fonts not found (%s, %s, %s)\n", body.c_str(), bold.c_str(), title.c_str());
      return 1;
    }
    sys.mirror = &canvas;
    canvas.painting = false;  // (frames are laid out with its font; only the last is painted)
    if (sc.gameFile) {
      makeDir(pathJoin(data, L"roms"));
      const std::vector<uint8_t> file = fake::syntheticGameFile();
      writeFileSafely(pathJoin(pathJoin(data, L"roms"), L"game.sfc"), file.data(), file.size());
    }
    {
      fake::Session s(data, shared);
      s.app->touchActive = true;
      s.frames(3);
      sc.setup(s);
      s.frames(40);
      canvas.painting = true;
      canvas.clear(colorF(0, 0, 0));
      sys.drawn.clear();
      s.app->render();
      const std::string path = out + "/" + sc.name + ".ppm";
      if (!canvas.savePpm(path, 2)) {
        std::fprintf(stderr, "could not write %s\n", path.c_str());
        failed++;
      } else {
        std::printf("%s\n", path.c_str());
      }
      sys.mirror = nullptr;
      s.kill();
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
  return failed ? 1 : 0;
}
