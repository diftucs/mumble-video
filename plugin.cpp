#include "MumblePlugin_v_1_0_x.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <thread>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include "SDL2/SDL.h"
#include "SDL2/SDL_events.h"
#include "SDL2/SDL_thread.h"

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
}

struct MumbleAPI_v_1_0_x mumbleAPI;
mumble_plugin_id_t ownID;
nlohmann::json config;

mumble_error_t mumble_init(mumble_plugin_id_t pluginID)
{
	ownID = pluginID;

	std::ifstream ifs("/home/user/.config/mumble-video/config.json");
	config = nlohmann::json::parse(ifs);

	return MUMBLE_STATUS_OK;
}

void mumble_shutdown() {}

struct MumbleStringWrapper mumble_getName()
{
	static const char *name = "Video";

	struct MumbleStringWrapper wrapper;
	wrapper.data = name;
	wrapper.size = strlen(name);
	wrapper.needsReleasing = false;

	return wrapper;
}

mumble_version_t mumble_getAPIVersion()
{
	return MUMBLE_PLUGIN_API_VERSION;
}

void mumble_registerAPIFunctions(void *apiStruct)
{
	mumbleAPI = MUMBLE_API_CAST(apiStruct);
}

void mumble_releaseResource(const void *pointer)
{
	// As we never pass a resource to Mumble that needs releasing, this function should never
	// get called
	printf("Called mumble_releaseResource but expected that this never gets called -> Aborting");
	abort();
}

mumble_version_t mumble_getVersion()
{
	mumble_version_t version;
	version.major = 1;
	version.minor = 0;
	version.patch = 0;

	return version;
}

struct MumbleStringWrapper mumble_getAuthor()
{
	static const char *author = "diftucs";

	struct MumbleStringWrapper wrapper;
	wrapper.data = author;
	wrapper.size = strlen(author);
	wrapper.needsReleasing = false;

	return wrapper;
}

struct MumbleStringWrapper mumble_getDescription()
{
	static const char *description = "Video integration";

	struct MumbleStringWrapper wrapper;
	wrapper.data = description;
	wrapper.size = strlen(description);
	wrapper.needsReleasing = false;

	return wrapper;
}

// Plugin logic
mumble_connection_t connection;
void mumble_onServerSynchronized(mumble_connection_t c)
{
	connection = c;
}

void stream()
{
	// Initialize devices, here used for x11grab
	avdevice_register_all();

	// Init input AVFormatContext. Will contain the input AVStream
	AVFormatContext *inputFormatContext = avformat_alloc_context();
	// Init input AVStream. Will have AVInputFormat x11grab and provide screen data
	avformat_open_input(&inputFormatContext, getenv("DISPLAY"), av_find_input_format("x11grab"), NULL);

	// Init an AVCodecContext with the codec needed to decode the input AVStream
	AVCodecContext *inputCodecContext = avcodec_alloc_context3(avcodec_find_decoder(inputFormatContext->streams[0]->codecpar->codec_id));
	// Apply the input AVStream's parameters to the codec
	avcodec_parameters_to_context(inputCodecContext, inputFormatContext->streams[0]->codecpar);
	// Initialize the input codec
	avcodec_open2(inputCodecContext, NULL, NULL);

	// Init the output AVFormatContext. Will contain the output AVStream
	AVFormatContext *outputFormatContext;
	avformat_alloc_output_context2(&outputFormatContext, NULL, "flv", NULL);
	// Init an AVCodecContext with the codec needed to encode the processed frames
	//
	// Note:
	// The default codec when initializing an AVFormatContext with an FLV container like above is AV_CODEC_ID_FLV1 (see AVFormatContext->oformat->video_codec).
	// That is the outdated H.263 codec, which can be replaced by H.264 by explicitly specifying AV_CODEC_ID_H264 when creating the AVCodecContext.
	// This will result in keeping the FLV container, but having H.264 encoded data within instead.
	AVCodecContext *outputCodecContext = avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_H264));

	// Set the desired properties of the encoded packets
	outputCodecContext->bit_rate = 400000;
	outputCodecContext->width = 1920;
	outputCodecContext->height = 1080;
	outputCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
	outputCodecContext->time_base = (AVRational){1, 60};

	// Init the output AVStream
	AVStream *outputStream = avformat_new_stream(outputFormatContext, NULL);
	// Apply the output AVCodecContext's codec and packet properties to the output AVStream
	avcodec_parameters_from_context(outputStream->codecpar, outputCodecContext);

	// Additionally, set properties unique to the encoded data
	// A minimum of 12 frames between each full frame (the rest only describe change)
	outputCodecContext->gop_size = 12;
	// Make a timestamp increment of one correspond to 1/60 of a second
	// During the encoding phase the timestamps are set to increase by one every frame
	// This results in a 60 FPS stream
	outputCodecContext->time_base = (AVRational){1, 60};

	// Open the output codec
	avcodec_open2(outputCodecContext, NULL, NULL);

	// Open the output stream
	avio_open(&outputFormatContext->pb, "rtmp://localhost/publishlive/livestream", AVIO_FLAG_WRITE);

	// Disable FLV-specific duration and filesize writing to header when calling av_write_trailer()
	// This should be set for streams since writing to a header that is already sent is impossible
	// Removing this results in a warning from FFmpeg
	AVDictionary *outputHeaderDictionary = NULL;
	av_dict_set(&outputHeaderDictionary, "flvflags", "no_duration_filesize", 0);

	// Write output header
	avformat_write_header(outputFormatContext, &outputHeaderDictionary);

	// Create scaling context
	SwsContext *sws_ctx = sws_getContext(inputFormatContext->streams[0]->codecpar->width,
										 inputFormatContext->streams[0]->codecpar->height,
										 static_cast<AVPixelFormat>(inputFormatContext->streams[0]->codecpar->format),
										 outputFormatContext->streams[0]->codecpar->width,
										 outputFormatContext->streams[0]->codecpar->height,
										 static_cast<AVPixelFormat>(outputFormatContext->streams[0]->codecpar->format),
										 SWS_BICUBIC, NULL, NULL, NULL);

	// Allocate memory for encoded pre- and post-converted packets
	AVPacket *inputPacket = av_packet_alloc();
	AVPacket *outPacket = av_packet_alloc();

	// Allocate memory for decoded pre- and post-scaling frames
	AVFrame *inputFrame = av_frame_alloc();
	AVFrame *outputFrame = av_frame_alloc();

	// Init the output frame buffer according to the codec's parameters on pixel format, width and height
	// The input frame's buffer is initialized by avcodec_receive_frame()
	outputFrame->format = outputCodecContext->pix_fmt;
	outputFrame->width = outputCodecContext->width;
	outputFrame->height = outputCodecContext->height;
	av_frame_get_buffer(outputFrame, 0);

	// Determine distance between each presentation timestamp
	// No idea why this is the correct scale
	int ptsScale = 2 * av_q2d(outputCodecContext->time_base) / av_q2d(outputFormatContext->streams[0]->time_base);
	for (int i = 0; i < 1000; i++)
	{
		// Get packet from input
		av_read_frame(inputFormatContext, inputPacket);

		// Send packet to decoder
		avcodec_send_packet(inputCodecContext, inputPacket);
		// Fetch frame from decoder
		avcodec_receive_frame(inputCodecContext, inputFrame);

		// Scale to output dimensions
		sws_scale(sws_ctx, inputFrame->data, inputFrame->linesize, 0, inputFormatContext->streams[0]->codecpar->height, outputFrame->data, outputFrame->linesize);

		// Write presentation timestamp
		outputFrame->pts = i * ptsScale;

		// Encode frame into packet
		avcodec_send_frame(outputCodecContext, outputFrame);
		if (avcodec_receive_packet(outputCodecContext, outPacket) < 0)
			continue;

		// Write packet to output
		av_interleaved_write_frame(outputFormatContext, outPacket);

		// Free the packet buffers since they are not reused
		av_packet_unref(inputPacket);
		av_packet_unref(outPacket);
	}

	// Write end of stream
	av_write_trailer(outputFormatContext);

	// Free memory
	sws_freeContext(sws_ctx);
	av_packet_free(&inputPacket);
	av_packet_free(&outPacket);
	av_frame_free(&inputFrame);
	av_frame_free(&outputFrame);
	avcodec_free_context(&inputCodecContext);
	avcodec_free_context(&outputCodecContext);
	avformat_close_input(&outputFormatContext);
	avformat_free_context(outputFormatContext);
	avformat_close_input(&inputFormatContext);
	avformat_free_context(inputFormatContext);
}

void receive()
{
	// Init input AVFormatContext. Will contain the input AVStream
	AVFormatContext *inputFormatContext = avformat_alloc_context();
	// Init input AVStream according to properties read from the input
	avformat_open_input(&inputFormatContext, "rtmp://localhost/publishlive/livestream", NULL, NULL);
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
	}

	// Free
	sws_freeContext(sws_ctx);
	av_packet_free(&inputPacket);
	av_frame_free(&inputFrame);
	av_frame_free(&outputFrame);
	avcodec_free_context(&inputCodecContext);
	avformat_close_input(&inputFormatContext);
	avformat_free_context(inputFormatContext);
}

bool isStreaming = false;
void mumble_onKeyEvent(uint32_t keyCode, bool wasPress)
{
	if (keyCode == MUMBLE_KC_0 && !wasPress)
	{
		// Start stream and broadcast RTMP id to peers

		// Only act if synced to server
		bool isSynced;
		mumbleAPI.isConnectionSynchronized(ownID, connection, &isSynced);

		if (isSynced)
		{
			// Get self
			mumble_userid_t selfID;
			mumbleAPI.getLocalUserID(ownID, connection, &selfID);

			// Get self channel
			mumble_channelid_t selfChannelID;
			mumbleAPI.getChannelOfUser(ownID, connection, selfID, &selfChannelID);

			// Get self channel users
			mumble_userid_t *otherUsers;
			size_t userCount;
			mumbleAPI.getUsersInChannel(ownID, connection, selfChannelID, &otherUsers, &userCount);

			// Remove self from otherUsers
			for (int i = 0; i < userCount; i++)
			{
				if (otherUsers[i] == selfID)
				{
					// Found self
					for (int j = i; j < userCount - 1; j++)
					{
						// Shift nextcoming one back, overwriting self
						otherUsers[j] = otherUsers[j + 1];
					}

					// Remove last user (not deallocating though)
					userCount--;
					break;
				}
			}

			// Send to all other users in channel
			uint8_t data[2] = "a";
			char dataID[] = "myid";
			if (mumbleAPI.sendData(ownID, connection, otherUsers, userCount, data, sizeof(mumble_userid_t) * userCount, dataID) == MUMBLE_EC_OK)
			{
				if (isStreaming)
				{
					mumbleAPI.log(ownID, "You are already streaming");
				}
				else
				{
					isStreaming = true;
					std::thread t(&stream);
					t.detach();
					mumbleAPI.log(ownID, "Screensharing launched");
				}
			}

			mumbleAPI.freeMemory(ownID, otherUsers);
			mumbleAPI.freeMemory(ownID, &userCount);
		}
	}
	else if (keyCode == MUMBLE_KC_9 && !wasPress)
	{
		if (isStreaming)
		{
			isStreaming = false;
			mumbleAPI.log(ownID, "Stopped streaming");
		}
		else
		{
			mumbleAPI.log(ownID, "You are not currently streaming");
		}
	}
}

bool mumble_onReceiveData(mumble_connection_t connection, mumble_userid_t sender, const uint8_t *data, size_t dataLength, const char *dataID)
{
	mumbleAPI.log(ownID, "Starting stream from someone");

	std::thread t(&receive);
	t.detach();
	return true;
}
