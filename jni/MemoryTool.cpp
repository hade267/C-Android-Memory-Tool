#include "MemoryTool.h"
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <thread>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>

using namespace std;

// ==============================================================================================
// Initialization & Utils
// ==============================================================================================

MemoryTool::~MemoryTool() {
    StopFreeze();
}

void MemoryTool::initXMemoryTools(const char* pkgName, const char* mode) {
    m_pkgName = pkgName;

    if (strcmp(mode, MODE_ROOT) == 0) {
        if (getuid() != 0) {
            printf("\033[31;1m[ERROR] This tool requires ROOT access!\033[0m\n");
            exit(1);
        }
    }

    // Try to optimize system for memory operations
    system("echo 0 > /proc/sys/fs/inotify/max_user_watches");

    int pid = getPID(pkgName);
    if (pid <= 0) {
        printf("\033[31;1m[ERROR] Failed to get PID for %s\n\033[0m", pkgName);
        exit(1);
    }

    if (!kpm.init(pid)) {
        printf("\033[31;1m[ERROR] Failed to init KPM driver! Is the kernel module loaded?\033[0m\n");
        exit(1);
    }
    printf("\033[32;1m[OK] KPM Driver Initialized for PID: %d\033[0m\n", pid);
}

int MemoryTool::getPID(const char* pkgName) {
    int pid = -1;
    DIR* dir = opendir("/proc");
    if (!dir) return -1;

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
            
            if (strcmp(cmdline, pkgName) == 0) {
                pid = atoi(ptr->d_name);
                break;
            }
        }
    }
    closedir(dir);
    return pid;
}

void MemoryTool::SetSearchRange(int range) {
    m_searchRange = range;
}

int MemoryTool::SetTextColor(int color) {
    switch (color) {
        case COLOR_SILVERY: printf("\033[30;1m"); break;
        case COLOR_RED:     printf("\033[31;1m"); break;
        case COLOR_GREEN:   printf("\033[32;1m"); break;
        case COLOR_YELLOW:  printf("\033[33;1m"); break;
        case COLOR_DARK_BLUE: printf("\033[34;1m"); break;
        case COLOR_PINK:    printf("\033[35;1m"); break;
        case COLOR_SKY_BLUE: printf("\033[36;1m"); break;
        case COLOR_WHITE:   printf("\033[37;1m"); break;
        default: printf("\033[37;1m"); break;
    }
    return 0;
}

// ==============================================================================================
// Memory Map Reading
// ==============================================================================================

std::vector<MemoryMap> MemoryTool::readmaps(int type) {
    std::vector<MemoryMap> maps;
    int pid = getPID(m_pkgName.c_str());
    if (pid <= 0) {
        printf("[Error] readmaps: PID not found for package '%s'. Did you connect?\n", m_pkgName.c_str());
        return maps;
    }

    char mapsPath[64];
    snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", pid);

    std::ifstream file(mapsPath);
    if (!file.is_open()) {
        perror("Failed to open maps");
        printf("[Error] Cannot open %s. Check Root permissions.\n", mapsPath);
        return maps;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Parse line: 7ff3b23000-7ff3b24000 rw-p 00000000 00:00 0 [anon:libc_malloc]
        
        // Filter for RW
        if (line.find("rw") == std::string::npos) continue;

        ADDRESS start, end;
        char perms[5];
        int offset;
        char device[6]; // major:minor
        int inode;
        char nameBuf[256] = {0};

        // Simplified parsing, assuming standard format
        if (sscanf(line.c_str(), "%lx-%lx %4s %x %5s %d %s", &start, &end, perms, &offset, device, &inode, nameBuf) < 2) {
            continue;
        }

        std::string name = nameBuf;

        // Dangerous / Useless Ranges Blacklist
        // Scanning these often causes detection or crashes
        bool isDangerous = false;
        if (name.find("kgsl") != std::string::npos ||       // GPU
            name.find("mali") != std::string::npos ||       // GPU
            name.find("fonts") != std::string::npos ||      // Fonts
            name.find("app_process") != std::string::npos || // Zygote/App
            name.find("system/lib") != std::string::npos || // Sys Libs
            name.find("system/framework") != std::string::npos || 
            name.find("[guard]") != std::string::npos ||    // Guard pages
            name.find(".dex") != std::string::npos ||       // Code
            name.find(".oat") != std::string::npos);        // Code

        if (m_safeMode && isDangerous) {
             continue; // Skip dangerous ranges in safe mode
        }

        bool keep = false;

        switch (type) {
            case ALL: keep = !isDangerous; break; // In ALL mode, skip dangerous by default now
            case B_BAD: keep = (name.find("kgsl-3d0") != std::string::npos); break;
            case C_ALLOC: keep = (name.find("[anon:libc_malloc]") != std::string::npos); break;
            case C_BSS: keep = (name.find("[anon:.bss]") != std::string::npos); break;
            case C_DATA: keep = (name.find("/data/app/") != std::string::npos); break; // Rough approx
            case C_HEAP: keep = (name.find("[heap]") != std::string::npos); break;
            case JAVA_HEAP: keep = (name.find("/dev/ashmem/") != std::string::npos && name.find("dalvik") == std::string::npos); break; // Approx
            case A_ANONYMOUS: keep = (name.empty()); break;
            case CODE_SYSTEM: keep = (name.find("/system") != std::string::npos); break;
            case STACK: keep = (name.find("[stack]") != std::string::npos); break;
            case ASHMEM: keep = (name.find("/dev/ashmem/") != std::string::npos); break;
            default: keep = true; break;
        }

        if (keep) {
            maps.push_back({start, end, name});
        }
    }
    return maps;
}


// ==============================================================================================
// Search Implementations
// ==============================================================================================

// Template Search Logic for optimized bulk reading
template <typename T>
void MemoryTool::SearchValue(T value, const std::vector<MemoryMap>& maps, int type) {
    m_results.clear();
    
    // Buffer for bulk reading. KPM driver supports up to 1MB, we use READ_CHUNK_SIZE (128KB default)
    std::vector<uint8_t> buffer(READ_CHUNK_SIZE); 
    
    for (const auto& map : maps) {
        ADDRESS curr = map.startAddr;
        while (curr < map.endAddr) {
            size_t readSize = std::min((size_t)(map.endAddr - curr), READ_CHUNK_SIZE);
            if (readSize < sizeof(T)) break;

            size_t bytesRead = kpm.read_raw(curr, buffer.data(), readSize);
            if (bytesRead > 0) {
                // Scan buffer
                size_t count = bytesRead / sizeof(T); // Only align to Type size? No, usually 4-byte aligned or 1-byte?
                // Standard game cheat tools usually scan aligned to the type size or 4 bytes.
                // To be safe and thorough, let's scan every 1 byte? No, too slow.
                // Standard alignment: 
                // DWORD/FLOAT -> 4 bytes aligned
                // WORD -> 2 bytes
                // BYTE -> 1 byte
                // QWORD/DOUBLE -> 4 or 8 bytes. Let's assume 4 for ARM64 compatibility or 8?
                
                size_t alignment = sizeof(T);
                if (alignment > 4) alignment = 4; // Allow 4-byte alignment for 64-bit values too (common in Android mapping)

                for (size_t i = 0; i <= bytesRead - sizeof(T); i += alignment) {
                    T* valPtr = (T*)(buffer.data() + i);
                    if (*valPtr == value) {
                        m_results.push_back({curr + i, type, map.name});
                    }
                }
            }
             curr += readSize;

            if (m_safeMode) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

template <typename T>
void MemoryTool::SearchRange(T from_val, T to_val, const std::vector<MemoryMap>& maps, int type) {
    m_results.clear();
    std::vector<uint8_t> buffer(READ_CHUNK_SIZE); 
    
    if (from_val > to_val) std::swap(from_val, to_val);

    for (const auto& map : maps) {
        ADDRESS curr = map.startAddr;
        while (curr < map.endAddr) {
            size_t readSize = std::min((size_t)(map.endAddr - curr), READ_CHUNK_SIZE);
            if (readSize < sizeof(T)) break;

            size_t bytesRead = kpm.read_raw(curr, buffer.data(), readSize);
            if (bytesRead > 0) {
                size_t alignment = sizeof(T);
                if (alignment > 4) alignment = 4;

                for (size_t i = 0; i <= bytesRead - sizeof(T); i += alignment) {
                    T* valPtr = (T*)(buffer.data() + i);
                    if (*valPtr >= from_val && *valPtr <= to_val) {
                        m_results.push_back({curr + i, type, map.name});
                    }
                }
            }
             curr += readSize;

            if (m_safeMode) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void MemoryTool::MemorySearch(const char* value, int type) {
    auto maps = readmaps(m_searchRange);
    printf("Scanning %zu memory regions...\n", maps.size());

    switch (type) {
        case TYPE_DWORD: SearchValue<DWORD>(atoi(value), maps, type); break;
        case TYPE_FLOAT: SearchValue<FLOAT>(strtof(value, nullptr), maps, type); break;
        case TYPE_DOUBLE: SearchValue<DOUBLE>(strtod(value, nullptr), maps, type); break;
        case TYPE_WORD: SearchValue<WORD>((WORD)atoi(value), maps, type); break;
        case TYPE_BYTE: SearchValue<BYTE>((BYTE)atoi(value), maps, type); break;
        case TYPE_QWORD: SearchValue<QWORD>(atoll(value), maps, type); break;
        default: printf("Unknown Type\n"); break;
    }
}

void MemoryTool::RangeMemorySearch(const char* from_value, const char* to_value, int type) {
    auto maps = readmaps(m_searchRange);
    printf("Scanning %zu memory regions (Range)...\n", maps.size());

     switch (type) {
        case TYPE_DWORD: SearchRange<DWORD>(atoi(from_value), atoi(to_value), maps, type); break;
        case TYPE_FLOAT: SearchRange<FLOAT>(strtof(from_value, nullptr), strtof(to_value, nullptr), maps, type); break;
        case TYPE_DOUBLE: SearchRange<DOUBLE>(strtod(from_value, nullptr), strtod(to_value, nullptr), maps, type); break;
        case TYPE_WORD: SearchRange<WORD>((WORD)atoi(from_value), (WORD)atoi(to_value), maps, type); break;
        case TYPE_BYTE: SearchRange<BYTE>((BYTE)atoi(from_value), (BYTE)atoi(to_value), maps, type); break;
        case TYPE_QWORD: SearchRange<QWORD>(atoll(from_value), atoll(to_value), maps, type); break;
        default: printf("Unknown Type\n"); break;
    }
}

// ==============================================================================================
// Offset / Refine
// ==============================================================================================

void MemoryTool::MemoryOffset(const char* value, long int offset, int type) {
    if (m_results.empty()) {
        printf("No results to refine.\n");
        return;
    }

    std::vector<MemoryResult> newResults;
    newResults.reserve(m_results.size()); // Expect reduction, but reserve to avoid reallocs

    // For refinement, we usually check singular addresses, so bulk read is harder per result.
    // However, if results are clustered, we could bulk read.
    // For simplicity and correctness with sparse results, we read individual values.
    // Optimization: If performance is issue, sort results and batch read.

    for (const auto& res : m_results) {
        ADDRESS targetAddr = res.addr + offset;
        bool match = false;
        
        switch (type) {
            case TYPE_DWORD: {
                DWORD val = 0;
                if (kpm.read_raw(targetAddr, &val, sizeof(val)) == sizeof(val)) {
                    if (val == atoi(value)) match = true;
                }
                break;
            }
            case TYPE_FLOAT: {
                FLOAT val = 0;
                if (kpm.read_raw(targetAddr, &val, sizeof(val)) == sizeof(val)) {
                    // Float comparison needs epsilon? For equality search usually exact match or very close.
                    // Implementation uses exact match for now as per original.
                     if (val == strtof(value, nullptr)) match = true;
                }
                break;
            }
             case TYPE_DOUBLE: {
                DOUBLE val = 0;
                if (kpm.read_raw(targetAddr, &val, sizeof(val)) == sizeof(val)) {
                     if (val == strtod(value, nullptr)) match = true;
                }
                break;
            }
            // ... implement others similarly
            case TYPE_WORD: {
                WORD val = 0;
                if (kpm.read_raw(targetAddr, &val, sizeof(val)) == sizeof(val)) match = (val == atoi(value));
                break;
            }
             case TYPE_BYTE: {
                BYTE val = 0;
                if (kpm.read_raw(targetAddr, &val, sizeof(val)) == sizeof(val)) match = (val == atoi(value));
                break;
            }
             case TYPE_QWORD: {
                QWORD val = 0;
                if (kpm.read_raw(targetAddr, &val, sizeof(val)) == sizeof(val)) match = (val == atoll(value));
                break;
            }
        }

        if (match) {
            // Keep original address or update to new offset address? 
            // Usually Offset/Refine filters the ORIGINAL list based on condition at offset.
            // Original code: pNew->addr = pTemp->addr; (Keeps original address)
            newResults.push_back(res);
        }
    }
    m_results = std::move(newResults);
}

void MemoryTool::MemoryWrite(const char* value, long int offset, int type) {
    for (const auto& res : m_results) {
        ADDRESS targetAddr = res.addr + offset;
        WriteAddress(targetAddr, value, type);
    }
}

int MemoryTool::WriteAddress(ADDRESS addr, const char* value, int type) {
    switch (type) {
        case TYPE_DWORD: { DWORD val = atoi(value); return kpm.write(addr, val); }
        case TYPE_FLOAT: { FLOAT val = strtof(value, nullptr); return kpm.write(addr, val); }
        case TYPE_DOUBLE: { DOUBLE val = strtod(value, nullptr); return kpm.write(addr, val); }
        case TYPE_WORD: { WORD val = (WORD)atoi(value); return kpm.write(addr, val); }
        case TYPE_BYTE: { BYTE val = (BYTE)atoi(value); return kpm.write(addr, val); }
        case TYPE_QWORD: { QWORD val = atoll(value); return kpm.write(addr, val); }
    }
    return 0;
}

// ==============================================================================================
// Helper / Printing
// ==============================================================================================

std::string MemoryTool::GetAddressValue(ADDRESS addr, int type) {
    char buffer[64];
    switch (type) {
        case TYPE_DWORD: snprintf(buffer, 64, "%d", kpm.read<DWORD>(addr)); break;
        case TYPE_FLOAT: snprintf(buffer, 64, "%f", kpm.read<FLOAT>(addr)); break;
        case TYPE_DOUBLE: snprintf(buffer, 64, "%lf", kpm.read<DOUBLE>(addr)); break;
        case TYPE_WORD: snprintf(buffer, 64, "%d", kpm.read<WORD>(addr)); break;
        case TYPE_BYTE: snprintf(buffer, 64, "%d", kpm.read<BYTE>(addr)); break;
        case TYPE_QWORD: snprintf(buffer, 64, "%lld", kpm.read<QWORD>(addr)); break;
        default: return "?";
    }
    return std::string(buffer);
}

void MemoryTool::PrintResults() {
    int count = 0;
    for (const auto& res : m_results) {
        std::string valStr = GetAddressValue(res.addr, res.type);
        const char* typeStr = "UNKNOWN";
        switch(res.type) {
            case TYPE_DWORD: typeStr = "DWORD"; break;
            case TYPE_FLOAT: typeStr = "FLOAT"; break;
            case TYPE_DOUBLE: typeStr = "DOUBLE"; break;
            case TYPE_WORD: typeStr = "WORD"; break;
            case TYPE_BYTE: typeStr = "BYTE"; break;
            case TYPE_QWORD: typeStr = "QWORD"; break;
        }

        printf("\e[37;1mAddr:\e[32;1m0x%lX  \e[37;1mType:\e[36;1m%s  \e[37;1mValue:\e[35;1m%s\n", res.addr, typeStr, valStr.c_str());
        
        if (++count >= 100) {
            printf("... (Showing first 100 of %zu results)\n", m_results.size());
            break;
        }
    }
}

// ==============================================================================================
// Freeze Logic
// ==============================================================================================

void MemoryTool::AddFreezeItem(ADDRESS addr, const char* value, int type, long int offset) {
    FreezeItem item;
    item.addr = addr + offset;
    item.type = type;
    item.value = value;
    m_freezeItems.push_back(item);
}

void MemoryTool::AddFreezeItem_All(const char* value, int type, long int offset) {
    for (const auto& res : m_results) {
        AddFreezeItem(res.addr, value, type, offset);
    }
}

void MemoryTool::RemoveFreezeItem(ADDRESS addr) {
    auto it = std::remove_if(m_freezeItems.begin(), m_freezeItems.end(), 
        [addr](const FreezeItem& item){ return item.addr == addr; });
    m_freezeItems.erase(it, m_freezeItems.end());
}

void MemoryTool::ClearFreezeItems() {
    m_freezeItems.clear();
}

void MemoryTool::PrintFreezeItems() {
    for (const auto& item : m_freezeItems) {
        printf("FreezeAddr:0x%lX  Type:%d  Value:%s\n", item.addr, item.type, item.value.c_str());
    }
}

void MemoryTool::StartFreeze() {
    if (m_isFreezing) return;
    m_isFreezing = true;
    m_freezeThread = std::thread(&MemoryTool::FreezeThreadLoop, this);
    m_freezeThread.detach(); // Detach to let it run in bg
}

void MemoryTool::StopFreeze() {
    m_isFreezing = false;
    // Thread will exit on next loop check
}

void MemoryTool::FreezeThreadLoop() {
    while (m_isFreezing) {
        // Check if process still alive?
        int pid = getPID(m_pkgName.c_str());
        if (pid <= 0) break;

        for (const auto& item : m_freezeItems) {
            WriteAddress(item.addr, item.value.c_str(), item.type);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(m_freezeDelay));
    }
}

// ==============================================================================================
// Legacy/Misc Support (stubs or implementations)
// ==============================================================================================

int MemoryTool::killprocess(const char* pkgName) {
    int pid = getPID(pkgName);
    if (pid > 0) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "kill -9 %d", pid);
        system(cmd);
        return 0;
    }
    return -1;
}

int MemoryTool::rebootsystem() {
    return system("reboot");
}
