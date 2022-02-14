#include "MumblePlugin_v_1_0_x.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <signal.h> 
#include <thread>

struct MumbleAPI_v_1_0_x mumbleAPI;
mumble_plugin_id_t ownID;

mumble_error_t mumble_init(mumble_plugin_id_t pluginID) {
	ownID = pluginID;
	return MUMBLE_STATUS_OK;
}

void mumble_shutdown() {}

struct MumbleStringWrapper mumble_getName() {
	static const char *name = "Video";

	struct MumbleStringWrapper wrapper;
	wrapper.data = name;
	wrapper.size = strlen(name);
	wrapper.needsReleasing = false;

	return wrapper;
}

mumble_version_t mumble_getAPIVersion() {
	return MUMBLE_PLUGIN_API_VERSION;
}

void mumble_registerAPIFunctions(void *apiStruct) {
	mumbleAPI = MUMBLE_API_CAST(apiStruct);
}

void mumble_releaseResource(const void *pointer) {
	// As we never pass a resource to Mumble that needs releasing, this function should never
	// get called
	printf("Called mumble_releaseResource but expected that this never gets called -> Aborting");
	abort();
}

mumble_version_t mumble_getVersion() {
	mumble_version_t version;
	version.major = 1;
	version.minor = 0;
	version.patch = 0;

	return version;
}

struct MumbleStringWrapper mumble_getAuthor() {
	static const char *author = "diftucs";

	struct MumbleStringWrapper wrapper;
	wrapper.data = author;
	wrapper.size = strlen(author);
	wrapper.needsReleasing = false;

	return wrapper;
}

struct MumbleStringWrapper mumble_getDescription() {
	static const char *description = "Video integration";

	struct MumbleStringWrapper wrapper;
	wrapper.data = description;
	wrapper.size = strlen(description);
	wrapper.needsReleasing = false;

	return wrapper;
}

// Plugin logic
mumble_connection_t connection;
void mumble_onServerSynchronized(mumble_connection_t c) {
	connection = c;
}

void streamVideo() {
	system("ffmpeg -f x11grab -s 1920x1080 -framerate 60 -i :0.0 -c:v libx264 -preset ultrafast -f flv rtmp://127.0.0.1:1935/mytv/a");
}

bool isStreaming = false;
void mumble_onKeyEvent(uint32_t keyCode, bool wasPress) {
	if(keyCode == MUMBLE_KC_0 && !wasPress) {
		// Start stream and broadcast RTMP id to peers

		// Only act if synced to server
		bool isSynced;
		mumbleAPI.isConnectionSynchronized(ownID, connection, &isSynced);

		if(isSynced) {
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
			for(int i = 0; i < userCount; i++) {
				if(otherUsers[i] == selfID) {
					// Found self
					for(int j = i; j < userCount-1; j++) {
						// Shift nextcoming one back, overwriting self
						otherUsers[j] = otherUsers[j+1];
					}

					// Remove last user (not deallocating though)
					userCount--;
					break;
				}
			}

			// Send to all other users in channel
			uint8_t data[2] = "a";
			char dataID[] = "myid";
			if(mumbleAPI.sendData(ownID, connection, otherUsers, userCount, data, sizeof(mumble_userid_t)*userCount, dataID) == MUMBLE_EC_OK) {
				if(isStreaming) {
					mumbleAPI.log(ownID, "You are already streaming");
				} else {
					std::thread t(&streamVideo);
					t.detach();

					isStreaming = true;
					mumbleAPI.log(ownID, "Screensharing launched");
				}
			}
		}
	} else if(keyCode == MUMBLE_KC_9 && !wasPress) {
		if(isStreaming) {
			system("pkill ffmpeg");
			isStreaming = false;
			mumbleAPI.log(ownID, "Stopped streaming");
		} else {
			mumbleAPI.log(ownID, "You are not currently streaming");
		}
	}
}

bool mumble_onReceiveData(mumble_connection_t connection, mumble_userid_t sender, const uint8_t *data, size_t dataLength, const char *dataID) {
	mumbleAPI.log(ownID, "Starting stream from someone");

	// Get command parts
	const char prefix[] = "ffplay -f flv -fflags nobuffer rtmp://127.0.0.1:1935/mytv/a -window_title Mumble\\ screensharing:\\ ";
	const char *username;
	mumbleAPI.getUserName(ownID, connection, sender, &username);

	// Merge them
	const size_t size = sizeof(prefix) + strlen(username);
	char *cmd = (char *)malloc(size);
	snprintf(cmd, size, "%s%s", prefix, username);

	// Start receiving stream
	system(cmd);

	// Signal that the data was used
	return true;
}
