#include "TgCloudApi.h"  // <-- 唯一必须包含的头文件
#include "ec_const.h"
#include "TgCloudApi_mufb.h"
#include "osal/task.h"
void mufbuff_demo(void*arg)
{
    MUFBCLT clt;
    int ret = TcfbClientInit(&clt);
    if(ret != 0){
        printf("failed\n");
        return;
    }

   MUFHEADER *pfh = NULL;
   int b_overwritten = 0;
   uint32_t utc_time = 0;
   int get_key_frame = 0;
   while(1){
        
       if(!pfh && !get_key_frame){
           pfh = TcfbFetchPreKeyFrame(&clt);
           if(!pfh){
               SA_Sleep(100);
               continue;
           }
           get_key_frame = 1;
       }else{
            pfh = TcfbFetchFrame(&clt, &b_overwritten);
            if(!pfh){
                SA_Sleep(20);
                continue;
            }else{
                if(b_overwritten){
                    printf("data is overwritten\n");
                }
            }
       }

       struct iovec vec[2];
       int vec_n = TcfbGetFrameDataPtr(&clt, pfh, vec);
       if(vec_n == 1){  
           if(TCMEDIA_IS_AUDIO(pfh->type&0xff)) {
               /*音频frame的第一个字节是格式位*/
               uint8_t u8AudioFlags = *((uint8_t*)vec[0].iov_base);
               vec[0].iov_base = (char*)(vec[0].iov_base) + 1;
               vec[0].iov_len --;
               printf("type=%d, ts=%d, len=%d, u8AudioFlags=%d\n", pfh->type, pfh->ts, pfh->len-1, u8AudioFlags);
//            TODO:
//                consume data
//                data_addr: vec[0].iov_base
//                data_len: vec[0].iov_len

           }
           else {
               if(TCMEDIA_IS_VIDEO(pfh->type&0xff) && (pfh->flags & 1) && pfh->utc_time) {
                  utc_time = pfh->utc_time; //每个视频关键帧都带有一个utc_time
                  printf("utc_time=%u\n", utc_time);
               }
               printf("type=%d, ts=%d, len=%d, flags=%d\n", pfh->type, pfh->ts, pfh->len, pfh->flags);
//             TODO:
//                consume data  
//                data_addr: vec[0].iov_base
//                data_len: vec[0].iov_len
           }
       }


       TcfbReleaseFrame(&clt, pfh);
   }
}

struct os_task mufbuff_test_hdl;

void mufbuff_test(void)
{
    OS_TASK_INIT("mufbuff_test", &mufbuff_test_hdl, mufbuff_demo, NULL, OS_TASK_PRIORITY_NORMAL, NULL, 1024);
}

