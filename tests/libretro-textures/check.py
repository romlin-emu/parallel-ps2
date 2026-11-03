#!/usr/bin/env python3
"""Compile the production folder/configuration functions with a mock frontend and real directories.

This checks the libretro boundary without booting a VM or requiring a BIOS/GPU.
The full core build separately checks these functions against the real platform headers.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ARMS = (ROOT / "pcsx2-libretro").is_dir()

def function(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

configure = function("pcsx2-libretro/Main.cpp", "void Host::LoadSettings(") if ARMS else function("libretro/main.cpp", "static void LoadGameTextureReplacements()")
selector = function("pcsx2/GS/Renderers/HW/GSTextureReplacements.cpp", "std::string GSTextureReplacements::GetGameTextureDirectory()")
join_path = "" if ARMS else function("pcsx2/GS/Renderers/HW/GSTextureReplacements.cpp", "static __noinline std::string JoinPath(")
prelude = r"""
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#define ENABLE_LIBRETRO
#define __noinline __attribute__((noinline))
#define PCSX2_PATH_MAX 4096
#define PATH_MAX_LENGTH 4096
#define TEXTURE_REPLACEMENT_SUBDIRECTORY_NAME "replacements"
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY 31
namespace fs = std::filesystem;
struct Config { struct { bool LoadTextureReplacements = false; } GS; } EmuConfig, s_option_config;
struct SettingsInterface {};
namespace Host { void LoadSettings(SettingsInterface&, std::unique_lock<std::mutex>&); }
namespace GSTextureReplacements { std::string GetGameTextureDirectory(); }
static std::string s_current_serial = "SLUS-12345";
static const char* save_directory = nullptr;
static bool available = true;
static bool environ_cb(unsigned command, void* out) {
    assert(command == RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY);
    *static_cast<const char**>(out) = save_directory;
    return available;
}
namespace Path { static std::string Combine(const std::string& a, const std::string& b) { return (fs::path(a) / b).string(); } }
namespace FileSystem { static bool DirectoryExists(const char* p) { return fs::is_directory(p); } }
static bool path_is_directory(const char* p) { return fs::is_directory(p); }
#define strlcpy test_strlcpy
static size_t strlcpy(char* out, const char* in, size_t n) {
    std::snprintf(out, n, "%s", in);
    return std::strlen(in);
}
static void pcsx2_path_join(char* out, size_t n, const char* a, const char* b) {
    strlcpy(out, Path::Combine(a, b).c_str(), n);
}
#define fill_pathname_join(out, a, b, n) pcsx2_path_join(out, n, a, b)
"""
prelude += "namespace EmuFolders { std::string Textures; }\n" if ARMS else "namespace EmuFolders { char Textures[PCSX2_PATH_MAX]; }\n"
reset = 'EmuFolders::Textures = native;' if ARMS else 'strlcpy(EmuFolders::Textures, native.c_str(), sizeof(EmuFolders::Textures));'
call = 'SettingsInterface si; std::mutex mutex; std::unique_lock<std::mutex> lock(mutex); Host::LoadSettings(si, lock);' if ARMS else 'LoadGameTextureReplacements();'
config = 'EmuConfig' if ARMS else 's_option_config'
main = r"""
int main(int argc, char** argv) {
    assert(argc == 2);
    const fs::path scratch(argv[1]);
    const std::string native = (scratch / "system/pcsx2/textures").string();
    const std::string game = (scratch / "game-a").string();
    fs::create_directories(native);
    auto check = [&](const char* save, bool present, bool expected) {
        save_directory = save; available = present;
        RESET
        CONFIG.GS.LoadTextureReplacements = false;
        CALL
        assert(CONFIG.GS.LoadTextureReplacements == expected);
        const std::string root = expected ? (fs::path(save) / "textures").string() : native;
        assert(std::string(EmuFolders::Textures) == root);
        assert(GSTextureReplacements::GetGameTextureDirectory() == (expected ? root : (fs::path(native) / s_current_serial).string()));
    };
    const std::string long_save(PCSX2_PATH_MAX, 'x');
    check(long_save.c_str(), true, false);
    check(nullptr, false, false);
    check(nullptr, true, false);
    check("", true, false);
    check(game.c_str(), true, false);
    CONFIG.GS.LoadTextureReplacements = true;
    CALL
    assert(CONFIG.GS.LoadTextureReplacements);
    fs::create_directories(fs::path(game) / "textures");
    { auto* f = std::fopen((fs::path(game) / "textures/replacements").string().c_str(), "w"); assert(f); std::fclose(f); }
    check(game.c_str(), true, false);
    fs::remove(fs::path(game) / "textures/replacements");
    fs::create_directories(fs::path(game) / "textures/replacements/subfolder");
    check(game.c_str(), true, true);
    check(game.c_str(), false, false);
    const std::string next = (scratch / "game-b").string();
    check(next.c_str(), true, false);
    s_current_serial = "SLES-54321";
    check(next.c_str(), true, false);
    std::puts("PASS: installed pack, native serial fallback, missing/empty/refused save directory, non-directory, next game");
}
""".replace('RESET', reset).replace('CONFIG', config).replace('CALL', call)
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    source = temp / "check.cpp"
    source.write_text(prelude + configure + "\n" + join_path + "\n" + selector + "\n" + main)
    binary = temp / "check"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary), str(temp / "data")], check=True)
