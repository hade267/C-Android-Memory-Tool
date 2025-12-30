#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <dirent.h>

// Overlay Includes
#include "draw.h" // drawBegin, drawEnd, initDraw, shutdown
#include "touch.h" // Init_touch_config, touchEnd
#include "imgui.h"

// MemoryTool Includes
#include "MemoryTool.h"

using namespace std;

// Global instance
MemoryTool tool;
bool g_drawMenu = true;
char g_pkgNameBuffer[128] = "com.garena.game.kgvn"; // Default
char g_searchValBuffer[128] = "";
char g_refineValBuffer[128] = "";
char g_writeValBuffer[128] = "";
int g_selectedType = 0; // DWORD

const char* DATA_TYPE_NAMES[] = { "DWORD", "FLOAT", "DOUBLE", "WORD", "BYTE", "QWORD" };

// Helper to get list of running apps
std::vector<std::string> GetRunningPackages() {
    std::vector<std::string> packages;
    DIR* dir = opendir("/proc");
    if (!dir) return packages;

    struct dirent* ptr;
    while ((ptr = readdir(dir)) != NULL) {
        if (!isdigit(*ptr->d_name)) continue;

        char cmdlinePath[256];
        snprintf(cmdlinePath, sizeof(cmdlinePath), "/proc/%s/cmdline", ptr->d_name);
        
        FILE* fp = fopen(cmdlinePath, "r");
        if (fp) {
            char cmdline[256] = {0};
            fgets(cmdline, sizeof(cmdline), fp);
            fclose(fp);
            
            // Filter for packages (must contain dots and not be system/google bloat)
            bool isSystem = (strstr(cmdline, "com.android.") != NULL) || 
                            (strstr(cmdline, "com.google.") != NULL) ||
                            (strstr(cmdline, "android.process.") != NULL);

            if (!isSystem && strchr(cmdline, '.') && strlen(cmdline) > 5) {
                packages.push_back(std::string(cmdline));
            }
        }
    }
    closedir(dir);
    std::sort(packages.begin(), packages.end());
    packages.erase(std::unique(packages.begin(), packages.end()), packages.end());
    return packages;
}

static std::vector<std::string> g_pkgList;
static int g_selectedPkgIdx = -1;
static bool g_showKeyboard = false;

// Focus Tracking
static char* g_focusedBuffer = nullptr;
static int g_focusedBufferSize = 0;

void CheckSetFocus(char* buf, int size) {
    if (ImGui::IsItemActive()) {
        g_focusedBuffer = buf;
        g_focusedBufferSize = size;
    }
}

void DrawVirtualKeyboard() {
    if (!g_showKeyboard) return;

    ImGui::SetNextWindowSize(ImVec2(700, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Keyboard", &g_showKeyboard, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        
        // Show what we are typing into
        if (g_focusedBuffer) {
            ImGui::Text("Typing into field: %s", g_focusedBuffer);
        } else {
            ImGui::TextDisabled("No input field selected. Tap an input box first.");
        }

        static const char* rows[] = {
            "1234567890-=",
            "qwertyuiop[]\\",
            "asdfghjkl;'",
            "zxcvbnm,./"
        };
        
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
        
        auto HandleChar = [](char c) {
            if (g_focusedBuffer && strlen(g_focusedBuffer) < g_focusedBufferSize - 1) {
                int len = strlen(g_focusedBuffer);
                g_focusedBuffer[len] = c;
                g_focusedBuffer[len+1] = '\0';
            }
        };

        // Number row
        for (int i = 0; i < strlen(rows[0]); i++) {
            char label[2] = { rows[0][i], 0 };
            if (ImGui::Button(label, ImVec2(45, 45))) HandleChar(rows[0][i]);
            ImGui::SameLine();
        }
        if (ImGui::Button("BKSP", ImVec2(90, 45))) {
            if (g_focusedBuffer && strlen(g_focusedBuffer) > 0) {
                g_focusedBuffer[strlen(g_focusedBuffer) - 1] = '\0';
            }
        }
        ImGui::NewLine();

        // QWERTY
        for (int i = 0; i < strlen(rows[1]); i++) {
             char label[2] = { rows[1][i], 0 };
             if (ImGui::Button(label, ImVec2(45, 45))) HandleChar(rows[1][i]);
             ImGui::SameLine();
        }
        ImGui::NewLine();

        // ASDF
        ImGui::Dummy(ImVec2(20, 0)); ImGui::SameLine();
        for (int i = 0; i < strlen(rows[2]); i++) {
             char label[2] = { rows[2][i], 0 };
             if (ImGui::Button(label, ImVec2(45, 45))) HandleChar(rows[2][i]);
             ImGui::SameLine();
        }
        if (ImGui::Button("ENTER", ImVec2(90, 45))) {
            // Optional: Close keyboard or trigger search? For now just hide
            // g_showKeyboard = false;
        }
        ImGui::NewLine();
        
        // ZXCV
        if (ImGui::Button("SHIFT", ImVec2(60, 45))) {} 
        ImGui::SameLine();
        for (int i = 0; i < strlen(rows[3]); i++) {
             char label[2] = { rows[3][i], 0 };
             if (ImGui::Button(label, ImVec2(45, 45))) HandleChar(rows[3][i]);
             ImGui::SameLine();
        }
        ImGui::NewLine();
        
        // SPACE
        ImGui::Dummy(ImVec2(100, 0)); ImGui::SameLine();
        if (ImGui::Button("SPACE", ImVec2(300, 45))) HandleChar(' ');
        
        ImGui::PopStyleVar();
    }
    ImGui::End();
}


void DrawMemoryToolWindow() {
    if (!g_drawMenu) return;

    // Use a slightly larger default window for tabs
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Memory Tool (KPM) v2.0", &g_drawMenu)) {
        
        // Toggle Virtual Keyboard (Global option)
        if (ImGui::Checkbox("Virtual Keyboard", &g_showKeyboard)) {
            // Logic handled in DrawVirtualKeyboard
        }
        DrawVirtualKeyboard();

        if (ImGui::BeginTabBar("MainTabs")) {
            
            // ==========================================================
            // TAB 1: CONNECTION (Process Selection & Status)
            // ==========================================================
            if (ImGui::BeginTabItem("Connection")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0,1,1,1), "--- Process Selection ---");
                
                // Refresh List Button
                if (ImGui::Button(" Refresh Process List ", ImVec2(200, 40))) {
                    g_pkgList = GetRunningPackages();
                }
                
                ImGui::SameLine();
                
                // Status Display Area
                ImGui::BeginGroup();
                    // Driver Status
                    static int frameCounter = 0;
                    static bool lastDriverStatus = false;
                    if (frameCounter++ % 60 == 0) { 
                        if (!lastDriverStatus) lastDriverStatus = tool.kpm.check_driver();
                    }
                    if (lastDriverStatus) ImGui::TextColored(ImVec4(0,1,0,1), "Driver Init: OK");
                    else {
                         int err = tool.kpm.last_error;
                         ImGui::TextColored(ImVec4(1,0,0,1), "Driver Init: FAILED (Err: %d)", err);
                    }
                    
                    // PID Status
                    int pid = tool.getPID(g_pkgNameBuffer);
                    if (pid > 0) ImGui::TextColored(ImVec4(0,1,0,1), "Connected PID: %d", pid);
                    else ImGui::TextColored(ImVec4(1,0.5,0,1), "Not Connected / Process Not Found");
                ImGui::EndGroup();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Process Combo
                static const char* current_item = NULL;
                if (g_selectedPkgIdx >= 0 && g_selectedPkgIdx < g_pkgList.size()) {
                    current_item = g_pkgList[g_selectedPkgIdx].c_str();
                } else {
                    current_item = "Select a package...";
                }

                ImGui::PushItemWidth(-1);
                if (ImGui::BeginCombo("##RunningApps", current_item)) {
                    for (int i = 0; i < g_pkgList.size(); i++) {
                        bool is_selected = (g_selectedPkgIdx == i);
                        if (ImGui::Selectable(g_pkgList[i].c_str(), is_selected)) {
                             g_selectedPkgIdx = i;
                             strncpy(g_pkgNameBuffer, g_pkgList[i].c_str(), sizeof(g_pkgNameBuffer) - 1);
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
                
                ImGui::InputText("Package Name (Manual)", g_pkgNameBuffer, sizeof(g_pkgNameBuffer));
                CheckSetFocus(g_pkgNameBuffer, sizeof(g_pkgNameBuffer));

                ImGui::Spacing();
                if (ImGui::Button("CONNECT TO PROCESS", ImVec2(-1, 50))) {
                    tool.initXMemoryTools(g_pkgNameBuffer, MODE_ROOT);
                }

                ImGui::EndTabItem();
            }

            // ==========================================================
            // TAB 2: SEARCH (Scanner)
            // ==========================================================
            if (ImGui::BeginTabItem("Search")) {
                ImGui::Spacing();
                
                // Config Section
                ImGui::BeginGroup();
                    ImGui::TextColored(ImVec4(0,1,1,1), "Configuration");
                    ImGui::Combo("Data Type", &g_selectedType, DATA_TYPE_NAMES, IM_ARRAYSIZE(DATA_TYPE_NAMES));
                    
                    const char* ranges[] = { "ALL", "B_BAD", "C_ALLOC", "C_BSS", "C_DATA", "C_HEAP", "JAVA_HEAP", "A_ANON", "CODE_SYSTEM", "STACK", "ASHMEM" };
                    if (ImGui::Combo("Memory Range", &tool.m_searchRange, ranges, IM_ARRAYSIZE(ranges))) {
                        tool.SetSearchRange(tool.m_searchRange);
                    }
                    
                    // Safe Mode Toggle
                    if (ImGui::Checkbox("Safe Mode (Slow & Stealth)", &tool.m_safeMode)) {
                        // Toggle logic handled by boolean ref
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slows down scan to prevent CPU spikes/detection.");
                ImGui::EndGroup();
                
                ImGui::Separator();
                ImGui::Spacing();
                
                // Input Section
                ImGui::Text("Value:");
                ImGui::InputText("##SearchVal", g_searchValBuffer, sizeof(g_searchValBuffer));
                CheckSetFocus(g_searchValBuffer, sizeof(g_searchValBuffer));
                
                ImGui::Spacing();
                
                // Action Buttons
                if (ImGui::Button("NEW SCAN", ImVec2(150, 50))) {
                    tool.MemorySearch(g_searchValBuffer, g_selectedType);
                }
                ImGui::SameLine();
                if (ImGui::Button("REFINE (Next)", ImVec2(150, 50))) {
                     tool.MemoryOffset(g_searchValBuffer, 0, g_selectedType); 
                }
                ImGui::SameLine();
                if (ImGui::Button("CLEAR", ImVec2(100, 50))) {
                    tool.ClearResults();
                }
                
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1,1,0,1), "Results found: %d", tool.GetResultCount());
                
                ImGui::EndTabItem();
            }

            // ==========================================================
            // TAB 3: RESULTS & EDIT
            // ==========================================================
            if (ImGui::BeginTabItem("Results")) {
                ImGui::Spacing();
                
                // Batch Edit
                ImGui::BeginGroup();
                    ImGui::InputText("Write Value", g_writeValBuffer, sizeof(g_writeValBuffer));
                    CheckSetFocus(g_writeValBuffer, sizeof(g_writeValBuffer));
                    ImGui::SameLine();
                    if (ImGui::Button("Write All", ImVec2(120, 0))) {
                         tool.MemoryWrite(g_writeValBuffer, 0, g_selectedType);
                    }
                ImGui::EndGroup();

                ImGui::Separator();
                
                // List
                ImGui::BeginChild("ResultsScroll", ImVec2(0, 0), true); // Fill remaining space
                const auto& results = tool.GetResults();
                
                // Display Columns
                ImGui::Columns(4, "ResCols"); 
                ImGui::Text("Address"); ImGui::NextColumn();
                ImGui::Text("Value"); ImGui::NextColumn();
                ImGui::Text("Type/Map"); ImGui::NextColumn();
                ImGui::Text("Action"); ImGui::NextColumn();
                ImGui::Separator();

                int count = 0;
                for (const auto& res : results) {
                    std::string valStr = tool.GetAddressValue(res.addr, res.type);
                    
                    // Col 1: Addr
                    ImGui::Text("0x%lX", res.addr); 
                    ImGui::NextColumn();
                    
                    // Col 2: Value
                    ImGui::TextColored(ImVec4(1,1,0,1), "%s", valStr.c_str());
                    ImGui::NextColumn();
                    
                    // Col 3: Info
                    ImGui::TextDisabled("%s", (res.mapName.empty() ? "?" : res.mapName.c_str()));
                    ImGui::NextColumn();
                    
                    // Col 4: Action
                    if (ImGui::Button(("Frz##" + std::to_string(res.addr)).c_str())) {
                         tool.AddFreezeItem(res.addr, valStr.c_str(), res.type);
                         tool.StartFreeze();
                    }
                    ImGui::NextColumn();
                    
                    if (++count > 200) { // Limit view
                        ImGui::Columns(1);
                        ImGui::TextDisabled("... %d more results hidden ...", (int)results.size() - 200);
                        break;
                    }
                }
                ImGui::Columns(1);
                ImGui::EndChild();
                
                ImGui::EndTabItem();
            }
            
            // ==========================================================
            // TAB 4: FROZEN LIST
            // ==========================================================
            if (ImGui::BeginTabItem("Frozen")) {
                if (ImGui::Button("Stop & Clear All Freeze", ImVec2(-1, 40))) {
                     tool.StopFreeze();
                     tool.ClearFreezeItems();
                }
                
                ImGui::Separator();
                
                ImGui::BeginChild("FreezeScroll");
                const auto& frozen = tool.m_freezeItems;
                for (const auto& item : frozen) {
                     ImGui::Text("0x%lX", item.addr);
                     ImGui::SameLine();
                     ImGui::TextColored(ImVec4(0,1,1,1), "= %s", item.value.c_str());
                     ImGui::SameLine();
                     if (ImGui::SmallButton(("X##" + std::to_string(item.addr)).c_str())) {
                         tool.RemoveFreezeItem(item.addr);
                         if(frozen.empty()) tool.StopFreeze(); // Auto stop if empty
                         break; // Break loop to avoid iterator invalidation
                     }
                }
                ImGui::EndChild();
                
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void DrawFloatingIcon() {
    // Only show when menu is hidden
    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(60, 60)); // Small square
    ImGui::Begin("Icon", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    
    // Simple Button with Logo "KPM"
    if (ImGui::Button("KPM", ImVec2(50, 50))) {
        g_drawMenu = true;
    }
    
    ImGui::End();
}

int main(int argc, char *argv[]) {
    // 1. Initialize Overlay
    if (!initDraw(true)) {
        cout << "Failed to init overlay" << endl;
        return -1;
    }

    // 2. Initialize Touch
    // printf("Calling Init_touch_config()...\n");
    Init_touch_config(); 

    // 3. Main Loop
    // printf("Entering Main Loop...\n");
    int frameCount = 0;
    while (true) { 
        // if (frameCount % 600 == 0) printf("Main Loop Tick (Frame %d)...\n", frameCount);
        frameCount++;

        drawBegin();
        
        if (g_drawMenu) {
            // Menu is visible
            DrawMemoryToolWindow();
        } else {
            // Menu is hidden, show icon
            DrawFloatingIcon();
        }
        
        // Smart Input Blocking:
        // Automatically Grab input (block game) ONLY when interacting with ImGui windows.
        // Otherwise, let input pass through to the game.
        ImGuiIO& io = ImGui::GetIO();
        SetInputGrab(io.WantCaptureMouse);
        
        drawEnd();
        std::this_thread::sleep_for(16ms); // ~60 FPS cap
    }

    // 4. Cleanup
    touchEnd();
    shutdown();

    return 0;
}
