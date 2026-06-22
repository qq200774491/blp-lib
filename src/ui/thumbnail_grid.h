#pragma once

struct AppState;

/**
 * @brief  Render the thumbnail-grid view of all files in the resource list.
 * @param  state  Application state; thumbCache is populated lazily each frame.
 */
void render_thumbnail_grid(AppState& state);
