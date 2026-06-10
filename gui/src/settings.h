#pragma once

#include <string>

struct AppSettings {
    int windowX = 100;
    int windowY = 100;
    int windowW = 1200;
    int windowH = 760;
    float splitterPos = 340.0f;

    int outputFormat = 0;  // 0=BLP 1=PNG 2=JPG 3=BMP 4=TGA
    int quality = 100;
    bool overwrite = false;
    bool recursive = true;

    int batchSizeMode = 0;      // 0=原始大小 1=64×64 2=128×128 3=自定义
    int batchWidth = 256;
    int batchHeight = 256;
    bool batchLockAspect = true;
    int batchResizeMethod = 1;  // 0=拉伸 1=居中透明

    std::string lastInputDir;
    std::string lastOutputDir;

    void load();
    void save() const;
    static std::string settingsPath();
};
