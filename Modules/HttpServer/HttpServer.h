#pragma once

namespace HttpWsServer
{
    int Init(void* uv);

    int Cleanup();

    typedef void (*make_rtsp_addr)(char *devid, char *rtsp);
    void set_rtsp_addr_func(make_rtsp_addr func);
};