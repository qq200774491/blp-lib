# BLP Viewer

[English](#blp-viewer) | [中文](#blp-viewer中文)

Windows GUI tool for viewing and batch-converting BLP (Blizzard texture format, Warcraft III).

Dear ImGui + Direct3D 11 frontend with a built-in C++ BLP1 codec.

---

## Features

- Preview BLP, PNG, JPG, BMP, TGA with zoom/pan
- Alpha channel overlay display
- Mipmap level selection
- Drag-and-drop batch conversion between formats
- Image resize (stretch / center-on-transparent), layer composite, border batch operations
- Windows Explorer thumbnail integration via a registered shell extension (`blp_thumbnail.dll`)
- File association manager

**Codec coverage**

| Format | Decode | Encode |
|--------|--------|--------|
| BLP1 palette (1 / 4 / 8-bit alpha) | ✓ | — |
| BLP1 JPEG-content | ✓ | ✓ (quality 0–100, full mipmap chain) |
| BLP2 (World of Warcraft) | — | — |

## Requirements

- Windows 10 x64 or later
- Visual Studio 2019+ (MSVC x64 toolchain)
- CMake 3.16+

All third-party dependencies are vendored under `third_party/` (Dear ImGui, stb_image, libjpeg-turbo static lib). No additional installs required.

## Build

```powershell
.\build.ps1 -Config Release
```

Outputs to `build/Release/`:

| File | Description |
|------|-------------|
| `blp_viewer.exe` | Main application |
| `blp_thumbnail.dll` | Shell extension — register separately if needed |
| `blp_codec_selftest.exe` | Codec regression test binary |

## Testing

```powershell
.\build\Release\blp_codec_selftest.exe test-data\blp test-data\png
```

Runs decode regression (PSNR comparison against reference PNGs) and encode round-trip validation over the samples in `test-data/`.

## Installer

Requires [Inno Setup 6](https://jrsoftware.org/isinfo.php).

```powershell
.\installer\build_installer.ps1 -Config Release
```

Output: `installer/dist/BLP_Viewer_Setup_x64.exe`

## License

MIT

---

# BLP Viewer（中文）

Windows 下的 BLP（暴雪纹理格式，魔兽争霸 III）查看与批量转换工具。

Dear ImGui + Direct3D 11 前端，内置 C++ BLP1 编解码器。

---

## 功能

- 预览 BLP、PNG、JPG、BMP、TGA，支持缩放/平移
- Alpha 通道叠加显示
- Mipmap 层级切换
- 拖拽批量格式互转
- 图像缩放（拉伸 / 居中透明背景）、图层合成、边框批量操作
- 通过注册 shell 扩展（`blp_thumbnail.dll`）在资源管理器中显示缩略图
- 文件关联管理器

**编解码范围**

| 格式 | 解码 | 编码 |
|------|------|------|
| BLP1 调色板（1 / 4 / 8-bit alpha） | ✓ | — |
| BLP1 JPEG-content | ✓ | ✓（quality 0–100，完整 mipmap 链） |
| BLP2（魔兽世界） | — | — |

## 环境要求

- Windows 10 x64 及以上
- Visual Studio 2019+（MSVC x64 工具链）
- CMake 3.16+

所有第三方依赖均已 vendored（`third_party/`：Dear ImGui、stb_image、libjpeg-turbo 预编译静态库），无需额外安装。

## 构建

```powershell
.\build.ps1 -Config Release
```

产物输出至 `build/Release/`：

| 文件 | 说明 |
|------|------|
| `blp_viewer.exe` | 主程序 |
| `blp_thumbnail.dll` | Shell 扩展，按需单独注册 |
| `blp_codec_selftest.exe` | 编解码回归测试二进制 |

## 测试

```powershell
.\build\Release\blp_codec_selftest.exe test-data\blp test-data\png
```

对 `test-data/` 下的样本执行解码回归（与参考 PNG 比较 PSNR）及编码往返验证。

## 安装包

需要 [Inno Setup 6](https://jrsoftware.org/isinfo.php)。

```powershell
.\installer\build_installer.ps1 -Config Release
```

输出：`installer/dist/BLP_Viewer_Setup_x64.exe`

## 许可证

MIT
