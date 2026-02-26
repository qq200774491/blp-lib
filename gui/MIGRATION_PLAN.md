# BLP Viewer: Qt -> Dear ImGui (DirectX11 + Win32) 迁移计划

## 背景

Qt 打包体积过大（50MB+），对于一个 BLP 图像查看/转换工具来说不可接受。迁移到 Dear ImGui + DirectX11 + Win32 原生 API 后，预计打包体积 **< 2MB**（exe + blp_lib.dll）。仅支持 Windows 平台，直接替换 gui 目录。

---

## 新目录结构

```
gui/
  CMakeLists.txt                    -- 新 CMake（DX11 + ImGui）
  src/
    main.cpp                        -- WinMain + DX11 初始化 + 主循环
    app.h / app.cpp                 -- DX11 设备管理
    ui_main.h / ui_main.cpp         -- 主布局 + AppState 定义
    ui_left_panel.h / .cpp          -- 左面板
    ui_right_panel.h / .cpp         -- 右面板
    image_view.h / .cpp             -- 自定义图像查看器
    image_texture.h / .cpp          -- DX11 纹理管理
    blp_api.h / .cpp                -- BLP 库加载器（Win32 原生）
    image_io.h / .cpp               -- 图像 I/O（STB + std::）
    file_dialogs.h / .cpp           -- Win32 文件对话框
    settings.h / .cpp               -- INI 设置持久化
    win_integration.h / .cpp        -- 文件关联/缩略图/拖放
    style.h / .cpp                  -- ImGui 主题
    utils.h / .cpp                  -- 工具函数
    blp_viewer.rc                   -- 保留（图标和版本信息）
  third_party/
    stb/ (保留 + 新增 stb_image_resize2.h)
    imgui/ (新增)
  shell_ext/ (保留不变)
```

---

## Phase 0: 基础设施搭建

### Step 0.1 — Dear ImGui 源码
将以下文件放入 `gui/third_party/imgui/`:
- 核心: `imgui.h`, `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `imgui_internal.h`, `imconfig.h`, `imstb_*.h`
- 后端: `imgui_impl_win32.h/.cpp`, `imgui_impl_dx11.h/.cpp`
- 可选: `imgui_demo.cpp`

### Step 0.2 — stb_image_resize2
将 `stb_image_resize2.h` 放入 `gui/third_party/stb/`。

### Step 0.3 — CMakeLists.txt
- 移除 Qt 依赖 (`find_package(Qt)`, `CMAKE_AUTOMOC/AUTORCC/AUTOUIC`)
- 编译 ImGui 源码
- 链接: `d3d11 d3dcompiler dxgi dwmapi ole32 comdlg32 shell32 shlwapi gdi32`
- 添加 `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `/utf-8`
- 保留 `BLP_STATIC_LINK` 和 `blp_thumbnail` DLL

---

## Phase 1: DX11 窗口 + ImGui 初始化

### app.h/.cpp
DX11 设备生命周期: `CreateDeviceD3D()` / `CleanupDeviceD3D()` / `CreateRenderTarget()`

### main.cpp
- `wWinMain` 入口, `RegisterClassExW` + `CreateWindowExW`
- `DragAcceptFiles(hwnd, TRUE)` 启用文件拖放
- DPI: `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`
- 主循环: `PeekMessage` → ImGui NewFrame → 渲染 UI → Present
- WndProc: `WM_DROPFILES`, `WM_SIZE`, `WM_DPICHANGED`
- 命令行: `CommandLineToArgvW`

### style.cpp
- 蓝白配色 (按钮 `#2d6cdf`, 圆角 6-8px)
- 加载 `msyh.ttc` + `GetGlyphRangesChineseFull()`

---

## Phase 2: 核心业务逻辑（Qt → std:: 替换）

### utils.h/.cpp
纯函数: `isPowerOfTwo()`, `nearestPowerOfTwo()`, `nextPowerOfTwo()`, `autoMipCount()`, `snapResizeValue()`, `normalizeInputPath()`, `formatFileSize()`, `normalizeFormat()`, `supportedExtensions()`, `isSupportedFile()`

### image_io.h/.cpp
类型替换:
| Qt 类型 | 替换为 |
|---------|--------|
| `QByteArray` | `std::vector<uint8_t>` |
| `QString` | `std::string` |
| `QFile` | `std::ifstream/ofstream` |
| `QDir::mkpath()` | `std::filesystem::create_directories()` |
| `QFileInfo` | `std::filesystem::path` |
| `QImage` | 移除 |

### blp_api.h/.cpp
动态加载替换:
| Qt API | Win32 API |
|--------|-----------|
| `QLibrary` | `HMODULE` + `LoadLibraryW()` |
| `QLibrary::resolve()` | `GetProcAddress()` |
| `QCoreApplication::applicationDirPath()` | `GetModuleFileNameW()` |
| `qEnvironmentVariable()` | `GetEnvironmentVariableW()` |

---

## Phase 3: DX11 纹理 + 图像查看器

### image_texture.h/.cpp
- `DXGI_FORMAT_R8G8B8A8_UNORM` 纹理创建/更新
- 棋盘格背景纹理生成

### image_view.h/.cpp
- 缩放: `MouseWheel * 1.15` 倍率，鼠标位置为中心
- 平移: `IsMouseDragging(Left)` + `MouseDelta`
- 适配: `fitToView()` = `min(viewW/imgW, viewH/imgH)`
- 渲染: `ImGui::Image()` + 手动偏移

---

## Phase 4: Win32 原生集成

### file_dialogs.h/.cpp
COM `IFileOpenDialog` / `IFileSaveDialog` 接口

### settings.h/.cpp
INI 文件持久化 (`%APPDATA%/blp_viewer/settings.ini`)

### win_integration.h/.cpp
- BLP 文件关联: `AssocQueryStringW` + `RegCreateKeyExW`
- 缩略图注册: `LoadLibraryW` + `GetProcAddress`
- 拖放: `WM_DROPFILES`

---

## Phase 5-7: UI 面板

### ui_main.h/.cpp — AppState + 主布局
```
+----------------------------------------------------+
| [Toolbar: 关联BLP | 缩略图开关]                       |
+------------------+--+------------------------------+
|   Left Panel     |分|      Right Panel             |
|   (文件列表/设置)  |割| (预览/信息/Mip/调整/缩放)       |
+------------------+--+------------------------------+
| [Status: 文件信息 | 缩放: 100% | BLP: v0.1.32]      |
+----------------------------------------------------+
```
分割条: `ImGui::InvisibleButton("##splitter")` + 拖动

### ui_left_panel.h/.cpp — 左面板
| Qt 控件 | ImGui 替代 |
|---------|-----------|
| `QListWidget` | `ImGui::Selectable` 循环 |
| `QPushButton` | `ImGui::Button()` |
| `QLineEdit` | `ImGui::InputText()` |
| `QRadioButton` | `ImGui::RadioButton()` |
| `QSlider` | `ImGui::SliderInt()` |
| `QCheckBox` | `ImGui::Checkbox()` |
| `QProgressBar` | `ImGui::ProgressBar()` |
| `QPlainTextEdit` | `ImGui::TextWrapped()` 循环 |

### ui_right_panel.h/.cpp — 右面板
- 信息栏（非 POT 红色警告）
- 图像预览区 + POT 对齐按钮 + Alpha 开关 + 背景颜色
- Mip 列表: `ImGui::Selectable()` 循环
- 调整控件: `ImGui::SliderInt()` + 预设按钮 + `stbir_resize_uint8_linear()`
- 缩放: 适应/1:1/滑块

---

## Phase 8: 收尾

- Alpha 通道: 修改 CPU RGBA buffer 后更新 DX11 纹理
- 临时文件: `GetTempPathW()` + `GetTempFileNameW()`
- DPI: `GetDpiForWindow(hwnd) / 96.0f`
- `blp_thumbnail` DLL: 保持不变
- `blp_viewer.rc`: 保持不变

---

## 实施状态

| 阶段 | 状态 |
|------|------|
| Phase 0: 基础设施 | ✅ 完成 (ImGui/stb 下载, CMakeLists.txt) |
| Phase 1: DX11+ImGui 初始化 | ✅ 完成 (app/main/style) |
| Phase 2: 核心业务逻辑 | ✅ 完成 (utils/image_io/blp_api) |
| Phase 3: DX11 纹理+查看器 | ✅ 完成 (image_texture/image_view) |
| Phase 4: Win32 集成 | ✅ 完成 (file_dialogs/settings/win_integration) |
| Phase 5-7: UI 面板 | ✅ 完成 (ui_main/ui_left_panel/ui_right_panel) |
| Phase 8: 编译调试 | ✅ 完成 (零警告编译通过, exe 958KB) |

---

## 代码量

| 模块 | 实际行数 |
|------|---------|
| main + app | 383 |
| UI (main + left + right) | 1,363 |
| image_view + image_texture | 268 |
| blp_api + image_io + utils | 699 |
| file_dialogs + settings + win_integration | 434 |
| style | 104 |
| CMakeLists.txt | 120 |
| **总计** | **3,371** |

---

## 风险与应对

| 风险 | 应对策略 |
|------|---------|
| 中文渲染 | 加载 `msyh.ttc` + `GetGlyphRangesChineseFull()` |
| 缩放/平移手感 | 参考 Qt 的 1.15 倍率和居中公式 |
| 图片缩放质量 | `stb_image_resize2` linear 滤波 |
| DPI 缩放 | `SetProcessDpiAwarenessContext` + 按倍率缩放 |
| MSVC UTF-8 | `/utf-8` 编译选项 + `NOMINMAX` |
