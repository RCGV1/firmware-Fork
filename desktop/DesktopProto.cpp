// Device-only database serialization is not a simulator storage API.
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/deviceonly_legacy.pb.h"
#include <cstdio>
bool meshtastic_NodeDatabase_callback(pb_istream_t *, pb_ostream_t *, const pb_field_t *)
{
    return false;
}
bool meshtastic_NodeDatabase_Legacy_callback(pb_istream_t *, pb_ostream_t *, const pb_field_t *)
{
    return false;
}
void wadaCommsWarning(const char *message)
{
    fprintf(stderr, "[sim PacketComms] %s\n", message);
}
