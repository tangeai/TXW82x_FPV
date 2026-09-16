#include "sys_config.h"
#include "integer.h"
#include "diskio.h"
#include "ff.h"
#include <stdio.h>
#include "osal/sleep.h"
#include "typesdef.h"
#include "osal/task.h"
#include "osal/semaphore.h"
#include "osal/mutex.h"
#include "osal/irq.h"
#include "list.h"
#include "dev.h"
#include "sdhost.h"
#include "devid.h"

#include "osal/string.h"
#include "osal/work.h"

// #include "osal.h"

#define FAT_INFO_SHOW(...) //printf(__VA_ARGS__)

// #define FAT_TIME

#if FS_EN
static uint8_t fat_ready = 0;
/* 仅显式挂载允许初始化硬件，普通 fopen 不得绕过应用的旧句柄排空。 */
static volatile uint8_t fat_mounting;
/* 索引缓存用的卷/故障代号：重挂载或磁盘 IO 失败后，旧查询快照立即失效。 */
static volatile uint32_t fat_storage_generation = 1;
uint32_t fatfs_storage_generation(void) { return fat_storage_generation; }
static void fatfs_storage_changed(void)
{
    uint32_t flags = disable_irq();
    ++fat_storage_generation;
    enable_irq(flags);
}
static DRESULT fatfs_index_io_result(DRESULT res)
{
    if (res != RES_OK) {
        fatfs_storage_changed();
        if (res != RES_PARERR) sd_storage_request_recovery();
    }
    return res;
}

uint8_t get_fat_isready()
{
	return fat_ready;
}

void set_fat_ready(uint8_t ready)
{
	fat_ready = ready;
}

static DSTATUS fatfs_status(void *status);
static DSTATUS fatfs_init(void *init_dev);
static DRESULT fatfs_read(void *dev, BYTE *buf, DWORD sector, UINT count);
static DRESULT fatfs_write(void *dev, BYTE *buf, DWORD sector, UINT count);
static DRESULT fatfs_ioctl(void *init_dev, BYTE cmd, void *buf);
uint32 get_sdhost_status(struct sdh_device *host);
uint32 sd_tran_stop(struct sdh_device *host);
static const struct fatfs_diskio  sdcdisk_driver = {
	.status = fatfs_status,
	.init = fatfs_init,
	.read = fatfs_read,
	.write = fatfs_write,
	.ioctl = fatfs_ioctl};

static FATFS fatfs[1];

DWORD get_fatbase(int num)
{
	return fatfs[num].fatbase;
}

DWORD get_fatfree(int num)
{
	DWORD fre_clust, fre_sect, tot_sect;
	FATFS *fs = &fatfs[num];
	fre_clust = fs->free_clst;
	tot_sect = (fs->n_fatent - 2) * fs->csize;
	fre_sect = fre_clust * fs->csize;
	printf("%s %ldKB\n", __FUNCTION__, fre_sect >> 1);
	return fre_sect >> 1;
}

static DSTATUS fatfs_status(void *status)
{
	// FAT_INFO_SHOW ("fatfs_status_test\r\n");
	uint32 err = get_sdhost_status(status);
	return err;
}

static DSTATUS fatfs_init(void *init_dev){
	printf ("fatfs_init_test\r\n");
	uint32 err = get_sdhost_status(init_dev);
	if(err)
	{
		if (sd_storage_app_managed() && !fat_mounting) return STA_NOINIT;
		/* 稳定性基线：24MHz；单扇区请求使用 CMD17/CMD24，多扇区仍批量传输。 */
		os_printf("[SD_BASELINE] clock=24000000 single_block=1 managed=%d\n", sd_storage_app_managed());
		err = sdhost_init(24 * 1000 * 1000, SDHC_INIT_FLAGS_SINGLE_BLK_RW_EN);
	}

	return err;
}

#if USE_FAT_CACHE
static volatile uint8_t fat_cache_paused;
// 内存分配函数
static void *fat_malloc(int size)
{
#ifdef PSRAM_HEAP
	return os_malloc_psram(size);
#else
	return os_malloc(size);
#endif
}

// 内存释放函数
static void fat_free(void *p)
{
#ifdef PSRAM_HEAP
	os_free_psram(p);
#else
	os_free(p);
#endif
}
struct fat_data_t
{
	// uint8 data[FAT_CACHE_SIZE * 512]; // 32KB 缓存
	BYTE *data;		// 32KB 缓存
	DWORD start_sector; // 缓存起始扇区
	DWORD fat_start;	// FAT起始扇区
	DWORD fat_end;
	DWORD offset;
	DWORD max_offset;
};

struct fat_cache_t
{
	BYTE fat_info_ready; //
	BYTE fat_init;
	BYTE fs_type;
	BYTE fs_fats;
	DWORD fs_size;
	DWORD fat_tick;
	#ifdef FAT_TIME
    os_timer_t fat_timer;
	#else
	struct os_work fat_wk;
	#endif
	struct os_mutex lock;
	struct fat_data_t fat1;
};



struct fat_cache_t fat_cache = {
	// lock和time初始化标志位，1是未初始化，0是已经初始化
	.fat_init = 1,
	.fat_info_ready = 1,
};

signed char update_fat_info(BYTE fmt, BYTE n_fats, DWORD sz_fat,DWORD fatbase, DWORD b_vol)
{
	if (fat_cache.fat_init != RET_OK){
		return RET_ERR;
	}

	os_mutex_lock(&fat_cache.lock, osWaitForever);

	fat_cache.fs_type = fmt;
	fat_cache.fs_fats = n_fats;
	fat_cache.fs_size = sz_fat;

	fat_cache.fat1.fat_start = fatbase;
	fat_cache.fat1.fat_end = fat_cache.fat1.fat_start + fat_cache.fs_size - 1;

	fat_cache.fat_info_ready = RET_OK;

	// 计算逻辑地址（扇区号）
	//UINT fat1_logical = fatbase - b_vol;                    // FAT1 logical start
	//UINT fat2_logical = fat1_logical + sz_fat;     // FAT2 logical start

	// 计算物理地址（加上分区偏移）
	//UINT partition_start = b_vol;                  // 分区起始扇区
	//UINT fat1_physical = partition_start + fat1_logical;
	//UINT fat2_physical = partition_start + fat2_logical;

	// if (fat_cache.fs_type == FS_EXFAT) // FS_EXFAT文件系统不需要优化
	// {
	// 	fat_cache.fat_info_ready = 0;
	// }

	if (fmt == FS_FAT12)
		FAT_INFO_SHOW("Filesystem Type: FS_FAT12 \r\n");
	else if (fmt == FS_FAT16)
		FAT_INFO_SHOW("Filesystem Type: FS_FAT16 \r\n");
	else if (fmt == FS_FAT32)
		FAT_INFO_SHOW("Filesystem Type: FS_FAT32 \r\n");
	else if (fmt == FS_EXFAT)
		FAT_INFO_SHOW("Filesystem Type: FS_EXFAT \r\n");

	FAT_INFO_SHOW("Filesystem fat_num %u \r\n", n_fats);
	FAT_INFO_SHOW("Filesystem fat_size %u \r\n", sz_fat);

	FAT_INFO_SHOW("Physical Address ===> fat1_start %u , fat1_end %u \r\n", fat_cache.fat1.fat_start, fat_cache.fat1.fat_end);
	FAT_INFO_SHOW("Logical Address ====> fat1_start %u , fat1_end %u \r\n", fat1_logical, fat1_logical + sz_fat - 1);

	if (n_fats > 1) {
		FAT_INFO_SHOW("Physical Address ===> fat2_start %u , fat2_end %u \r\n",  fat2_physical, fat2_physical + sz_fat - 1);
		FAT_INFO_SHOW("Logical Address ====> fat2_start %u , fat2_end %u \r\n", fat2_logical, fat2_logical + sz_fat - 1);
	}

	os_mutex_unlock(&fat_cache.lock);

	return RET_OK;
}

void update_io_timestamp()
{
	if (fat_cache_paused || fat_cache.fat_init != RET_OK || fat_cache.fat_info_ready != RET_OK){
		return;
	}
	os_mutex_lock(&fat_cache.lock, osWaitForever);
	fat_cache.fat_tick = os_jiffies();
	os_mutex_unlock(&fat_cache.lock);
}

/* 已持有缓存锁时同步；任一副本失败都保留脏标记供后续重试。 */
static int fat_cache_flush_locked(struct sdh_device *host, struct fat_data_t *cache)
{
    if (!cache->max_offset) return RES_OK;
    if (sd_multiple_write(host, cache->start_sector, cache->max_offset * 512, cache->data))
        return RES_ERROR;
    if (fat_cache.fs_fats > 1 &&
        sd_multiple_write(host, cache->start_sector + fat_cache.fs_size,
                          cache->max_offset * 512, cache->data))
        return RES_ERROR;
    cache->max_offset = 0;
    return RES_OK;
}
static int fat_cache_sync(struct sdh_device *host)
{
    int ret = RES_OK;
    if (fat_cache.fat_init != RET_OK || fat_cache.fat_info_ready != RET_OK) return ret;
    os_mutex_lock(&fat_cache.lock, osWaitForever);
    ret = fat_cache_flush_locked(host, &fat_cache.fat1);
    os_mutex_unlock(&fat_cache.lock);
    return ret;
}

#ifdef FAT_TIME
static void fat_loop(void *arg)
#else
static int32 fat_loop(struct os_work *work)
#endif
{
	if (fat_cache_paused || sd_storage_app_busy() ||
		fat_cache.fat_init != RET_OK || fat_cache.fat_info_ready != RET_OK){
		goto fat_loop_end;
	}

	uint8 ret = 0;
	struct sdh_device *sdh = NULL;
	sdh = (struct sdh_device *)dev_get(HG_SDIOHOST_DEVID);

	ret = os_mutex_lock(&sdh->lock, 0);
	if (ret != RET_OK)
	{
		fat_cache.fat_tick = os_jiffies();
		goto fat_loop_end; // 获取锁失败
	}
	os_mutex_unlock(&sdh->lock);
	ret = os_mutex_lock(&fat_cache.lock, 0);
	if (ret != RET_OK)
	{
		goto fat_loop_end; // 获取锁失败
	}

	// 检测到200ms没有操作SD卡，并SD卡在线，fat信息回写SD
	if (os_jiffies() - fat_cache.fat_tick > 200 && SD_OFF != sdh->sd_opt)
	{
		fat_cache.fat_tick = os_jiffies();
		fatfs_index_io_result(fat_cache_flush_locked(sdh, &fat_cache.fat1));
	}

	os_mutex_unlock(&fat_cache.lock);
fat_loop_end:
	#ifdef FAT_TIME
	return;
	#else
    if (!fat_cache_paused) os_run_work_delay(work, 50);
	return 0;
	#endif

}

static void init_fat_cache(FATFS *fs)
{
	if (update_fat_info(fs->fs_type, fs->n_fats, fs->fsize,fs->fatbase, fs->volbase) != RET_OK){
		return;
	}
	FAT_INFO_SHOW("init_fat_cache \r\n");
	struct sdh_device *sdh = NULL;
	sdh = (struct sdh_device *)dev_get(HG_SDIOHOST_DEVID);
	// 初始化后第一次读 FAT，在同一把锁内发布，避免后台读到半初始化窗口。
	os_mutex_lock(&fat_cache.lock, osWaitForever);
	fat_cache.fat1.start_sector = fs->fatbase;
	fat_cache.fat1.max_offset = 0;
	if (sd_multiple_read(sdh, fat_cache.fat1.start_sector, FAT_CACHE_SIZE * 512, fat_cache.fat1.data))
		fat_cache.fat1.start_sector = 0xffffffffU;
	os_mutex_unlock(&fat_cache.lock);
}

static void del_fat_cache(void)
{
	if (fat_cache.fat_init != RET_OK){
		return;
	}

	fat_cache.fat_init = 1;
	fat_cache.fat_info_ready = 1;
	FAT_INFO_SHOW("########### del_fat_cache \r\n");

	#ifdef FAT_TIME
	os_timer_stop(&fat_cache.fat_timer);
	os_timer_del(&fat_cache.fat_timer);// 先卸载定时器
	#else
	os_work_cancle(&fat_cache.fat_wk,1);
	#endif
	/* 取消任务时不能持有它可能正在等待的缓存锁。 */
	os_mutex_lock(&fat_cache.lock, osWaitForever);

	// 释放fat缓存
	if (fat_cache.fat1.data)
	{
		FAT_INFO_SHOW("%s %d fat free \r\n", __func__, __LINE__);
		fat_free(fat_cache.fat1.data);
		fat_cache.fat1.data = NULL;
	}
	os_mutex_unlock(&fat_cache.lock);
	os_mutex_del(&fat_cache.lock);
}

/* 未命中时先确认旧缓存落盘，再读取新窗口；读取失败不能发布无效窗口。 */
static DRESULT read_from_fat_cache(void *dev, struct fat_data_t *cache, BYTE *buf, DWORD sector, UINT count)
{
    int ret = RES_OK;
    if (fat_cache.fat_init != RET_OK || fat_cache.fat_info_ready != RET_OK)
        return sd_multiple_read(dev, sector, count * 512, buf);
    os_mutex_lock(&fat_cache.lock, osWaitForever);
    if (sector < cache->start_sector || sector + count > cache->start_sector + FAT_CACHE_SIZE) {
        ret = fat_cache_flush_locked(dev, cache);
        if (ret) goto done;
        /* 大请求或 FAT 尾部直接读，避免缓存预读跨出卡尾。 */
        if (count > FAT_CACHE_SIZE || sector + FAT_CACHE_SIZE - 1 > cache->fat_end) {
            ret = sd_multiple_read(dev, sector, count * 512, buf);
            goto done;
        }
        cache->start_sector = 0xffffffffU;
        ret = sd_multiple_read(dev, sector, FAT_CACHE_SIZE * 512, cache->data);
        if (ret) goto done;
        cache->start_sector = sector;
    }
    memcpy(buf, cache->data + (sector - cache->start_sector) * 512, count * 512);
done:
    os_mutex_unlock(&fat_cache.lock);
    return ret;
}
static DRESULT write_to_fat_cache(void *dev, struct fat_data_t *cache, BYTE *buf, DWORD sector, UINT count)
{
    int ret = RES_OK;
    if (fat_cache.fat_init != RET_OK || fat_cache.fat_info_ready != RET_OK)
        return sd_multiple_write(dev, sector, count * 512, buf);
    os_mutex_lock(&fat_cache.lock, osWaitForever);
    if (sector < cache->start_sector || sector + count > cache->start_sector + FAT_CACHE_SIZE) {
        ret = fat_cache_flush_locked(dev, cache);
        if (ret) goto done;
        if (count > FAT_CACHE_SIZE || sector + FAT_CACHE_SIZE - 1 > cache->fat_end) {
            cache->start_sector = 0xffffffffU;
            ret = sd_multiple_write(dev, sector, count * 512, buf);
            goto done;
        }
        cache->start_sector = 0xffffffffU;
        ret = sd_multiple_read(dev, sector, FAT_CACHE_SIZE * 512, cache->data);
        if (ret) goto done;
        cache->start_sector = sector;
    }
    cache->offset = sector - cache->start_sector;
    memcpy(cache->data + cache->offset * 512, buf, count * 512);
    if (cache->max_offset < cache->offset + count)
        cache->max_offset = cache->offset + count;
done:
    os_mutex_unlock(&fat_cache.lock);
    return ret;
}
#endif

/* 仅在应用排空全部 SD 使用者后调用：格式化前丢弃旧卷缓存，禁止后台再次回写。 */
void fatfs_cache_discard(void)
{
#if USE_FAT_CACHE
    if (fat_cache.fat_init != RET_OK) return;
    os_mutex_lock(&fat_cache.lock, osWaitForever);
    fat_cache.fat_info_ready = 1;
    fat_cache.fat1.max_offset = 0;
    fat_cache.fat1.start_sector = 0xffffffffU;
    fat_cache.fat1.fat_start = fat_cache.fat1.fat_end = 0;
    os_mutex_unlock(&fat_cache.lock);
#endif
}

DRESULT fatfs_read(void *dev, BYTE *buf, DWORD sector, UINT count)
{
#if USE_FAT_CACHE
	update_io_timestamp();
	if (sector >= fat_cache.fat1.fat_start && sector <= fat_cache.fat1.fat_end)
	{
		return fatfs_index_io_result(read_from_fat_cache((struct sdh_device *)dev, &fat_cache.fat1, buf, sector, count));
	}
#endif
    return fatfs_index_io_result(sd_multiple_read((struct sdh_device *)dev, sector, count * 512, buf));
}

static DRESULT fatfs_write(void *dev, BYTE *buf, DWORD sector, UINT count)
{
#if USE_FAT_CACHE
	update_io_timestamp();
	if (sector >= fat_cache.fat1.fat_start && sector <= fat_cache.fat1.fat_end)
	{
		return fatfs_index_io_result(write_to_fat_cache((struct sdh_device *)dev, &fat_cache.fat1, buf, sector, count));
	}
#endif
	return fatfs_index_io_result(sd_multiple_write((struct sdh_device *)dev, sector, count * 512, buf));
}

extern unsigned int sd_dwCap;
extern uint32 fatfs_sd_tran_stop(struct sdh_device *host);
static DRESULT fatfs_ioctl(void *init_dev, BYTE cmd, void *buf)
{
	uint8 ret = RES_OK;
	switch (cmd)
	{
	case CTRL_SYNC:
		if (fatfs_sd_tran_stop(init_dev)) ret = RES_ERROR;

#if USE_FAT_CACHE
		if (fat_cache_sync(init_dev)) ret = RES_ERROR;
		/* FAT 回写也可能启动多块传输，完成它以后才可向 f_sync 报成功。 */
		if (fatfs_sd_tran_stop(init_dev)) ret = RES_ERROR;
#endif
		break;
	case GET_SECTOR_COUNT:
		*(DWORD *)buf = sd_dwCap * 2;
		ret = RES_OK;
		break;

	case GET_SECTOR_SIZE:
		*(WORD *)buf = 512;
		ret = RES_OK;

		break;
	case GET_BLOCK_SIZE:
		*(DWORD *)buf = 4;
		// printf("*0B:%d\n",*B);
		ret = RES_OK;
		break;

	default:
		ret = RES_PARERR; /* 未支持的控制命令不是介质故障。 */
		printf("rtos_sd_ioctl err\n");
		break;
	}
	return fatfs_index_io_result(ret);
}

bool fatfs_register()
{
	fatfs_storage_changed();
	set_fat_ready(0);  /* 挂载失败时不能继续沿用上一次就绪状态。 */
	int ret = 1;
	struct sdh_device *fatfs_sdh;
	// printf(">>>>>>>>>> enter %s test\r\n", __func__);
	fatfs_sdh = (struct sdh_device *)dev_get(HG_SDIOHOST_DEVID);

#if USE_FAT_CACHE
	fat_cache_paused = 0;
	if (fat_cache.fat_init)
	{
		// 分配 fat1 缓存
		fat_cache.fat1.data = fat_malloc(FAT_CACHE_SIZE * 512);
		if (!fat_cache.fat1.data) return FR_NOT_ENOUGH_CORE;
		if (os_mutex_init(&fat_cache.lock) != RET_OK) {
			fat_free(fat_cache.fat1.data);
			fat_cache.fat1.data = NULL;
			return FR_NOT_ENOUGH_CORE;
		}
#ifdef FAT_TIME
		ret = os_timer_init(&fat_cache.fat_timer, fat_loop, OS_FAT_TIMER_MODE_PERIODIC, 0);
#else
		ret = OS_WORK_INIT(&fat_cache.fat_wk, fat_loop, 0);
#endif
		if (ret != RET_OK) {
			/* 重复恢复遇到分配失败时必须归还缓存，避免每轮泄漏 32KB。 */
			os_mutex_del(&fat_cache.lock);
			fat_free(fat_cache.fat1.data);
			fat_cache.fat1.data = NULL;
			return FR_NOT_ENOUGH_CORE;
		}
		fat_cache.fat_init = RET_OK;
#ifdef FAT_TIME
		os_timer_start(&fat_cache.fat_timer, 50);
#else
		os_run_work_delay(&fat_cache.fat_wk, 50);
#endif

	}
#endif

	ret = FR_NOT_READY;
	if (fatfs_sdh)
	{
		fatfs_register_drive(0, (struct fatfs_diskio*)&sdcdisk_driver, fatfs_sdh);
		fat_mounting = 1;
		ret = f_mount(&fatfs[0], _SYSDSK_, 1);
		fat_mounting = 0;
		if (ret)
		{
			printf("%s ret:%d\n", __FUNCTION__, ret);
			f_mount(NULL, _SYSDSK_, 0);
			return ret;
		}
		FAT_INFO_SHOW("f_mount success\r\n");
		set_fat_ready(1);
#if USE_FAT_CACHE
		init_fat_cache(&fatfs[0]);
#endif
	}
	return ret;
}

/* 调用者必须先封闭应用准入并等待所有文件使用者退出。 */
int fatfs_prepare_remount(void)
{
	fatfs_storage_changed();
	int ret = 1;
	FAT_INFO_SHOW(">>>>>>>>>>enter %s test\r\n", __func__);
	struct sdh_device *fatfs_sdh;
	fatfs_sdh = (struct sdh_device *)dev_get(HG_SDIOHOST_DEVID);
	if (fatfs_sdh)
	{
#if USE_FAT_CACHE
		fat_cache_paused = 1;
		if (fat_cache.fat_init == RET_OK) {
#ifdef FAT_TIME
			os_timer_stop(&fat_cache.fat_timer);
#else
			os_work_cancle(&fat_cache.fat_wk, 1);
#endif
		}
		fatfs_cache_discard();
#endif
		fatfs_register_drive(0, (struct fatfs_diskio*)&sdcdisk_driver, fatfs_sdh);
		ret = f_mount(NULL, _SYSDSK_, 0);
		if (ret)
		{
			printf("%s ret:%d\n", __FUNCTION__, ret);
			return ret;
		}
		set_fat_ready(0);
#if USE_FAT_CACHE
		del_fat_cache();
#endif
	}
	return ret;
}

void fatfs_unregister(void)
{
	fatfs_prepare_remount();
}

int fatfs_recover_mount(void)
{
	int ret = fatfs_prepare_remount();
	if (ret) return ret;
	struct sdh_device *host = (struct sdh_device *)dev_get(HG_SDIOHOST_DEVID);
	if (!host) return FR_NOT_READY;
	os_mutex_lock(&host->lock, osWaitForever);
	host->sd_opt = SD_OFF;
	host->sd_stop = 0;
	os_mutex_unlock(&host->lock);
	return fatfs_register();
}

#endif
