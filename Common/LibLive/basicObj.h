#pragma once

#include "libLive.h"

enum NalType;
enum STREAM_TYPE;

/**
 * RTSP功能模块接口
 * 使用前主程序必须先初始化<指UdpSocket必须初始化才能使用>
 */
class CBasicObj : public IlibLive
{
public:
    CBasicObj();
    ~CBasicObj();


    /** H264中sps解析回调 */
    void H264SpsCb(uint32_t nWidth, uint32_t nHeight, double fFps);

    /** FLV合成回调 */
    void FlvCb(FLV_FRAG_TYPE eType, char* pBuff, int nBuffSize);

    /** MP4合成回调 */
    void Mp4Cb(MP4_FRAG_TYPE eType, char* pBuff, int nBuffSize);

    /** TS合成回调 */
    void TsCb(char* pBuff, int nBuffSize);

    /** H264合成回调 */
    void H264Cb(char* pBuff, int nBuffSize);
    
    /**
     * 设置处理数据回调的对象
     * @param[in] pHandle
     */
    void SetCallback(IlibLiveCb* pHandle)
    {
        m_pCallBack = pHandle;
    }

protected:
    void*       m_pH264;            // H264解析类
    void*       m_pTs;              // TS组包类
    void*       m_pFlv;             // FLV组包类
    void*       m_pMp4;             // MP4组包类
    IlibLiveCb* m_pCallBack;        // 回调对象
};

