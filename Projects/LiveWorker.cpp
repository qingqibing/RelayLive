#include "stdafx.h"
#include "HttpServer.h"
#include "HttpLiveServer.h"
#include "LiveWorker.h"
#include "DeviceInfo.h"

extern "C"
{
#define snprintf  _snprintf
#include "libavdevice/avdevice.h"
#include "libavcodec/avcodec.h"  
#include "libavformat/avformat.h"  
#include "libswscale/swscale.h"  
#include "libavutil/imgutils.h"
#include "libavutil/timestamp.h"
}
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avdevice.lib")
#pragma comment(lib,"avfilter.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"postproc.lib")
#pragma comment(lib,"swresample.lib")
#pragma comment(lib,"swscale.lib")

namespace HttpWsServer
{
	extern uv_loop_t *g_uv_loop;

    static map<string,CLiveWorker*>  m_workerMap;
    static CriticalSection           m_cs;

    static string m_strRtpIP;            //< RTP服务IP
    static int    m_nRtpBeginPort;       //< RTP监听的起始端口，必须是偶数
    static int    m_nRtpPortNum;         //< RTP使用的个数，从strRTPPort开始每次加2，共strRTPNum个
    static int    m_nRtpCatchPacketNum;  //< rtp缓存的包的数量
	static int    m_nRtpStreamType;      //< rtp包的类型，传给libLive。ps h264

    static vector<int>     m_vecRtpPort;     //< RTP可用端口，使用时从中取出，使用结束重新放入
    static CriticalSection m_csRTP;          //< RTP端口锁
    static bool _do_port = false;

    static void real_play(void* arg){
        CLiveWorker* pLive = (CLiveWorker*)arg;
        pLive->Play();
    }

    static int write_buffer(void *opaque, uint8_t *buf, int buf_size){
        CLiveWorker* pLive = (CLiveWorker*)opaque;
        //Log::debug("write_buffer size:%d \n", buf_size);
        pLive->push_flv_frame((char*)buf, buf_size);
        return buf_size;
    }

    static int GetRtpPort()
    {
        MutexLock lock(&m_csRTP);
        if(!_do_port) {
            _do_port = true;
            m_strRtpIP           = Settings::getValue("RtpClient","IP");                    //< RTP服务IP
            m_nRtpBeginPort      = Settings::getValue("RtpClient","BeginPort",10000);       //< RTP监听的起始端口，必须是偶数
            m_nRtpPortNum        = Settings::getValue("RtpClient","PortNum",1000);          //< RTP使用的个数，从strRTPPort开始每次加2，共strRTPNum个
            m_nRtpCatchPacketNum = Settings::getValue("RtpClient", "CatchPacketNum", 100);  //< rtp缓存的包的数量
			m_nRtpStreamType     = Settings::getValue("RtpClient", "Filter", 0);            //< rtp缓存的包的数量

            Log::debug("RtpConfig IP:%s, BeginPort:%d,PortNum:%d,CatchPacketNum:%d"
                , m_strRtpIP.c_str(), m_nRtpBeginPort, m_nRtpPortNum, m_nRtpCatchPacketNum);
            m_vecRtpPort.clear();
            for (int i=0; i<m_nRtpPortNum; ++i) {
                m_vecRtpPort.push_back(m_nRtpBeginPort+i*2);
            }
        }

        int nRet = -1;
        auto it = m_vecRtpPort.begin();
        if (it != m_vecRtpPort.end()) {
            nRet = *it;
            m_vecRtpPort.erase(it);
        }

        return nRet;
    }

    static void GiveBackRtpPort(int nPort)
    {
        MutexLock lock(&m_csRTP);
        m_vecRtpPort.push_back(nPort);
    }

    static void destroy_ring_node(void *_msg)
    {
        LIVE_BUFF *msg = (LIVE_BUFF*)_msg;
        free(msg->pBuff);
        msg->pBuff = NULL;
        msg->nLen = 0;
    }


    /** CLiveWorker析构中删除m_pLive比较耗时，会阻塞event loop，因此使用线程。 */
    static void live_worker_destory_thread(void* arg) {
        CLiveWorker* live = (CLiveWorker*)arg;
        SAFE_DELETE(live);
    }

    //////////////////////////////////////////////////////////////////////////

    CLiveWorker::CLiveWorker(string strCode, pss_http_ws_live *pss)
        : m_strCode(strCode)
        , m_pPssList(pss)
        , m_bStop(false)
    {
        m_pRing  = lws_ring_create(sizeof(LIVE_BUFF), 100, destroy_ring_node);
    }

    CLiveWorker::~CLiveWorker()
    {
        lws_ring_destroy(m_pRing);
        Log::debug("CLiveWorker release");
    }

    bool CLiveWorker::Play()
    {
        AVFormatContext *ifc = NULL;
        AVFormatContext *ofc = NULL;

        //输入 打开rtsp
        string strRtsp = DeviceInfo::GetRtspAddr(m_strCode);
        const char *in_filename = strRtsp.c_str();

        AVDictionary* options = NULL;
        //av_dict_set(&options, "buffer_size", "102400", 0); //设置缓存大小，1080p可将值调大
        //av_dict_set(&options, "rtsp_transport", "udp", 0); //以udp方式打开，如果以tcp方式打开将udp替换为tcp
        //av_dict_set(&options, "stimeout", "2000000", 0); //设置超时断开连接时间，单位微秒
        //av_dict_set(&options, "max_delay", "500000", 0); //设置最大时延
	    int ret = avformat_open_input(&ifc, in_filename, NULL, &options);
        if (ret != 0) {
		    char tmp[1024]={0};
		    av_strerror(ret, tmp, 1024);
            Log::error("Could not open input file '%s': %d(%s)", in_filename, ret, tmp);
            goto end;
        }
        ret = avformat_find_stream_info(ifc, NULL);
        if (ret < 0) {
            char tmp[1024]={0};
            av_strerror(ret, tmp, 1024);
            Log::error("Failed to retrieve input stream information %d(%s)", ret, tmp);
            goto end;
        }
        Log::debug("show input format info");
        av_dump_format(ifc, 0, in_filename, 0);

        //输出 自定义回调
        AVOutputFormat *ofmt = NULL;
        ret = avformat_alloc_output_context2(&ofc, NULL, "flv", NULL);
        if (!ofc) {
            Log::error("Could not create output context\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        ofmt = ofc->oformat;

	    unsigned char* outbuffer=(unsigned char*)av_malloc(65536);
	    AVIOContext *avio_out =avio_alloc_context(outbuffer, 65536,1,this,NULL,write_buffer,NULL);  
	    ofc->pb = avio_out; 
	    ofc->flags = AVFMT_FLAG_CUSTOM_IO;
	    ofmt->flags |= AVFMT_NOFILE;

        //根据输入流信息生成输出流信息
        int in_video_index = -1, in_audio_index = -1, in_subtitle_index = -1;
        int out_video_index = -1, out_audio_index = -1, out_subtitle_index = -1;
        for (unsigned int i = 0, j = 0; i < ifc->nb_streams; i++) {
            AVStream *is = ifc->streams[i];
            if (is->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
                in_video_index = i;
                out_video_index = j++;
            //} else if (is->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            //    in_audio_index = i;
            //    out_audio_index = j++;
            //} else if (is->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            //    in_subtitle_index = i;
            //    out_subtitle_index = j++;
            } else {
                continue;
            }

            AVStream *os = avformat_new_stream(ofc, NULL);
            if (!os) {
                Log::error("Failed allocating output stream\n");
                ret = AVERROR_UNKNOWN;
                goto end;
            }

            ret = avcodec_parameters_copy(os->codecpar, is->codecpar);
            if (ret < 0) {
                Log::error("Failed to copy codec parameters\n");
                goto end;
            }
        }
        Log::debug("show output format info");
        av_dump_format(ofc, 0, NULL, 1);

        //Log::debug("index:%d %d %d %d",in_video_index, in_audio_index, out_video_index, out_audio_index);

        ret = avformat_write_header(ofc, NULL);
        if (ret < 0) {
		    char tmp[1024]={0};
		    av_strerror(ret, tmp, 1024);
            Log::error("Error avformat_write_header %d:%s \n", ret, tmp);
            goto end;
        }

	    bool first = true;
        while (!m_bStop) {
            AVStream *in_stream, *out_stream;
	        AVPacket pkt;
            av_init_packet(&pkt);
            ret = av_read_frame(ifc, &pkt);
            if (ret < 0)
                break;
            //Log::debug("read_index %d",pkt.stream_index);
            in_stream  = ifc->streams[pkt.stream_index];
            if (pkt.stream_index == in_video_index) {
                //Log::debug("video dts %d", pkt.dts);
                //Log::debug("video pts %d", pkt.pts);
                pkt.stream_index = out_video_index;
                out_stream = ofc->streams[pkt.stream_index];
                /* copy packet */
		        if(first){
			        pkt.pts = 0;
			        pkt.dts = 0;
			        first = false;
		        } else {
			        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF/*|AV_ROUND_PASS_MINMAX*/);
			        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF/*|AV_ROUND_PASS_MINMAX*/);
		        }
                pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
                pkt.pos = -1;
                //Log::debug("video2 dts %d", pkt.dts);
                //Log::debug("video2 pts %d", pkt.pts);

                int wret = av_interleaved_write_frame(ofc, &pkt);
                if (wret < 0) {
                    char tmp[1024]={0};
                    av_strerror(ret, tmp, 1024);
                    Log::error("video error muxing packet %d:%s \n", ret, tmp);
                    //break;
                }
            } //else if (pkt.stream_index == in_audio_index) {
            //    //Log::debug("audio dts %d pts %d", pkt.dts, pkt.pts);
            //    pkt.stream_index = out_audio_index;
            //    out_stream = ofc->streams[pkt.stream_index];
            //    /* copy packet */
            //    pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF/*|AV_ROUND_PASS_MINMAX*/);
            //    pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF/*|AV_ROUND_PASS_MINMAX*/);
            //    pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
            //    pkt.pos = -1;
            //    //Log::debug("audio2 dts %d pts %d", pkt.dts, pkt.pts);

            //    int wret = av_interleaved_write_frame(ofc, &pkt);
            //    if (wret < 0) {
            //        char tmp[1024]={0};
            //        av_strerror(ret, tmp, 1024);
            //        Log::error("audio error muxing packet %d:%s \n", ret, tmp);
            //        //break;
            //    }
            //}
            av_packet_unref(&pkt);
        }

        av_write_trailer(ofc);
end:
        /** 关闭输入 */
        avformat_close_input(&ifc);

        /* 关闭输出 */
        if (ofc && !(ofmt->flags & AVFMT_NOFILE))
            avio_closep(&ofc->pb);
        avformat_free_context(ofc);

        /** 返回码 */
        if (ret < 0 && ret != AVERROR_EOF) {
            char tmp[AV_ERROR_MAX_STRING_SIZE]={0};
            av_make_error_string(tmp,AV_ERROR_MAX_STRING_SIZE,ret);
            Log::error("Error occurred: %s\n", tmp);
            stop();
            return false;
        }
        Log::debug("client stop, delete live worker");
        delete this;
	    return true;
    }

	void CLiveWorker::Clear2Stop() {
		Log::debug("need close live stream");
        if(m_bStop) {
            Log::debug("already stop, delete live worker");
            delete this;
        } else {
            Log::debug("need stop ffmpeg");
            m_bStop = true;
        }
	}

    void CLiveWorker::push_flv_frame(char* pBuff, int nLen)
    {
        Log::debug("push_flv_frame len:%d", nLen);
        if(m_bStop) {
            Log::error("client has already leave");
            return;
        }
        //内存数据保存至ring-buff
        int n = (int)lws_ring_get_count_free_elements(m_pRing);
        if (!n) {
            lws_set_timeout(m_pPssList->wsi, PENDING_TIMEOUT_LAGGING, LWS_TO_KILL_ASYNC);
            Log::error("to many data can't send");
            return;
        }

        // 将数据保存在ring buff
        char* pSaveBuff = (char*)malloc(nLen + LWS_PRE);
        memcpy(pSaveBuff + LWS_PRE, pBuff, nLen);
        LIVE_BUFF newTag = {pSaveBuff, nLen};
        if (!lws_ring_insert(m_pRing, &newTag, 1)) {
            destroy_ring_node(&newTag);
            Log::error("dropping!");
            return;
        }

        //向客户端发送数据
        lws_callback_on_writable(m_pPssList->wsi);
    }

    void CLiveWorker::stop()
    {
        //视频源没有数据并超时
        Log::debug("no data recived any more, stopped");
        //状态改变为超时，此时前端全部断开，不需要延时，直接销毁

        //断开客户端连接
        lws_set_timeout(m_pPssList->wsi, PENDING_TIMEOUT_CLOSE_SEND, LWS_TO_KILL_ASYNC);
    }

    LIVE_BUFF CLiveWorker::GetFlvVideo(uint32_t *tail)
    {
        LIVE_BUFF ret = {nullptr,0};
        LIVE_BUFF* tag = (LIVE_BUFF*)lws_ring_get_element(m_pRing, tail);
        if(tag) ret = *tag;

        return ret;
    }

    void CLiveWorker::NextWork(pss_http_ws_live* pss)
    {
        struct lws_ring *ring;
        pss_http_ws_live* pssList;
        if(pss->media_type == media_flv) {
            ring = m_pRing;
            pssList = m_pPssList;
        } else {
            return;
        }

        //Log::debug("this work tail:%d\r\n", pss->tail);
        lws_ring_consume_and_update_oldest_tail(
            ring,	          /* lws_ring object */
            pss_http_ws_live, /* type of objects with tails */
            &pss->tail,	      /* tail of guy doing the consuming */
            1,		          /* number of payload objects being consumed */
            pssList,	      /* head of list of objects with tails */
            tail,		      /* member name of tail in objects with tails */
            pss_next	      /* member name of next object in objects with tails */
            );
        //Log::debug("next work tail:%d\r\n", pss->tail);

        /* more to do for us? */
        if (lws_ring_get_element(ring, &pss->tail))
            /* come back as soon as we can write more */
                lws_callback_on_writable(pss->wsi);
    }

    void CLiveWorker::cull_lagging_clients(MediaType type)
    {
        struct lws_ring *ring = m_pRing;
        pss_http_ws_live* pssList = m_pPssList;
  
        uint32_t oldest_tail = lws_ring_get_oldest_tail(ring);
        pss_http_ws_live *old_pss = NULL;
        int most = 0, before = lws_ring_get_count_waiting_elements(ring, &oldest_tail), m;

        lws_start_foreach_llp_safe(pss_http_ws_live **, ppss, pssList, pss_next) {
            if ((*ppss)->tail == oldest_tail) {
                //连接超时
                old_pss = *ppss;
                Log::debug("Killing lagging client %p", (*ppss)->wsi);
                lws_set_timeout((*ppss)->wsi, PENDING_TIMEOUT_LAGGING, LWS_TO_KILL_ASYNC);
                (*ppss)->culled = 1;
                lws_ll_fwd_remove(pss_http_ws_live, pss_next, (*ppss), pssList);
                continue;
            } else {
                m = lws_ring_get_count_waiting_elements(ring, &((*ppss)->tail));
                if (m > most)
                    most = m;
            }
        } lws_end_foreach_llp_safe(ppss);

        if (!old_pss)
            return;

        lws_ring_consume_and_update_oldest_tail(ring,
            pss_http_ws_live, &old_pss->tail, before - most,
            pssList, tail, pss_next);

        Log::debug("shrunk ring from %d to %d", before, most);
    }

    //////////////////////////////////////////////////////////////////////////


    CLiveWorker* CreatLiveWorker(string strCode, pss_http_ws_live *pss)
    {
        Log::debug("CreatLiveWorker begin");
        //int rtpPort = GetRtpPort();
        //if(rtpPort < 0) {
        //    Log::error("play failed %s, no rtp port",strCode.c_str());
        //    return nullptr;
        //}

        CLiveWorker* pWorker = new CLiveWorker(strCode, pss);
        pss->m_pWorker = pWorker;
        uv_thread_t tid;
        uv_thread_create(&tid, real_play, (void*)pWorker);
        Log::debug("RealPlay ok: %s",strCode.c_str());

        return pWorker;
    }

    static void ffmpeg_log_cb(void* ptr, int level, const char* fmt, va_list vl){
        char text[256];              //日志内容
        memset(text,0,256);
        vsprintf_s(text, 256, fmt, vl);

        if(level <= AV_LOG_ERROR){
            Log::error(text);
        } else if(level == AV_LOG_WARNING) {
            Log::warning(text);
        } else {
            Log::debug(text);
        }
    }

    void InitFFmpeg(){
        av_log_set_callback(ffmpeg_log_cb);
    }
}