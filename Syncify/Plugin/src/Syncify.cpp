#include "pch.h"
#include "Syncify.h"

#include "bakkesmod/wrappers/GuiManagerWrapper.h"
#include "rendering/impl/SimpleOverlay.h"
#include "font_ranges.h"

#include <fstream>
#include <chrono>

BAKKESMOD_PLUGIN(Syncify, "Syncify", plugin_version, PLUGINTYPE_FREEPLAY)

// Global CVar manager instance
std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

void Syncify::onLoad()
{
    _globalCvarManager = cvarManager;
    m_SpotifyApi = std::make_shared<SpotifyAPI>();

    LoadData();

    // Initialize overlay instances
    m_OverlayInstances.emplace(Simple, std::make_unique<SimpleOverlay>());
    m_OverlayInstances.emplace(Compact, std::make_unique<CompactOverlay>());

    // Set current display mode
    auto it = m_OverlayInstances.find(Settings::CurrentDisplayMode);
    m_CurrentDisplayMode = (it != m_OverlayInstances.end()) ? 
                           it->second.get() : 
                           m_OverlayInstances.at(Simple).get(); // fallback

    // Refresh Spotify access token (async)
    m_SpotifyApi->RefreshAccessToken(nullptr);

#ifdef SYNCIFY_STATUSIMPL
    m_Status = std::make_shared<StatusImpl>(gameWrapper, m_SpotifyApi);
    m_Status->ApplyStatus();
#endif

    // Load fonts
    auto gui = gameWrapper->GetGUIManager();
    
    if (auto [codeLarge, fontLarge] = gui.LoadFont("FontLarge", "../../font.ttf", 18, nullptr, ranges); 
        codeLarge == 2)
    {
        Font::FontLarge = fontLarge;
    }
    else
    {
        Log::Error("Failed to load FontLarge");
    }

    if (auto [codeRegular, fontRegular] = gui.LoadFont("FontRegular", "../../font.ttf", 14, nullptr, ranges); 
        codeRegular == 2)
    {
        Font::FontRegular = fontRegular;
    }
    else
    {
        Log::Error("Failed to load FontRegular");
    }

    // Register canvas renderer
    gameWrapper->RegisterDrawable([this](CanvasWrapper canvas) {
        RenderCanvas(canvas);
    });
}

void Syncify::onUnload()
{
    SaveData();
    m_SpotifyApi->ForceServerClose();
}

bool Syncify::IsInGame() const
{
    return gameWrapper->IsInGame() || gameWrapper->IsInOnlineGame() || gameWrapper->IsInFreeplay();
}

bool Syncify::ShouldShowControls() const
{
    return gameWrapper->IsCursorVisible() == 2;
}

bool Syncify::IsNotPlaying() const
{
    return *m_SpotifyApi->GetTitle() == "Not Playing" && *m_SpotifyApi->GetArtist() == "Not Playing";
}

void Syncify::RenderSettings()
{
    if (!m_SpotifyApi->IsAuthenticated())
    {
        RenderAuthenticationUI();
        return;
    }
    
    RenderSettingsUI();
}

void Syncify::RenderAuthenticationUI()
{
    float titleWidth = ImGui::GetCurrentWindow()->Size.x / 2 - ImGui::CalcTextSize("Authentication").x / 2;

    ImGui::SetCursorPosX(titleWidth);
    ImGui::Text("Authentication");

    ImGui::Separator();

    ImGui::InputText("Client Id", m_SpotifyApi->GetClientId());
    ImGui::InputText("Client Secret", m_SpotifyApi->GetClientSecret(), ImGuiInputTextFlags_Password);

    if (ImGui::Button("Authenticate"))
    {
        m_SpotifyApi->Authenticate();
        SaveData();
    }
}

void Syncify::RenderSettingsUI()
{
    float settingsWidth = ImGui::GetCurrentWindow()->Size.x / 2 - ImGui::CalcTextSize("Settings").x / 2;

    ImGui::SetCursorPosX(settingsWidth);
    ImGui::Text("Settings");

    ImGui::Separator();

    if (ImGui::Checkbox("Show Overlay", &Settings::ShowOverlay))
    {
        gameWrapper->Execute([this](GameWrapper* gw) {
            const std::string menuCommand = Settings::ShowOverlay ? 
                "openmenu " + GetMenuName() : 
                "closemenu " + GetMenuName();
            cvarManager->executeCommand(menuCommand);
        });
    }

    ImGui::Checkbox("Hide When 'Not Playing'", &Settings::HideWhenNotPlaying);

    ImGui::Checkbox("Display Custom Status", m_SpotifyApi->UseCustomStatus());
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored({ 1.0f, 0.0f, 0.0f, 1.f }, "This feature is experimental and can cause game crashes.");
        ImGui::EndTooltip();
    }

    ImGui::SliderFloat("Overlay Width", &Settings::SizeX, 175.f, 400.f);
    ImGui::SliderFloat("Overlay Height", &Settings::SizeY, 70.f, 125.f);
    ImGui::SliderInt("Overlay Opacity", &Settings::Opacity, 0, 255);

    ImGui::Separator();

    float modeTitleWidth = ImGui::GetCurrentWindow()->Size.x / 2 - ImGui::CalcTextSize("Style").x / 2;

    ImGui::SetCursorPosX(modeTitleWidth);
    ImGui::Text("Style");

    ImGui::Separator();

    if (ImGui::BeginCombo("Type", GetDisplayModeName(Settings::CurrentDisplayMode)))
    {
        for (uint8_t modeIndex = 0; modeIndex < m_OverlayInstances.size(); ++modeIndex)
        {
            if (modeIndex == Extended) // Don't display extended since it doesn't render anything yet
                continue;

            bool isSelected = modeIndex == Settings::CurrentDisplayMode;

            if (ImGui::Selectable(GetDisplayModeName(modeIndex), isSelected))
            {
                Settings::CurrentDisplayMode = modeIndex;
                m_CurrentDisplayMode = m_OverlayInstances.at(modeIndex).get();
            }
        }

        ImGui::EndCombo();
    }

    if (Settings::CurrentDisplayMode != Simple)
    {
        ImGui::Separator();
        ImGui::SliderFloat("Background Rounding", &Settings::BackgroundRounding, 0.f, 14.f, "%.1f");
        ImGui::Separator();
        ImGui::ColorEdit3("ProgressBar Color", Settings::DurationBarColor, 
                         ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoInputs);
        ImGui::SliderFloat("ProgressBar Rounding", &Settings::DurationBarRounding, 0.f, 10.f, "%.1f");
    }
}

void Syncify::Render()
{
    if (m_SpotifyApi && !m_SpotifyApi->IsAuthenticated())
    {
        RenderOverlayUI();
        return;
    }

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;

    if (Settings::CurrentDisplayMode != Simple)
    {
        windowFlags |= ImGuiWindowFlags_NoBackground;
    }

    if (!ImGui::Begin(menuTitle_.c_str(), &isWindowOpen_, windowFlags))
    {
        ImGui::End();
        return;
    }

    RenderWindow();
    ImGui::End();

    if (!isWindowOpen_)
    {
        _globalCvarManager->executeCommand("togglemenu " + GetMenuName());
    }
}

void Syncify::RenderOverlayUI()
{
    if (!ImGui::Begin(menuTitle_.c_str(), &isWindowOpen_, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
    {
        ImGui::End();
        return;
    }

    ImGui::SetWindowSize({ 155, 50 });
    ImGui::Text("Authentication Required");
    ImGui::Text("F2 -> Plugins -> Syncify");
    ImGui::End();
}

void Syncify::RenderWindow()
{
    if (!Settings::ShowOverlay || !isWindowOpen_ || !gameWrapper || !m_SpotifyApi)
        return;

    // Ensure fonts are loaded
    if (!Font::FontLarge)
    {
        auto gui = gameWrapper->GetGUIManager();
        Font::FontLarge = gui.GetFont("FontLarge");
    }

    if (!Font::FontRegular)
    {
        auto gui = gameWrapper->GetGUIManager();
        Font::FontRegular = gui.GetFont("FontRegular");
    }

    std::string title = *m_SpotifyApi->GetTitle();
    std::string artist = *m_SpotifyApi->GetArtist();

    if (m_CurrentDisplayMode)
    {
        m_CurrentDisplayMode->RenderOverlay(
            title.c_str(), 
            artist.c_str(), 
            m_SpotifyApi->GetProgress(), 
            m_SpotifyApi->GetDuration()
        );
    }
}

void Syncify::RenderCanvas(CanvasWrapper& canvas)
{
    if (!m_SpotifyApi || !m_SpotifyApi->IsAuthenticated())
        return;

    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();

    if (duration >= 1000)
    {
        m_SpotifyApi->FetchMediaData();
        lastTime = now;
    }

    if (!IsInGame())
    {
        if (isWindowOpen_)
            cvarManager->executeCommand("closemenu " + GetMenuName());
        return;
    }

    if (!isWindowOpen_ && Settings::ShowOverlay) // Overlay Is Closed But Should Be Open
    {
        if (IsNotPlaying() && Settings::HideWhenNotPlaying) // If there is no song playing and we should hide when not playing
        {
            return;
        }

        cvarManager->executeCommand("openmenu " + GetMenuName()); // Open the window and render Overlay
        return;
    }

    if (isWindowOpen_ && Settings::HideWhenNotPlaying && IsNotPlaying()) // Window is open but needs closing
    {
        cvarManager->executeCommand("closemenu " + GetMenuName());
    }
}

void Syncify::SaveData()
{
    std::filesystem::path latestSavePath = gameWrapper->GetDataFolder() / "Syncify" / "LatestSave.json";

    if (!exists(latestSavePath.parent_path()))
    {
        create_directories(latestSavePath.parent_path());
    }

    nlohmann::json j;

    // Authentication data
    j["ClientId"] = *m_SpotifyApi->GetClientId();
    j["ClientSecret"] = *m_SpotifyApi->GetClientSecret();
    j["AccessToken"] = *m_SpotifyApi->GetAccessToken();
    j["RefreshToken"] = *m_SpotifyApi->GetRefreshToken();

    // Options
    j["Options"]["ShowOverlay"] = Settings::ShowOverlay;
    j["Options"]["HideWhenNotPlaying"] = Settings::HideWhenNotPlaying;
    j["Options"]["ListeningOLS"] = *m_SpotifyApi->UseCustomStatus();

    j["Options"]["OverlayWidth"] = Settings::SizeX;
    j["Options"]["OverlayHeight"] = Settings::SizeY;
    j["Options"]["OverlayOpacity"] = Settings::Opacity;

    // Style settings
    j["Style"]["DisplayMode"] = Settings::CurrentDisplayMode;
    j["Style"]["Rounding"] = Settings::BackgroundRounding;

    j["Style"]["ProgressBar"]["Color"]["R"] = Settings::DurationBarColor[0];
    j["Style"]["ProgressBar"]["Color"]["G"] = Settings::DurationBarColor[1];
    j["Style"]["ProgressBar"]["Color"]["B"] = Settings::DurationBarColor[2];
    j["Style"]["ProgressBar"]["Rounding"] = Settings::DurationBarRounding;

    std::ofstream outFile(latestSavePath);

    if (outFile.is_open())
    {
        outFile << j.dump(4);
        outFile.close();
        Log::Info("Saved Settings!");
    }
    else
    {
        Log::Error("Failed to open file for saving: {}", latestSavePath.string());
    }
}

void Syncify::LoadData()
{
    std::filesystem::path latestSavePath = gameWrapper->GetDataFolder() / "Syncify" / "LatestSave.json";

    if (!exists(latestSavePath))
        return;

    std::ifstream inFile(latestSavePath);
    
    if (!inFile.is_open())
    {
        Log::Error("Failed to open save file: {}", latestSavePath.string());
        return;
    }

    try
    {
        nlohmann::json data = nlohmann::json::parse(inFile, nullptr, true, true);

        if (data.is_null())
        {
            Log::Error("Failed to parse latest save file.");
            return;
        }

        // Load authentication data
        if (data.contains("ClientId")) m_SpotifyApi->SetClientId(data["ClientId"]);
        if (data.contains("ClientSecret")) m_SpotifyApi->SetClientSecret(data["ClientSecret"]);
        if (data.contains("AccessToken")) m_SpotifyApi->SetAccessToken(data["AccessToken"]);
        if (data.contains("RefreshToken")) m_SpotifyApi->SetRefreshToken(data["RefreshToken"]);

        // Load options
        if (data.contains("Options"))
        {
            const auto& options = data["Options"];
            
            if (options.contains("ShowOverlay")) Settings::ShowOverlay = options["ShowOverlay"];
            if (options.contains("HideWhenNotPlaying")) Settings::HideWhenNotPlaying = options["HideWhenNotPlaying"];
            if (options.contains("ListeningOLS")) m_SpotifyApi->SetCustomStatusEnabled(options["ListeningOLS"]);
            if (options.contains("OverlayWidth")) Settings::SizeX = options["OverlayWidth"];
            if (options.contains("OverlayHeight")) Settings::SizeY = options["OverlayHeight"];
            if (options.contains("OverlayOpacity")) Settings::Opacity = options["OverlayOpacity"];
        }

        // Load style settings
        if (data.contains("Style"))
        {
            const auto& style = data["Style"];
            
            if (style.contains("Rounding")) Settings::BackgroundRounding = style["Rounding"];
            if (style.contains("DisplayMode")) Settings::CurrentDisplayMode = style["DisplayMode"];

            if (style.contains("ProgressBar"))
            {
                const auto& progressBar = style["ProgressBar"];
                
                if (progressBar.contains("Rounding")) Settings::DurationBarRounding = progressBar["Rounding"];
                
                if (progressBar.contains("Color"))
                {
                    const auto& pbColor = progressBar["Color"];
                    
                    if (pbColor.contains("R")) Settings::DurationBarColor[0] = pbColor["R"];
                    if (pbColor.contains("G")) Settings::DurationBarColor[1] = pbColor["G"];
                    if (pbColor.contains("B")) Settings::DurationBarColor[2] = pbColor["B"];
                }
            }
        }
    }
    catch (const nlohmann::json::exception& e)
    {
        Log::Error("JSON parsing error: {}", e.what());
    }
    
    inFile.close();
}

const char* Syncify::GetDisplayModeName(uint8_t mode) const
{
    switch (mode)
    {
    case Simple: return "Simple";
    case Compact: return "Compact";
    case Extended: return "Extended";
    default: return "Unknown";
    }
}