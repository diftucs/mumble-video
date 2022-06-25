#pragma once

#include "MumblePlugin_v_1_0_x.h"
#include "StreamHandler.h"

#include <thread>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
}

class Streamer : public StreamHandler
{
    using StreamHandler::StreamHandler;

private:
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

    void processingLoop();

public:
    void start(uint32_t streamID);
    void stop();
};
