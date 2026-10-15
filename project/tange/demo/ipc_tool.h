#ifndef __ipc_tool_h__
#define __ipc_tool_h__


#include "cjson/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SAMPLES_PER_PACKAGE  320

struct IpcWorkParam {
#define IPCP_F_BIND_TO_USER   0x00000001  //bind to user
#define IPCP_F_STATUS_LED_OFF 0x00000002 //disable the status led
#define IPCP_F_DISABLE_MIC    0x00000004
#define IPCP_F_DISABLE_SPEAKER 0x00000008
	uint32_t flags;

	uint8_t irled_cfg;       //0: 1: 2:
	uint8_t whitelight_cfg;  //0: 1: 2:
	uint8_t sdrec_cfg;       //0:²»Â¼ÏñÏñ; 1:ÊÂ¼þ; 2:È«Ìì
	uint8_t mic_level; //set default XXX in LoadSetting
	uint8_t spkr_volume;

	char wifi_ssid[48];
	char wifi_key[32];


  //ÐÂµÄÅäÖÃÏîÔÚºóÃæ×·¼Ó¡£¾ÉµÄÅäÖÃÏîÒ»°ãÇé¿öÏÂ²»ÄÜÉ¾³ý
	// ...
} __PACKED__;


int IpctLoadSetting(struct IpcWorkParam*); //user's setting
int IpctSaveSetting(const struct IpcWorkParam*);
void IpctResetSetting(struct IpcWorkParam *);

struct IpcLic {
    char uuid[128];
};
int IpctLoadLicsence(struct IpcLic*); //uuid etc.
int IpctSaveLicsence(const struct IpcLic*);
void IpctResetLicsence(struct IpcLic*);

cJSON *IpctLoadConfig(); //from sdc



#ifdef __cplusplus
}
#endif

#endif

