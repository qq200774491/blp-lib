#pragma once

#include "imgui.h"

void ApplyImGuiStyle(float dpiScale);
void LoadFonts(float dpiScale);

ImVec4 UiColorSuccess();
ImVec4 UiColorWarning();
ImVec4 UiColorError();

void PushPrimaryButtonStyle();
void PushSecondaryButtonStyle();
void PushDangerButtonStyle();
void PopButtonStyle();
