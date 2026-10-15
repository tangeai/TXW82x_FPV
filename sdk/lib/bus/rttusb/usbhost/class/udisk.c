/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2011-12-12     Yi Qiu      first version
 */

#include <rtthread.h>
#include <include/rttusb_host.h>

#ifdef RT_USBH_MSTORAGE
#include "mass.h"

#include "dev.h"
#include "devid.h"
#include "diskio.h"
#include "ff.h"

#include "osal/string.h"
#include "osal_file.h"
#include "lib/ota/fw.h"
#include "tx_platform.h"
#include "dev/csi/hgdvp.h"

#define UDISK_OTA_DEMO         0

#define UDISK_MAX_COUNT        2
#define UDISK_CACHE_SIZE       (512)
#define UDISK_SRAM_XFER_SIZE   (4096)
#define UDISK_SRAM_XFER_SECTORS (UDISK_SRAM_XFER_SIZE / SECTOR_SIZE)

static rt_uint8_t _udisk_idset = 0;
static rt_uint8_t udisk_ota = 0;

FATFS *udisk_fs[UDISK_MAX_COUNT] = {NULL};

static int udisk_get_id(void)
{
    int i;

    for(i=0; i< UDISK_MAX_COUNT; i++)
    {
        if((_udisk_idset & (1 << i)) != 0) continue;
        else break;
    }

    /* it should not happen */
    if(i == UDISK_MAX_COUNT) RT_ASSERT(0);

    _udisk_idset |= (1 << i);
    return i;
}

static void udisk_free_id(int id)
{
    RT_ASSERT(id < UDISK_MAX_COUNT);

    _udisk_idset &= ~(1 << id);
}

static rt_bool_t udisk_is_psram_buffer(void *buffer, rt_size_t len)
{
    rt_uint32_t start;
    rt_uint32_t end;

    if((buffer == RT_NULL) || (len == 0))
    {
        return RT_FALSE;
    }

    start = (rt_uint32_t)buffer;
    end = start + len - 1;
    if(end < start)
    {
        return RT_FALSE;
    }

#ifdef IS_PSRAM_ADDR
    if(IS_PSRAM_ADDR(start) && IS_PSRAM_ADDR(end))
    {
        return RT_TRUE;
    }
#endif

    return RT_FALSE;
}

static rt_err_t udisk_alloc_rx_sram_xfer_buff(struct udisk_device *disk)
{
    if(disk == RT_NULL)
    {
        return -RT_ERROR;
    }

    if(disk->rx_buff == RT_NULL)
    {
        disk->rx_buff = (rt_uint8_t *)rt_malloc(UDISK_SRAM_XFER_SIZE + USB_RX_BUFF_RESERVE_SIZE);
        if(disk->rx_buff == RT_NULL)
        {
            rt_kprintf("udisk alloc rx sram buff failed\n");
            return -RT_ENOMEM;
        }
    }

    return RT_EOK;
}

static rt_err_t udisk_alloc_tx_sram_xfer_buff(struct udisk_device *disk)
{
    if(disk == RT_NULL)
    {
        return -RT_ERROR;
    }

    if(disk->tx_buff == RT_NULL)
    {
        disk->tx_buff = (rt_uint8_t *)rt_malloc(UDISK_SRAM_XFER_SIZE + USB_RX_BUFF_RESERVE_SIZE);
        if(disk->tx_buff == RT_NULL)
        {
            rt_kprintf("udisk alloc tx sram buff failed\n");
            return -RT_ENOMEM;
        }
    }

    return RT_EOK;
}

static void udisk_free_sram_xfer_buff(struct udisk_device *disk)
{
    if(disk == RT_NULL)
    {
        return;
    }

    if(disk->rx_buff)
    {
        rt_free(disk->rx_buff);
        disk->rx_buff = RT_NULL;
    }

    if(disk->tx_buff)
    {
        rt_free(disk->tx_buff);
        disk->tx_buff = RT_NULL;
    }
}

static DSTATUS rt_udisk_status(void *dev)
{
    struct udisk_device *disk = (struct udisk_device *)dev;
    if((disk==NULL) || (disk->intf == NULL))
    {
        rt_kprintf("%s disk is null!!!\n",__FUNCTION__);
        return RES_ERROR;
    }

    return RES_OK;
}

static DSTATUS rt_udisk_init(void *dev)
{
    printf("%s %d\n",__FUNCTION__,__LINE__); 

    struct udisk_device *disk = (struct udisk_device *)dev;
    if(!disk)
    {
        rt_kprintf("disk is null!!!\n");
        return RES_ERROR;
    }


    return RES_OK;
}

static DRESULT rt_udisk_read(void *dev, BYTE* buffer, DWORD sector,
    UINT count)
{  
    rt_err_t ret;
    struct uhintf* intf;
    int timeout = USB_TIMEOUT_LONG/5;
    rt_size_t total_len = (rt_size_t)count * SECTOR_SIZE;
    struct udisk_device *disk = (struct udisk_device *)dev;

    /* check parameter */
    if((disk==NULL) ||(buffer==NULL) || (disk->intf == NULL))
    {
        rt_kprintf("%s disk is null!!!\n",__FUNCTION__);
        return RES_ERROR;
    }

    if(total_len > 4096) timeout *= 2;

    intf = disk->intf;

    //os_printf("%s sector:%d count:%d\n",__FUNCTION__,sector,count);

    if(udisk_is_psram_buffer(buffer, total_len))
    {
        UINT remain = count;
        DWORD cur_sector = sector;
        rt_uint8_t *dst = (rt_uint8_t *)buffer;

        if((disk->rx_buff == RT_NULL) && (udisk_alloc_rx_sram_xfer_buff(disk) != RT_EOK))
        {
            return RES_ERROR;
        }

        while(remain)
        {
            UINT cur_count = (remain > UDISK_SRAM_XFER_SECTORS) ? UDISK_SRAM_XFER_SECTORS : remain;
            rt_size_t cur_len = (rt_size_t)cur_count * SECTOR_SIZE;

            ret = rt_usbh_storage_read10(intf, disk->rx_buff, cur_sector, cur_count, timeout);
            if (ret != RT_EOK)
            {
                rt_kprintf("usb mass_storage read failed\n");
                return RES_ERROR;
            }

            hw_memcpy(dst, disk->rx_buff, cur_len);
            dst += cur_len;
            cur_sector += cur_count;
            remain -= cur_count;
        }

        return RES_OK;
    }

    ret = rt_usbh_storage_read10(intf, (rt_uint8_t*)buffer, sector, count, timeout);

    if (ret != RT_EOK)
    {
        rt_kprintf("usb mass_storage read failed\n");
        return RES_ERROR;
    }

    return RES_OK;
}

static DRESULT rt_udisk_write (void *dev, BYTE* buffer, DWORD sector,
    UINT count)
{
    rt_err_t ret;
    struct uhintf* intf;
    int timeout = USB_TIMEOUT_LONG/5;
    rt_size_t total_len = (rt_size_t)count * SECTOR_SIZE;
    struct udisk_device *disk = (struct udisk_device *)dev;

    /* check parameter */
    if((disk==NULL) ||(buffer==NULL) || (disk->intf == NULL)){
	    rt_kprintf("udisk write parameter error\n");
	    return RES_ERROR;
    }

    if(total_len > 4096) timeout *= 2;

    intf = disk->intf;

    //os_printf("%s write sector:%d count:%d \n",__FUNCTION__,sector,count);

    if(udisk_is_psram_buffer(buffer, total_len))
    {
        UINT remain = count;
        DWORD cur_sector = sector;
        rt_uint8_t *src = (rt_uint8_t *)buffer;

        if((disk->tx_buff == RT_NULL) && (udisk_alloc_tx_sram_xfer_buff(disk) != RT_EOK))
        {
            return RES_ERROR;
        }

        while(remain)
        {
            UINT cur_count = (remain > UDISK_SRAM_XFER_SECTORS) ? UDISK_SRAM_XFER_SECTORS : remain;
            rt_size_t cur_len = (rt_size_t)cur_count * SECTOR_SIZE;

            hw_memcpy(disk->tx_buff, src, cur_len);
            ret = rt_usbh_storage_write10(intf, disk->tx_buff, cur_sector, cur_count, timeout);
            if (ret != RT_EOK)
            {
                rt_kprintf("usb mass_storage write %d sector failed\n", cur_count);
                return RES_ERROR;
            }

            src += cur_len;
            cur_sector += cur_count;
            remain -= cur_count;
        }

        return RES_OK;
    }

    ret = rt_usbh_storage_write10(intf, (rt_uint8_t*)buffer, sector, count, timeout);
    if (ret != RT_EOK)
    {
        rt_kprintf("usb mass_storage write %d sector failed\n", count);
        return RES_ERROR;
    }

    return RES_OK;

}

static DRESULT rt_udisk_control(void *dev, BYTE cmd, void *buf)
{
    printf("%s %d\n",__FUNCTION__,__LINE__);

    struct udisk_device *udisk = (struct udisk_device *)dev;
	rt_uint8_t ret	= RES_OK;
    //os_printf("cmd:%d\n",cmd);
	switch(cmd)
    {
		case CTRL_SYNC:
			break;
		case GET_SECTOR_COUNT:
			*(DWORD *)buf = udisk->count;	
			ret = RES_OK;
			break;

		case GET_SECTOR_SIZE:
			*(WORD *)buf = udisk->sector_size;
			ret = RES_OK;
			
			break;
		case GET_BLOCK_SIZE:
			*(DWORD *)buf = 1;

			ret = RES_OK;
			break;

				
		default:
			ret = RES_ERROR;
			printf("rtos_sd_ioctl err\n");
			break;
    }

    return ret;
}

#ifdef RT_USING_DEVICE_OPS
const static struct rt_device_ops udisk_device_ops =
{
    rt_udisk_init,
    RT_NULL,
    RT_NULL,
    rt_udisk_read,
    rt_udisk_write,
    rt_udisk_control
};
#endif

static struct fatfs_diskio udisk_driver = 
{
    .status = rt_udisk_status,
    .init = rt_udisk_init,
    .read = rt_udisk_read,
    .write = rt_udisk_write,
    .ioctl = rt_udisk_control,
};


void rt_udisk_ota_thread(void)
{

#if DVP_EN	
		void *dvp = (void *)dev_get(HG_DVP_DEVID);
		if(dvp)
		{
			dvp_close(dvp);
		}
#endif

if(!udisk_ota){
    rt_uint8_t *cache_buf = NULL;
    void *fp = osal_fopen("USB:/UPDATE.BIN","r");
    if(!fp)
    {
        rt_kprintf("udisk ota file not open\n");
        goto __udisk_ota_end;
    }
    rt_uint32_t filesize = osal_fsize(fp);
    rt_uint32_t filesize_tmp = filesize;
    rt_uint32_t readsize = UDISK_CACHE_SIZE;
    rt_uint32_t ota_offset = 0;
    cache_buf = (rt_uint8_t *)os_malloc(UDISK_CACHE_SIZE);
    if(!cache_buf)
    {
        rt_kprintf("cache_buf malloc failed\n");
        goto __udisk_ota_end;
    }
    rt_kprintf("filesize:%d cache_buf:%x\n",filesize,cache_buf);

    while(filesize)
    {
        if(filesize < UDISK_CACHE_SIZE)
        {
            readsize = filesize;
        }

        osal_fread(cache_buf,readsize,1,fp);
        libota_write_fw(filesize_tmp,ota_offset,cache_buf,readsize);
        filesize -= readsize;
        ota_offset += readsize;
    }

    udisk_ota = 1;

__udisk_ota_end:
    if(fp)
    {
        osal_fclose(fp);
    }

    if(cache_buf)
    {
        os_free(cache_buf);
    }
}  

}

/**
 * This function will run udisk driver when usb disk is detected.
 *
 * @param intf the usb interface instance.
 *
 * @return the error code, RT_EOK on successfully.
 */
rt_err_t rt_udisk_run(struct uhintf* intf)
{
    int i = 0;
    rt_err_t ret;
    char dname[8];
    char sname[8];
    rt_align(4) rt_uint8_t max_lun[1 + USB_RX_BUFF_RESERVE_SIZE];
    rt_uint8_t *sector;
    rt_align(4) rt_uint8_t sense[18 + USB_RX_BUFF_RESERVE_SIZE];
    rt_align(4) rt_uint8_t inquiry[36 + USB_RX_BUFF_RESERVE_SIZE];
    ustor_t stor;

    /* check parameter */
    RT_ASSERT(intf != RT_NULL);
    printf("%s %d\n",__FUNCTION__,__LINE__);
    /* set interface */
//    ret = rt_usbh_set_interface(intf->device, intf->intf_desc->bInterfaceNumber);
//    if(ret != RT_EOK)
//        rt_usbh_clear_feature(intf->device, 0, USB_FEATURE_ENDPOINT_HALT);
    /* reset mass storage class device */
    ret = rt_usbh_storage_reset(intf);
    if(ret != RT_EOK) return ret;

    stor = (ustor_t)intf->user_data;
    stor->dev_cnt = 1;

    /* get max logic unit number */
    ret = rt_usbh_storage_get_max_lun(intf, max_lun);
    if(ret != RT_EOK)
        rt_usbh_clear_feature(intf->device, 0, USB_FEATURE_ENDPOINT_HALT);

    /* reset pipe in endpoint */
    if(stor->pipe_in->status == UPIPE_STATUS_STALL)
    {
        ret = rt_usbh_clear_feature(intf->device,
        stor->pipe_in->ep.bEndpointAddress, USB_FEATURE_ENDPOINT_HALT);
        printf("%s %d\n",__FUNCTION__,__LINE__);
        if(ret != RT_EOK) return ret;
    }


    /* reset pipe out endpoint */
    if(stor->pipe_out->status == UPIPE_STATUS_STALL)
    {
        ret = rt_usbh_clear_feature(intf->device,
        stor->pipe_out->ep.bEndpointAddress, USB_FEATURE_ENDPOINT_HALT);
        printf("%s %d\n",__FUNCTION__,__LINE__);
        if(ret != RT_EOK) return ret;
    }

    while((ret = rt_usbh_storage_inquiry(intf, inquiry)) != RT_EOK)
    {
        if(ret == -RT_EIO) return ret;

        rt_thread_delay(5);
        if(i++ < 10) continue;
        rt_kprintf("rt_usbh_storage_inquiry error\n");
        return -RT_ERROR;
    }

    i = 0;

    /* wait device ready */
    while((ret = rt_usbh_storage_test_unit_ready(intf)) != RT_EOK)
    {
        if(ret == -RT_EIO) return ret;

        ret = rt_usbh_storage_request_sense(intf, sense);
        if(ret == -RT_EIO) return ret;

        rt_thread_delay(10);
        if(i++ < 10) continue;

        rt_kprintf("rt_usbh_storage_test_unit_ready error\n");
        return -RT_ERROR;
    }

    i = 0;
    rt_memset(stor->capicity, 0, sizeof(stor->capicity));

    /* get storage capacity */
    while((ret = rt_usbh_storage_get_capacity(intf,
        (rt_uint8_t*)stor->capicity)) != RT_EOK)
    {
        if(ret == -RT_EIO) return ret;

        rt_thread_delay(50);
        if(i++ < 10) continue;

        stor->capicity[0] = 2880;
        stor->capicity[1] = 0x200;

        rt_kprintf("rt_usbh_storage_get_capacity error\n");
        break;
    }

    stor->capicity[0] = uswap_32(stor->capicity[0]);
    stor->capicity[1] = uswap_32(stor->capicity[1]);
    stor->capicity[0] += 1;

    rt_kprintf("capicity %d, block size %d\n",
        stor->capicity[0], stor->capicity[1]);

    /* get the first sector to read partition table */
    sector = (rt_uint8_t*) rt_malloc (SECTOR_SIZE + USB_RX_BUFF_RESERVE_SIZE);
    if (sector == RT_NULL)
    {
        rt_kprintf("allocate partition sector buffer failed\n");
        return -RT_ERROR;
    }

    rt_memset(sector, 0, SECTOR_SIZE);

    rt_kprintf("read partition table\n");

    /* get the partition table */
    ret = rt_usbh_storage_read10(intf, sector, 0, 1, USB_TIMEOUT_LONG);
    if(ret != RT_EOK)
    {
        rt_kprintf("read parition table error\n");

        rt_free(sector);
        return -RT_ERROR;
    }

    rt_kprintf("finished reading partition\n");


    int res = 0;

    struct ustor_data* data = rt_zalloc(sizeof(struct ustor_data));
    if (data == RT_NULL)
    {
        rt_kprintf("Allocate partition data buffer failed.");
        rt_free(sector);
        return -RT_ERROR;
    }

    struct udisk_device *udisk = rt_zalloc(sizeof(struct udisk_device));
    if (udisk == RT_NULL)
    {
        rt_kprintf("Allocate udisk failed.");
        rt_free(data);
        rt_free(sector);
        return -RT_ERROR;   
    }

    rt_memset(data, 0, sizeof(struct ustor_data));
    data->intf = intf;
    data->udisk_id = udisk_get_id();
    rt_kprintf("udisk_id:%d\n", data->udisk_id);
    os_snprintf(dname, 6, "ud%d-%d", data->udisk_id, 0);
    os_snprintf(sname, 8, "sem_ud%d",  0);

    /* register sdcard device */
    stor->dev[0].type    = 0;           //RT_Device_Class_Block;
#ifdef RT_USING_DEVICE_OPS
    stor->dev[0].ops     = &udisk_device_ops;
#else
    stor->dev[0].status  = rt_udisk_status;
    stor->dev[0].init    = rt_udisk_init;
    stor->dev[0].read    = rt_udisk_read;
    stor->dev[0].write   = rt_udisk_write;
    stor->dev[0].ioctl = rt_udisk_control;
#endif
    stor->dev[0].user_data = (void*)data;

    stor->udisk = udisk;

    rt_kprintf("%s %d alloc data:0x%x\n",__FUNCTION__,__LINE__,data);
    rt_kprintf("%s %d alloc disk:0x%x\n",__FUNCTION__,__LINE__,udisk);

    struct udisk_device *disk = stor->udisk;
    disk->count = stor->capicity[0];
    disk->sector_size = stor->capicity[1];
    disk->user_data = data;
    disk->intf = intf;


    char target[8];
    os_sprintf(target, "%d:", DEV_USB + data->udisk_id);
    fatfs_register_drive(DEV_USB + data->udisk_id, &udisk_driver, disk);
    if(!udisk_fs[data->udisk_id])
    {
        udisk_fs[data->udisk_id] = (FATFS *)rt_malloc(sizeof(FATFS));
        rt_kprintf("%s %d id:%d udisk_fs:0x%x\n",__FUNCTION__,__LINE__,data->udisk_id,udisk_fs[data->udisk_id]);
    }

    if(udisk_fs[data->udisk_id])
    {
        res = f_mount(udisk_fs[data->udisk_id], target, 1);
        if(res)
        {
            rt_kprintf("%s mount fatfs err:%d\n",__FUNCTION__,res);
            rt_free(sector);
            return -RT_ERROR;
        }
    }
  
    rt_kprintf("%s %d: target:%s source:%s\n", __FUNCTION__, __LINE__, target, DEV_USB + data->udisk_id);

    DIR dir;
    FILINFO f_info;
    rt_uint8_t maxdir = 0;
    FRESULT rets;
    rets = f_opendir(&dir, target);
    if (rets != FR_OK) {
        printf("failed open\n");
        rt_free(sector);
        return -RT_ERROR;
    }
    rt_kprintf("===========USB DIR===========\n");
    while (1) {
            rets = f_readdir(&dir, &f_info); 
            if (rets != FR_OK) {
                break;
            }
            if (f_info.fname[0] == 0) {
                break; 
            } else {
            if (f_info.fattrib) { 
                printf("%s \n", f_info.fname);
                maxdir++;
            }
        }
    }
    rt_kprintf("=============================\n");
    rt_kprintf("%s %d\n",__FUNCTION__,__LINE__);



#if UDISK_OTA_DEMO
    rt_thread_t thread;
    thread = rt_thread_create("udisk_test",rt_udisk_ota_thread,NULL,4096,OS_TASK_PRIORITY_NORMAL,0);
    if(thread != RT_NULL)
    {
        rt_thread_startup(thread);
    }    
#endif

    rt_free(sector);

    return RT_EOK;
}

/**
 * This function will be invoked when usb disk plug out is detected and it would clean
 * and release all udisk related resources.
 *
 * @param intf the usb interface instance.
 *
 * @return the error code, RT_EOK on successfully.
 */
rt_err_t rt_udisk_stop(struct uhintf* intf)
{
    int i;
    ustor_t stor;
    struct ustor_data* data;

    /* check parameter */
    RT_ASSERT(intf != RT_NULL);
    RT_ASSERT(intf->device != RT_NULL);

    stor = (ustor_t)intf->user_data;
    RT_ASSERT(stor != RT_NULL);

    for(i=0; i<stor->dev_cnt; i++)
    {
        struct ustor_device *dev = &stor->dev[i];
        data = (struct ustor_data*)dev->user_data;
        if(data == NULL){
            continue; 
        }
        struct udisk_device *disk = stor->udisk;
        if (disk) {
            disk->intf = NULL;
        }

        /* unmount filesystem */
        char target[8];
        os_sprintf(target, "%d:", DEV_USB + data->udisk_id);
        f_umount(target);

        rt_kprintf("%s %d: target:%s source:%d\n", __FUNCTION__, __LINE__, target, data->udisk_id);

        if(udisk_fs[data->udisk_id])
        {
            rt_kprintf("%s %d id:%d udisk_fs:0x%x\n",__FUNCTION__,__LINE__,data->udisk_id,udisk_fs[data->udisk_id]);
            rt_free(udisk_fs[data->udisk_id]);
            udisk_fs[data->udisk_id] = NULL;
        }

        udisk_free_sram_xfer_buff(disk);
        udisk_free_id(data->udisk_id);

        if (disk) {
            rt_kprintf("%s %d free disk:0x%x\n",__FUNCTION__,__LINE__,disk);
            rt_free(disk);
        }

        if (data) {
            rt_kprintf("%s %d free data:0x%x\n",__FUNCTION__,__LINE__,data);
            rt_free(data);
        }
    }

    return RT_EOK;
}

#endif

