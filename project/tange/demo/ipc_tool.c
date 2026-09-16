#include "platforms.h"
#include <stdlib.h>
#include <string.h>
#include "ipc_tool.h"
#include "project_config.h"
#include "logfile.h"
#include "TgCloudApi.h"
#include "tg_crc32.h"
#include "osal/string.h"

//-----------------------------------------------------------------------------------------
/* Flash 空间分配 */
enum {
    PAREA_ID_LIC = 0,
	PAREA_ID_SDK = 1,
	PAREA_ID_APP = 2
};
/* 参数文件头 */
//#define IPCPARAMF_TAG 'FMRP'
#define IPCPARAMF_TAG 'FMRX'
struct IPrmfHead {
	uint32_t tag;
	uint32_t len;  //length of data
	uint32_t crc32;
};
#define HDR_SIZE sizeof(struct IPrmfHead)

extern uint32_t calc_crc32(uint32_t init, const void *p, int size);

#if 0
#define SEC_SIZE 250
struct FlashSpc {
	uint8_t area_id;
	uint8_t blk_cnt;
	uint16_t blk_start;  //vm index
};
struct FlashSpc _flsh_tbl[] = {
    { PAREA_ID_LIC, 1, 10 },
	{ PAREA_ID_SDK, 8, 11 },
	{ PAREA_ID_APP, 8, 19 }
};
#else
#define SEC_SIZE 4096
struct FlashSpc {
	uint16_t area_id;
	uint16_t blk_cnt;
	uint32_t blk_start;  //start address of a sector
};
struct FlashSpc _flsh_tbl[] = {
    { PAREA_ID_LIC, 1, 0x1F0000 },
	{ PAREA_ID_SDK, 1, 0x1F1000 },
	{ PAREA_ID_APP, 1, 0x1F2000 }
};
#endif


extern struct spi_nor_flash flash0;
int read_flash(int address, int size, uint8_t *buff)
{
	//TODO: read from flash. return number of bytes read, -1 if has error
	int ret;

	os_printf("flash read :%p, size:%d\r\n", address, size);

	ret = spi_nor_open(&flash0);
	if (ret != 0) {
		os_printf("spi_nor_open fail\r\n");
		return -1;
	}

	spi_nor_read(&flash0, address, buff, size);
	spi_nor_close(&flash0);
	return size;
}
int write_flash(int address, const uint8_t *buff, int len)
{
	//TODO: write to flash. return number of bytes written, -1 if has error
	int ret;

	os_printf("flash write :%p, size:%d\r\n", address, len);

	ret = spi_nor_open(&flash0);
	if (ret != 0) {
		os_printf("spi_nor_open fail\r\n");
		return -1;
	}

	spi_nor_sector_erase(&flash0, address);
	spi_nor_write(&flash0, address, buff, len);
	spi_nor_close(&flash0);
	return len;
}
/* return: >0 实际复制出的字节数
           <=0 无数据或数据校验错
*/
static int paramf_read(int which, void *out, int size)
{
    uint8_t *sec = (uint8_t*)os_malloc_psram(SEC_SIZE);
    uint32_t crc32 = 0;
    int byts_copied = 0, i, ret = -1;
    struct IPrmfHead hdr;


    for(i=0; i<_flsh_tbl[which].blk_cnt; i++) {
        if(read_flash(_flsh_tbl[which].blk_start + i*SEC_SIZE, SEC_SIZE, sec) < 0) {
            os_printf("read_flash(addr=0x%x) failed\n", _flsh_tbl[which].blk_start + i*SEC_SIZE);
			goto out;
        }
        int sec_dlen;
        if(i == 0) {
            memcpy(&hdr, sec, sizeof(hdr));
            if(hdr.tag != IPCPARAMF_TAG || !hdr.len || hdr.len > _flsh_tbl[which].blk_cnt*SEC_SIZE-HDR_SIZE) {
                os_printf("file %d: invalid header\n", which);
				goto out;
            }
            sec_dlen = hdr.len > (SEC_SIZE - HDR_SIZE) ? (SEC_SIZE - HDR_SIZE) : hdr.len;
            crc32 = calc_crc32(0, sec+HDR_SIZE, sec_dlen);
        }
        else {
            sec_dlen = hdr.len > SEC_SIZE ? SEC_SIZE : hdr.len;
            crc32 = calc_crc32(crc32, sec, sec_dlen);
        }
        hdr.len -= sec_dlen;

        if(sec_dlen && byts_copied < size) {
            int byt_cp = size - byts_copied;
            if(byt_cp > sec_dlen) byt_cp = sec_dlen;
            memcpy((char*)out + byts_copied, sec+HDR_SIZE, byt_cp);
            byts_copied += byt_cp;
        }

        if(!hdr.len) {
            if(crc32 != hdr.crc32) {
                os_printf("file %d: verify failed\n", which);
				goto out;
            }
            break;
        }
    }
	ret = byts_copied;

out:
	os_free_psram(sec);
	return ret;
}

static int paramf_write(int which, const void *_data, int size)
{
    if(size > _flsh_tbl[which].blk_cnt * SEC_SIZE - HDR_SIZE) {
        os_printf("file %d: size(%d) is too big\n", which, size);
        return -1;
    }

    uint8_t *sec = (uint8_t*)os_calloc_psram(1, SEC_SIZE);
    struct IPrmfHead *hdr = (struct IPrmfHead*)sec;
    int total_wrt = 0, i;
    char *data = (char*)_data;

	hdr->tag = IPCPARAMF_TAG;
	hdr->len = size;
	hdr->crc32 = calc_crc32(0, data, size);
    for(i=0; i<_flsh_tbl[which].blk_cnt; i++) {
        int byt_cp, valid_byts=0;
        if(i==0) {
            byt_cp = size>(SEC_SIZE-HDR_SIZE) ? (SEC_SIZE-HDR_SIZE) : size;
            memcpy(sec + HDR_SIZE, data, byt_cp);
            valid_byts = HDR_SIZE + byt_cp;
            memset(sec+HDR_SIZE+byt_cp, 0, SEC_SIZE-HDR_SIZE-byt_cp);
        }
        else {
            byt_cp = size > SEC_SIZE ? SEC_SIZE : size;
            memcpy(sec, data, byt_cp);
            valid_byts = byt_cp;
            memset(sec+byt_cp, 0, SEC_SIZE-byt_cp);
        }
        uint32_t addr = _flsh_tbl[which].blk_start+i*SEC_SIZE;
		if(write_flash(addr, sec, SEC_SIZE) < 0) {
            os_printf("file %d: write failed\n", which);
			total_wrt = -1;
			break;
        }
        total_wrt += byt_cp;
        size -= byt_cp;
        data += byt_cp;
        if(size <= 0) break;
    }

	os_free_psram(sec);
    return total_wrt;
}


int IpctLoadSetting(struct IpcWorkParam *param)
{
	memset(param, 0, sizeof(struct IpcWorkParam));
    int ret = paramf_read(PAREA_ID_APP, param, sizeof(struct IpcWorkParam));

    /*在这里设置缺省值*/
    if(param->mic_level == 0) param->mic_level = 50;
	return ret;
}

int IpctSaveSetting(const struct IpcWorkParam *param)
{
    return paramf_write(PAREA_ID_APP, param, sizeof(struct IpcWorkParam));
}

void IpctResetSetting(struct IpcWorkParam *param)
{
    memset(param, 0, sizeof(struct IpcWorkParam));
    paramf_write(PAREA_ID_APP, param, sizeof(struct IpcWorkParam));
}

int IpctLoadLicsence(struct IpcLic* lic)
{
    int ret = paramf_read(PAREA_ID_LIC, lic, sizeof(struct IpcLic));
    if(ret <= 0) memset(lic, 0, sizeof(struct IpcLic));
    return ret;
}
int IpctSaveLicsence(const struct IpcLic* lic)
{
    return paramf_write(PAREA_ID_LIC, lic, sizeof(struct IpcLic));
}
void IpctResetLicsence(struct IpcLic* lic)
{
    memset(lic, 0, sizeof(struct IpcLic));
    paramf_write(PAREA_ID_LIC, lic, sizeof(struct IpcLic));
}

cJSON *IpctLoadConfig()
{
#if 0 //TODO: read configuratiob from SD card
    FILE *fp = fopen(CONFIG_STORAGE_PATH"/C/jl_ipccfg.dat", "r");
    if(fp) {
        int fsize = flen(fp);
        char *buf = (char*)malloc(fsize+1);
        fread(fp, buf, fsize);
        buf[fsize] = '\0';
        fclose(fp);
        cJSON *json = cJSON_Parse(buf);
        free(buf);
        return json;
    }
    LogV("Can not open jl_ipccfg.dat\n");
#endif
    return NULL;
}

/*-----------------------------------------------------------------------------
 * 探鸽 SDK 内部参数读写接口
 *
 * 设计上有两个走法：
 *   PARAM_IN_MEM=1 (默认)：alloc_and_readall 把"上一次 write_params 缓存的 buffer"
 *     直接搬给 SDK；write_params 把 SDK 给的 buffer 持有住。重启不持久化。
 *   PARAM_IN_MEM 未定义：每次 read 都从 flash PAREA_ID_SDK 读、write 都写回 flash。
 *     重启持久化，但每次写都触发一次 flash erase。
 *
 * #defined PARAM_IN_MEM （仅内存缓存）。需要持久化时去掉这个宏。
 *---------------------------------------------------------------------------*/
//#define PARAM_IN_MEM

#ifdef PARAM_IN_MEM
static int _data_len = 0;
static uint8_t *_data = NULL;
#endif
static int alloc_and_readall(SIMPLEBUFFER *bf, int cbExtra)
{
    if(!bf->data || bf->size <= 0){
        bf->size = 4096;       /* 一个 sector，留给 SDK 写参数足够 */
        bf->data = (uint8_t*)os_calloc_psram(1, bf->size);
        if (!bf->data) {
            printf("alloc_and_readall os_calloc_psram(%u) failed\n", bf->size);
            bf->len = 0;
            return 0;
        }
    }
#ifdef PARAM_IN_MEM
    if(!_data){
		_data = (uint8_t*)os_calloc_psram(1, 4096);
        if(!_data){
            printf("os_calloc_psram failed\n");
            return 0;
        }else{
            int n = paramf_read(PAREA_ID_SDK, bf->data, bf->size);
            if(n > 0 && n <= 4096){
                memcpy(_data, bf->data, n);
                _data_len = bf->len = n;
            }
        }
    }else {
        if (_data && _data_len > 0 && (uint32_t)_data_len <= bf->size) {
            memcpy(bf->data, _data, _data_len);
            bf->len = _data_len;
        } else {
            bf->len = 0;
        }
    }
#else
    int n = paramf_read(PAREA_ID_SDK, bf->data, bf->size);
    bf->len = (n > 0) ? n : 0;
#endif
    return 1;
}

static int write_params(SIMPLEBUFFER *bf)
{
#ifdef PARAM_IN_MEM
	if (!_data) {
		_data = (uint8_t*)os_calloc_psram(1, 4096);
        if(!_data){
            printf("os_calloc_psram failed\n");
            return 0;
        }
	}
    if(bf->len > 4096){
        printf("data too large\n");
    }
    memcpy(_data, bf->data, bf->len);
	_data_len = bf->len;
#else
    paramf_write(PAREA_ID_SDK, bf->data, bf->len);
#endif
    return 1;
}

static void free_buff(SIMPLEBUFFER *bf)
{
    if(bf->data){
        os_free_psram(bf->data);
        bf->data = NULL;
    }
}

static void remove_paramf()
{
#ifdef PARAM_IN_MEM
    if (_data) {
        os_free_psram(_data);
        _data = NULL;
    }
    _data_len = 0;
#else
    paramf_write(PAREA_ID_SDK, NULL, 0);
#endif
}

struct paramf_ops _txw81x_paramf_ops = {
    alloc_and_readall,
    write_params,
    free_buff,
    remove_paramf
};
