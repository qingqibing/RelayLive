#include "stdafx.h"
#include "DeviceInfo.h"
#include "libOci.h"

namespace DeviceInfo
{
    enum devType {
        DEV_DAHUA = 0,
        DEV_HIK,
		DEV_UNIVIEW
    };

    struct DEV_INFO
    {
        string     ssid;
        string     ip;
        uint32_t   port;
        string     user_name;
        string     password;
        uint32_t   channel;     //通道号，1、2...等
        uint32_t   stream;      //码流 0:主码流 1:子码流
        uint32_t   rtp_port;
        devType    dev_type;    //设备类型
    };

    static OracleClient* client;
    static string _db_connect_name = "DB";
    static map<string,DEV_INFO> _map_devs;

    static void init_db(){
        auto oci_error_handler = [](OCI_Error *err){
            OCI_Connection * conn = OCI_ErrorGetConnection(err);
            Log::error("Error[ORA-%05d] - msg[%s] - database[%s] - user[%s] - sql[%s]"
                , OCI_ErrorGetOCICode(err)
                , OCI_ErrorGetString(err)
                , OCI_GetDatabase(conn)
                , OCI_GetUserName(conn)
                , OCI_GetSql(OCI_ErrorGetStatement(err)));
        };
        string path = Settings::getValue("DataBase", "Path");

        if(!OCI_Initialize(oci_error_handler, path.c_str(), OCI_ENV_THREADED))
            return;

        ConPool_Setting dbset;
        dbset.database = Settings::getValue("DataBase","Addr");
        dbset.username = Settings::getValue("DataBase","User");
        dbset.password = Settings::getValue("DataBase","PassWord");
        dbset.max_conns = 5;
        dbset.min_conns = 2;
        dbset.inc_conns = 2;
        client = OracleClient::GetInstance();
        client->connect(_db_connect_name, dbset);
    }

    static bool get_devs_from_db()
    {
        Log::debug("loadDevicesFromOracle begin");
        conn_pool_ptr pool = OracleClient::GetInstance()->get_conn_pool(_db_connect_name);
        if (pool == NULL) {
            Log::error("fail to get pool: basic");
            return false;
        }
        int index = pool->getConnection();
        if (index == -1) {
            Log::error("fail to get connection: basic");
            return false;
        }
        OCI_Connection *cn = pool->at(index);
        OCI_Statement *st = OCI_CreateStatement(cn);

        //查询设备信息
        const char *sql = "select t.FACILITY_ID,t.ACCESS_TYPE,t.IP_ADDRESS,"
            "t.PORT,t.USER_NAME,t.PASSWORD,t.CHANEL from VIDEO_INFO t"
            " where t.IP_ADDRESS is not null and t.IS_USABLE = 1"
            " order by t.FACILITY_ID";

        OCI_ExecuteStmt(st, sql);
        OCI_Resultset *rs = OCI_GetResultset(st);
        while (OCI_FetchNext(rs))
        {
            DEV_INFO dev;
            dev.ssid      = OCI_GET_STRING(rs,1);
            dev.dev_type  = (devType)stoi(OCI_GET_STRING(rs,2));
            dev.ip        = OCI_GET_STRING(rs,3);
            dev.port      = 554;//stoi(OCI_GET_STRING(rs,4));
            dev.user_name = OCI_GET_STRING(rs,5);
            dev.password  = OCI_GET_STRING(rs,6);
            dev.channel   = stoi(OCI_GET_STRING(rs,7));
            dev.stream    = stoi(Settings::getValue("DataBase","Mode"));
            _map_devs.insert(make_pair(dev.ssid, dev));

            stringstream ss;
            ss << "ssid:" << dev.ssid <<" devtype:" << dev.dev_type << " ip:" << dev.ip
                << " port:" << dev.port << " user:" << dev.user_name << " pwd:" << dev.password
                << " chanel:" << dev.channel << " stream:" << dev.stream;
            Log::debug(ss.str().c_str());
        }

        OCI_FreeStatement(st);
        pool->releaseConnection(index);
        Log::debug("get devs from db finish successful");
        return true;
    }

    static DEV_INFO find_dev(string ssid) {
        auto it = _map_devs.find(ssid);
        if (it != _map_devs.end())
        {
            return it->second;
        }
        DEV_INFO ret;
        ret.ssid = "err";
        return ret;
    }

    void Init(){
        init_db();
        get_devs_from_db();
        SAFE_DELETE(client);
    }

    string GetRtspAddr(string devid){
        DEV_INFO dev = find_dev(devid);
        char rtsp[100] = {0};
        if(dev.dev_type == DEV_HIK) {
            sprintf_s(rtsp, 100, "rtsp://%s:%s@%s:%d/h264/ch/%s/av_stream"
                , dev.user_name.c_str(), dev.password.c_str()
                , dev.ip.c_str(), dev.port, dev.stream==0?"main":"sub");
		} else if(dev.dev_type == DEV_UNIVIEW) {
			sprintf_s(rtsp, 100, "rtsp://%s:%s@%s:%d/video%d"
                , dev.user_name.c_str(), dev.password.c_str()
                , dev.ip.c_str(), dev.port, dev.stream+1);
		}
        return rtsp;
    }
}