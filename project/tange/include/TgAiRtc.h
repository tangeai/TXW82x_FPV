/** \file TgAiRtc.h
 *
 * \brief AIGC RTC 功能接口和命令.
 *
 *
 * # 连接代理
 * 设备通过AIGC RTC 代理与云端的AI服务实现双向通话。 \n
 * 根据业务约定，代理可能主动连接设备，也可能需要设备先发起连接请求(调用 TgAiRtcDial()). \n
 * 代理可能主动断开设备(例如在一段时间静默后)，也可能需要设备主动请求断开(调用 TgAiRtcHangup())。
 *
 * # 双向数据和通知
 * 连接建立后，设备和代理间就可以双向发送媒体数据和通知。音视频的收发参见 <a href="index.html">探鸽云平台设备接入</a>.
 *
 * ## 通知: 代理->设备
 * 代理到设备的通知通过命令 @ref TCI_CMD_AIRTC_NOTI 发送.  \n
 * 通知类型包括应答的开始(@ref ANOTI_ANSWER_START)/结束(@ref ANOTI_ANSWER_END)标志和其它事件(@ref ANOTI_EVENT).  \n
 * 其它事件内容需要参看平台端文档，可能包含字幕等信息.
 *
 * ## 通知: 设备->代理
 * 设备到代理的通知内容由平台约定，可能包含主动打断等功能. \n
 * <参见 https://tange-ai.feishu.cn/docx/UTsBdkRSCol81Nx0x78cUJpvn4g> 
 *
 * 通知在设备连上代理(可以以收到 @ref TciCB::on_talkback_start() 回调为标志)后调用 TciSendRtMsg() 发送. 
 *
 * TciSendRtMsg()调用采用以下参数:
 *     \code{.c}
            #define TAG_AIGC 0x41494743
            #define TAG_JSON 0x4a534f4e
            TciSendRtMsg(0, RTM_USER, TAG_AIGC, TAG_JSON, <notification>, <length_of_notification>);
 *     \endcode
 *
 *  例如
 *     \code{.c}
            const char event[] = "{\"jsonrpc\":\"2.0\",\"method\":\"interupt\"}";
            TciSendRtMsg(0, RTM_USER, TAG_AIGC, TAG_JSON, event, sizeof(event) - 1);
 *     \endcode
 */
#pragma once

#include "basedef.h"

#ifdef __cplusplus
extern "C" {
#endif


#define TCI_CMD_AIRTC_NOTI   0x803A   ///< 从代理收到的通知命令。req: TcisAiRtcState; 无需应答
typedef enum {
    ANOTI_ANSWER_START,   ///< 应答开始. 无额外数据
    ANOTI_ANSWER_END,     ///< 应答结束. 无额外数据
    ANOTI_EVENT           ///< 其它事件. 
} EAIRTCNOTI;

/** \struct TcisAiRtcState.
 * @ref TCI_CMD_AIRTC_NOTI 命令的参数
 */
typedef struct {
    uint32_t state;  ///< @ref EAIRTCNOTI
    uint32_t reserved;
    char     data[0]; ///< ANOTI_EVENT 的数据. 内容由平台规定
} TcisAiRtcState;

/** 主动请求连接.
 * @param jstr 呼叫配置，json格式的字符串，内容由设备与平台约定(参看<a href="https://tange-ai.feishu.cn/docx/UTsBdkRSCol81Nx0x78cUJpvn4g#share-WgBvdkFMQoWOecxaJlkcrDkkn0b">5. QWEN-AGENT 建连</a>)
 * @return 0 或错误码 */
int TgAiRtcDial(const char *jstr);

/** 主动挂断连接.
 * @return 0 或错误码 */
int TgAiRtcHangup();


#ifdef __cplusplus
} /* extern "C" */
#endif
