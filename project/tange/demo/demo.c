#include "TgCloudApi.h"  // <-- 唯一必须包含的头文件

#include "platforms.h"
#include "logfile.h"
#include "ipc_tool.h"

#include "project_config.h"
#include "syscfg.h"
#include "lib/umac/ieee80211.h"

/* SD 卡录像/回放模块 API */
#include "rec_playback.h"
#if BLE_SUPPORT
#include "ble_tange_netcfg.h"   /* BLE 蓝牙配网 (探鸽云协议) */
#endif
#include "osal/time.h"
#include "osal/work.h"

enum{
    E_NET_NONE,
    E_NET_AP,
    E_NET_STA
};

extern volatile uint8 ipc_inited;
extern void mcu_reset(void);
extern void snapshot_release(void *jpg_data);
extern int snapshot_capture(uint8_t **out_data, uint32_t *out_len, uint32_t timeout_ms);
extern void event_demo_exit(void);

struct IpcLic g_Lic;
struct IpcWorkParam g_IpcParam;

static uint8_t registered = 0;
volatile uint8_t time_sync_flag = 0;
static uint8_t *md_jpg_out_data = NULL;
static uint8_t *call_jpg_out_data = NULL;
static uint8_t net_state = 0; //0:not ready; 1:ap started; 2: lan ok

void SetNetworkState(int state)
{
    net_state = state;
}
int GetNetworkState(void)
{
    return net_state;
}

//------------ 以下是要注册到SDK的SDK内部事件回调函数 ------------------------

/* 设备能力级. 完整描述参见文档 */
int get_device_feature(const char* key, char* buf, int bytes)
{
    _os_printf("%s, get %s\n", __FUNCTION__, key);
    if (strcmp(key, "Microphone") == 0) {
        strncpy(buf, "Yes", bytes);
    }
//    if(strcmp(key, "Resolutions") == 0){
//        strncpy(buf, "0HD", bytes);
//    }
/*
    else if (strcmp(key, "SupportPTZ") == 0) {
        strncpy(buf, "Yes", bytes);
    }
    else if (strcmp(key, "AutoTracking") == 0)
    {
        strncpy(buf, "No", bytes);
    }
    else if(strcmp(key, "DayNight") == 0)
    {
        strncpy(buf, "Yes", bytes);
    }
    else if (strcmp(key, "DoubleLight") == 0) {
        strncpy(buf, "Yes", bytes);
    }
    else if (strcmp(key, "AlertSound") == 0) {
        strncpy(buf, "Yes", bytes);
    }
    else if(strcmp(key, "MD-Capabilities") == 0) {
        strncpy(buf, "No", bytes);
    }
    else if(strcmp(key, "Cap-Defence") == 0) {
        strncpy(buf, "bundle", bytes);
    }
    else if(strcasecmp(key, "4G") == 0) {
        strcpy(buf, "No");
        //strcpy(buf, "iccid:8986031846206443846H");
    }
    else if(strcasecmp(key, "ExtInstructions") == 0) {
        strcpy(buf, "close-device");
    }
    else if(strcasecmp(key, "RecordConf") == 0) {
        strcpy(buf, "Yes");
    }
*/
    else if(strcasecmp(key, "RecordConf") == 0) {
        strcpy(buf, "Yes");  //支持设置卡录像模式，全时或事件
    }

    else if(strcasecmp(key, "BatteryCam") == 0) {
        return -1;//strcpy(buf, "Solar"); //Should be: Dormant
    }
    else if(strcasecmp(key, "DeviceType") == 0) {
        //strcpy(buf,  "LockBell");
        strcpy(buf, "IPC");
    }
	else if(strcasecmp(key, "AudioFmt") == 0)
		strcpy(buf, "g711a:8000:16:1");
    else if(strcasecmp(key, "EventSet") == 0)
        strcpy(buf, "[65536,0]");  //上报doorbell呼叫事件能力
    else {
        return -1;
    }

	return 0;
}

/** @name 设备参数默认值
  */
int get_device_state(const char* key, char* buf, int bytes)
{
    //default cloud storage channel and stream
    if(strcasecmp(key, "CVideoQuality") == 0)
    {
        //"stream:channel"
        //stream: 0-main, 1-sub

#if defined (__TXW826__)      //826 内存不够，云存储上传子码流
        strcpy(buf, "1:0");
#elif defined(__TXW828__)
        strcpy(buf, "0:0");
#endif
    }
    //default p2p channel and stream
    else if(strcasecmp(key, "streamquality") == 0)
    {
        //"stream:channel"
        //stream: 0-main, 1-sub
        strcpy(buf, "0:0");
    }
    else if(strcasecmp(key, "mac") == 0)
    {
        extern struct sys_config sys_cfgs;
        uint8_t *mac = sys_cfgs.mac;
        sprintf(buf, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    else
    {
        return -1;
    }

	return 0;
}

int get_info(TCIDEVICEINFO *info)
{   //TODO
    strcpy(info->firmware_id, "aj_ipc_TX_002"); //由平台分配
    strcpy(info->vendor, "OEM");
    strncpy(info->device_type, "IPC-xxx", sizeof(info->device_type)); //未用到. 设备类型要在 get_featur("DeviceType", ...) 回调里返回
    strcpy(info->firmware_ver, "01000001"); //固件版本
    strcpy(info->model, "TXW82x-IPC-001"); //产品型号
    return 0;
}

int on_apmode_login(const char *user, const char *key)
{
    return 0;
}

//-----------------------------------------------------------
static unsigned total_size = 0;
int on_ota_download_start(const char *new_version, unsigned int size)
{
    LogV("new_version = %s, size = %u\n", new_version, size);
    total_size = size;

	/*应用在此停止录像、清除缓存等相关资源释放*/
	LogV("stop record\n");
    rec_set_mode(REC_MODE_OFF);


    /* 分配内存或创建文件，准备在之后写入升级包数据 */


	return 0; //准备好升级后返回0
}

int on_ota_download_data(const uint8_t *buff, int size)
{
    static unsigned download_size = 0;
    download_size += size;
	LogV("size:%d, download_size=%u, total_size=%u\n", size, download_size, total_size);
	//写入升级包数据

    return 0;
}

int on_ota_download_finished(int status)
{
	LogV("status:%d\n", status);
	if(status == 0) //ok
	{
		_ok("success!!\n");
		//.关闭看门狗，执行升级
	}
	else //if(status == 1)
	{
		//下载失败，基本应该重启
	}
    return 0;
}
//-----------------------------------------------------------


static void PrintfCallState(int state)
{
    char line[128];
    line[0] = '\0';;
    switch(state >> 16)
    {
    case 0: strcpy(line, "doorbell "); break;
    case 1: strcpy(line, "wxvoip "); break;
    case 2: strcpy(line, "app call "); break;
    }
    switch(state & 0xffff)
    {
    case CALLSTATE_MISSED: strcat(line, ": missed\n"); break;
    case CALLSTATE_ANSWERED: strcat(line, ": answered\n"); break;
    case CALLSTATE_REJECTED: strcat(line, ": rejected\n"); break;
    case CALLSTATE_CANCELLED: strcat(line, ": cancelled\n"); break;
    case CALLSTATE_HANGUP: strcat(line, ": hangup\n"); break;
    case CALLSTATE_BUSY: strcat(line, ": busy\n"); break;
    }
    LogV("%s\n", line);
}
int on_status(int status, const void *pData,  int len)
{
	//LogI("on status : %d\r\n", status);

	switch(status)
	{
		case STATUS_LOGON:
			LogI("STATUS_LOGON\n");
			if(!(g_IpcParam.flags & IPCP_F_BIND_TO_USER)) {
                g_IpcParam.flags |= IPCP_F_BIND_TO_USER;
                IpctSaveSetting(&g_IpcParam);
			}
			break;
		case STATUS_LOGOFF:
			LogI("STATUS_LOGOFF\n");
			break;
		case STATUS_DELETED:
			LogI("STATUS_DELETED\n");
            _info("Reset setting and reboot....\n\n");
            IpctResetSetting(&g_IpcParam);
            ieee80211_iface_stop(WIFI_MODE_STA);
            os_sleep_ms(2000);
            mcu_reset();
			break;
        case STATUS_UPDATE_SERVICE:
            {
                TCISERVICEINFO *svc = (TCISERVICEINFO*)pData;
                LogI("STATUS_UPDATE_SERVICE type: %d, will expiration at utc time %ld\n", svc->serviceType, (long)svc->expiration);
            }
            break;
        case STATUS_AP_CONNECT:
            LogI("STATUS_AP_CONNECT\n");
            break;
        /*事件上传完成*/
        case STATUS_EVENT_CALLBACK:
            if(len == sizeof(EVENTREPORTNOTIFICATION)){
                EVENTREPORTNOTIFICATION *p = (EVENTREPORTNOTIFICATION*)pData;
                _os_printf("what:%d, state:%d\n", p->what, p->state);
                if(p->what == 0){
                    _os_printf("free jpg addr:%p\n", p->pic_addr);

                    if(p->pic_addr == md_jpg_out_data){
                        snapshot_release(md_jpg_out_data);
                        md_jpg_out_data = NULL;
                    }else if(p->pic_addr == call_jpg_out_data){
                        snapshot_release(call_jpg_out_data);
                        call_jpg_out_data = NULL;
                    }
                }else{
                    _os_printf("time:%u, event:%d, tag:%s\n", p->e.t_happen, p->e.event, p->e.tag);
                }
            }else{
                _os_printf("len = %d\n", len);
            }
            break;
		default:
			break;//LogI("unkown status [%d]\n", status);
	}
    return 0;
}

extern void wifi_start_sta(const char *ssid, const char *key, int key_mgmt);
extern void wifi_start_ap(const char *ssid, const char *key, int key_mgmt, int channel);
extern void wifi_stop_ap();
void wifi_setup_sta(const char *ssid, const char *key)
{
    wifi_start_sta(ssid, key, WPA_KEY_MGMT_PSK);
}
void wifi_setup_ap(const char *uuid)
{
	char ssid[64];
	sprintf(ssid, "AICAM_%s", uuid);
	wifi_start_ap(ssid, "", WPA_KEY_MGMT_NONE, 7);
}
int set_wifi(int is_switching, const char *ssid, const char *key)
{
    LogV("is_switching:%d. ssid:%s, key:%s\n", is_switching, ssid, key);
	wifi_setup_sta(ssid, key);
    int count = 60; //最多等60秒
    while(count--){
        os_sleep_ms(1000);
        if(net_state == E_NET_STA){
            break;
        }
    }
    if(net_state != E_NET_STA ){
        _err("network connect failed\n");
        //超时配网失败，有可能密码错误或其他原因，直接重启
        ieee80211_iface_stop(WIFI_MODE_STA);
        os_sleep_ms(2000);
        mcu_reset();
        return -1;
    }
    strncpy(g_IpcParam.wifi_ssid, ssid, sizeof(g_IpcParam.wifi_ssid));
    strncpy(g_IpcParam.wifi_key, key, sizeof(g_IpcParam.wifi_key));
    if(is_switching){ //-- 这个是添加后修改wifi配置，要立即保存。添加设备时可以放到on_status(STATUS_LOGON) 再保存
        IpctSaveSetting(&g_IpcParam);
    }

    return 0;
}
static int tzoffset(const char *tzs)
{
    if (tzs == NULL) return 0;

    const char *p = tzs;
    // 1. 跳过前导字母（如 "GMT" 或 "UTC"），找到 '+' 或 '-'
    while (*p && !(*p == '+' || *p == '-' || (*p >= '0' && *p <= '9'))) {
        p++;
    }

    // 2. 记录正负号
    int sign = 1;
    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }

    // 3. 解析小时
    char *next;
    long hours = strtol(p, &next, 10);

    // 4. 解析分钟（跳过冒号）
    long minutes = 0;
    if (*next == ':') {
        minutes = strtol(next + 1, NULL, 10);
    }

    // 5. 统一计算总秒数
    int o = sign * (hours * 60 + minutes) * 60;
    return o;
}

static int _setRtc(time_t time)
{
    struct tm *tm = gmtime(&time);
#if 0
    st.year = _tm.tm_year + 1900;
    st.month = _tm.tm_mon + 1;
    st.day = _tm.tm_mday;
    st.hour = _tm.tm_hour;
    st.min = _tm.tm_min;
    st.sec = _tm.tm_sec;
#endif
    //TODO: 保存时间到RTC;
    return 0;
}
int set_timezone(const char *tzs)
{
	LogV("timezone: %s\n", tzs);
    _tg_timezone_ = tzoffset(tzs);
    _setRtc(time(NULL) + _tg_timezone_);

    return 0;
}
int set_time(time_t time)
{
    LogV("set time to %u\n", (unsigned)time);
    struct timeval tv = {time, 0};
    settimeofday(&tv, NULL);
    _setRtc(time + _tg_timezone_);
    /* 通知卡录像模块时间已同步. rec_on_time_synced 内部只记基准 + 置标志,
     * 零 IO 零阻塞, UNSYNC 文件迁移由 rec_bootstrap_thread 异步做, 不卡回调. */
    rec_on_time_synced((uint32_t)time);
    time_sync_flag = 1;
    return 0;
}


int on_talkback_start();
int talkback(TCMEDIA at, const uint8_t *audio, int len);
void on_talkback_stop();

int request_iframe_ex(int channel, int vstream)
{
    LogV("Request I-Frame of stream %d:%d\n", channel, vstream);
    // vstream: 0-主码流, 1-辅码流 → dev_num: 1-主编码器, 2-副编码器
    h264_reflash_new_gop(vstream + 1, 1);
    return 0;
}
void transfer_stat(const struct TransStatUser *_stat)
{
    _info("%d:%d:%d:%s ==> Buffed:%d, Sent:%d, Thrown:%d, Total:%d, Interval:%d\n",
            (int)_stat->id, (int)_stat->vchannel, (int)_stat->vstream, _stat->is_igop?"full":"psec",
            _stat->nBytesInBuff, _stat->nBytesSent, _stat->nBytesThrow, _stat->nBytesTotal, _stat->msStatInterval);
}
int on_log(int action, char *path)
{
    switch(action)
    {
    case 0: _os_printf("start long log\n"); break;
    case 1: _os_printf("stop long log\n"); break;
    case 2: //if you have other file than /tmp/icam365.log, return its path
            strcpy(path, "/tmp/my.log");
            _os_printf("return my private file: %s\n", path);
            break;
    }
    return 0;
}

//-------------- p2p命令处理：_Handle_P2p_Cmd ---------------------
void switch_quality(int channel, int stream, const char *qstr)
{
	//do nothing
}

static void printClockTime(const CLOCKTIME *c) { _os_printf("%02d:%02d:%02d", c->hour, c->minute, c->second); }
static void printTimeRange(const TIMERANGE *r)
{
    printClockTime(&r->from); _os_printf(" -- "); printClockTime(&r->to);
}

#if defined(__TXW826__)
#define GET_LIST_MALLOC(sz)       _os_malloc(sz)
#define GET_LIST_FREE(p)          do { if(p) { _os_free(p); (p)=NULL; } } while(0)
#else
#define GET_LIST_MALLOC(sz)       _os_malloc_psram(sz)
#define GET_LIST_FREE(p)          do { if(p) { _os_free_psram(p); (p)=NULL; } } while(0)
#endif

//p2p命令处理入口。注册到SDK
int Handle_P2p_Cmd(p2phandle_t handle, int cmd, const void *buf, int size)
{
	switch(cmd)
	{
		case TCI_CMD_LISTEVENT_REQ:
		{
			_os_printf("TCI_CMD_LISTEVENT_REQ\n");
			Tcis_ExListEventReq *p = (Tcis_ExListEventReq *)buf;

			time_t t_start = TcuTimeDay2T(&p->stStartTime);
			time_t t_end = TcuTimeDay2T(&p->stEndTime);
			_os_printf("from %d-%d-%d %d:%d:%d(utc=%d, event=%d) to %d-%d-%d %d:%d:%d(utc:%d)\n",
					p->stStartTime.year,p->stStartTime.month,p->stStartTime.day,p->stStartTime.hour,p->stStartTime.minute,p->stStartTime.second, t_start, p->event,
					p->stEndTime.year,p->stEndTime.month,p->stEndTime.day,p->stEndTime.hour,p->stEndTime.minute,p->stEndTime.second, t_end);

			/* 从本地索引获取录像列表 (已合并相邻同类事件) */
			SAvExEvent *items = NULL;
			int nRec = rec_list_get((uint32_t)t_start, (uint32_t)t_end, &items);
			if (nRec < 0) {
				_os_printf("rec_list_get failed\n");
				break;
			}
			_os_printf("%d records\n", nRec);
            int i;
            for(i = 0; i < nRec; i++){
                _os_printf("start_time:%d:%d:%d_%d:%d:%d, file_len:%d, event:%d\n",
                    items[i].start_time.year, items[i].start_time.month, items[i].start_time.day,
                    items[i].start_time.hour, items[i].start_time.minute, items[i].start_time.second, items[i].file_len, items[i].event);
            }

			/* 单次最多 50 条, 过多分多次发送 */
			#define MAX_PER_PKT  50
			int pkt_num = (nRec + MAX_PER_PKT - 1) / MAX_PER_PKT;
			if (pkt_num == 0) pkt_num = 1;

			/* resp buf 按 MAX_PER_PKT 的上限一次申请, 多包复用不反复 malloc/free.
			 * 实际每包的 resp_size 按 this_num 计算 (末包可能更小) */
			int resp_cap = sizeof(Tcis_ExListEventResp) + (MAX_PER_PKT - 1) * sizeof(SAvExEvent);
			Tcis_ExListEventResp *resp = (Tcis_ExListEventResp *) GET_LIST_MALLOC(resp_cap);
			if (!resp) {
				_os_printf("LISTEVENT resp malloc %d fail\n", resp_cap);
				if (items) GET_LIST_FREE(items);
				break;
			}

			int isrc = 0;
			for (int pk = 0; pk < pkt_num; pk++) {
				int this_num = nRec - isrc;
				if (this_num > MAX_PER_PKT) this_num = MAX_PER_PKT;
				int resp_size = sizeof(Tcis_ExListEventResp) + (this_num > 0 ? this_num - 1 : 0) * sizeof(SAvExEvent);

				/* 只清 header, stExEvent 后面 for 循环会逐个拷贝覆盖 */
				memset(resp, 0, sizeof(Tcis_ExListEventResp));
				resp->channel = 0;
				resp->num     = nRec;       /* 总数 */
				resp->count   = this_num;   /* 本包数量 */
				resp->index   = pk;
				resp->endflag = (pk == pkt_num - 1) ? 1 : 0;
				resp->estype  = 0;           /* SAvExEvent 格式 */
				for (int i = 0; i < this_num && items; i++) {
					resp->stExEvent[i] = items[isrc++];
				}
				TciSendCmdResp(handle, TCI_CMD_LISTEVENT_RESP, resp, resp_size);
			}

			GET_LIST_FREE(resp);
			if (items) GET_LIST_FREE(items);
		}
		break;

        case TCI_CMD_LIST_RECORDDAYS:
        {
            _os_printf("TCI_CMD_LIST_RECORDDAYS\n");

            SDay *days = NULL;
            int n = rec_list_days_get(&days);
            if (n < 0) n = 0;

            /* 应答结构: Tcis_DaysList { int n_day; SDay days[n]; }
             * days 数组是变长, 要按实际个数申请 */
            uint32_t resp_size = sizeof(Tcis_DaysList) + (n > 0 ? (n - 1) * sizeof(SDay) : 0);
            Tcis_DaysList *resp = (Tcis_DaysList *) GET_LIST_MALLOC(resp_size);
            if (!resp) {
                _os_printf("TCI_CMD_LIST_RECORDDAYS: malloc %u fail\n", resp_size);
                if (days) _os_free_psram(days);
                break;
            }
            memset(resp, 0, resp_size);
            resp->n_day = n;
            for (int i = 0; i < n; i++) {
                resp->days[i] = days[i];
            }
            _os_printf("LIST_RECORDDAYS: %d days\n", n);

            TciSendCmdResp(handle, TCI_CMD_LIST_RECORDDAYS, (char *) resp, resp_size);

            GET_LIST_FREE(resp);
            if (days) GET_LIST_FREE(days);
        }
        break;
		case TCI_CMD_RECORD_PLAYCONTROL:
		{
			Tcis_PlayRecord *p = (Tcis_PlayRecord *)buf;
			Tcis_PlayRecordResp res;
            _os_printf("TCI_CMD_RECORD_PLAYCONTROL, sub command:%d\n", p->command);

			res.command = p->command;
			res.result = 0;
			if(p->command == TCIC_RECORD_PLAY_START) {
				_os_printf("TCIC_RECORD_PLAY_START: %u-%u-%u %u:%u:%u, Param=%u\n",
                        p->stTimeDay.year,p->stTimeDay.month,p->stTimeDay.day,p->stTimeDay.hour,p->stTimeDay.minute,p->stTimeDay.second, p->Param);
				uint32_t t_seek = (uint32_t) TcuTimeDay2T(&p->stTimeDay);
				uint8_t mode_bit = (p->Param >> 1) & 0x01;  /* bit1: 0连续 1事件 */
				if (pb_start(handle, t_seek, mode_bit) != 0)
					res.result = -1;
			}
			else if(p->command == TCIC_RECORD_PLAY_PAUSE) {
				_os_printf("TCIC_RECORD_PLAY_PAUSE\n");
				pb_pause();
			}
			else if(p->command == TCIC_RECORD_PLAY_CONTINUE) {
				_os_printf("TCIC_RECORD_PLAY_CONTINUE\n");
				pb_resume();
			}
			else if(p->command == TCIC_RECORD_PLAY_STOP) {
				_os_printf("TCIC_RECORD_PLAY_STOP\n");
				pb_stop();
			}
            else if(p->command == TCIC_RECORD_PLAY_FORWARD) {
				_os_printf("TCIC_RECORD_PLAY_FORWARD Param=%u\n", p->Param);
				pb_set_forward(p->Param);
            }
            else
                res.result = -3;

			TciSendCmdResp(handle, TCI_CMD_RECORD_PLAYCONTROL_RESP, (char *)&res, sizeof(Tcis_PlayRecordResp));
		}
		break;
        case TCI_CMD_GET_EXTERNAL_STORAGE_REQ:
            {
                _os_printf("TCI_CMD_GET_EXTERNAL_STORAGE_REQ\n");
                Tcis_SDCapResp resp;
                memset(&resp, 0, sizeof(resp));
                resp.channel = 0;
                uint32_t total_mb = 0, free_mb = 0;
                sd_get_capacity(&total_mb, &free_mb);
                resp.total = total_mb;
                resp.free  = free_mb;
                _os_printf("TCI_CMD_GET_EXTERNAL_STORAGE %d/%d MB\n", resp.free, resp.total);
                TciSendCmdResp(handle, TCI_CMD_GET_EXTERNAL_STORAGE_RESP, (char *)&resp, sizeof(Tcis_SDCapResp));
            }
            break;

        case TCI_CMD_FORMATEXTSTORAGE_REQ:
            {
                _os_printf("TCI_CMD_FORMATEXTSTORAGE_REQ ...\n");
                sd_format(handle);   /* 内部会停录像 + 格式化 + 恢复录像 */
            }
            break;
        case TCI_CMD_GETRECORD_REQ:
        {
            _os_printf("TCI_CMD_GETRECORD_REQ\n");
            Tcis_GetRecordResp resp;
			memset(&resp, 0, sizeof(resp));
			resp.recordType = (unsigned char) rec_get_mode();
            TciSendCmdResp(handle, TCI_CMD_GETRECORD_RESP, (char *)&resp, sizeof(Tcis_GetRecordResp));
        }
        break;
		case TCI_CMD_SETRECORD_REQ:
		{
			_os_printf("TCI_CMD_SETRECORD_REQ\n");
			Tcis_SetRecordReq *req = (Tcis_SetRecordReq*)buf;
			Tcis_SetRecordResp resp;
			_os_printf("recordType=%d, recordStream=%d, flags=%d\n",req->recordType, (int)req->recordStream, (int)req->flags);
			resp.result = (rec_set_mode((rec_mode_t)req->recordType) == 0) ? 0 : 1;
			TciSendCmdResp(handle, TCI_CMD_SETRECORD_RESP, (char *)&resp, sizeof(Tcis_SetRecordResp));
		}
		break;

		case TCI_CMD_LISTWIFIAP_REQ:
		{
			_os_printf("TCI_CMD_LISTWIFIAP_REQ\n");
			//TODO
			//获取wifi列表
			Tcis_ListWifiApResp resp;
            resp.number = 1;
            strcpy(resp.stWifiAp[0].ssid, "TG-2.4G");
            resp.stWifiAp[0].enctype = TCIC_WIFIAPENC_WPA2_PSK_TKIP;
            resp.stWifiAp[0].signal = 100;
            resp.stWifiAp[0].status = 0;
            resp.stWifiAp[0].mode =  1;
			_os_printf("TCI_CMD_LISTWIFIAP_REQ n_ap: 1\n");
			TciSendCmdResp(handle, TCI_CMD_LISTWIFIAP_RESP, (char *)&resp, sizeof(resp));
		}
		break;
		case TCI_CMD_GET_VIDEOMODE_REQ: // 查询画面翻转模式
		{
			_os_printf("TCI_CMD_GET_VIDEOMODE_REQ\n");
			int MirrorMode = 0;
			Tcis_GetVideoModeReq* req = (Tcis_GetVideoModeReq*)buf;
			Tcis_GetVideoModeResp resp;
			memset(&resp, 0, sizeof(resp));
			//TODO
			MirrorMode = 3;
			resp.channel = req->channel;
			resp.mode = MirrorMode; //0正向，3旋转180°
			TciSendCmdResp(handle, TCI_CMD_GET_VIDEOMODE_RESP, (char *)&resp, sizeof(Tcis_GetVideoModeResp));
			return 0;
		}
		break;
		case TCI_CMD_SET_VIDEOMODE_REQ :
		{
			_os_printf("TCI_CMD_SET_VIDEOMODE_REQ\n");
			Tcis_SetVideoModeReq* req = (Tcis_SetVideoModeReq*)buf;
			Tcis_SetEnvironmentResp resp;
			memset(&resp,0,sizeof(Tcis_SetEnvironmentResp));
			_os_printf("req.channel = [%d], req.mode = [%d]\n",req->channel, req->mode);
			// TODO
			// 设置画面翻转模式
			resp.result = 0;
			TciSendCmdResp(handle, TCI_CMD_SET_VIDEOMODE_RESP, (char *)&resp, sizeof(Tcis_SetEnvironmentResp));
		}
		break;
		case TCI_CMD_GET_ENVIRONMENT_REQ: // 查询工频
		{
			_os_printf("TCI_CMD_GET_ENVIRONMENT_REQ\n");
			Tcis_GetEnvironmentReq* req = (Tcis_GetEnvironmentReq*)buf;
			Tcis_GetEnvironmentResp resp;
			memset(&resp, 0, sizeof(resp));
			//TODO
			resp.mode = 0; //1-60Hz, 0-50Hz
			TciSendCmdResp(handle, TCI_CMD_GET_ENVIRONMENT_RESP, (char *)&resp, sizeof(resp));
			return 0;
		}
		break;
		case TCI_CMD_SET_ENVIRONMENT_REQ:
		{
			_os_printf("TCI_CMD_SET_ENVIRONMENT_REQ\n");
			Tcis_SetEnvironmentReq* req = (Tcis_SetEnvironmentReq*)buf;
			_os_printf("<----- Set Environment mode is [%d]--channel is [%d]--->\r\n", req->mode, req->channel);

			Tcis_SetEnvironmentResp resp;
			memset(&resp,0,sizeof(Tcis_SetEnvironmentResp));
			resp.channel = req->channel;
			resp.result = 0;
			TciSendCmdResp(handle, TCI_CMD_SET_ENVIRONMENT_RESP, (char *)&resp, sizeof(Tcis_SetEnvironmentResp));
			// TODO
			// 开发者实现， 设置电源频率
		}
		break;
		case TCI_CMD_GETWIFI_REQ:
		{
			SWifiAp resp;
			memset(&resp, 0x0, sizeof(resp));
            strcpy(resp.ssid, g_IpcParam.wifi_ssid);
			//TODO: 获取设备当前wifi信号强度
            resp.signal = 75;
			_os_printf("current ssid:%s, signal:%d\n", resp.ssid, resp.signal);
			TciSendCmdResp(handle, TCI_CMD_GETWIFI_RESP, (char *)&resp, sizeof(SWifiAp));
		}
		break;
#if 1  //有云台时才支持如下命令
		case TCI_CMD_PTZ_SHORT_COMMAND:
		{
			_os_printf("TCI_CMD_PTZ_SHORT_COMMAND\n");
			Tcis_PtzShortCmd *p = (Tcis_PtzShortCmd *)buf;
			// 云台短按
			LogV("x:%d, y:%d, z:%d\n", p->space.x, p->space.y, p->space.zoom);
		}
		break;
		case TCI_CMD_PTZ_LONG_COMMAND:
		{
			_os_printf("TCI_CMD_PTZ_LONG_COMMAND\n");
			Tcis_PtzCmd *p = (Tcis_PtzCmd*)buf;

			_os_printf("ptz control = [%d]\n",p->control);
			// TODO
		}
		break;

        case TCI_CMD_SET_PTZ_POS:
        {
            Tcis_SetPtzPosReq *req = (Tcis_SetPtzPosReq*)buf;
            _os_printf("TCI_CMD_SET_PTZ_POS: x=%0.2f, y=%.2f, z=%.2f\n", req->pos.x, req->pos.y , req->pos.z);
            //move to the pos...
            TciSendCmdRespStatus(handle, cmd, 0);
        }
        break;
        case TCI_CMD_GET_PTZ_POS:
        {
            _info("TCI_CMD_GET_PTZ_POS");
            Tcis_GetPtzPosResp resp;
            resp.pos.x = 0.5;
            resp.pos.y = -1.0;   //ignored
            resp.pos.z = 0.5;
            TciSendCmdResp(handle, cmd, (const char*)&resp, sizeof(resp));
        }
        break;
#endif
		case TCI_CMD_DEV_REBOOT_REQ:
		{
			_os_printf("TCI_CMD_DEV_REBOOT_REQ\n");
			Tcis_DevRebootReq *req = (Tcis_DevRebootReq *)buf;
			Tcis_DevRebootResp resp;
			resp.result = 0;
			TciSendCmdResp(handle, TCI_CMD_DEV_REBOOT_RESP, (char *)&resp, sizeof(Tcis_DevRebootResp));

			//其它清理操作 ....
			rec_set_mode(REC_MODE_OFF);

            os_sleep_ms(1000);
			// 重启设备
			mcu_reset();
		}
		break;
#if 0 //没有白光灯
		case TCI_CMD_GET_DOUBLELIGHT_REQ:
		{
			_os_printf("TCI_CMD_GET_DOUBLELIGHT_REQ\n");
			Tcis_GetDoubleLightResp resp;

			memset(&resp, 0, sizeof(resp));
			//TODO
			//resp.support = 2;
			//resp.mode = ? //当前模式，0-关闭（白光不工作）;1-打开（全彩色）;2-智能模式（移动侦测触发自动白光）
			_os_printf("mode:%d, support:%d\n", resp.mode, resp.support);
			TciSendCmdResp(handle, TCI_CMD_GET_DOUBLELIGHT_RESP, (char *)&resp, sizeof(Tcis_GetDoubleLightResp));
		}
		break;
		case TCI_CMD_SET_DOUBLELIGHT_REQ:
		{
			_os_printf("TCI_CMD_SET_DOUBLELIGHT_REQ\n");
			Tcis_SetDoubleLightReq *req = (Tcis_SetDoubleLightReq *)buf;
			Tcis_SetDoubleLightResp resp;

			_os_printf("recvive double light mode:%d\n", req->mode);
			// TODO
			// 设置双光模式
			TciSendCmdResp(handle, TCI_CMD_SET_DOUBLELIGHT_RESP, (char *)&resp, sizeof(Tcis_SetDoubleLightResp));
		}
		break;
#endif
		case TCI_CMD_GET_DAYNIGHT_REQ:
		{
			_os_printf("TCI_CMD_GET_DAYNIGHT_REQ\n");
			Tcis_GetDayNightResp resp;
			memset(&resp, 0, sizeof(resp));
			//TODO
			//resp.support = 2;
			//resp.mode = ?
			_os_printf("DayNight support:%d mode:%d\n", resp.support, resp.mode);
			TciSendCmdResp(handle, TCI_CMD_GET_DAYNIGHT_RESP, (char *)&resp, sizeof(resp));
			return 0;
		}
		case TCI_CMD_SET_DAYNIGHT_REQ:
		{
			_os_printf("TCI_CMD_SET_DAYNIGHT_REQ\n");
			Tcis_SetDoubleLightReq *req = (Tcis_SetDoubleLightReq *)buf;
			Tcis_SetDoubleLightResp resp;
			_os_printf("receive daynight mode:%d\n", req->mode);
			// TODO
			// 设置夜视模式
			resp.result = 0;
			TciSendCmdResp(handle, TCI_CMD_SET_DAYNIGHT_RESP, (char *)&resp, sizeof(Tcis_SetDoubleLightResp));
		}
		break;

#if 0
		case TCI_CMD_SET_MICROPHONE_REQ:
			LogI("TCI_CMD_SET_MICROPHONE_REQ\n");
			LogI("status:%d\n", ((Tcis_SetMicroPhoneReq *)buf)->status); //req->status: //麦克风 0- 关, 1- 开
            TciSendCmdRespStatus(handle, cmd, 0);
		    break;
		case TCI_CMD_GET_BUZZER_REQ:
		{
			LogI("TCI_CMD_GET_BUZZER_REQ\n");
			Tcis_GetBuzzerResp resp;
			resp.status = 1; //蜂鸣器 status: 0- 关, 1- 开
			TciSendCmdResp(handle, TCI_CMD_GET_BUZZER_RESP, (char *)&resp, sizeof(resp));
		}
		break;
		case TCI_CMD_SET_BUZZER_REQ:
		{
			LogI("TCI_CMD_SET_BUZZER_REQ\n");
			Tcis_SetBuzzerReq *req = (Tcis_SetBuzzerReq *)buf;
			LogI("status:%d\n", req->status);
			Tcis_SetBuzzerResp resp;
			//req->status: 0- 关, 1- 开
			resp.result = 0;
			TciSendCmdResp(handle, TCI_CMD_SET_BUZZER_RESP, (char *)&resp, sizeof(resp));
		}
		break;
#endif
		//p2p 异常断开时候，应用可做相关处理
		case TCI_CMD_SESSION_CLOSE:
		{
			//如停止正在进行的SD回放
			_os_printf("TCI_CMD_SESSION_CLOSE\n");
		}
		break;
#if 0
        case TCI_CMD_SETMOTIONDETECT_REQ:
        {
            const Tcis_SetMotionDetectReq *req = (const Tcis_SetMotionDetectReq*)buf;
            Tcis_SetMotionDetectResp resp;
#if 0
            _os_printf("motion detection setting:\n");
            _os_printf("\tenabled: %d\n", req->enabled);
            if(req->enabled)
            {
                _os_printf("\tflags: %d\n\tsensitivity: %d\n\thasZone: %d\n\tnZones: %d\n", req->flags, req->sensitivity, req->hasZone, req->nZones);
                if(req->hasZone && req->nZones)
                {
                    int i;
                    for(i=0; i<req->nZones; i++)
                    {
                        const MdZone *z = &req->zones[i];
                        _os_printf("\t\t<%d,%d>-<%d,%d>\n", z->left, z->top, z->left+z->width, z->top+z->height);
                    }
                }
            }
#endif
            memset(&resp, 0, sizeof(resp));
            resp.result = 0;
            TciSendCmdResp(handle, cmd|1, (char*)&resp,sizeof(resp));
        }
        break;
        case TCI_CMD_GETMOTIONDETECT_REQ:
        {
			LogI("TCI_CMD_GETMOTIONDETECT_REQ\n");
            Tcis_GetMotionDetectResp resp;
            memset(&resp, 0, sizeof(resp)); //default value
            resp.enabled = 1;
            resp.sensitivity = 1;
            TciSendCmdResp(handle, cmd|1, (char*)&resp, sizeof(resp));
        }
        break;
        case TCI_CMD_GET_DEFENCE_REQ:
        {
            LogI("TCI_CMD_GET_DEFENCE_REQ\n");
            struct {
                Tcis_GetDefenceResp resp;
                DEFENCEITEM item[1];
            } _dflt = //默认设置, 所有功能全天便能
            {
                { 1 },
                {
                    {
                        { { { 0, 0, 0, 0 }, {23, 59, 59, 0 } } },
                        ECEVENT_ALL,
                        0,
                        0x7f
                    }
                }
            };
            TciSendCmdResp(handle, cmd, (const char*)&_dflt, sizeof(_dflt));
            break;
        }
        case TCI_CMD_SET_DEFENCE_REQ:
        {
            Tcis_SetDefenceReq *req = (Tcis_SetDefenceReq*)buf;
            LogI("TCI_CMD_SET_DEFENCE_REQ: %d items\n", req->nItems);
            int i;
            for(i=0; i<req->nItems; i++)
            {
                if(req->items[i].u.time_range.from.hour == 0xff)
                    _os_printf("At %s ", req->items[i].u.tr2.spec_time == TR2_S_DAY?"day":"night");
                else
                    printTimeRange(&req->items[i].u.time_range);
                _os_printf(" --- event mask: 0x%08x, day mask: 0x%02x\n", req->items[i].event_mask, req->items[i].day_mask);
            }
            TciSendCmdRespStatus(handle, TCI_CMD_SET_DEFENCE_REQ, 0);
        }
        break;
        case TCI_CMD_SET_DEVICE_STATUS:
        {
            Tcis_SetDeviceStatusReq *req = (Tcis_SetDeviceStatusReq*)buf;
            LogI("TCI_CMD_SET_DEVICE_STATUS: %d\n", req->status);
            TciSendCmdRespStatus(handle, TCI_CMD_SET_DEVICE_STATUS, 0);
        }
        break;

        case TCI_CMD_GET_ALARMTONE_CAP:
            _os_printf("TCI_CMD_GET_ALARMTONE_CAP\n"); {
                Tcis_GetAlarmToneCap_Resp resp;
                resp.nSamplePerSec = 8000;
                resp.nBitsPerSample = 16;
                resp.nChannels = 1;
                resp.nExpectedFileFormats = 1;
                resp.ExpectedFileFormats[0] = AF_FMT_WAV;
                resp.nSupportedAudioCodecs = 2;
                resp.SupportedAudioCodecs[0] = TCMEDIA_AUDIO_G711A;
                resp.SupportedAudioCodecs[1] = TCMEDIA_AUDIO_PCM;
                resp.idAlarmTone = 0; //init default
                resp.uiFileSizeLmt = 100;
                TciSendCmdResp(handle, cmd, (char*)&resp, sizeof(resp));
            }
            break;
        case TCI_CMD_PLAY_ALARMTONE:
            _os_printf("TCI_CMD_PLAY_ALARMTONE\n");
            //play the alarm-tone
            TciSendCmdRespStatus(handle, cmd, 0);
            break;
#if 0
        case TCI_CMD_SET_ALARMTONE: {
            Tcis_SetAlarmTone_Req *req = (Tcis_SetAlarmTone_Req*)buf;
            _os_printf("TCI_CMD_SET_ALARMTONE, id=%d, type=%d, af_fmt=%d, a_codec=%d\n", req->id, req->type, req->af_fmt, req->a_codec);
            if(req->id == 0)
                SA_remove("alarmton.dat"); //use default alarm-tone
            else {
                if(req->type == 1) //url
                {
                    extern int http_simple_get_file(const char *url, int timeout, const char *fname);

                    char *url = (char*)My_malloc(req->data_len + 1);
                    memcpy(url, req->data, req->data_len);
                    url[req->data_len] = '\0';
                    if(http_simple_get_file(url, 8000, "alarmton.dat") != 0)
                    {
                        //failed...
                    }
                    My_free(url);
                }
                else if(req->type == 0) {
                    FILE *fp = SA_fopen("alarmton.dat", "wb");
                    if(fp) {
                        SA_fwrite(req->data, 1, req->data_len, fp);
                        SA_fclose(fp);
                    }
                }
            }
            //save the tone file...
            TciSendCmdRespStatus(handle, cmd, 0);
            }
        break;
#endif
        case TCI_CMD_SET_LED_STATUS:
            {
                Tcis_SetLedStatusReq *req = (Tcis_SetLedStatusReq*)buf;
                _os_printf("TCI_CMD_SET_LED_STATUS, status = %d\n", req->status);
                if(req->status)
                    g_IpcParam.flags |= IPCP_F_STATUS_LED_OFF;
                else
                    g_IpcParam.flags &= ~IPCP_F_STATUS_LED_OFF;
                IpctSaveSetting(&g_IpcParam);
                TciSendCmdRespStatus(handle, cmd, 0);
            }
        break;
        case TCI_CMD_GET_LED_STATUS:
            {
                _os_printf("TCI_CMD_GET_LED_STATUS\n");
                Tcis_GetLedStatusResp resp;
                memset(&resp, 0, sizeof(resp));
                resp.status = !(g_IpcParam.flags & IPCP_F_STATUS_LED_OFF);
                TciSendCmdResp(handle, cmd, (char *)&resp, sizeof(resp));
            }
        break;
#endif

        case TCI_CMD_GET_BATTERY_STATUS:
            {
                _os_printf("TCI_CMD_GET_BATTERY_STATUS\n");
                Tcis_GetBatteryStatusResp resp;
                resp.batteryMode = 0;  //0-放电，1-充电
                resp.batteryPower = 50; //0-100(%),-1(未知)
                TciSendCmdResp(handle, cmd, (char *)&resp, sizeof(resp));
            }
        break;

        case TCI_CMD_GET_WIFI_SIGNALLEVEL:
            {
                _os_printf("TCI_CMD_GET_WIFI_SIGNALLEVEL\n");
                Tcis_GetWifiLevelResp resp;
                resp.activeNetIntf = 1;     //0-有线，1-WiFi，2-4G
                resp.signalLevel = 100;     //0-100(%),-1(未知)
                TciSendCmdResp(handle, cmd, (char*)&resp, sizeof(resp));
            }
        break;

        case TCI_CMD_GET_MAX_AWAKE_TIME:
            {
                Tcis_GetMaxAwakeTimeResp resp;
                memset(&resp, 0, sizeof(resp));
                resp.max_awake_time = 20;
                TciSendCmdResp(handle, cmd, &resp, sizeof(resp));
            }
            break;
        case TCI_CMD_SET_MAX_AWAKE_TIME:
            {
                Tcis_SetMaxAwakeTimeReq *req = (Tcis_SetMaxAwakeTimeReq *)buf;
                _os_printf("TCI_CMD_SET_MAX_AWAKE_TIME max time = %d\n", req->max_awake_time);
                TciSendCmdRespStatus(handle, cmd, 0);
            }
        break;

#if 0
        case TCI_CMD_SET_CLOSE_PLAN:
            {
                Tcis_SetClosePlanReq *req = (Tcis_SetClosePlanReq *)buf;
                _os_printf("TCI_CMD_SET_CLOSE_PLAN nItems = %d\n", req->nItems);
                int i;
                for(i = 0;i<req->nItems;i++)
                {
                    _os_printf("plan[%d] from [%d:%d:%d] to [%d:%d:%d]\n", i, \
                            req->items[i].time_range.from.hour, req->items[i].time_range.from.minute, req->items[i].time_range.from.second,\
                            req->items[i].time_range.to.hour, req->items[i].time_range.to.minute, req->items[i].time_range.to.second);
                }
                TciSendCmdRespStatus(handle, cmd, 0);
            }
        break;
        case TCI_CMD_GET_CLOSE_PLAN:
            {
                _os_printf("TCI_CMD_GET_CLOSE_PLAN\n");
                Tcis_GetClosePlanResp resp;
                memset(&resp, 0, sizeof(resp));
                resp.nItems = 0;
                TciSendCmdResp(handle, cmd, (char*)&resp, sizeof(resp));
            }
        break;

            /* GET/SET 命令响应的简化写法 */
#define _SETTER_(cmd, S, _action_, fmt, args...)  case cmd: { \
            S *req = (S*)buf; \
            _action_; \
            LogI(#cmd": "fmt"\n", args); \
            IpctSaveSetting(&g_IpcParam); \
            TciSendCmdRespStatus(handle, cmd, 0); }\
            break

#define _GETDEF_(cmd, S, _init) case cmd: { \
            LogI(#cmd"\n"); \
            S resp = { }; _init; \
            TciSendCmdResp(handle, cmd, &resp, sizeof(resp)); } \
            break

        _SETTER_(TCI_CMD_SET_MIC_LEVEL, Tcis_SetMicLevelReq, g_IpcParam.mic_level=req->sensitivity, "sensitivity=%d", req->sensitivity);
        _GETDEF_(TCI_CMD_GET_MIC_LEVEL, Tcis_GetMicLevelResp, resp.sensitivity=g_IpcParam.mic_level);
#if 0
        _SETTER_(TCI_CMD_SET_VOLUME, Tcis_SetVolumeReq, g_IpcParam.spkr_volume=req->volume, "flags=%d, volume=%d", req->flags, req->volume);
        _GETDEF_(TCI_CMD_GET_VOLUME, Tcis_GetVolumeResp, resp.volume=90;resp.flags=1);

        _SETTER_(TCI_CMD_SET_ALARMLIGHT, Tcis_SetAlarmLightStateReq, do{}while(0), "state=%d", req->state);
        _GETDEF_(TCI_CMD_GET_ALARMLIGHT, Tcis_GetAlarmLightStateResp, resp.state=2);

        _SETTER_(TCI_CMD_SET_PIR, Tcis_SetPirSensReq, do{}while(0), "sens:%d", req->sens);
        _GETDEF_(TCI_CMD_GET_PIR, Tcis_GetPirSensResp, resp.sens=2);
#endif
        //_SETTER_(TCI_CMD_SET_ENABLE_DORMANCY, Tcis_DormancyState, do{}while(0), "enabled=%d", req->enable);
        //_GETDEF_(TCI_CMD_GET_ENABLE_DORMANCY, Tcis_DormancyState, resp.enable=1);
#endif
        case TCI_CMD_ANSWERTOCALL:
             _os_printf("TCI_CMD_ANSWERTOCALL, state=0x%x, more=%d\n", ((Tcis_AnswerToCall*)buf)->state, ((Tcis_AnswerToCall*)buf)->more);
             PrintfCallState(((Tcis_AnswerToCall*)buf)->state);
             break;

        case 0x80000000: //自定义的命令
            {char resp[512];
                memset(resp, 'A', 511);
                resp[511] = 0;
            TciSendCmdResp(handle, cmd, resp, 512);
            }
            break;
        default:
            TciSendCmdRespStatus(handle, cmd, TCI_E_UNSUPPORTED_CMD);
			_os_printf("non-handle cmd[%X]\n", cmd);
	}

	return 0;
}

static struct TciCB _tci_cb;

//g++版本低于4.7，不支持 .member = xxx 这样的初始化方式 -- song
static void init_tcicb()
{
    _tci_cb.get_info = get_info;
    _tci_cb.get_feature = get_device_feature;
    _tci_cb.get_state = get_device_state,
    _tci_cb.on_apmode_login = on_apmode_login;

//    _tci_cb.qrcode_start = on_qrcode_start;
//    _tci_cb.qrcode_get_y_data = on_get_y_data;
//    _tci_cb.qrcode_end = on_qrcode_end;

    _tci_cb.on_ota_download_start = on_ota_download_start;
    _tci_cb.on_ota_download_data = on_ota_download_data;
    _tci_cb.on_ota_download_finished = on_ota_download_finished;

    _tci_cb.on_talkback_start = on_talkback_start;
    _tci_cb.talkback = talkback;
    _tci_cb.on_talkback_stop = on_talkback_stop;

//    _tci_cb.snapshot = snapshot;
    _tci_cb.set_wifi = set_wifi;
    _tci_cb.set_timezone = set_timezone;
    _tci_cb.set_time = set_time;
    _tci_cb.on_status = on_status;

    //_tci_cb.request_iframe = request_iframe;
    _tci_cb.log = on_log;
    _tci_cb.request_iframe_ex = request_iframe_ex;
    _tci_cb.switch_quality = switch_quality;

    //_tci_cb.trans_stat = transfer_stat;
};

//#define TG_MFGR_TOOL
#ifdef TG_MFGR_TOOL
#include "ipcam.h"
int ctpWriteToFile(const char *path, const void *content, int length)
{
    _info("target=%s, length=%d\n", path, length);
    if(strcmp(path, "/config/uuid") == 0) { //写UUID
        //{content} is uuid, save it
    }
    else if(strcmp(path, "/config/hwcfg") == 0) { //写配置
        //{content} is configuration
    }
    else if(strcmp(path, "/tmp/firmware.bin") == 0) { //升级包
    }
    //else {
    //... save or process the content
    //}
    return 0;
}

int ctpExecCommand(const char *cmd, int len, struct CmdRes *res)
{
    if(strncmp(cmd, "get_config_uuid", 15) == 0) { //获取本机uuid
        if(g_Lic.uuid[0]) { //如果已经烧号
            res->status = 0;  //有id号，返回0
            res->len = sprintf(res->buff, "%s", g_Lic.uuid); //返回id号
        }
        else {
            res->status = -1;  //无id号，返回非0
            res->buff[0] = '\0';
            res->len = 0;
        }
        return 0;
    }
    else
        return -1;
}
#endif

extern volatile uint32_t vpp_md_cnt;
extern volatile uint32_t call_test_flag;
extern volatile uint32_t dev_status_test;
extern volatile uint32_t dev_status;
extern void tcSetDeviceStatus(int status);

#include "lib/heap/sysheap.h"
#include "lib/heap/av_psram_heap.h"   /* av_psram_heap */
#include "lib/heap/av_heap.h"         /* av_heap (AV SRAM 堆) */

extern struct sys_psramheap psram_heap;
extern int check_webrtc_online(void);
extern void Tg_memory_usage(int flag);

void event_report_demo(void*arg)
{
    uint32_t last_cnt = 0;
    uint32_t last_report_time_ms = os_jiffies();
    uint32_t last_stats_time_ms = os_jiffies();
    /* 历史最低 free 水位 (越低代表瞬时峰值占用越大). 启动后所有 alloc 都会
     * 把 free 推低, 关注一次完整跑 (实时+录像+回放) 后的最低值 */
    uint32_t lowest_free_b     = 0xFFFFFFFF;   /* PSRAM 主堆 */
    uint32_t lowest_av_free_b  = 0xFFFFFFFF;   /* AV PSRAM 堆 */
    uint32_t lowest_sram_b     = 0xFFFFFFFF;   /* SRAM 主堆 */
    uint32_t lowest_avsram_b   = 0xFFFFFFFF;   /* AV SRAM 堆 */

    while(1){

        os_sleep_ms(1000);

        /* 每 5 秒 dump 一次: PSRAM 主堆 + av_psram_heap 余量 */
        if ((os_jiffies() - last_stats_time_ms) >= 5000) {
            last_stats_time_ms = os_jiffies();

            uint32_t free_b  = sysheap_freesize(&psram_heap);
            uint32_t total_b = sysheap_totalsize(&psram_heap);
            if (free_b < lowest_free_b) lowest_free_b = free_b;
            _os_printf("=== PSRAM HEAP: free=%u/%u B (%u%%) lowest=%u (used_peak=%u) ===\r\n",
                   free_b, total_b,
                   total_b ? (uint32_t)((uint64_t)free_b * 100 / total_b) : 0,
                   lowest_free_b,
                   total_b - lowest_free_b);

            /* av_psram_heap 是独立的视频专用堆 (CONFIG_PSRAM_AVHEAP_SIZE).
             * h264/jpg/mp4/webrtc 等模块用 av_psram_malloc 都从这里出, 是
             * av_psram: malloc fail 错误的直接来源, 单独看余量更精确. */
            uint32_t av_free_b  = sysheap_freesize(&av_psram_heap);
            uint32_t av_total_b = sysheap_totalsize(&av_psram_heap);
            if (av_free_b < lowest_av_free_b) lowest_av_free_b = av_free_b;
            _os_printf("=== AV_PSRAM HEAP: free=%u/%u B (%u%%) lowest=%u (used_peak=%u) ===\r\n",
                   av_free_b, av_total_b,
                   av_total_b ? (uint32_t)((uint64_t)av_free_b * 100 / av_total_b) : 0,
                   lowest_av_free_b,
                   av_total_b - lowest_av_free_b);

            /* SRAM 主堆: os_malloc / _os_malloc 都从这里出 (含各 task 栈/小对象) */
            uint32_t sram_free_b  = sysheap_freesize(&sram_heap);
            uint32_t sram_total_b = sysheap_totalsize(&sram_heap);
            if (sram_free_b < lowest_sram_b) lowest_sram_b = sram_free_b;
            _os_printf("=== SRAM HEAP: free=%u/%u B (%u%%) lowest=%u (used_peak=%u) ===\r\n",
                   sram_free_b, sram_total_b,
                   sram_total_b ? (uint32_t)((uint64_t)sram_free_b * 100 / sram_total_b) : 0,
                   lowest_sram_b,
                   sram_total_b - lowest_sram_b);

            /* AV SRAM 堆: av_malloc / av_free 走这里 (LCD/OSD/部分视频小缓冲) */
            uint32_t avsram_free_b  = sysheap_freesize(&av_heap);
            uint32_t avsram_total_b = sysheap_totalsize(&av_heap);
            if (avsram_free_b < lowest_avsram_b) lowest_avsram_b = avsram_free_b;
            _os_printf("=== AV_SRAM HEAP: free=%u/%u B (%u%%) lowest=%u (used_peak=%u) ===\r\n",
                   avsram_free_b, avsram_total_b,
                   avsram_total_b ? (uint32_t)((uint64_t)avsram_free_b * 100 / avsram_total_b) : 0,
                   lowest_avsram_b,
                   avsram_total_b - lowest_avsram_b);

           _os_printf("CPU loading: %u%%\r\n", os_cpuloading());

          /*检测webrtc是否在线，在线返回1，离线返回0，未启动返回-1*/
           int ret = check_webrtc_online();
           _os_printf("webrtc online %d\n", ret);

         /*探鸽SDK内部psram使用情况, 仅调试用*/
            Tg_memory_usage(0);
        }

        //移动侦测事件抓图上报
        //1. 未开通云存储服务
        /* 1.1 连续触发（间隔小于1'）的，上报间隔2倍渐增 */
        /* 1.2 限制每天上报次数:
         *    上电后上报次数超过50，则将之后的上报速度限制在每6分钟一次(单种事件一天最多上报~240次)。
         *    但是如果事件距上次上报超10分钟，则临时解除限制。以免上电初期的频繁上报导致后来一段时间一直不报。
         */
        //2. 开通了云存储服务的， 上报间隔30秒
        if(!ipc_inited){
            last_report_time_ms = os_jiffies();
        }
        else if(ipc_inited && vpp_md_cnt != last_cnt && (os_jiffies() - last_report_time_ms)/1000 > 30/*上报间隔30秒*/){
            _os_printf("start md report\n");
            last_cnt = vpp_md_cnt;
            last_report_time_ms = os_jiffies();
            uint32_t  jpg_len  = 0;
            int ret = snapshot_capture(&md_jpg_out_data, &jpg_len, 2000);
            if(ret == 0){
                EVENTPARAM evtp;
                memset(&evtp, 0x0, sizeof(evtp));
                evtp.cbSize = sizeof(evtp);
                evtp.event = ECEVENT_MOTION_DETECTED;
                evtp.tHappen = time(NULL);
                evtp.status = 1;
                evtp.jpg_pic = md_jpg_out_data;
                evtp.pic_len = jpg_len;
                evtp.evtp_flags = 0;
                ret = TciSetEventEx(&evtp);
                if(ret != 0){
                    _os_printf("event report failed\n");
                    snapshot_release(md_jpg_out_data);//上报失败释放内存，上报成功后在on_status回调内释放。
                }else{
                    //模拟连续多次触发事件，延长云存储录像时长
//                    int i;
//                    for(i=0; i < 3; i++){
//                        os_sleep_ms(4000);
//                        memset(&evtp, 0x0, sizeof(evtp));
//                        evtp.cbSize = sizeof(evtp);
//                        evtp.event = ECEVENT_MOTION_DETECTED;
//                        evtp.tHappen = time(NULL);
//                        evtp.status = 1;
//                        evtp.jpg_pic = NULL;
//                        evtp.pic_len = 0;
//                        evtp.evtp_flags = EPF_RECORD_ONLY;
//                        TciSetEventEx(&evtp);
//                    }
                }
            }else{
                _os_printf("snapshot jpg failed\n");
            }
        }

        //模拟呼叫事件,最小间隔5秒
        if(call_test_flag){
            _os_printf("start call event\n");
            uint32_t  jpg_len  = 0;
            int ret = snapshot_capture(&call_jpg_out_data, &jpg_len, 2000);
            if(ret == 0){
                EVENTPARAM evtp;
                memset(&evtp, 0x0, sizeof(evtp));
                evtp.cbSize = sizeof(evtp);
                evtp.event = ECEVENT_DOORBELL;
                evtp.tHappen = time(NULL);
                evtp.status = 1;
                evtp.jpg_pic = call_jpg_out_data;
                evtp.pic_len = jpg_len;
                evtp.evtp_flags = 0;
                ret = TciSetEventEx(&evtp);
                if(ret != 0){
                    _os_printf("doorbell call failed, ret = %d\n", ret);
                    snapshot_release(call_jpg_out_data);
                }
                call_test_flag = 0;
            }
        }

        //设备开关状态测试，关闭将停止发送音视频流、停止事件和云存储上传
        if(dev_status_test){
            tcSetDeviceStatus(dev_status); // dev_status： 0 关闭， 1 开启
            dev_status_test = 0;
        }
    }
}

extern int psram_heap_size;

//#define TG_DEBUG_MODE  1

int IpcStep1(void)
{
    _os_printf("psram_heap_size = %dMB\n", psram_heap_size);
    _os_printf("**** tg_demo: build at %s %s ****\n\n", __DATE__, __TIME__);

    LogfSetLevel(6); //-- for debug

    extern struct paramf_ops _txw81x_paramf_ops;
    TciSetParamFileOps(&_txw81x_paramf_ops);   //配置SDK内部参数读写接口

    IpctLoadSetting(&g_IpcParam);
    IpctLoadLicsence(&g_Lic);

    init_tcicb();
    TciSetCallback(&_tci_cb);
    TciSetCmdHandler(Handle_P2p_Cmd);

#if 0
    cJSON *jcfg = IpctLoadConfig(); //from sdc

    if(jcfg) {
        cJSON *item = tgJSON_GetObjectItem(jcfg, "loglevel");
        if(item) LogfSetLevel(item->valueint);

        if((item = tgJSON_GetObjectItem(jcfg, "uuid")))
            strcpy(g_Lic.uuid, item->valuestring);
    }
#endif


    //必需，探鸽内部需要根据设备psram大小来申请不同大小的内存
    TciSetSysOption(TCOPT_MAX_SYS_PSRAM_SIZE, &psram_heap_size);

    if(!g_Lic.uuid[0]) {
		os_printf("uuid not set, use default\r\n");
        #ifdef TG_DEBUG_MODE
		strcpy(g_Lic.uuid, "5A3AYHCS6KZ7");
		#else
		strcpy(g_Lic.uuid, "AT3M86U5M2HE,xcbFm2IssVKjInmouNl0VKMCAQiyPeSl");   //需替换为自己的 "uuid,key"
        #endif
    }

    int ret = TciInit(NULL, g_Lic.uuid);
    if(ret) {
        LogE("TciInit return %d\n", ret);
        return -1;
    }

#if 0
    if(!g_Lic.uuid[0]) {
        //生产模式: 用卡配置wifi
        if(jcfg) {
            cJSON *ssid = cJSON_GetObjectItem(jcfg, "wifi_ssid");
            cJSON *pswd = cJSON_GetObjectItem(jcfg, "wifi_key");
            if(ssid && pswd) { //测试模式
                wifi_setup_sta(ssid->valuestring, pswd->valuestring);
            }
            cJSON_Delete(jcfg);

            while(!_net_ready) SA_Sleep(1000);
            //InitCTP();
        }
        return 0;
    }
#endif


    if(g_IpcParam.flags & IPCP_F_BIND_TO_USER){
        registered = 1; //已注册
    }
    LogV("g_IpcParam.flags=0x%X, registered=%d\n", g_IpcParam.flags, registered);


#if TG_DEBUG_MODE
    //仅调试时使用，绑定成功后，写入固定wifi信息，免去每次烧录后都要重新配网
    registered = 1;
    wifi_setup_sta("zhang", "32107799");
#else
    if(!registered) {//not registered
        IpctResetSetting(&g_IpcParam);
        char uuid[32] = {0};
        char *colon = strchr(g_Lic.uuid, ',');
        if(colon){
            memcpy(uuid, g_Lic.uuid, colon-g_Lic.uuid);
        }else{
            strcpy(uuid, g_Lic.uuid);
        }
        net_state = E_NET_AP;
        wifi_setup_ap(uuid);    //未注册，启动AP模式，准备配网，仅传uuid，不要传key进去
#if BLE_SUPPORT
        /* 未注册: AP 热点配网 + BLE 蓝牙配网 同时启动, APP 用哪种都行.
         * BLE 名 = AICAM_<uuid>, 与 AP 热点名一致. 见 ble_tange_netcfg.c */
        tg_ble_netcfg_init(uuid);
#endif
    }
    else{
        wifi_setup_sta(g_IpcParam.wifi_ssid, g_IpcParam.wifi_key);//已经注册过，连接路由器
    }
#endif
	return 0;
}

void IpcStep2(void*arg)
{
	if(!registered) {
        //未注册，调用SDK配网，配网模式为热点配网，TciConfigWifi会阻塞，直到set_wifi函数返回
		_info("Config wifi ...\n");
        TciConfigWifi(/*GWM_QRCODE|*/GWM_AP);
		_info("Config wifi finished.........\n");
        //在set_wifi()里返回sta模式
    }

    /*
     * 正常工作模式
     */
    while(net_state != E_NET_STA) os_sleep_ms(1000);

	_info("TciStart......\n");

    unsigned cloud_cache_szie = 0; //置0则不开启云存储
#if defined (__TXW826__)
    cloud_cache_szie = 150*1024;
#elif defined(__TXW828__)
    cloud_cache_szie = 200*1024;
#endif
    int ret = TciStart(registered, cloud_cache_szie);
    if(ret) {
        LogE("TciStart return %d\n", ret);
        return;
    }


//#define MUFBRFF_TEST
#ifdef MUFBRFF_TEST
    extern void mufbuff_test(void);
    mufbuff_test();
#endif

	_info("Ipc Started end\n");
    ipc_inited = 1;
}
