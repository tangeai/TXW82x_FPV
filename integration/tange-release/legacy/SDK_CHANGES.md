# SDK 修改分类及必要性

按示例用途分类；不宣称所有 SDK 修改均为探鸽库的强依赖。

| 文件 | 补丁分类 | 目的、条件及影响 |
|---|---|---|
| `sdk/app/algorithm/stream_frame/stream_define.h` | 2 | 新增 MSI_VIDEO_DEMUX_FAST_OUTPUT 控制项，与 demux 实现及旧录像 seek 调用配套；只使用实时流的客户不需要。 |
| `sdk/app/audio_media_ctrl/aac/aac_encode.c` | 3 | 可选：AAC 发送池 20 调至 4；旧录像默认不依赖 AAC 编码，启用其他 AAC 功能时须重新测量。 |
| `sdk/app/audio_msi/audio_adc.c` | 2 | 旧 alaw 录像回调可能在音频任务写卡，任务栈由 1024 调至 2048；属于所选录像实现的栈预算，客户可采用等效任务模型。 |
| `sdk/app/audio_msi/audio_adc.h` | 3 | 可选音频参数：ADC 出帧 20ms 调至 40ms；客户调整时须同步检查发送帧采样数/时间戳。 |
| `sdk/app/mp4/mp4_encode.c` | 2,3,diagnostics | 录像：修正 mdat 实际大小和关闭截断、匹配主/子流的容量与索引参数；建议：NAL 聚合写及常驻 PSRAM 缓冲；慢操作计时另放诊断。容量值不能直接用于任意时长/码率。 |
| `sdk/app/mp4/mp4_encode.h` | 3 | 仅聚合写优化新增 nal_wr_buf/size 字段，必须与对应实现同时编译；不要混用基于旧结构体编译的对象文件。 |
| `sdk/app/video_app/auto_h264_msi.c` | 1 | 默认示例关闭原厂逐帧 F/+ 打印，与开发版本一致；不随可选诊断启用。仅影响日志，不是探鸽库的接口依赖。 |
| `sdk/app/video_app/jpg_concat_msi.c` | 3 | 可选旧 MSI 抓拍路径：静态拼接缓冲、节点大小与池深度；本示例 SNAPSHOT_USE_LEGACY=0 时不把它宣称为必要依赖。 |
| `sdk/app/video_app/video_app_h264_msi.c` | 3 | 可选 H264 静态缓冲池、对应归还逻辑及大帧动态分配回退；没有恢复旧的超大帧直接丢弃分支。须测大 I 帧及长时内存压力。 |
| `sdk/chip/txw82x/system0.c` | 1,3 | 适配：导出 psram_heap_size 供示例容量选项使用；建议：模拟容量限制及 MAIN 工作队列优先级。客户有等效容量接口时可自行对接。 |
| `sdk/include/lib/video/dvp/cmos_sensor/csi.h` | 4 | 板级 H63S 注册声明，与 H63S sensor/mipi 注册配套，不是探鸽库依赖。 |
| `sdk/include/lib/video/h264/h264_drv.h` | 3 | 可选编码资源：节点数量、帧数量和 BS 空间；需要与码率、分辨率、队列深度一起预算。 |
| `sdk/include/lib/video/vpp/vpp_dev.h` | 3 | 可选 826 VPP 缓冲模式；不强制客户更换自有视频管线配置。 |
| `sdk/lib/bluetooth/uble/ble_adv.c` | 1 | BLE 销毁后清空控制指针；避免残留指针被再次使用。需要检查配网启停重复调用。 |
| `sdk/lib/bus/iic/sensor/sensor_gc1084_mipi.c` | 4 | GC1084 开发板传感器参数，客户按传感器型号和画质要求评估。 |
| `sdk/lib/bus/iic/sensor/sensor_h63s_mipi.c` | 4 | H63S 驱动/寄存器表更新，与对应板型配套。 |
| `sdk/lib/bus/iic/sensor/sensor_sc1346.c` | 4 | SC1346 驱动/寄存器表更新；原厂补充变更不等于探鸽库必须。 |
| `sdk/lib/fs/fatfs/ff.c` | 2 | 格式化路径 update_fat_info 增加 FS_EN 条件，与所选文件系统配置配套；不是所有产品的强制项。 |
| `sdk/lib/fs/fatfs/osal_file.c` | 1,diagnostics | 适配提交仅初始化 writeLen、修正 fclose 失败时设置 errno；失败/部分写的打印单独放诊断。 |
| `sdk/lib/net/lwip/src/api/api_lib.c` | diagnostics | 可选诊断：errno=12 或发送失败统计；默认不启用。 |
| `sdk/lib/net/lwip/src/api/api_msg.c` | diagnostics | 可选诊断：errno=12 或发送失败统计；默认不启用。 |
| `sdk/lib/net/lwip/src/api/sockets.c` | 1,diagnostics | 适配提交只迁移 lwip_close 失败路径的 socket 清理变化；sendmsg/sendto 的 errno=12 输出另放诊断。必须验证重连/重复关闭，不因没有新增符号而忽略行为变化。 |
| `sdk/lib/net/lwip/src/core/ipv4/etharp.c` | diagnostics | 可选诊断：errno=12 或发送失败统计；默认不启用。 |
| `sdk/lib/net/lwip/src/include/lwip/cc.h` | diagnostics | 可选诊断：errno=12 或发送失败统计；默认不启用。 |
| `sdk/lib/net/lwip/src/include/lwipopts.h` | 3,diagnostics | 建议提交仅增加 DNS_TABLE_SIZE；统计开关 LWIP_STATS/LWIP_STATS_DISPLAY 只在可选诊断补丁中开启。 |
| `sdk/lib/net/lwip/src/netif/ethernetif.c` | diagnostics | 可选诊断：errno=12 或发送失败统计；默认不启用。 |
| `sdk/lib/sdhost/sdhost.c` | 3 | 可选：无卡探测间隔延长到 2 秒，减少反复探测及刷日志；会影响插卡检测延迟，不属于纯打印修改。 |
| `sdk/lib/video/dvp/jpeg/jpg_v3_msi.c` | 3 | 可选旧 JPEG 路径：固定节点池和 MSI 对象生命周期；要与 jpg_concat 节点长度配套，默认裸抓拍不依赖其优化。 |
| `sdk/lib/video/h264/h264_drv.c` | 3 | 示例/建议编码配置：码率、帧率字段、GOP 和动静切换，屏蔽未用数组；实际帧率仍受 sensor 配置影响。 |
| `sdk/lib/video/miniMP4/mp4_demux_msi.c` | 2 | 录像回放：视频-only 防除零、快速输出 seek、Annex-B/SPS/PPS 帧组装；PSRAM 兜底与来源匹配释放、单帧池是该示例资源策略，客户有等效实现可替换。 |
| `sdk/lib/video/miniMP4/mp4_encode_msi2.c` | 2 | 录像：帧计数同步与收尾栈余量；另含 1200ms 队列/不做启动大预分配的实现策略。属于所选旧录像实现，不是探鸽二进制库固有依赖。 |
| `sdk/lib/video/mipi_csi/mipi_csi.c` | 4 | 传感器注册、preset/识别及复位时序等板级适配；源文件含较多格式变化，客户只需移植适用传感器路径。 |
| `sdk/lib/video/vpp/vpp_dev.c` | 1 | 默认示例关闭原厂逐帧 F/+ 打印，与开发版本一致；不随可选诊断启用。仅影响日志，不是探鸽库的接口依赖。 |
| `sdk/osal/csky/time.c` | 1 | 示例时间语义：gettimeofday 叠加 _tg_timezone_。需要配套 tange 时区来源；这不是普遍正确的 POSIX UTC 实现，已有校时框架应单独适配。 |
