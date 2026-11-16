#include "WadaPlatform.h"
#include "WadaStorage.h"
#include "wada_host.h"
#include <Arduino.h>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>
WadaStorageFS wadaStorage;
namespace Time
{
uint32_t getMillis()
{
    return millis();
}
} // namespace Time
void wadaSpiLock() {}
void wadaSpiUnlock() {}
uint16_t wadaBatteryMillivolts()
{
    return 4100;
}
void wadaInputActivity() {}
uint32_t wadaRtcNow()
{
    return time(nullptr);
}
void wadaSetRtc(uint32_t) {}
bool wadaPhoneConnected()
{
    return false;
}
void wadaPollUiActivity() {}
void wadaInitPacketApi() {}
namespace
{
std::filesystem::path snapshot(const char *kind, unsigned slot)
{
    return std::filesystem::path(wadaHostDataDir()) / (std::string(kind) + std::to_string(slot) + ".bin");
}
size_t read(const char *kind, unsigned slot, uint8_t *out, size_t cap)
{
    if (slot > 1)
        return 0;
    std::ifstream f(snapshot(kind, slot), std::ios::binary | std::ios::ate);
    if (!f)
        return 0;
    auto size = f.tellg();
    if (size <= 0 || size > std::streamoff(cap))
        return 0;
    f.seekg(0);
    f.read(reinterpret_cast<char *>(out), size);
    return f ? size_t(size) : 0;
}
bool write(const char *kind, unsigned slot, const uint8_t *bytes, size_t size)
{
    if (slot > 1 || size > 65536)
        return false;
    auto path = snapshot(kind, slot);
    std::filesystem::create_directories(path.parent_path());
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char *>(bytes), size);
        f.flush();
        if (!f)
            return false;
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec)
        return false;
    std::vector<uint8_t> check(size);
    return read(kind, slot, check.data(), check.size()) == size && std::equal(check.begin(), check.end(), bytes);
}
} // namespace
size_t wadaTelemetryRead(unsigned slot, uint8_t *out, size_t cap)
{
    return read("telemetry-", slot, out, cap);
}
bool wadaTelemetryWrite(unsigned slot, const uint8_t *bytes, size_t size)
{
    return write("telemetry-", slot, bytes, size);
}
size_t wadaNodeStateRead(unsigned slot, uint8_t *out, size_t cap)
{
    return read("node-state-", slot, out, cap);
}
bool wadaNodeStateWrite(unsigned slot, const uint8_t *bytes, size_t size)
{
    return write("node-state-", slot, bytes, size);
}
