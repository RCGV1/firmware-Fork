#pragma once
#if !MESHTASTIC_EXCLUDE_STATUS
#include "SinglePortModule.h"
#include "configuration.h"

class StatusMessageModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    /** Constructor
     * name is for debugging output
     */
    StatusMessageModule();

    virtual int32_t runOnce() override;
    void onConfigChanged();
    const char *currentStatus() const { return committedStatus; }

  private:
    char committedStatus[sizeof(meshtastic_StatusMessage::status)]{};
    bool announceChange = false;

  protected:
    /** Called to handle a particular incoming message. The cached most-recent
     * status for each node lives on NodeDB; use nodeDB->copyNodeStatus(num, ...).
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};

extern StatusMessageModule *statusMessageModule;
#endif
