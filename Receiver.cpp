#include "MumblePlugin_v_1_0_x.h"

#include <thread>
#include "SDL2/SDL.h"
#include "SDL2/SDL_events.h"
#include "SDL2/SDL_thread.h"

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
}

class Receiver
{
private:
    std::thread *decodingThread;
    bool decodingActive;

    void display(u_int32_t streamID)
    {
        // Init input AVFormatContext. Will contain the input AVStream
        AVFormatContext *inputFormatContext = avformat_alloc_context();
        // Init input AVStream according to properties read from the input
        char *baseURL = "rtmp://localhost/publishlive/";
        const size_t size = strlen(baseURL) + sizeof(streamID);
        char *cmd = (char *)malloc(size);
        snprintf(cmd, size, "%s%zu", baseURL, streamID);
        avformat_open_input(&inputFormatContext, cmd, NULL, NULL);
        avformat_find_stream_info(inputFormatContext, NULL); // Will hang waiting for input

        // Init an AVCodecContext with the codec needed to decode the input AVStream
        AVCodecContext *inputCodecContext = avcodec_alloc_context3(avcodec_find_decoder(inputFormatContext->streams[0]->codecpar->codec_id));
        // Apply the input AVStream's parameters to the codec
        avcodec_parameters_to_context(inputCodecContext, inputFormatContext->streams[0]->codecpar);
        // Initialize the input codec
        avcodec_open2(inputCodecContext, NULL, NULL);

        // Allocate memory needed for decoding
        AVPacket *inputPacket = av_packet_alloc();
        AVFrame *inputFrame = av_frame_alloc();
        AVFrame *outputFrame = av_frame_alloc();

        // Allocate buffer for outputFrame since av_frame_alloc only allocates memory for the frame itself
        outputFrame->format = inputCodecContext->pix_fmt;
        outputFrame->width = inputCodecContext->width;
        outputFrame->height = inputCodecContext->height;
        av_frame_get_buffer(outputFrame, 0);

        // Init SDL library
        SDL_Init(SDL_INIT_VIDEO);
        // SDL display window
        SDL_Window *window = SDL_CreateWindow("Stream display", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, inputFormatContext->streams[0]->codecpar->width, inputFormatContext->streams[0]->codecpar->height, SDL_WINDOW_RESIZABLE);
        // SDL rendering context
        SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
        // What SDL draws on the window through the rendering context
        SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, inputFormatContext->streams[0]->codecpar->width, inputFormatContext->streams[0]->codecpar->height);

        // Init FFmpeg scaling to fit frames to the SDL texture
        SwsContext *sws_ctx = sws_getContext(inputFormatContext->streams[0]->codecpar->width,
                                             inputFormatContext->streams[0]->codecpar->height,
                                             static_cast<AVPixelFormat>(inputFormatContext->streams[0]->codecpar->format),
                                             inputFormatContext->streams[0]->codecpar->width,
                                             inputFormatContext->streams[0]->codecpar->height,
                                             static_cast<AVPixelFormat>(inputFormatContext->streams[0]->codecpar->format),
                                             SWS_BILINEAR,
                                             NULL,
                                             NULL,
                                             NULL);

        SDL_Event event;
        while (1)
        {
            // Handle SDL window resizing
            while (SDL_PollEvent(&event))
            {
                if (event.type = SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                {
                    // Resize the SDL texture to fit the SDL window
                    SDL_DestroyTexture(texture);
                    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STATIC, event.window.data1, event.window.data2);

                    // Change the target resolution for the FFmpeg rescaler to fit the new SDL texture resolution
                    sws_ctx = sws_getCachedContext(sws_ctx,
                                                   inputFormatContext->streams[0]->codecpar->width,
                                                   inputFormatContext->streams[0]->codecpar->height,
                                                   static_cast<AVPixelFormat>(inputFormatContext->streams[0]->codecpar->format),
                                                   event.window.data1,
                                                   event.window.data2,
                                                   static_cast<AVPixelFormat>(inputFormatContext->streams[0]->codecpar->format),
                                                   SWS_BILINEAR,
                                                   NULL,
                                                   NULL,
                                                   NULL);

                    // Reallocate output frame to fit the new resolution
                    av_frame_free(&outputFrame);
                    outputFrame = av_frame_alloc();
                    outputFrame->format = inputCodecContext->pix_fmt;
                    outputFrame->width = event.window.data1;
                    outputFrame->height = event.window.data2;
                    av_frame_get_buffer(outputFrame, 0);
                }
            }

            // Get packet from input
            av_read_frame(inputFormatContext, inputPacket);

            // Send packet to decoder
            avcodec_send_packet(inputCodecContext, inputPacket);
            // Fetch frame from decoder and loop if more data is needed
            if (avcodec_receive_frame(inputCodecContext, inputFrame) == AVERROR(EAGAIN))
                continue;

            // Scale frame with FFmpeg to fit the texture
            sws_scale(sws_ctx, inputFrame->data, inputFrame->linesize, 0, inputFormatContext->streams[0]->codecpar->height, outputFrame->data, outputFrame->linesize);

            // Draw the new data to the texture
            SDL_UpdateYUVTexture(texture, NULL, outputFrame->data[0], outputFrame->linesize[0],
                                 outputFrame->data[1], outputFrame->linesize[1],
                                 outputFrame->data[2], outputFrame->linesize[2]);

            // Clear the renderer's old data and copy the new texture to it's backbuffer
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);

            // Apply the rendering context's backbuffer
            SDL_RenderPresent(renderer);

            // Draw the renderer's data onto the window
            SDL_UpdateWindowSurface(window);

            // Free the packet buffers since they are not reused
            av_packet_unref(inputPacket);

            if (!decodingActive)
                break;
        }

        // Free
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window); // Also closes the physical window
        sws_freeContext(sws_ctx);
        av_packet_free(&inputPacket);
        av_frame_free(&inputFrame);
        av_frame_free(&outputFrame);
        avcodec_free_context(&inputCodecContext);
        avformat_close_input(&inputFormatContext);
        avformat_free_context(inputFormatContext);
    }

public:
    void start(u_int32_t streamID)
    {
        // Start decoding
        decodingActive = true;
        decodingThread = new std::thread(&Receiver::display, this, streamID);
    }

    void stop()
    {
        // Start decoding
        decodingActive = false;
        decodingThread->join();
    }
};
