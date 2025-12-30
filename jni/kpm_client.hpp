#pragma once

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <errno.h>
#include <iostream>
#include <stdint.h>

#define TAG "KPMClient"
#define LOGD(fmt, ...) printf("[%s] [D] " fmt "\n", TAG, ##__VA_ARGS__)
#define LOGE(fmt, ...) printf("[%s] [E] " fmt "\n", TAG, ##__VA_ARGS__)

#define MAGIC_CODE_64 0x6e616d36 // "nam6"
#define MAGIC_CODE_32 0x6e616d33 // "nam3"

// Auto select magic based on pointer size
#if defined(__LP64__) || defined(_LP64)
    #define MAGIC_CODE MAGIC_CODE_64
    #define KPM_IS_64BIT 1
#else
    #define MAGIC_CODE MAGIC_CODE_32
    #define KPM_IS_64BIT 0
#endif

#define CMD_READ 0
#define CMD_WRITE 1

struct kpm_cmd {
    int pid;
    int op;
    uint64_t addr;
    uint64_t len;
    uint64_t data; // User pointer
};

class KPMClient {
private:
    int target_pid = -1;

public:
    int last_error = 0;
    KPMClient() : target_pid(-1), last_error(0) {}

    bool init(int pid) {
        if (pid <= 0) return false;
        target_pid = pid;
        last_error = 0;
        LOGD("Initialized for PID %d (Mode: %s-bit, Magic: 0x%X)", target_pid, (KPM_IS_64BIT ? "64" : "32"), MAGIC_CODE);
        return true;
    }

    int get_last_error() { return last_error; }

    bool check_driver() {
        struct kpm_cmd cmd;
        int dummy_src = 12345;
        int dummy_dst = 0;
        
        cmd.pid = 1; // Use init process (PID 1) which always exists, to avoid "PID not found" on self
        cmd.op = CMD_READ;
        cmd.addr = (uint64_t)&dummy_src;
        cmd.len = sizeof(int); 
        cmd.data = (uint64_t)&dummy_dst;
        
        int ret;
        int retries = 0;
        do {
            ret = syscall(SYS_prctl, MAGIC_CODE, &cmd, 0, 0, 0);
            retries++;
        } while (ret < 0 && errno == EINTR && retries < 100); 
        
        if (ret == 0) return true;
        
        last_error = errno; // Capture error for GUI display
        
        // If errno is 2 (ENOENT), it means driver returned -2 (PID not found). 
        // If errno is 6 (ENXIO), it means driver returned -6 (Read failed).
        // These PROVE the driver is loaded and intercepted the call!
        if (errno == ENOENT || errno == ENXIO) {
            return true;
        }
        
        if (ret < 0 && errno != EINTR) {
             // Only log real errors
             LOGE("Driver check failed! syscall returned: %d, errno: %d (%s)", ret, errno, strerror(errno));
             LOGE("Magic used: 0x%X", MAGIC_CODE);
        }
        return false;
    }

    // Static helper to find PID
    static int find_pid_by_name(const char* name) {
        // ... (omitted) ...
        DIR* dir;
        struct dirent* ent;
        char buf[512];
        long pid;
        char cmdline[512];
        FILE* fp;

        if (!(dir = opendir("/proc"))) return -1;

        while ((ent = readdir(dir)) != NULL) {
            long lpid = strtol(ent->d_name, NULL, 10);
            if (lpid == 0) continue; 

            snprintf(buf, sizeof(buf), "/proc/%s/cmdline", ent->d_name);
            fp = fopen(buf, "r");
            if (fp) {
                if (fgets(cmdline, sizeof(cmdline), fp)) {
                    if (strstr(cmdline, name)) {
                        fclose(fp);
                        closedir(dir);
                        return (int)lpid;
                    }
                }
                fclose(fp);
            }
        }
        closedir(dir);
        return -1;
    }

    uint64_t get_module_base(const char* name) {
        if (target_pid <= 0) return 0;
        
        FILE* fp;
        char mapsPath[64];
        char line[512];
        uint64_t start = 0;
        char perm[5];
        char mapname[256];

        snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", target_pid);
        fp = fopen(mapsPath, "r");
        if (!fp) {
            LOGE("Failed to open maps: %s", mapsPath);
            return 0;
        }

        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, name)) {
                // Parse 64-bit hex logic manually or use long long
                unsigned long long start_ulong; 
                sscanf(line, "%llx-%*x %s %*s %*s %s", &start_ulong, perm, mapname);
                start = (uint64_t)start_ulong;
                break;
            }
        }
        fclose(fp);
        return start;
    }

    size_t read_raw(uint64_t address, void* buffer, size_t size) {
        if (target_pid <= 0) return 0;
        
        struct kpm_cmd cmd;
        cmd.pid = target_pid;
        cmd.op = CMD_READ;
        cmd.addr = (uint64_t)address;
        cmd.len = (uint64_t)size;
        cmd.data = (uint64_t)buffer;

        int ret;
        int retries = 0;
        do {
            ret = syscall(SYS_prctl, MAGIC_CODE, &cmd, 0, 0, 0);
            retries++;
        } while (ret < 0 && errno == EINTR && retries < 100);

        if (ret < 0) {
            last_error = errno;
            LOGE("read_raw failed: ret=%d, errno=%d (%s)", ret, errno, strerror(errno));
            return 0;
        }
        last_error = 0;
        return (size_t)ret;
    }

    size_t write_raw(uint64_t address, const void* buffer, size_t size) {
         if (target_pid <= 0) return 0;

        struct kpm_cmd cmd;
        cmd.pid = target_pid;
        cmd.op = CMD_WRITE;
        cmd.addr = (uint64_t)address;
        cmd.len = (uint64_t)size;
        cmd.data = (uint64_t)buffer; 

        int ret;
        int retries = 0;
        do {
            ret = syscall(SYS_prctl, MAGIC_CODE, &cmd, 0, 0, 0);
            retries++;
        } while (ret < 0 && errno == EINTR && retries < 100);

        if (ret < 0) {
            last_error = errno;
            LOGE("write_raw failed: ret=%d, errno=%d (%s)", ret, errno, strerror(errno));
            return 0;
        }
        last_error = 0;
        return (size_t)ret;
    }

    template <typename T>
    T read(uint64_t address) {
        T data = {};
        read_raw(address, &data, sizeof(T));
        return data;
    }

    template <typename T>
    bool write(uint64_t address, T data) {
        return write_raw(address, &data, sizeof(T)) == sizeof(T);
    }
};
