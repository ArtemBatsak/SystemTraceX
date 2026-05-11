#include "disk.h"

#ifdef _WIN32
#include <windows.h>
namespace Disk {

    DiskSystemSnapshot GetSnapshot() {
        DiskSystemSnapshot systemSnap;
        char drives[256];
        DWORD size = GetLogicalDriveStringsA(sizeof(drives), drives);

        char* drive = drives;
        while (*drive) {
            UINT type = GetDriveTypeA(drive);
            // Take into account only fixed and removable drives
            if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
                ULARGE_INTEGER free, total, available;
                if (GetDiskFreeSpaceExA(drive, &available, &total, &free)) {
                    DiskSnapshot snap;
                    snap.name = drive; // Name like "C:\\"
                    snap.totalBytes = total.QuadPart;
                    snap.freeBytes = available.QuadPart;
                    systemSnap.disks.push_back(snap);
                }
            }
            drive += strlen(drive) + 1; // strlen = give us the length of the current drive string, +1 to move past the null terminator
        }
        return systemSnap;
    }
}
#endif

#ifdef __linux__
#include <sys/vfs.h>
#include <fstream>
#include <sstream>
namespace Disk {
    DiskSystemSnapshot GetSnapshot() {
        DiskSystemSnapshot systemSnap;
        std::ifstream mounts("/proc/mounts");
        std::string line;

        while (std::getline(mounts, line)) {
            std::stringstream ss(line);
            std::string device, target, type;
            ss >> device >> target >> type;

            // Фильтруем только реальные физические диски
            if (device.compare(0, 5, "/dev/") == 0) {
                struct statfs s;
                if (statfs(target.c_str(), &s) == 0) {
                    DiskSnapshot snap;
                    snap.name = target; // Точка монтирования, например "/"

                    // f_bsize — размер блока, f_blocks — всего блоков
                    snap.totalBytes = (uint64_t)s.f_blocks * s.f_frsize;
                    snap.freeBytes = (uint64_t)s.f_bavail * s.f_frsize;

                    // Избегаем дубликатов (иногда один диск виден в разных точках)
                    systemSnap.disks.push_back(snap);
                }
            }
        }
        return systemSnap;
    }
}
#endif
