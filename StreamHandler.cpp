#include "bits/stdint-uintn.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "StreamHandler.h"

StreamHandler::StreamHandler()
{
    active = false;
}

void StreamHandler::setTargetURL(u_int32_t streamID)
{
    char *baseURL = "rtmp://localhost/publishlive/";
    const size_t size = strlen(baseURL) + sizeof(streamID);
    targetURL = (char *)malloc(size);
    snprintf(targetURL, size, "%s%zu", baseURL, streamID);
}

StreamHandler::~StreamHandler()
{
    free(targetURL);
}

bool StreamHandler::isActive()
{
    return active;
}
