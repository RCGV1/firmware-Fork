#include "WadaMeshtasticBackend.h"
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace
{
char *token(char *&s)
{
    while (*s == ' ' || *s == '\t')
        ++s;
    if (!*s)
        return nullptr;
    char *result = s;
    while (*s && *s != ' ' && *s != '\t')
        ++s;
    if (*s)
        *s++ = 0;
    return result;
}
bool eq(const char *a, const char *b)
{
    return a && strcasecmp(a, b) == 0;
}
} // namespace
bool WadaMeshtasticBackend::runNodeCli(const char *input)
{
    char buffer[256];
    if (!input || strlen(input) >= sizeof(buffer)) {
        events->beTerminalLine("Command is too long");
        return true;
    }
    snprintf(buffer, sizeof(buffer), "%s", input);
    char *cursor = buffer;
    char *command = token(cursor);
    if (!command)
        return true;
    auto line = [&](const char *s) { events->beTerminalLine(s); };
    auto queued = [&](bool success) {
        line(success ? "Queued through Meshtastic; waiting for firmware confirmation"
                     : "Could not queue request; check synchronization and queue capacity");
    };
    if (eq(command, "help") || eq(command, "?")) {
        line("Meshtastic node terminal");
        line("info | nodes [offset] | channels | lora | device | position");
        line("lora region <US|EU_868|...> | lora preset <LONG_FAST|...>");
        line("lora hop <0-7> | lora power <0-30> | lora tx <on|off> | lora slot <0-65535>");
        line("lora cr <default|5-8> | userinfo <!nodeid>");
        line("nodeinfo [onehop|request] | nodeinfo interval <seconds>");
        line("position interval <seconds> | name <long name> | sync");
        line("reboot | shutdown. Chat: to <name>, send <text>, public <text>, exit.");
        return true;
    }
    if (eq(command, "info") || eq(command, "status") || eq(command, "ver")) {
        char text[180];
        snprintf(text, sizeof(text), "Meshtastic !%08x | %s | %u nodes | API RX %u TX %u", self,
                 configured ? "ready" : "synchronizing", unsigned(nodeCount), received, transmitted);
        line(text);
        return true;
    }
    if (eq(command, "nodes") || eq(command, "list") || eq(command, "contacts")) {
        unsigned start = 0;
        char *arg = token(cursor);
        if (arg && !wada::unsignedValue(arg, 255, start)) {
            line("Usage: nodes [offset]");
            return true;
        }
        for (size_t i = start; i < nodeCount && i < start + 32; ++i) {
            const auto &n = nodes[i];
            char text[160];
            snprintf(text, sizeof(text), "!%08x  %s%s | hops %u", n.num, n.user.long_name, n.is_favorite ? " [favorite]" : "",
                     n.hops_away);
            line(text);
        }
        if (nodeCount > start + 32) {
            char text[80];
            snprintf(text, sizeof(text), "More nodes: nodes %u", start + 32);
            line(text);
        }
        return true;
    }
    if (eq(command, "channels")) {
        for (unsigned i = 0; i < 8; ++i) {
            const auto &c = channels[i];
            if (!c.role)
                continue;
            char text[140];
            snprintf(text, sizeof(text), "[%u] %s | %s | %u-byte PSK | MQTT up:%s down:%s", i,
                     c.settings.name[0] ? c.settings.name : wada::presetName(lora.modem_preset),
                     c.role == meshtastic_Channel_Role_PRIMARY ? "primary" : "secondary", unsigned(c.settings.psk.size),
                     c.settings.uplink_enabled ? "on" : "off", c.settings.downlink_enabled ? "on" : "off");
            line(text);
        }
        return true;
    }
    if (eq(command, "sync")) {
        resync();
        line("Requested a fresh Meshtastic configuration snapshot");
        return true;
    }
    if (eq(command, "lora") || eq(command, "get")) {
        char *field = token(cursor);
        if (!field) {
            char text[200];
            snprintf(text, sizeof(text), "Region %s | %s | hops %u | TX %s | power %d (0=auto) | frequency slot %u",
                     wada::regionName(lora.region), wada::presetName(lora.modem_preset), unsigned(lora.hop_limit),
                     lora.tx_enabled ? "on" : "off", int(lora.tx_power), unsigned(lora.channel_num));
            line(text);
            return true;
        }
        char *value = token(cursor);
        unsigned number = 0;
        auto changed = lora;
        bool valid = value && !*cursor;
        if (eq(field, "region")) {
            int n = wada::namedRegion(value);
            valid = valid && n >= 0;
            if (valid) {
                changed.region = static_cast<meshtastic_Config_LoRaConfig_RegionCode>(n);
                if (changed.use_preset)
                    changed.modem_preset = presetForRegionChange(lora.region, changed.region, changed.modem_preset);
                if (changed.use_preset)
                    changed.coding_rate = wada::normalizedCodingRate(changed.modem_preset, changed.coding_rate);
            }
        } else if (eq(field, "preset")) {
            int n = wada::namedPreset(value);
            valid = valid && n >= 0;
            if (valid) {
                changed.modem_preset = static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(n);
                changed.use_preset = true;
                changed.coding_rate = wada::normalizedCodingRate(changed.modem_preset, changed.coding_rate);
            }
        } else if (eq(field, "hop")) {
            valid = valid && wada::unsignedValue(value, 7, number);
            changed.hop_limit = number;
        } else if (eq(field, "power")) {
            valid = valid && wada::unsignedValue(value, 30, number);
            changed.tx_power = number;
        } else if (eq(field, "slot")) {
            valid = valid && wada::unsignedValue(value, 65535, number);
            changed.channel_num = number;
        } else if (eq(field, "cr")) {
            valid = valid && (eq(value, "default") || (wada::unsignedValue(value, 8, number) && number >= 5));
            changed.coding_rate = eq(value, "default") ? 0 : number;
            if (changed.use_preset)
                changed.coding_rate = wada::normalizedCodingRate(changed.modem_preset, changed.coding_rate);
        } else if (eq(field, "tx")) {
            valid = valid && (eq(value, "on") || eq(value, "off"));
            changed.tx_enabled = eq(value, "on");
        } else
            valid = false;
        if (!valid) {
            line("Invalid LoRa setting. Use help for accepted fields and values.");
            return true;
        }
        if (changed.use_preset && !validPreset(changed.region, changed.modem_preset)) {
            line("That modem preset is unavailable in this region");
            return true;
        }
        meshtastic_Config c = {};
        c.which_payload_variant = meshtastic_Config_lora_tag;
        c.payload_variant.lora = changed;
        queued(setConfig(c));
        return true;
    }
    if (eq(command, "nodeinfo") || eq(command, "advert") || eq(command, "advert.zerohop")) {
        char *action = token(cursor);
        if (eq(action, "interval")) {
            unsigned seconds;
            char *v = token(cursor);
            if (!wada::unsignedValue(v, 604800, seconds) || (seconds != 0 && seconds < 3600)) {
                line("Node-info interval: 0 (firmware default), or 3600..604800 seconds");
                return true;
            }
            meshtastic_Config c = {};
            c.which_payload_variant = meshtastic_Config_device_tag;
            c.payload_variant.device = deviceConfig;
            c.payload_variant.device.node_info_broadcast_secs = seconds;
            queued(setConfig(c));
            return true;
        }
        if (action && !eq(action, "onehop") && !eq(action, "request")) {
            line("Usage: nodeinfo [onehop|request|interval <seconds>]");
            return true;
        }
        queued(sendNodeInfo(eq(action, "onehop") || eq(command, "advert.zerohop"), eq(action, "request")));
        return true;
    }
    if (eq(command, "userinfo")) {
        const char *id = token(cursor);
        unsigned node = 0;
        if (!id || id[0] != '!' || strlen(id) != 9 || *cursor) {
            line("Usage: userinfo !1234abcd");
            return true;
        }
        for (unsigned i = 1; i < 9; ++i) {
            char c = id[i];
            unsigned digit;
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                digit = c - 'A' + 10;
            else {
                line("Node ID must contain eight hexadecimal digits");
                return true;
            }
            node = (node << 4) | digit;
        }
        queued(requestUserInfo(node));
        return true;
    }
    if (eq(command, "device")) {
        char text[120];
        snprintf(text, sizeof(text), "Node-info interval %u s (0=firmware default), device role %u",
                 unsigned(deviceConfig.node_info_broadcast_secs), unsigned(deviceConfig.role));
        line(text);
        return true;
    }
    if (eq(command, "position")) {
        char *action = token(cursor);
        if (!action) {
            char text[130];
            snprintf(text, sizeof(text), "Position interval %u s | fixed:%s | GPS mode %u",
                     unsigned(positionConfig.position_broadcast_secs), positionConfig.fixed_position ? "yes" : "no",
                     unsigned(positionConfig.gps_mode));
            line(text);
            return true;
        }
        unsigned seconds;
        char *v = token(cursor);
        if (!eq(action, "interval") || !wada::unsignedValue(v, 604800, seconds)) {
            line("Usage: position interval <0..604800 seconds; 0 uses firmware default>");
            return true;
        }
        meshtastic_Config c = {};
        c.which_payload_variant = meshtastic_Config_position_tag;
        c.payload_variant.position = positionConfig;
        c.payload_variant.position.position_broadcast_secs = seconds;
        queued(setConfig(c));
        return true;
    }
    if (eq(command, "name")) {
        while (*cursor == ' ' || *cursor == '\t')
            ++cursor;
        queued(setOwner(cursor, owner.short_name));
        return true;
    }
    if (eq(command, "reboot") || eq(command, "shutdown")) {
        queued(power(eq(command, "shutdown")));
        return true;
    }
    line("Unknown Meshtastic command. Type help; use Settings > Channels for channel keys.");
    return true;
}
