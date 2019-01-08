#pragma once

#include "basicObj.h"
#include <string>


/**
 * RTSP功能模块接口
 * 使用前主程序必须先初始化<指UdpSocket必须初始化才能使用>
 */
class CRtspObj : public CBasicObj
{
public:
    CRtspObj(std::string rtsp);
    ~CRtspObj(void);

    /** 启动UDP端口监听 */
    void StartListen();

    /**
     * ES帧解析回调
     * @param[in] pBuff H264帧数据
     * @param[in] nLen H264帧长度
     * @param[in] nNalType Nalu的类型
     */
    void ESParseCb(char* pBuff, long nLen/*, uint8_t nNalType*/);

    

    bool        m_bRun;
private:
    string      m_strRtspAddr;
};

