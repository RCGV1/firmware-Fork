// Verify the UI cannot select a firmware/default partition through legacy calls.
#include "WadaStorage.h"
#include <cassert>
#include <cstdio>
WadaStorageFS wadaStorage;
int main()
{
    assert(SPIFFS.begin(false));
    assert(wadaStorage.partition == "wada" && wadaStorage.mount == "/wada");
    assert(SPIFFS.begin(true, "/littlefs", 10, "spiffs"));
    assert(wadaStorage.partition == "wada" && wadaStorage.mount == "/wada");
    WadaStorageFS fresh;
    assert(fresh.format());
    assert(fresh.partition == "wada" && fresh.formats == 1);
    puts("PASS: WADAMESH mount/format partition isolation");
}
