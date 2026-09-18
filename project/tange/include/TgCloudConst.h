/**
 * @file TgCloudConst.h
 * @brief 探鸽云SDK常数定义
 */

#ifndef __TgCloudConst_h__
#define __TgCloudConst_h__

#include "ec_const.h"

/** @addtogroup api_const
 * @{*/

/** status_code TciCB::on_status()回调状态码 */
typedef enum {
    STATUS_LOGON       =  1, ///< 设备上线. data: NULL.
    STATUS_LOGOFF      =  2, ///< 设备下线. data: NULL.
    STATUS_DELETED     =  3, ///< 设备被删除. data: NULL.
    STATUS_UPDATE_SERVICE = 4, ///< 更新云服务. data: TCISERVICEINFO *; len: sizeof(TCISERVICEINFO)
    STATUS_AP_CONNECT	= 5,  ///< 用户通过ap直连. data:NULL
    STATUS_STREAMING    = 6,  ///< 用户打开实时视频. data: int(number of clients); len:0
    STATUS_START_TELNETD = 7,  ///< 请求开启telnetd服务(用于调试). data:NULL
    STATUS_SDER         = 8,  ///< 服务器定义事件上传(server defined event record). data: @ref SDER. Return: 0(ok), -1(错误, 无文件等)
    /** 设备休眠查询.
      \param data: NULL
      \param len:
      - \c >0:  设备可以进入休眠时，应用在此回调里做清理动作，并与唤醒服务器建立。当这些动作完成后，再返回0
      - \c ==0: sdk已经下线，设备要立即下电

      \return 本状态当 \c len!=0 时, 要求应用返回一个值:
      - 返回值大于0时，sdk会在返回值(秒)后再次查询.
      - 返回值<0时，sdk不会再触发本查询
      - 返回值=0时，sdk执行内部的清理过程。清理完成后会以 `len=0` 再触发一次本事件. 此时设备应给主控下电, 系统进入休眠状态
      */
    STATUS_IDLE         = 9,
    STATUS_AI           = 10, ///< 服务器端AI检测到正的结果. data: struct AiResult *
    STATUS_SWD_TIMEOUT  = 11, ///< soft watchdog timeout. data: name
    //STATUS_ERROR_SERVICE 10 ///< 服务器或外部网络错
    STATUS_USER_DATA     = 12, ///< 用户数据. data: @ref TgUserData *; len: TgUserData结构长度

    /** 呼入状态通知.
     *
     * - \c data != NULL, \c len > 0: \n
     *   有用户呼入， data 类型为 INCALLINFO *， len为data指向空间的长度。 \n
     *   固件要显示一个界面展示是谁呼叫，并给用户决定是要接听(TciAcceptInCall2()) \n
     *   还是拒接(TciRejectInCall2())。不能直接在回调里调用这两个接口。\n
     *   应用不能阻塞`on_status`调用。\n
     *   返回: @ref ECALLFLAVOR
     *     - <=0  呼叫被忽略(sdk立即拒接呼叫).
     *     - 1    将由用户在后面通过TciAcceptInCall2()或TciRejectInCall2()接听或拒接.
     *
     * - \c data != NULL, \c len == 0: \n
     *   用户取消呼叫
     *   返回: 0
     *
     * - \c data == NULL, \c len == 0: \n
     *   通话结束
     *   返回: 0
     *
     * - \c data == NULL, \c len == 1: \n
     *   长连接上的呼入通知。应用层在这里调用TciSetP2pDown(0) 打开p2p(如果之前关闭了的话). \n
     *   这种情况下App呼叫设备，应用层会收到两次 STATUS_INCALL2通知。以后sdk可能会优化掉这 \n
     *   步骤。
     *   返回: 0
     *
     */
    STATUS_INCALL2        = 13,

    /** 实时传输监控.
     * - \c data: TRANSMONITOR *
     * - \c len: sizeof(TRANSMONITOR)
     *
     * <b>返回:</b>\n
     *  开启自动码流切换(参见 @ref Feature_Resolutions)并且用户请求流为自动时, 返回值有如下功能:
     *    - \c 0 SDK会在丢帧时切换到低分码流
     *    - \c 1 表示固件自己调整了码流, SDK不再处理。
     *    - \c 2 告诉SDK降低当前连接的分辨率.
     *    - \c 3 告诉SDK提高当前连接的分辨率.
     */
    STATUS_TRANSFER_MONITOR = 14,

    STATUS_RESOLVE_FAILED = 21,  ///< 域名解析错

    STATUS_WRITE_BACKSTORE = 22,

    /** 长连接状态.
     * - \c data:
     *   - \c NULL: 连接断开
     *   - \c non-NULL: 连接建立
     */
    STATUS_WS_CONNECTION  = 23,

    /** 获取取注册信息
     * - \c data: 注册信息
     * - \c len:  长度
     */
    STATUS_GOT_REGINFO     = 24,

    /** 事件录像开始和结束通知.
     * - \c data: EVENTRECORDNOTIFICATION *
     * - \c len: sizeof(EVENTRECORDNOTIFICATION)
     */
    STATUS_EVENT_RECORD    = 25,

    /** 云存质量调整通知.
     * \param data CSQADJUSTNOTI *
     * \param len sizeof(CSQADJUSTNOTI)
     *
     * @retval 0 SDK自动处理(将录像切换到主码流或辅码流)
     * @retval 1 应用层调整了bitrate, SDK不再处理
     *
     * @note 此功能需要调用 TciSetSysOption() 设置 @ref TCOPT_ENABLE_ADJUSTMENT_FOR_CS 后才会开启.
     * \see @ref a45_CsIntegrity
     */
    STATUS_CS_QOS          = 26,

    /** 事件上报处理结果通知.
     * - \c data: EVENTREPORTNOTIFICATION *
     * - \c len: sizeof(EVENTREPORTNOTIFICATION).
     */
    STATUS_EVENT_CALLBACK = 27,

    /** http请求被重定向, 多数是因为路由器开启了EAP, 少数是被截持.
     * - \c data: char *, 重定向地址
     * - \c len: 长度
     */
    STATUS_ACCESS_REDIRECTED = 28,
    /** 云存储文件上传状态.
     * - \c data: time_t *, 云存储文件的utc时间
     * - \c len: ==0 上传成功，>0 上传失败
     */
    STATUS_CLOUD_FILE_UPLOAD = 29,


} ESTATUSCODE;

/** @name Wifi 配置获取方式
 * @{ */
#define GWM_QRCODE        0x1 ///<二维码
#define GWM_AP            0x2 ///<AP或蓝牙模式
/**@}*/

/** @anchor pic_type*/
/** @name 图片类别指示
 * @{ */
#define PIC_USE_THUMBNAIL   0 ///<缩略图。大小不超过320*240
#define PIC_USE_AI_BD       1 ///<人形检测。图片大小约640*352
#define PIC_USE_AI_FD       2 ///<人脸检测
#define PIC_HIGH_RES        3 ///<高清晰度
/**@}*/

/** 呼叫类型 */
typedef enum {
    CALLTYPE_NONE,   ///< no
    CALLTYPE_EVENT,  ///< ECEVENT_DOORBELL

    CALLTYPE_TANGE,  ///< 探鸽p2p
    CALLTYPE_WEIXIN  ///< 微信小程序
} ECALLTYPE;

/** 通话类型 */
typedef enum {
    NONE_CALL = 0,
    VOICE_CALL, ///< 语音通话
    VIDEO_CALL, ///< 视频通话
} ECALLFLAVOR;

/**@}*///api_const

/** \addtogroup api_structure
 * @{*/
/** 呼叫者信息.
 * 应用通过 on_status() 回调的 STATUS_INCALL2 状态收到本信息。\n
 * 除 type/user_id/nickname/flavor 外，其它域对应用是透明的 */
typedef
struct InCallInfo {
    int  type;      ///< 呼叫类型。 @ref CALLTYPE_TANGE 或 CALLTYPE_WEIXIN
    char *user_id;  ///< 呼叫者ID, 可为NULL
    char *nickname; ///< 呼叫者昵称, 可为NULL

    int  flavor;    ///< 主叫请求的通话类型(@ref ECALLFLAVOR)。应用在 TciAcceptInCall2()里传入接受的通话类型.

    int  state;     ///< 0:呼入; 1:用户取消呼叫; 2:超时
    //以下数据在SDK内部使用并可能变化，应用不要访问
    union {
        struct {
            char *room_id;   // 微信呼叫房间号
            char *server_token; // 纯云下才有
            char *session_key;  //
            char *app_payload;
        } wx;
        struct {
            int sub_type;
            void *ptr;
        } tg;
    };
    char data[1];
} INCALLINFO;

/** 传输监测 */
typedef struct TRANSMONITOR {
    /** 发送缓冲区占用百分比.
     *  - >0 当前占比相对之前在增加.
     *  - <0 相比上一次统计在减少.
     *  - 100 出现丢帧.
     */
    int sndbuf_waterline;
    int channel, vstream;
} TRANSMONITOR;

/** 云存质量调整通知.  */
typedef struct CSQADJUSTNOTI {
    int stream;     ///< 当前录像码流
    int up_grade;   ///< 码流调用方向: 1:提升; 0:降低
} CSQADJUSTNOTI;
/**@}*/

/** \addtogroup api_const
 * @{*/

/** 插入到媒体流(实时或回放)或命令通道中的消息类型.
 * - 插入命令流的是通知类信息，用TciSendRtMsg()发送一个 RTMSG_t 结构
 * - 插入媒体流的信息用于辅助描述流相关的事件或提供额外信息, 调用 TciSendLiveMessage() 或 TciSendPbMessage(),
 *   发送的是 RTMSGHEAD_t 结构
 * 有的消息是设备SDK自动插入，设备应用层不要主动发送.
 */
typedef enum {
    /** 回放: 时间同步帧, 用于告诉播放器下一帧发生时的的UTC时间. 在时间戳中断时（例如自动跳到下一个文件）发送
     *  - data2 = 下一帧发生时的UTC时间;
     *  - data1 = 时间的毫秒部分;
     *  - extra_len = 0;
     *  SDK 提供辅助宏 @ref TciSendPbSyncFrame() 来发送这个帧
     */
    RTM_SYNCTIME = 0,

    /** 实时流: 多目摄像机在变焦过程中发生镜头切换，在切换完成后、新镜头的第一个I帧前发送此标志
     *  - data1 = 视频通道号;
     *  - data2 = 码流编号;
     *  - extra_len = 0;
     *
     *  \n使用辅助宏 @ref TciSendLiveMsg_LensSwitch() （或旧的 TciSendLensSwitchFlag()）发送
     */
    RTM_LENS_SWITCH = 1,

    /** 回放: 同 @ref RTM_SYNCTIME, 但在响应 @ref TCI_CMD_RECORD_PLAYCONTROL 命令发生跳转的第一帧前发送
     *  - data1 = 0;
     *  - data2 = 下一帧发生时的UTC时间;
     *  - extra_len = 0;
     */
    RTM_SYNCTIME_RESPONSE_TO_USER = 2,

    /** 实时: 到达预置位.
     * 响应APP调用预置位命令, 在转到预置位置时在实时流中发送.用于通知APP更新预置位图片。 \n
     * 当App端没有预置位缩略图时(分享、换手机、第三方设置了预置位等)，依赖此特性自动更新。
     * - data1: 视频通道号(或摄像头索引)
     * - data2: 预置位编号(>0)
     * - extra_len: 0
     * \n使用辅助宏 @ref TciSendLiveMsg_ReachPsp()  来发送这个消息
     */
    RTM_REACH_PSP = 3,

    /** 回放: 缩时录像回放启停标志.
     * 在sd卡回放时进入和退出缩时录像发送此标志。
     * - data1: 1:缩时录像回放开始; 0:缩时录像回放结束
     * - data2: 0 或 倍速
     * - extra_len: 0
     * \n使用辅助宏 @ref TciSendPbTimelapseFlag() 发送本消息
     */
    RTM_TIME_LAPSED = 4,

    /** 呼叫结束.
     * 呼叫事件在设备端超时，或者接听者挂断，向所有(别的)连接发送此通知. \n
     * 内部事件。目前在实时流里发送
     * - data1: 状态 @ref ECALLSTATE
     * - data2: 0
     * - extra_len: 0
     */
    RTM_UPDATE_CALL_STATE = 5,

    /** 实时或命令: 设备休眠通知, APP收到命令后要关闭连接
     * - data1: 0
     * - data2: 0
     * - extra_len: 0
     */
    RTM_GOINGTO_SLEEP = 6,

    /** 回放: 事件结束标志
     * 事件(单文件)(参见 @ref TCIC_RECORD_PLAY_START)回放模式下，当事件(文件)播放结束时发送此标志。
     * 支持单文件模式时必需发送
     * - data1: 0
     * - data2: 0
     * - extra_len: 0
     * \n使用辅助宏 @ref TciSendPbEndOfEvent() 发送本消息
     */
    RTM_END_OF_EVENT = 7,

    /** 命令: 取消呼叫小程序. 这个是发给转发服务器的内部消息 */
    RTM_CANCEL_CALL = 8,

    /** 用户自定义的消息 */
    RTM_USER = 255
} RTMTYPE;
/**@}*/ //group: api_const

/** \addtogroup api_structure
 * @{*/

/** 服务端定义的事件上报(Server Defined Event Report), 在 on_status() 回调里通过 STATUS_SDER 通知 */
typedef struct SDER {
    char event[16]; ///< 事件名。这个值传给 TciUduBegin2() 的evt参数
    time_t t_start; ///< 录像开始时间
    int tLen;       ///< 录像长度
    int need_image; ///< 1:上传图片和视频; 2:仅上传图片
} SDER;

/** 用户自定义数据通知.
 * 用户自定义数据通过STATUS_USER_DATA 在本结构中传递给应用
 */
typedef struct TgUserData {
    unsigned char *data;  ///< 用户数据
    int len;              ///< 用户数据长度
    char *id;             ///< 请求标识
    unsigned char *resp;  ///< 应用返回的数据. 目前忽略
    int resp_len;         ///< 返回数据长度
} TgUserData;

/** 在媒体流中插入的 消息/数据帧 帧头 (for App Developer).
 * 本结构与 @ref FRAMEINFO_t 一样有相同的长度，并用 codec_id来区分结构体内容；extra_len 为结构后的数据长度。 \n
 * 插放器遇到不能识别的 codec_id 时，可以跳过本结构和后面的 extra_len 字节。 \n
 * 设备端调用 TciSendLiveMessage() 或 TciSendPbMessage() 分别在实时流或回放流中插入消息
 */
typedef struct RTMSGHEAD_t
{
    unsigned short codec_id;	///< 0: 标志本结构
    unsigned short type;        ///< 消息类型。 @ref RTMTYPE

    unsigned int data1;	        ///< 消息类型相关数据1

    unsigned int extra_len;	///< Size of frame

    unsigned int data2;	        ///< 消息类型相关数据2
} RTMSGHEAD_t;

/**@}*/ //addtogroup api_struct

/** \defgroup error_code 错误码
 * @{
 * \name 普通错误码
 * @{*/

#define TCE_OK               0     ///< 成功
#define TCE_GENERIC_ERROR    -1    ///< 一般性错误

/*一般性错误*/
#define TCE_INVALID_PARAMETER  -10001001  ///<输入参数错
#define TCE_INVALID_UUID       -10001002  ///<无效UUID
#define TCE_INVALID_AI_UUID    -10001003  ///<无效AI UUID
#define TCE_NOT_ALLOWED        -10001004  ///<操作不允许
#define TCE_IN_PROCESSING      -10001005  ///<操作进行中,不要重复启动

/*服务器错误*/
#define TCE_SERVER_FAILURE     -10002001  ///<服务器错误
#define TCE_SERVER_IS_DOWN     -10002002  ///<服务器没有运行

/*用户错误*/
#define TCE_ALGRTHM_DISABLED   -10003001  ///<算法没有开启
#define TCE_BUFFER_TOO_SMALL   -10003002  ///<缓冲区太小

/*其它*/
#define TCE_NETWORK_BUSY       -10004001  ///<网络拥堵
#define TCE_MD_NOT_MATCH       -10004002  ///<下载文件内容校验失败
#define TCE_SERVICE_UNVAILABLE -10004003  ///<服务不可用
#define TCE_INCALL_HAS_GONE    -10004004  ///<呼入已经无效
#define TCE_LACK_OF_RESOURCE   -10004005  ///<资源(内存)不足
/**@}*/
/**@}*///error_code


#endif
