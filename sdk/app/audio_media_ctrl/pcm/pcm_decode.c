#include "basic_include.h"
#include "csi_kernel.h"
#include "lib/multimedia/msi.h"
#include "lib/multimedia/framebuff.h"
#include "lib/audio/audio_code/audio_code.h"
#include "autpc_msi/autpc_msi.h"
#include "osal_file.h"
#include "pcm_decode.h"

#define PCM_BUFF_SIZE           1024
#define MAX_PCM_DECODE_RXBUF    4
#define MAX_PCM_DECODE_TXBUF    4

struct pcm_decode_struct {
    AUDIO_TRACK audio_track;
    struct fbpool tx_pool;
    struct os_event event;
    struct msi *msi;
    struct msi *src_msi;
    struct msi *autpc_msi;
    char *msi_name;
    void *task_hdl;
    void *pcm_fp;
    uint32_t samplerate;
    uint32_t channels;
    uint8_t loop_mode;
    uint8_t direct_to_dac;
    uint8_t use_tpc;
    uint8_t speed;
    uint8_t pitch;
    uint8_t destroy_self;
    uint8_t next_status;
    uint8_t current_status;
    uint8_t *inbuf;
};

static int32_t pcm_file_read(struct pcm_decode_struct *pcm_decode_s, uint8_t *buf, uint32_t size)
{
    int32_t read_len = 0;

    read_len = osal_fread(buf, size, 1, pcm_decode_s->pcm_fp);
    return read_len;
}

static void pcm_file_decode(struct pcm_decode_struct *s)
{
    int32_t ret = 0;
    int32_t read_len = 0;
    uint32_t interval_time = 0;
    struct framebuff *frame_buf = NULL;
    AUDIO_TRACK *audac_fiter_track = NULL;

    while(1) {
        if(s->next_status == AUCODEC_PAUSE) {
            if (s->current_status == AUCODEC_RUN) {
                msi_output_cmd(s->msi,MSI_CMD_AUTPC,MSI_AUTPC_END_STREAM,(uint32_t)(&(s->audio_track)));
                msi_cmd("R_AUDAC",MSI_CMD_AUDAC,MSI_AUDAC_END_STREAM,(uint32_t)(&(s->audio_track)));
            }
            s->current_status = AUCODEC_PAUSE;
            while (s->next_status == AUCODEC_PAUSE) {
                os_sleep_ms(1);
            }
        }
        if(s->next_status == AUCODEC_EXIT) {
            s->current_status = AUCODEC_EXIT;
            goto pcm_decode_end;
        }
        s->current_status = AUCODEC_RUN;
        frame_buf = fbpool_get(&s->tx_pool, 0, s->msi);
        if(frame_buf) {
            read_len = pcm_file_read(s, frame_buf->data, PCM_BUFF_SIZE);
            if(read_len <= 0) {
                PCM_INFO("pcm read end or fail, ret:%d, line:%d!\r\n", read_len, __LINE__);
                goto pcm_decode_end;
            }
            frame_buf->priv = &s->audio_track;
            frame_buf->len = read_len;
            frame_buf->mtype = F_AUDIO;
            frame_buf->stype = FSTYPE_AUDIO_PCM;
            s->audio_track.samplerate = s->samplerate;
            if(s->use_tpc && !s->autpc_msi) {
                uint32_t samples = read_len >> 1;
                s->autpc_msi = autpc_msi_init(s->audio_track.samplerate,s->speed,s->pitch,samples,&(s->audio_track));
                if(s->autpc_msi == NULL) {
                    goto pcm_decode_end;
                }
                if(s->direct_to_dac) {
                    msi_add_output(s->autpc_msi, NULL, "R_AUDAC");
                }
                msi_add_output(s->msi, NULL, s->autpc_msi->name);
            }
            ret = msi_output_fb(s->msi, frame_buf);
            PCM_DEBUG("pcm file send framebuff:%p, ret:%d\r\n", frame_buf, ret);
            msi_cmd("R_AUDAC",MSI_CMD_AUDAC,MSI_AUDAC_GET_FILTER_TRACK,(uint32_t)(&audac_fiter_track));
            if(s->direct_to_dac && (!s->use_tpc) && (audac_fiter_track != (&(s->audio_track)))) {
                interval_time = (frame_buf->len >> 1) * 1000 / s->audio_track.samplerate;
                os_sleep_ms(interval_time);
            }
            frame_buf = NULL;
        }
        else {
            os_sleep_ms(1);
        }
    }
pcm_decode_end:
    msi_output_cmd(s->msi,MSI_CMD_AUTPC,MSI_AUTPC_END_STREAM,(uint32_t)(&(s->audio_track)));
    msi_cmd("R_AUDAC",MSI_CMD_AUDAC,MSI_AUDAC_END_STREAM,(uint32_t)(&(s->audio_track)));
    if (frame_buf) {
        msi_delete_fb(s->msi, frame_buf);
        frame_buf = NULL;
    }
}

static void pcm_msi_decode(struct pcm_decode_struct *s)
{
    uint8_t clear_finish = 1;
    int32_t ret = 0;
    uint32_t clear_flag = 0;
    struct framebuff *recv_frame_buf = NULL;
    struct framebuff *send_frame_buf = NULL;

    while(1) {
        os_event_wait(&s->event,coder_clear_event,&clear_flag,OS_EVENT_WMODE_OR | OS_EVENT_WMODE_CLEAR,0);
        if(clear_flag & coder_clear_event) {
            clear_flag = 0;
            clear_finish = 0;
        }
        if(s->next_status == AUCODEC_PAUSE) {
            if(s->current_status == AUCODEC_RUN) {
                msi_output_cmd(s->msi,MSI_CMD_AUTPC,MSI_AUTPC_END_STREAM,(uint32_t)(&(s->audio_track)));
                msi_cmd("R_AUDAC",MSI_CMD_AUDAC,MSI_AUDAC_END_STREAM,(uint32_t)(&(s->audio_track)));
            }
            s->current_status = AUCODEC_PAUSE;
        }
        recv_frame_buf = msi_get_fb(s->msi, 0);
        if(recv_frame_buf) {
            if(clear_finish == 0) {
                goto pcm_decode_frame_end;
            }
            if(s->current_status == AUCODEC_PAUSE) {
                goto pcm_decode_frame_end;
            }
            s->current_status = AUCODEC_RUN;
            while(!send_frame_buf) {
                send_frame_buf = fbpool_get(&s->tx_pool, 0, s->msi);
                if(!send_frame_buf) {
                    os_sleep_ms(1);
                }
            }
            if((recv_frame_buf->len <= 0) || (recv_frame_buf->data == NULL)) {
                msi_delete_fb(s->msi, send_frame_buf);
                send_frame_buf = NULL;
                goto pcm_decode_frame_end;
            }
            uint32_t copy_len = recv_frame_buf->len;
            if (copy_len > PCM_BUFF_SIZE) {
                copy_len = PCM_BUFF_SIZE;
            }
            copy_len &= ~0x01;
            if(copy_len <= 0) {
                msi_delete_fb(s->msi, send_frame_buf);
                send_frame_buf = NULL;
                goto pcm_decode_frame_end;
            }
            os_memcpy(send_frame_buf->data, recv_frame_buf->data, copy_len);
            send_frame_buf->priv = &s->audio_track;
            send_frame_buf->len = copy_len;
            send_frame_buf->mtype = F_AUDIO;
            send_frame_buf->stype = FSTYPE_AUDIO_PCM;
            s->audio_track.samplerate = s->samplerate;
            if (s->use_tpc && !s->autpc_msi) {
                uint32_t samples = copy_len >> 1;
                s->autpc_msi = autpc_msi_init(s->audio_track.samplerate,s->speed,s->pitch,samples,&(s->audio_track));
                if(s->autpc_msi == NULL) {
                    goto pcm_decode_end;
                }
                if (s->direct_to_dac) {
                    msi_add_output(s->autpc_msi, NULL, "R_AUDAC");
                }
                msi_add_output(s->msi, NULL, s->autpc_msi->name);
            }
            ret = msi_output_fb(s->msi, send_frame_buf);
            PCM_DEBUG("pcm msi send framebuff:%p, ret:%d\r\n", send_frame_buf, ret);
            send_frame_buf = NULL;
pcm_decode_frame_end:
            PCM_DEBUG("pcm decode delete recv framebuff:%p, len:%d\r\n",recv_frame_buf,recv_frame_buf->len);
            msi_delete_fb(s->msi, recv_frame_buf);
            recv_frame_buf = NULL;
        } 
        else {
            if(clear_finish == 0) {
                clear_finish = 1;
                os_event_set(&s->event, coder_clear_finish_event, NULL);
            }
            os_sleep_ms(1);
        }
        if(s->next_status == AUCODEC_EXIT) {
            s->current_status = AUCODEC_EXIT;
            goto pcm_decode_end;
        }
    }
pcm_decode_end:
    msi_output_cmd(s->msi,MSI_CMD_AUTPC,MSI_AUTPC_END_STREAM,(uint32_t)(&(s->audio_track)));
    msi_cmd("R_AUDAC",MSI_CMD_AUDAC,MSI_AUDAC_END_STREAM,(uint32_t)(&(s->audio_track)));
    if(recv_frame_buf) {
        msi_delete_fb(NULL, recv_frame_buf);
        recv_frame_buf = NULL;
    }
    if(send_frame_buf) {
        msi_delete_fb(s->msi, send_frame_buf);
        send_frame_buf = NULL;
    }
}

static void pcm_decode_thread(void *d)
{
    struct pcm_decode_struct *s = (struct pcm_decode_struct *)d;

    if(s->direct_to_dac) {
        msi_cmd("R_AUDAC",MSI_CMD_AUDAC,MSI_AUDAC_SET_FILTER_TRACK,(uint32_t)(&(s->audio_track)));
    }

    s->msi->enable = 1;
    if(s->pcm_fp) {
        do {
            osal_fseek(s->pcm_fp, 0);
            pcm_file_decode(s);
            if (s->current_status == AUCODEC_EXIT) {
                break;
            }
        }while(s->loop_mode);
    }
    else {
        pcm_msi_decode(s);
    }

    if(s->direct_to_dac) {
        s->audio_track.priority &= 0x3F;
        msi_cmd("R_AUDAC",MSI_CMD_AUDAC,MSI_AUDAC_SET_FILTER_TRACK,(uint32_t)(&(s->audio_track)));
    }

    os_event_set(&s->event, coder_exit_event, NULL);

    while((s->next_status != AUCODEC_EXIT) && (s->destroy_self == 0)) {
        s->current_status = AUCODEC_END;
        os_sleep_ms(5);
    }

    if(s->src_msi) {
        msi_del_output(s->src_msi, NULL, s->msi->name);
        s->src_msi = NULL;
    }

    if(s->next_status != AUCODEC_EXIT) {
        msi_destroy(s->msi);
    }

    msi_put(s->msi);
}

static int32_t pcm_decode_msi_action(struct msi *msi, uint32_t cmd_id, uint32_t param1, uint32_t param2)
{
    int32_t ret = RET_OK;
    struct pcm_decode_struct *pcm_decode_s = (struct pcm_decode_struct*)(msi->priv);
    switch(cmd_id) {
		case MSI_CMD_AUCODER:
		{
            ret = RET_ERR;
			if(pcm_decode_s) {
				uint32_t cmd_self = (uint32_t)param1;
				switch(cmd_self) {	
                    case MSI_AUCODER_PAUSE:
                    {
                        if(pcm_decode_s->current_status == AUCODEC_RUN) {
                            pcm_decode_s->next_status = AUCODEC_PAUSE;
                        }
                        ret = RET_OK;  
                        break;                      
                    }
                    case MSI_AUCODER_CONTINUE:
                    {
                        if(pcm_decode_s->current_status == AUCODEC_PAUSE) {
                            pcm_decode_s->next_status = AUCODEC_RUN;
                        }
                        ret = RET_OK;  
                        break;                      
                    }
                    case MSI_AUCODER_GET_STATUS:
                    {
                        *((uint32_t*)param2) = (uint32_t)(pcm_decode_s->current_status);
                        ret = RET_OK;  
                        break;                      
                    }
					case MSI_AUCODER_SET_SPEED:
					{
                        pcm_decode_s->speed = (uint8_t)param2;
                        ret = msi_output_cmd(pcm_decode_s->msi,MSI_CMD_AUTPC,MSI_AUTPC_SET_SPEED,(uint32_t)(pcm_decode_s->speed));
						break;
					}
					case MSI_AUCODER_GET_SPEED:
					{
                        *((uint32_t*)param2) = (uint32_t)(pcm_decode_s->speed);
                        ret = RET_OK;
						break;
					}
					case MSI_AUCODER_SET_PITCH:
					{
						pcm_decode_s->pitch = (uint8_t)param2;
                        ret = msi_output_cmd(pcm_decode_s->msi,MSI_CMD_AUTPC,MSI_AUTPC_SET_PITCH,(uint32_t)(pcm_decode_s->pitch));
						break;
					}
					case MSI_AUCODER_GET_PITCH:
					{
                        *((uint32_t*)param2) = (uint32_t)(pcm_decode_s->pitch);
                        ret = RET_OK;
						break;
					}
                    case MSI_AUCODER_CLEAR_STREAM:
                    {
                        os_event_set(&pcm_decode_s->event, coder_clear_event, NULL);
                        os_event_wait(&pcm_decode_s->event, coder_clear_finish_event, NULL, OS_EVENT_WMODE_OR | OS_EVENT_WMODE_CLEAR, osWaitForever);
                        msi_output_cmd(pcm_decode_s->msi,MSI_CMD_AUTPC,MSI_AUTPC_END_STREAM,(uint32_t)(&(pcm_decode_s->audio_track)));
                        msi_cmd("R_AUDAC",MSI_CMD_AUDAC,MSI_AUDAC_CLEAR_STREAM,(uint32_t)(&(pcm_decode_s->audio_track)));
                        ret = RET_OK;  
                        break;                                
                    }
                    case MSI_AUCODER_SET_SRCMSI:
                    {
                        if(pcm_decode_s->src_msi) {
                            msi_del_output(pcm_decode_s->src_msi, NULL, msi->name);
                        }
                        pcm_decode_s->src_msi = NULL;
                        ret = msi_add_output((struct msi*)param2, NULL, msi->name);
                        if(ret == RET_OK) {
                            pcm_decode_s->src_msi = (struct msi*)param2;
                        }
                        break;
                    }
                    case MSI_AUCODER_DIRECT_TO_DAC:
                    {
                        uint32_t direct_to_dac = param2;
                        if(direct_to_dac && pcm_decode_s->direct_to_dac == 0) {
                            pcm_decode_s->direct_to_dac = 1;
                            if(pcm_decode_s->use_tpc == 0) {
                                msi_add_output(msi, NULL, "R_AUDAC");
                            }
                            else if(pcm_decode_s->autpc_msi) {
                                msi_add_output(pcm_decode_s->autpc_msi, NULL, "R_AUDAC");
                            }
                            msi_cmd("R_AUDAC",MSI_CMD_AUDAC,MSI_AUDAC_SET_FILTER_TRACK,(uint32_t)(&(pcm_decode_s->audio_track)));
                        }
                        else if(pcm_decode_s->direct_to_dac == 1) {
                            pcm_decode_s->direct_to_dac = 0;
                            if(pcm_decode_s->use_tpc == 0) {
                                msi_del_output(msi, NULL, "R_AUDAC");
                            }
                            else if(pcm_decode_s->autpc_msi) {
                                msi_del_output(pcm_decode_s->autpc_msi, NULL, "R_AUDAC");
                            }
                            pcm_decode_s->audio_track.priority &= 0x3F;
                            msi_cmd("R_AUDAC",MSI_CMD_AUDAC,MSI_AUDAC_SET_FILTER_TRACK,(uint32_t)(&(pcm_decode_s->audio_track)));
                        }
                        ret = RET_OK;
                        break;
                    }
					case MSI_AUCODER_DEINIT:
					{
						msi_destroy(msi);
						ret = RET_OK;	
						break;					
					}
					default:
						break;
				}
			}
			break;
	}
        case MSI_CMD_TRANS_FB:
        {
            ret = RET_ERR;
            struct framebuff *frame_buf = (struct framebuff *)param1;
            if(frame_buf && frame_buf->mtype == F_AUDIO) {
                ret = RET_OK;
            } 
            break;
        }            
        case MSI_CMD_FREE_FB:
        {
            ret = RET_ERR;
            if(pcm_decode_s) {
                struct framebuff *frame_buf = (struct framebuff *)param1;
                fbpool_put(&pcm_decode_s->tx_pool, frame_buf);
            }
            break; 
        }  
        case MSI_CMD_PRE_DESTROY:
        {
            if(pcm_decode_s && pcm_decode_s->task_hdl) {
                pcm_decode_s->next_status = AUCODEC_EXIT;
            }
            break;
        }   
		case MSI_CMD_POST_DESTROY:
        {
            if(pcm_decode_s) {
                if(pcm_decode_s->task_hdl) {
                    os_event_wait(&pcm_decode_s->event, coder_exit_event, NULL, OS_EVENT_WMODE_OR | OS_EVENT_WMODE_CLEAR, osWaitForever);
                }
                for(uint32_t i=0; i<MAX_PCM_DECODE_TXBUF; i++) {
                    struct framebuff *frame_buf = (pcm_decode_s->tx_pool.pool)+i;
                    if(frame_buf->data) {
                        PCM_DECODE_FREE(frame_buf->data);
                        frame_buf->data = NULL;
                    }
                }
                fbpool_destroy(&pcm_decode_s->tx_pool);
                if(pcm_decode_s->event.hdl)  {
                    os_event_del(&pcm_decode_s->event);
                }
                if(pcm_decode_s->pcm_fp) {
                    osal_fclose(pcm_decode_s->pcm_fp);
                    pcm_decode_s->pcm_fp = NULL;
                }
				if(pcm_decode_s->autpc_msi) {
					autpc_msi_deinit(pcm_decode_s->autpc_msi);
                    pcm_decode_s->autpc_msi = NULL;
				}
                if(pcm_decode_s->msi_name) {
                    PCM_DECODE_FREE(pcm_decode_s->msi_name);
                    pcm_decode_s->msi_name = NULL;
                }
				if(pcm_decode_s->inbuf) {
					PCM_DECODE_FREE(pcm_decode_s->inbuf);
					pcm_decode_s->inbuf = NULL;
				}
                PCM_DECODE_FREE(pcm_decode_s);
                pcm_decode_s = NULL;
            }
            break;
        }     
        default:
            break;    
    }
    return ret;
}

struct msi *pcm_decode_init(uint32_t samplerate, char *filename, uint8_t loop_mode, AUDEC_INIT *audec_init)
{
#if AUDIO_EN
    uint8_t msi_isnew = 0;
    char *msi_name = NULL;
    uint32_t random_bytes = 0;
    struct msi *msi = NULL;
    struct pcm_decode_struct *pcm_decode_s = NULL;
    if(audec_init == NULL) {
        return NULL;
    }
    msi_name = (char *)PCM_DECODE_ZALLOC(sizeof(char) * 32);
    if(msi_name == NULL) {
        os_printf("alloc pcm decode msi name fail\n");
        return NULL;
    }
create_msi_again:
    os_random_bytes((uint8_t *)(&random_bytes), 4);
    os_snprintf(msi_name, 20, "SR_PCM_DECODE_%04u", random_bytes%10000);
    msi = msi_new(msi_name, MAX_PCM_DECODE_RXBUF, &msi_isnew);
    if(msi == NULL) {
        PCM_INFO("create pcm decode msi fail!\r\n");
        PCM_DECODE_FREE(msi_name);
        return NULL;
    }
	else if(msi_isnew == 0) {
		goto create_msi_again;
	}
    pcm_decode_s = (struct pcm_decode_struct *)PCM_DECODE_ZALLOC(sizeof(struct pcm_decode_struct));
    if(!pcm_decode_s) {
        PCM_INFO("pcm_decode_s malloc fail!\r\n");
        goto pcm_decode_init_err;
    }
    msi->priv = pcm_decode_s;
    msi->action = (msi_action)pcm_decode_msi_action;
    fbpool_init(&pcm_decode_s->tx_pool, MAX_PCM_DECODE_TXBUF);
    for(uint32_t i = 0; i < MAX_PCM_DECODE_TXBUF; i++) {
        struct framebuff *frame_buf = (pcm_decode_s->tx_pool.pool) + i;
        frame_buf->data = (uint8_t *)PCM_DECODE_MALLOC(PCM_BUFF_SIZE);
        if(frame_buf->data == NULL) {
            PCM_INFO("pcm decode malloc framebuff data fail!\r\n");
            goto pcm_decode_init_err;
        }
    }
    if(os_event_init(&pcm_decode_s->event) != RET_OK) {
        PCM_INFO("create pcm decode event fail!\r\n");
        goto pcm_decode_init_err;
    }
    pcm_decode_s->inbuf = (uint8_t *)PCM_DECODE_MALLOC(PCM_BUFF_SIZE);
    if(pcm_decode_s->inbuf == NULL) {
        PCM_INFO("pcm decode alloc inbuf fail!\r\n");
        goto pcm_decode_init_err;
    }
    if(filename) {
        pcm_decode_s->pcm_fp = osal_fopen((const char *)filename, "rb");
        if(pcm_decode_s->pcm_fp == NULL) {
            PCM_INFO("open pcm file %s fail!\r\n", filename);
            goto pcm_decode_init_err;
        }
    }
    if(audec_init->src_msi && (msi_add_output(audec_init->src_msi, NULL, msi->name) != RET_OK)) {
        goto pcm_decode_init_err;
    }
    pcm_decode_s->msi = msi;
    pcm_decode_s->msi_name = msi_name;
    pcm_decode_s->src_msi = audec_init->src_msi;
    pcm_decode_s->samplerate = samplerate;
    pcm_decode_s->loop_mode = loop_mode;
    pcm_decode_s->direct_to_dac = audec_init->direct_to_dac;
    pcm_decode_s->use_tpc = audec_init->use_tpc;
    pcm_decode_s->speed = audec_init->speed;
    pcm_decode_s->pitch = audec_init->pitch;
    pcm_decode_s->destroy_self = audec_init->destroy_self;
    pcm_decode_s->audio_track.priority = audec_init->priority;
    pcm_decode_s->audio_track.track_type = audec_init->track_type;
    pcm_decode_s->audio_track.samplerate = samplerate;
    pcm_decode_s->next_status = AUCODEC_RUN;
    pcm_decode_s->current_status = AUCODEC_RUN;
    if(audec_init->direct_to_dac && !pcm_decode_s->use_tpc) {
        msi_add_output(msi, NULL, "R_AUDAC");
    }
    pcm_decode_s->task_hdl = os_task_create("pcm_decode_thread",pcm_decode_thread,(void *)pcm_decode_s,OS_TASK_PRIORITY_NORMAL,0,NULL,1024);
    if (pcm_decode_s->task_hdl == NULL) {
        PCM_INFO("create pcm decode task fail!\r\n");
        goto pcm_decode_init_err;
    }
    msi_get(msi);
    return msi;
pcm_decode_init_err:
    if (msi) {
        msi_destroy(msi);
    }
#endif
    return NULL;
}