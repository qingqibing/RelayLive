// sever.cpp : 定义控制台应用程序的入口点。
//
#include "common.h"
#include "MiniDump.h"
#include "utilc_api.h"
#include "HttpServer.h"
#include "DeviceInfo.h"
#include "uv.h"
#include <stdio.h>

int main()
{
    /** Dump设置 */
    CMiniDump dump("rtsp2ws.dmp");

    /** 创建日志文件 */
    char path[MAX_PATH];
    sprintf_s(path, MAX_PATH, ".\\log\\rtsp2ws.txt");
    Log::open(Log::Print::both, Log::Level::debug, path);

    /** 加载配置文件 */
    if (!Settings::loadFromProfile(".\\config.txt"))
    {
        Log::error("配置文件错误");
        return -1;
    }
    Log::debug("Settings::loadFromProfile ok");

    //全局loop
    static uv_loop_t *p_loop_uv = nullptr;
    p_loop_uv = uv_default_loop();

    /** 创建一个http服务器 */
    HttpWsServer::Init((void*)p_loop_uv);
    Log::debug("GB28181 Sever start success\r\n");

    /** 数据库模块 */
    DeviceInfo::Init();
    Log::debug("Get devices from oracle success\r\n");

    // 事件循环
    while(true)
    {
        uv_run(p_loop_uv, UV_RUN_DEFAULT);
        Sleep(1000);
    }
    sleep(INFINITE);
    return 0;
}