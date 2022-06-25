#pragma once

#include "MumblePlugin_v_1_0_x.h"

#include <thread>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
}

class Streamer
{
private:
    bool streamState;
    AVFormatContext *inputFormatContext;
    AVCodecContext *inputCodecContext;
    AVFormatContext *outputFormatContext;
    AVCodecContext *outputCodecContext;
    AVStream *outputStream;
    AVDictionary *outputHeaderDictionary;
    SwsContext *sws_ctx;
    AVPacket *inputPacket;
    AVPacket *outPacket;
    AVFrame *inputFrame;
    AVFrame *outputFrame;
    std::thread *encodingThread;

    void stream();

public:
    bool isStreaming();
    void start(uint32_t streamID);
    void stop();
};
