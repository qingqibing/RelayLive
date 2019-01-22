#pragma once

namespace HttpWsServer
{
    struct pss_http_ws_live;
    enum MediaType;

    struct LIVE_BUFF {
        char *pBuff;
        int   nLen;
    };

    class CLiveWorker
    {
    public:
        CLiveWorker(string strCode, int rtpPort);
        ~CLiveWorker();

        bool Play();

        /** 客户端连接 */
        bool AddConnect(pss_http_ws_live* pss);
        bool DelConnect(pss_http_ws_live* pss);

		/** 客户端全部断开，延时后销毁实例 */
		void Clear2Stop();
        bool m_bStop;

        /** 请求端获取视频数据 */
        LIVE_BUFF GetFlvHeader();
        LIVE_BUFF GetFlvVideo(uint32_t *tail);
        void NextWork(pss_http_ws_live* pss);


        /**
         * 从源过来的视频数据，单线程输入 
         * 以下继承自IlibLiveCb的方法由rtp接收所在的loop线程调用
         * 类中其他方法包括构造、析构都由http所在的loop线程调用
         */
        void push_flv_frame(char* pBuff, int nLen);
        void stop();
    private:
        void cull_lagging_clients(MediaType type);


    private:
        string                m_strCode;     // 播放媒体编号

        /**
         * lws_ring无锁环形缓冲区，只能一个线程写入，一个线程读取
         * m_pFlvRing、m_pH264Ring、m_pMP4Ring由rtp读取的loop线程写入，http服务所在的loop线程读取
         */
        //flv
        LIVE_BUFF             m_stFlvHead;  //flv头数据保存在libLive模块，外部不需要释放
        struct lws_ring       *m_pFlvRing;
        pss_http_ws_live      *m_pFlvPssList;
    };

    /** 直播 */
    CLiveWorker* CreatLiveWorker(string strCode);
};