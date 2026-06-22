# Changelog

## [1.5.0] - 2026-06-22

### Added

- **Layer composite & border batch operations** — apply image overlays (with anchor, margin, scale, opacity) or solid borders to all files in the resource list at once; supports canvas-expand or inset border modes
- **Preview toolbar: save with size / alignment** — save the previewed image at a custom resolution with alignment options (stretch, center on transparent background, etc.)
- **Built-in C++ BLP1 codec** — replaced the external Rust `blp_lib.dll` dependency; codec is now statically compiled into the application, no side DLLs required
- **Installer improvements** — bilingual (Chinese / English) setup wizard, desktop shortcut option, full uninstall cleanup

### Fixed

- Single-image save now supports format conversion (e.g. BLP → PNG) and replaces the source file when saving back to the same path
- War3 BLP JPEG encoding now correctly uses CMYK colour space, matching the original War3 encoder behaviour
- BLP1 JPEG decoding now handles 3-component (RGB) and grayscale JPEG payloads in addition to the standard 4-component CMYK stream

---

## [1.4.0]

- Batch conversion between BLP / PNG / JPG / BMP / TGA
- Windows Explorer thumbnail integration via `blp_thumbnail.dll` shell extension
- File association manager
- Mipmap level selection in preview
- Alpha channel overlay display

---

# 更新日志

## [1.5.0] - 2026-06-22

### 新增

- **图层合成 / 边框批量处理** — 对资源列表中的全部图像批量执行图层叠加（支持锚点、边距、缩放、不透明度）或加边框（扩展画布 / 内描边）
- **预览工具栏：保存尺寸 / 对齐** — 按指定分辨率和对齐方式（拉伸、居中透明背景等）保存当前预览图
- **内置 C++ BLP1 编解码器** — 移除对外部 Rust `blp_lib.dll` 的依赖，编解码器静态编译进主程序，无需额外 DLL
- **安装包完善** — 双语（中文 / 英文）安装界面、桌面快捷方式选项、完整卸载清理

### 修复

- 单图保存支持纯格式转换（如 BLP → PNG），保存回原路径时正确替换原文件
- War3 BLP JPEG 编码现在正确使用 CMYK 色彩空间，与原版 War3 编码器行为一致
- BLP1 JPEG 解码新增对 3 通道（RGB）和灰度 JPEG 负载的支持

---

## [1.4.0]

- BLP / PNG / JPG / BMP / TGA 批量格式互转
- 通过 `blp_thumbnail.dll` Shell 扩展在资源管理器显示缩略图
- 文件关联管理器
- 预览支持 Mipmap 层级切换
- Alpha 通道叠加显示
