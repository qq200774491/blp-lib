#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <Windows.h>
#include <ShlObj.h>

std::string AppSettings::settingsPath() {
    wchar_t* appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        char buf[MAX_PATH * 3] = {};
        WideCharToMultiByte(CP_UTF8, 0, appData, -1, buf, sizeof(buf), nullptr, nullptr);
        CoTaskMemFree(appData);
        std::filesystem::path dir = std::filesystem::path(buf) / "blp_viewer";
        std::filesystem::create_directories(dir);
        return (dir / "settings.ini").string();
    }
    return "settings.ini";
}

void AppSettings::load() {
    std::ifstream file(settingsPath());
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == '[') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Trim
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());

        if (key == "windowX") windowX = std::atoi(value.c_str());
        else if (key == "windowY") windowY = std::atoi(value.c_str());
        else if (key == "windowW") windowW = std::atoi(value.c_str());
        else if (key == "windowH") windowH = std::atoi(value.c_str());
        else if (key == "splitterPos") splitterPos = static_cast<float>(std::atof(value.c_str()));
        else if (key == "outputFormat") outputFormat = std::atoi(value.c_str());
        else if (key == "quality") quality = std::atoi(value.c_str());
        else if (key == "overwrite") overwrite = (value == "1");
        else if (key == "recursive") recursive = (value == "1");
        else if (key == "lastInputDir") lastInputDir = value;
        else if (key == "lastOutputDir") lastOutputDir = value;
    }
}

void AppSettings::save() const {
    std::string path = settingsPath();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return;

    file << "[window]\n";
    file << "windowX=" << windowX << "\n";
    file << "windowY=" << windowY << "\n";
    file << "windowW=" << windowW << "\n";
    file << "windowH=" << windowH << "\n";
    file << "splitterPos=" << splitterPos << "\n";
    file << "\n[convert]\n";
    file << "outputFormat=" << outputFormat << "\n";
    file << "quality=" << quality << "\n";
    file << "overwrite=" << (overwrite ? "1" : "0") << "\n";
    file << "recursive=" << (recursive ? "1" : "0") << "\n";
    file << "\n[paths]\n";
    file << "lastInputDir=" << lastInputDir << "\n";
    file << "lastOutputDir=" << lastOutputDir << "\n";
}
