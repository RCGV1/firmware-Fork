#pragma once
void wadaCommsWarning(const char *message);
#define ILOG_WARN(message, ...) wadaCommsWarning(message)
