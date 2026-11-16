#pragma once
#include <cstdint>

namespace WadaDelivery
{
constexpr uint8_t ChannelChanged = 252;
constexpr uint8_t TimedOut = 253;
constexpr uint8_t Interrupted = 254;
enum class State : uint8_t { Pending, Sent, Delivered, Failed, Mesh, Relayed, Unconfirmed };

inline const char *label(State state, uint8_t error = 0)
{
    switch (state) {
    case State::Pending:
    case State::Sent:
        return "Sending...";
    case State::Delivered:
        return "Delivered to recipient";
    case State::Mesh:
        return "Delivered to mesh";
    case State::Relayed:
        return "Relayed, not confirmed by recipient";
    case State::Unconfirmed:
        return "Sent without delivery confirmation";
    case State::Failed:
        switch (error) {
        case ChannelChanged:
            return "Channel changed before sending";
        case TimedOut:
        case Interrupted:
            return "Delivery not confirmed";
        case 4:
            return "No radio interface";
        case 6:
            return "Channel/key mismatch";
        case 7:
            return "Message is too large to send";
        case 8:
            return "No app response";
        case 9:
            return "Duty cycle limit";
        case 32:
            return "Invalid request";
        case 33:
            return "Not authorized";
        case 34:
            return "Could not send encrypted message";
        case 35:
            return "Recipient needs your key";
        case 36:
            return "Admin session expired";
        case 37:
            return "Admin key not authorized";
        case 38:
            return "Rate limited";
        case 39:
            return "Recipient key unavailable";
        default:
            return "Failed to deliver to mesh";
        }
    }
    return "Sending...";
}

inline bool retryable(State state, uint8_t error = 0)
{
    return state == State::Relayed || (state == State::Failed && error != ChannelChanged && error != 7);
}

inline const char *detail(State state, uint8_t error = 0)
{
    switch (state) {
    case State::Pending:
    case State::Sent:
        return "The radio is sending this message and handling retries. Delivery has not been confirmed yet.";
    case State::Delivered:
        return "The destination node acknowledged receiving this message. This does not confirm that a person has read it.";
    case State::Mesh:
        return "Another node relayed this channel message. This does not confirm that every node received it.";
    case State::Relayed:
        return "Another node relayed this message, but the destination has not confirmed receipt. You can try again.";
    case State::Unconfirmed:
        return "This message was sent without requesting an acknowledgment. Its delivery cannot be confirmed.";
    case State::Failed:
        switch (error) {
        case TimedOut:
            return "No delivery confirmation was received before the status check expired. The message may have arrived. You can "
                   "try again.";
        case ChannelChanged:
            return "The channel settings changed while this message was queued. Open the intended channel and compose the "
                   "message again.";
        case Interrupted:
            return "The device restarted before delivery was confirmed. The message may have arrived. You can try again.";
        case 4:
            return "The radio could not find an available connection. Check the radio settings, then try again.";
        case 6:
            return "No usable channel was found. Check the channel and key on both nodes, then try again.";
        case 7:
            return "Shorten this message before sending it again.";
        case 8:
            return "The destination application did not answer. Check that it supports this request, then try again.";
        case 9:
            return "The radio must wait before transmitting again. Try again after its airtime limit clears.";
        case 32:
            return "The message was rejected. Check its contents and settings before sending again.";
        case 33:
            return "The destination did not permit this message. Check the channel permissions.";
        case 34:
            return "Encryption could not be completed. Exchange node information and check the recipient key, then try again.";
        case 35:
            return "The recipient needs your public key. Broadcast your node information, then try again.";
        case 36:
            return "The administration session expired. Reconnect to the node before trying again.";
        case 37:
            return "Your public key is not authorized to administer this node. Check its administrator keys before trying again.";
        case 38:
            return "The radio is limiting outgoing messages. Wait before trying again.";
        case 39:
            return "Your node needs the recipient's public key. Exchange node information, then try again.";
        default:
            return "No delivery acknowledgment was received. The message may still have arrived. You can try again.";
        }
    }
    return "";
}
} // namespace WadaDelivery
