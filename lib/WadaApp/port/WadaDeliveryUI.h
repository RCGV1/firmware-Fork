#pragma once
#include "WadaDelivery.h"

// The UI history predates the native backend. Keep its byte values stable.
namespace WadaDeliveryUI
{
inline WadaDelivery::State state(uint8_t value)
{
    using S = WadaDelivery::State;
    switch (value) {
    case 1:
        return S::Sent;
    case 2:
        return S::Delivered;
    case 3:
        return S::Failed;
    case 4:
        return S::Pending;
    case 5:
        return S::Mesh;
    case 6:
        return S::Relayed;
    case 7:
        return S::Unconfirmed;
    default:
        return S::Unconfirmed;
    }
}
inline uint8_t stored(WadaDelivery::State state)
{
    using S = WadaDelivery::State;
    switch (state) {
    case S::Pending:
        return 4;
    case S::Sent:
        return 1;
    case S::Delivered:
        return 2;
    case S::Failed:
        return 3;
    case S::Mesh:
        return 5;
    case S::Relayed:
        return 6;
    case S::Unconfirmed:
        return 7;
    }
    return 0;
}
inline uint8_t restored(uint8_t value)
{
    // No pending router state survives a restart. This makes no claim about
    // whether a packet arrived; its explanation explicitly says it may have.
    return value == 1 || value == 4 ? 3 : value;
}
} // namespace WadaDeliveryUI
