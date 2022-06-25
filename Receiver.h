#pragma once

#include "MumblePlugin_v_1_0_x.h"

#include <thread>

class Receiver
{
private:
    std::thread *decodingThread;
    bool decodingActive;

    void display(u_int32_t streamID);

public:
    void start(u_int32_t streamID);
    void stop();
};
