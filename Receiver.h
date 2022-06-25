#pragma once

#include "MumblePlugin_v_1_0_x.h"
#include "StreamHandler.h"

#include <thread>

class Receiver : public StreamHandler
{
    using StreamHandler::StreamHandler;

private:
    std::thread *decodingThread;
    bool decodingActive;

    void processingLoop(u_int32_t streamID);

public:
    void start(u_int32_t streamID);
    void stop();
};
