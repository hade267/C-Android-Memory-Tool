#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include "kpm_client.hpp"

// Modern Types
using ADDRESS = uint64_t;
using QWORD = int64_t;
using DWORD = int32_t;
using WORD = int16_t;
using BYTE = int8_t;
using FLOAT = float;
using DOUBLE = double;

// Structures tailored for Modern C++
struct MemoryMap {
    ADDRESS startAddr;
    ADDRESS endAddr;
    std::string name; // Optional: useful for debugging
    // No 'next' pointer, use vector
};

struct MemoryResult {
    ADDRESS addr;
    int type; // TYPE_DWORD, etc.
    std::string mapName; // Holds the memory range name (e.g. [anon:libc_malloc])
};

struct FreezeItem {
    ADDRESS addr;
    std::string value; // Stored as string to handle all types simply
    int type;
};

// Enums
enum DataType {
    TYPE_DWORD,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_WORD,
    TYPE_BYTE,
    TYPE_QWORD,
};

enum Range {
    ALL,
    B_BAD,
    C_ALLOC,
    C_BSS,
    C_DATA,
    C_HEAP,
    JAVA_HEAP,
    A_ANONYMOUS,
    CODE_SYSTEM,
    STACK,
    ASHMEM
};

enum Color {
    COLOR_SILVERY,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_YELLOW,
    COLOR_DARK_BLUE,
    COLOR_PINK,
    COLOR_SKY_BLUE,
    COLOR_WHITE
};

#define MODE_ROOT "MODE_ROOT"
#define MODE_NO_ROOT "MODE_NO_ROOT"

class MemoryTool {
public:
    KPMClient kpm;
    
    // Modern Storage
    std::vector<MemoryResult> m_results;
    std::vector<FreezeItem> m_freezeItems;
    
    std::string m_pkgName;
    int m_searchRange = Range::ALL;
    
    // Threading
    std::thread m_freezeThread;
    bool m_isFreezing = false;
    bool m_safeMode = false; // Toggle for slow scanning
    int m_freezeDelay = 30000; // us
    
    // Optimization
    size_t READ_CHUNK_SIZE = 128 * 1024; // 128KB default

    MemoryTool() = default;
    ~MemoryTool();

    // Initialization
    void initXMemoryTools(const char* pkgName, const char* mode);
    int getPID(const char* pkgName);

    // Helpers
    void SetSearchRange(int range);
    int GetResultCount() const { return (int)m_results.size(); }
    const std::vector<MemoryResult>& GetResults() const { return m_results; }
    void ClearResults() { m_results.clear(); }
    void PrintResults();
    int SetTextColor(int color);
    
    // Core Search
    void MemorySearch(const char* value, int type);
    void MemoryOffset(const char* value, long int offset, int type);
    void MemoryWrite(const char* value, long int offset, int type);
    
    // Range Search (Search for value BETWEEN a and b)
    void RangeMemorySearch(const char* from_value, const char* to_value, int type);
    void RangeMemoryOffset(const char* from_value, const char* to_value, long int offset, int type);

    // Direct Write
    int WriteAddress(ADDRESS addr, const char* value, int type);

    // Freeze
    void StartFreeze();
    void StopFreeze();
    void AddFreezeItem(ADDRESS addr, const char* value, int type, long int offset = 0);
    void AddFreezeItem_All(const char* value, int type, long int offset = 0);
    void RemoveFreezeItem(ADDRESS addr);
    void ClearFreezeItems();
    void PrintFreezeItems();
    void SetFreezeDelay(long int delay) { m_freezeDelay = delay; }

    // Misc
    int killprocess(const char* pkgName);
    int rebootsystem();
    
private:
    std::vector<MemoryMap> readmaps(int type);
    
    // Generic Search Implementation (Template usually better but keeping structure similar for porting)
    template <typename T>
    void SearchValue(T value, const std::vector<MemoryMap>& maps, int type);
    
    template <typename T>
    void SearchRange(T from_val, T to_val, const std::vector<MemoryMap>& maps, int type);
    
    // Freeze Loop
    void FreezeThreadLoop();

public:
    std::string GetAddressValue(ADDRESS addr, int type);
    
private:
};



