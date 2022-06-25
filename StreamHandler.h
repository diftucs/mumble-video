#pragma once

#include "bits/stdint-uintn.h"

class StreamHandler
{
protected:
    char *targetURL;
    bool active;
    void setTargetURL(u_int32_t streamID);
    void processingLoop();

public:
    StreamHandler();
    ~StreamHandler();
    bool isActive();
    void start(uint32_t streamID);
    void stop();
};
