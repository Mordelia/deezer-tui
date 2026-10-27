
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/config.hpp"
#include "app/tui.hpp"
#include "cli/play.hpp"
#include "cli/selftest.hpp"
#include "cli/show_config.hpp"

namespace {

void print_usage(const char* program) {
  std::printf(
      "usage: %s [command]\n"
      "\n"
      "  (none)                    open the interface\n"
      "  --play <id> [--format F]  play a track (needs an ARL)\n"
      "  --show-config             print the loaded configuration\n"
      "  --self-test               check the crypto layer\n"
      "  --help                    print this help\n"
      "\n"
      "Configuration: %s\n"
      "  DEEZER_ARL / DEEZER_FORMAT override the file.\n",
      program, app::config_path().c_str());
}

bool parse_id(const std::string& text, long long* out) {
  try {
    std::size_t consumed = 0;
    const long long value = std::stoll(text, &consumed);
    if (consumed != text.size()) {
      return false;
    }
    *out = value;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

int run_play_command(const std::vector<std::string>& args, const char* program) {
  cli::PlayOptions options;

  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--format") {
      if (i + 1 >= args.size()) {
        std::fprintf(stderr, "error: --format expects a value\n");
        return 2;
      }
      options.format = args[++i];
      continue;
    }
    if (options.track_id == 0 && parse_id(args[i], &options.track_id)) {
      continue;
    }
    std::fprintf(stderr, "unexpected argument: %s\n", args[i].c_str());
    print_usage(program);
    return 2;
  }

  if (options.track_id == 0) {
    std::fprintf(stderr, "error: missing track id\n");
    print_usage(program);
    return 2;
  }
  return cli::run_play(options);
}

}

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);

  if (args.empty()) {
    return app::run_tui();
  }
  if (args[0] == "--help" || args[0] == "-h") {
    print_usage(argv[0]);
    return 0;
  }
  if (args[0] == "--self-test") {
    return cli::run_self_test();
  }
  if (args[0] == "--show-config") {
    return cli::run_show_config();
  }
  if (args[0] == "--play") {
    return run_play_command(args, argv[0]);
  }

  std::fprintf(stderr, "unknown command: %s\n", args[0].c_str());
  print_usage(argv[0]);
  return 2;
}
