#pragma once

#include <Plugin/external/bakkes/gui/GuiBase.h>
#include <bakkesmod/plugin/bakkesmodplugin.h>
#include <memory>
#include <unordered_map>

#include "version.h"
#include "spotify/SpotifyAPI.h"
#include "rendering/impl/CompactOverlay.h"

// Status Implementation
#ifdef SYNCIFY_STATUSIMPL
#include "hidden/StatusImpl.h"
#endif

constexpr auto plugin_version = stringify(VERSION_MAJOR) "." stringify(VERSION_MINOR) "." stringify(VERSION_PATCH);

class Syncify final : public BakkesMod::Plugin::BakkesModPlugin,
                      public SettingsWindowBase,
                      public PluginWindowBase
{
public:
	// Overrides
	void onLoad() override;
	void onUnload() override;
	void RenderSettings() override;
	void RenderWindow() override;
	void Render() override;

	void RenderCanvas(CanvasWrapper& canvas);
	void SaveData();
	void LoadData();

private:
	// Helper methods
	bool IsInGame() const;
	bool ShouldShowControls() const;
	bool IsNotPlaying() const;
	const char* GetDisplayModeName(uint8_t displayMode) const;

	// Authentication UI
	void RenderAuthenticationUI();

	// Settings UI
	void RenderSettingsUI();

	// Main overlay UI
	void RenderOverlayUI();

	std::shared_ptr<SpotifyAPI> m_SpotifyApi;
	std::unordered_map<uint8_t, std::unique_ptr<Overlay>> m_OverlayInstances;
	Overlay* m_CurrentDisplayMode = nullptr;

#ifdef SYNCIFY_STATUSIMPL
	std::shared_ptr<StatusImpl> m_Status;
#endif
};