### v1.369.8
    修改SDK版本号打印接口
    更新webrtc库，修复引用泰芯头文件路径错误问题
    优化云存储上传
    Repair：
	    定时同步UTC时间异常
		
### v1.369.7
    适配泰芯新版低功耗SDK，增加txw82x_lp平台
    适配泰芯低功耗setsockopt设置及获取发送/接收超时接口

### v1.369.6
    优化chunked云存储AES加密流程，减少内存开销
    调整TXW826的TLSF内存池大小
    支持低功耗设备挂起/恢复云存储任务
    Repair: mufbuff错误导致chunked上传丢帧的问题

### v1.369.5
    chunked云存储上传支持AES加密

### v1.369.4
    调整chunked事件上传流程，启用两个任务队列
    Repair:
        chunked云存储上传异常
        BK7258的sys_free接口实现错误

### v1.369.3
    更新webrtc的TURN服务器配置接口
    Repair:
        带分钟偏移的时区解析错误
        设备呼叫APP异常

### v1.369.2
    将相关time()调用替换为SA_time()
    更新BK7258库，调整系统时间的时区处理
    Repair:
        BK7258签名校验错误
        BK7258的time_t类型数据打印异常

### v1.369.1
    调整分片及流式云录像索引上报，文件上传成功后才上报索引
    调整DNS策略，相关配置下停用HTTPDNS，增加解析结果为本地地址的检测
    关闭socket失败时增加返回值检查和重试
    Repair: 断网时录像未上传成功却上报了录像索引

### v1.369.0
    Repair:
        流式事件上传不完整
        流式云存储桶过期后未自动更新
        socket重复释放导致系统崩溃

### v1.368.2
    分片云存增加chunked流式上传模式
    调整TciStop相关流程

### v1.368.1
    Repair: UUID签名校验错误

### v1.368.0
    调整启动流程，避免在sockio回调中重入同类业务，将webrtc信令启动放到初始化后期
    Repair:
        配网时select超时参数计算错误导致卡死
        启动流程导致系统看门狗异常重启

### v1.366.13
    TXW82x/BK7258引入TLSF管理动态内存，使用PSRAM内存池

### v1.366.12
    调整BK7258的My_malloc等内存分配接口

### v1.367.4
    修复TXW82x和BK7258平台问题，调整BK7258平台文件及预编译库
    Repair:
        BK7258使用不匹配的webrtc库导致启动HardFault
        云存储代码编译错误

### v1.366.11
    整合TXW82x和BK7258的平台适配

### v1.367.3
    增加云存储文件上传结果回调通知(STATUS_CLOUD_FILE_UPLOAD)
    调整对讲流程
    更新webrtc至v1.4.4，增加在线状态检测接口

### v1.367.0
    适配泰芯微42229版本SDK
    支持AP直连
    优化云存储内存使用
    TXW826与TXW828共用一个库

### v1.366.9
    Repair: 卡录像不能与云存储共享内存的问题

### v1.366.8
    补充当前平台的卡录像和回放支持

### v1.366.7
    Repair: 频繁上报事件时概率性崩溃

### v1.366.5
    TXW828最多支持5个用户
    Repair: 设置最大用户数无效

### v1.366.3
    适配TXW826和TXW828
    调整相关平台SDK内存分配，使用PSRAM
    TXW826云存储采用子码流，增加分片文件缓存大小设置和查询
    云存数据超出缓存容量时调整丢帧处理并请求视频关键帧
    更新webrtc头文件及NT9856x、FH8626V200预编译库
    调整webrtc启动流程，未初始化时不启动信令服务
    Repair: FIFO接收长数据指令失败

### v1.366.2
    增加TXW82x平台支持及对应webrtc库
    调整ecEarlyInit()调用时机，确保通知处理器使用的锁已初始化
    Repair:
        strtok_r()实现问题
        任务入队失败时重复调用资源释放接口

### v1.366.0
    tinysys支持APP呼入
    调整信令启动，兼容小系统未适配配置读写接口的情况
    调整非主动挂断处理，避免通话用户之外的连接断开影响当前通话
    Repair:
        VDP主叫未设置TVC_F_PUSH_HIRES时不发送音频
        对命令应答调用_cmd_handler可能导致应用层错误行为
        eyecloud_upload_thread()中刷新上报标志处理错误

### v1.365.0
    支持云录像AES加密
    加入缩时录像的回放速度指示
    云服务结束会回调给应用
    长连接心跳间隔探测
    通过长连接发起呼叫(目前仅用于应用唤醒p2p)
    vdp下支持call事件功能的更新
    增加sdk释放事件图片通知(STATUS_EVENT_CALLBACK)
    增加http请求被重定向的反馈(STATUS_ACCESS_REDIRECTED)
    增加tcp测速功能
    Repair:
        删除物属性http请求方法由POST改为DELETE
        platform里的可能的内存泄漏
        无网络时频繁触发事件导致添加任务失败时资源泄漏

### rev.362
    增加 TOPT_ONLY_REPORT_AI_EVENT2 选项，在无云存服务时允许上报普通事件
    调用wxInit()会自动在Profile中插入 {"WxVoIP":1}，应用无需再上报
    支持esp32p4
    Repair:
        tinysys接收大于512字节的命令时会崩溃
        TgAiRtcDial() 未处理参数

### rev.361
    更新日志上传功能，现在上传到专用的日志bucket

### rev.360
    支持wxvoip云-云呼叫
    Repair: vdp模块挂断时阻塞、设备呼设备超时一半时接听主叫崩溃

### rev.358
    修正同时呼叫多个webrtc设备时可能崩溃的问题

### rev.326
    更新 webrtc 崩溃问题
    低功耗预cache事件录像时间更精确
    vdp接口变动

### rev.322
    阿里云改用AWS协议
    STATUS_UPDATESERVICE 可获取云存服务类型
    尚云p2p每次发送时都检查发送缓冲区

### rev.320
    Repair: 新购wxvoip服务没有立即生效

### rev.318
    加大获取能力集时的缓冲区
    Repair: 上传云文件没有重传(开启后备无效), rev217(b14fac) 引入

### rev.316
    支持 KLM(伴读宝)
    315 部分平台webrtc库没有更新

### rev.314
    用户文件下载分开成查询和下载两个接口

### rev.310
    修改221引入的访问p2p通道6的锁无效(越界)的问题。该问题会导致ios卡回放崩溃

### rev.221
    修复造成taskq "lack of slot" 的问题。该错误会导致云存有文件上传但无上报

### rev.219
    1. 增加 telnet on p2p tunnel 功能
    2. 更新云存统计相关
    Repair: 
        * TciSetKeyVideoTime() 在没有事件时也会触发录像
        * 之前引入的卡回放network_busy重传会一直失败

### rev.216
    修复215直连崩溃问题
    oss和流量统计上报

### rev.215
    禁止30x重定向

### rev.214
    webrtc v2 特殊情况连不上的问题更新

### rev.213
    Repair: webrtc2 send_candidate bug
            wxvoip 设备无屏时小程序端右上角仍有小窗口

### rev.207
    低功耗能耗模式接口更新

### rev.205
    喂食器接口更新

### rev.203
    1. 微信小程序和可视对讲VDP开发接口
    2. 录像从后备上传时对卡异常的优化
    3. 支持移动云
    4. 事件上报加入覆盖指定旧事件的参数
    5. 加入喂食器支持

### rev.194
    1. SD卡录像查询增加录像日期列表和事件时间戳功能
    2. 唤醒原因增加上报信号强度
    3. 低功耗设备增加功耗策略配置

### rev.193
    Repair: 
        192引入的二维码解析错误

### rev.192
    增加微信VoIP功能

### rev.190
    修改"4G"能力描述，增加 "simtype" 键用于支持VSIM卡
    废弃 TciReport4GInfo(), 用TciReport4GInfoEx() 代替  
    支持自定义的唤醒原因

### rev.188
    1. 低功耗设备在休眠前检查是否有后备文件要上传
    Repair:
       rev184(185) 引入的不能切换清晰度的bug
       webrtc 命令缓冲区小不能处理长命令(TCI_CMD_PLAY_AUDIO)

### rev.185
    移除TciSetEventHandleOver2()
    增加自定义事件支持
    TciSetBackStore()参数语义有变化
    增加TciGetSdkState()

### rev.183
   修复 178~181 webrtc连接时可能崩溃的问题

### rev 179
    更新webrtc库(某些情况连接不上的问题)
    音视频分开到不同bucket
    可视对讲

### rev 167
    支持最多4路视频
    更新二枪一球联动的配置和命令参数说明

### rev 165
    支持联通云
    回放新增 TCIC_RECORD_PLAY_CONTINUE 命令

### rev 164
    更新测试模式下崩溃的问题
    支持用户自定义能力

### rev 162
    阿里云security token 长度可能超过1K(之前~600B), tencentCosSignature()签名计算处缓冲区长度不足

### rev 161
    支持腾讯云 https 上传

### rev 160
    修正刚添加时云存码流(默认标清)没有采用设备端配置的问题

### rev 159
    支持在长连接上主求高清图片
    加入Profile 能力描述

### rev 156
+ Repair:
    1. rev.148 引入的上传日志文件错误
    2. http_simple_get_file()里清理response的位置不对, 导致下载用户文件(TciGetUserFile())失败时崩溃

### rev 152
    增加仅上报AI事件的选项(喂鸟器)

### rev 149
    stun/turn 服务器地址支持域名

### rev 148
    修复webrtc设备厂测不出图问题

### rev 147
    新增与mcu同步绑定信息的接口. 流程在给mcu的库的文档里

### rev 145
    TciSetP2pInfo()支持新格式串

### rev 144
    webrtc 正式发布

### rev 141
    webrtc 本地访问

### rev 140
    rev135 庭院灯接口更新

### rev 139
    云存和后备存储调节选项
    增加拒接回调

### rev 137
    支持自定义的 IJPG 格式

### rev 135/136
    用文件下载
    庭院灯控制

### rev131
    新增停车监控、跌倒告警事件
    更新mbedtls版本到3.2.1

### rev123
    优化呼叫多人接收时状态同步逻辑
    增加呼叫超时时间配置
    支持BirdFeeder设备类型

### rev121
    修复105开始的云存崩溃问题
    呼叫接听完善
    增加提示音开关

### rev106
    呼叫事件接听过程云端全录像

### rev105
    1. 增加巡航能力和接口
    2. 支持多种音频采样频率

### rev98
    1. 增加哭声AI能力描述和事件类型
    2. 增加文件上传超时值, 缩短补传网络检测间隔
    3. 修改AI服务逻辑：仅对AI事件录像，但会上报普通事件。修正事件上报中的is_pay标志
    4. 4G低功耗设备进入/离开NETDOWN状态过程优化

### rev96
    修改事件云存处理逻辑。4G+AI 只在识别到对象时才上报和录像

### rev95
    门锁远程开锁流程改变

### rev94
    支持AAC
    上报能力集到服务器
    支持 LampCam 设备

### rev91
    增加手动电源管理控制接口 TciSetPowerMode()
    AI功能内置到sdk

### rev89
    温湿度能力上报到服务器
    修改GET_PTZ_POS/SET_PTZ_POS 请求和返回参数
    添加日志上传接口

### rev88
    修正85引入的远程telnet连接不上的问题

### rev86
    画中画支持云存双路高清
    守望位、流事件、温湿度支持

### rev84
    纯4G生产模式分配p2p id和心跳方式更新
    能力 Cap-AI/SupportPTZ/Cap-Defence 描述更新，相应新增指令
    ExtInstructions增加温湿度支持

### rev82
    能力描述支持json格式以支持多通道
    Repair: 更新内部配置文件时可能崩溃

### rev81
    1. by_pos 预置位增加num 标识
    2. 预置位demo更新
    3. fh885x/qg2101 p2p库更新
    4. p2p口令校验支持 lpsimdr 类型
    5. ipconfig和红外白光联动能力描述
    6. sockio_cb("service") json解析类型兼容null和字符串表达的数值

### rev78
    增加移动侦测区域设置命令的语义。结构/文档/demo更新
    定向流量卡域名解析上的一点改进：重试一次/字节对齐

### rev77
    1. 更正定向流量卡需要访问的IP过多时，访问列表刷新方式
    2. 更正流量统计上报和上传文件大小统计
    3. 增加OSD/预置位/IP设置接口

### rev76
    修复长连接服务器地址改变重新生成的url错误

### rev75
    1. 4G定向流量卡加入对p2p服务器的解析刷新
    2. gps信息上报间隔调整

### rev74
    1. 支持腾讯云
    2. liteos内存跟踪支持

### rev72
    1. 新增门铃设备类型和事件
    2. 支持双目变焦设备
    3. 支持liteos内存分配跟踪
    4. 支持关闭MIC
    5. 补传线程优化禁止上传时的逻辑
    6. 修复设备关闭仍然上报事件的问题

### rev70
    1. 修正电池设备类型能力解析, 支持长供电liteos系统
    2. 新增 联咏、富翰、杰理 支持
    3. 更新 Microphone 能力，启用关闭和调节MIC敏度功能
    4. 更新注释和文档

### rev68
- 支持多质量(>2码流)选择
- 支持定向sim卡
- 频繁报警通知过滤

### rev64
  增加TciSetEventEx()接口，事件上报可以协带特定参数
  过滤过于频繁的事件上报

### rev63
- 修改纯4G设备生产测试p2pid分配方式为服务器分发
- 增加AI功能开关接口
- TciUduBegin()接口变动

### rev61
- 休眠条件放到sdk内部处理
- 新增 Resolutions 能力

### rev60
- TciUduBegin(evt, ...) 事件类型参数改为字符指针，接收平台定义任意事件类型

### rev59
- 开启设备转发

### rev57
- TciCB::on_status()` 新增 STATUS_SDER 通知，并且现在要有返回值。
- `ECEVENT_POWEROFF/ECEVENT_POWERON`事件被 `ECEVENT_PARK/ECEVENT_SETOFF`事件代替，设备在响应 STATUS_SDER 通知时上传。

### rev56
    - TciUduBegin()接口变动
    - 新增TciSetEvent2()/TciSetEventHandleOver2()，可以传入事件时间参数
    - 新增错误上报接口 EyeCloudReportError()

### rev54(2020-10-13)
- 新增报警灯和PIR能力

### rev53(2020-09-29)
+ Update<br>
    1. 自定义上传可以传一张缩略图
    2. 更新补录逻辑，保证gps及时上传
    3. ai库只上传一张图片, 默认超时时间改为2s; 支持测试服务器
    4. sd卡回放缓冲超限时不丢包, 但发送函数引入延迟
    5. 补录上传条件看最近(PutObject)上传速度, 如果没有上传，触发 ECCBE_CHECK_NETWORK事件
    6. 检查补录条件的时间间隔采用2倍递增，从30"开始，最多32’
    7. 行程缩略图在第5'时同gps信息一起上报
    8. gps信息增加方位角上报(TciReportGpsInfo()接口有变化)
+ Repair<br>
    1. 行车记录仪的speed-up/speed-down/power-off/power-on事件被过滤

### rev51(2020-09-16)
- 新增用户定义补录功能
- 可以通过http或日志接口修改日志等级
- sd卡回放时允许更大的cache

### rev50(2020-09-07)
- 画中画设备双画面云存储和sdk卡回放支持

### rev49(2020-08-27)
1. 修复: 4g设备上线时sdk没有主动上报4g信息
2. 增加画中画设备的画面切换通知接口

### rev47
+ Update<br>
    1. 增加网传统计回调 trans_stat
    2. 增加电池状态事件上报接口 TciReportBatteryStatus(),取消ECEVENT_BATTERY_LOW定义
    3. 新增外部连接的休眠接口
    4. 更新gps信号强度/gsensor灵敏度/唤醒时长设置语义
    5. 增加mic/speaker音量设置
    5. 达到最大连接数时自动关闭不活跃连接
    7. 加大上传缩略图间隔

+ Repair<br>
    1. 缺省实时码流不能选择

### 1.0.46(2020-07-25)
1. 增加gsensor灵敏度设置接口
2. 增加p2p厂测模式
3. 5秒云存储分片基于视频时间戳。处理应用传入大量连续音频问题(丢音频)
4. 录像索引上报放到任务队列，不会阻塞ecloud线程

### 1.0.45(2020-07-09)
1. gps信息增加信号强度
2. 增加gps信息补传功能
3. 云存储后备增加缓存使用指示，更新补录时机选择逻辑
4. 获取日志的log回调放到长连接之外执行

### 1.0.43(2020-7-1)
修改TciSetBackStore()接口，增加缓存使用指示<br>
保证回放线程会退出<br>
增加sdk内部错误信息上报功能<br>

### 1.0.42(2020-06-18)
+ Repair<br>
    TCI_CMD_STOP 兼容ios app(没有传参数) <br>
    rev38引入的对讲没有声音的问题<br>

### 1.0.40(2020-06-13)
+ Update<br>
    支持云存储补录<br>
    增加gps上报接口<br>
    更新p2p库到3.4以上<br>
    与平台通信增加mac. 云录像增加 "ndays" tag<br>
    重启原因日志
+ Repair<br>
    httpclt.c::_HttpRecvRespWithCB 接收问题寻到有时候会丢掉sockio服务器返回的第一个包<br>
    达到最大连接数时重启的问题<br>
    rev37引入的无logoff通知问题

#### 1.0.38(2020-05-29)
+ Repair: 
	录像索引起始时间误差
	mufb缓冲区为空时 mufb_fetch_first_keyframe() 返回错误值, 导致后续操作崩溃
+ Update:
	支持画中画
	长连接断开后3次重试失败才通知应用层LOGOFF

#### 1.0.37(2020-05-07)
    1. 增加CgiRegisterHandler接口，允许用户通过http曝露内部状态
+ Repair:
    修复回放时app非正常退出应用层收不到停止回放通知的问题

#### 1.0.36(2020-04-30)
    1. 更新p2p重新初始化逻辑，防止崩溃
    2. 更新文件上传操作，超时计时更准确，能避免事件上报排队太多图片
    3. 升级下载文件超时放宽到90"
    4. 加大p2p缓冲区丢帧阀值(256K-->384K)
	5. 导出内部帧缓冲区共享接口

1.0.35(2020-04-22)
Repair:
	非普通(wifi)设备注册过程中第一次上报类型失败，之后重试时不再上报设备类型

1.0.34(2020-4-18)
	长连接心跳优化
	防止丢包打印过频繁

1.0.32(2020-4-8)
Update:
	内存分配跟踪
	保存initstring，优化启动速度
	为低功耗产品增加TciAllocCloudBuffer()接口，不必注册成功后再开始接收帧
	低功耗产口改变p2p口令认证方式
	mbedtls库统一采用2.16.3版本
Repare:
	调用TciSetEventHandleOver太频繁会造成内在泄漏
	rev30引入的ap模式不能添加bug

1.0.31
Repair:
	rev24后存在的一个分配内存不足问题(liteos上导致应用频繁崩溃，linux下不易出现)
Update:
	支持telnet tunnel服务，便于远程诊断
	增加DeviceType能力查询, 支持行车记录仪
	创建失败会调用p2p的REBOOT命令

1.0.30
Update:
    云录像加入时间同步帧
    平台命令请求加入user_id, 辅助解决重号串图像问题
    长连接连续连接失败会重新请求服务结点
    on_status回调加入STATUS_STREAMING事件

1.0.29
Update:
	云录像加入时间同步帧
	平台命令请求加入user_id
	长连接连续连接失败会重新请求服务结点

1.0.28
Repair:
	云存储连续录像中5~10"断点问题
	/service接口加入设备uid

1.0.27
	添加自定义报警音接口
	由于最初demo中写错,能力Cap-Instrucations改名为ExtInstructions

1.0.26(2020-02-13)
Repair:
	httpdns线程崩溃
	云录像实际时间和上报时间偏差大

1.0.25(2020-02-13)
Update:
	支持海外部署
	支持liteos
Repair:
	key为空时二维码解析错误
	引入httpdns后，首次dns解析失败，重试时间间隔过长

1.0.24(2020-01-02)
Update:
	引入httpdns

1.0.22
Update:
	引入任务队列。事件/图片和丢失文件上报在任务队列线程里的执行.
	新增TciSetEventHandleOver()处理是否由sdk释放图片。TciSetEvent不释放图片
	增加警铃配置接口
Repair:
	修正有线添加的设备长连接重连时还会上报unbind=1的问题

1.0.21(2019-12-19)
Update:
	云数据缓冲区消费者在使用完后立即释放锁, 避免写入线程等待。

1.0.20(2019-12-19)
Update:
	保留4G能力回调

1.0.19(2019-12-12)
Update:
	增加4G状态报告接口 TciReport4GInfo(...)

1.0.18(2019-12-10)
Update:
	加入更多的注册过程失败上报点
Repair:
	解决mstar音视频不同步(先包后至)时包的时间长度计算问题

1.0.17(2019-12-06)
Update:
	重命名sstrip函数以避免与爱加库的符号冲突
	发布文件中加入testcase.ini和测试用的数据包data.tgz

1.0.16(2019-12-05)
Update:
	增加设备开关接口
Repair:
	在1.0.15中引入的请求feature错误
	解决ai服务token过期问题

1.0.15(2019-12-04)
Update:
	优化http通信超时处理，避免服务器持续的慢速响应
	增加日志配置接口和自定义日志上传功能, 生成本地sdk日志文件
	当域名解析错误时更新 /etc/resolv.conf
	上传错误报告(目前只支持dns解析错误)
	libaiclient.a 调用http_post_multiparts()上传文件
Repair:
	ota下载进度上报(不影响ota本身)

1.0.13(2019-11-15)
Update:
	更新httpclt库支持 mulitpart/form-data

1.0.12(2019-11-11)
Update:
	更新变焦描述和接口
	增加人形跟踪开关接口
	增加日志上传回调

1.0.11(2019-11-01)
Update:
	增加4G和变焦

1.0.10(2019-10-26)
Update:
	支持h.265
	云存储索引上报消除重复记录
	增加布防逻辑
Repair:
	APP回放时非正常退出,设备端没有产生回放的STOP命令

1.0.9(2019-09-23)
Update:
	更新demo支持保存设置
	参见 "protocol_update_log.html" 2019-09-17 ~ 2019-09-19
Repair:
	解决对讲延时大的问题

1.0.8(2019-09-11)
Update:
    更新demo支持回放
Repair:
	事件在云存储初始化前发生并持续到初始化之后，起始时间计算不准确
	修改切换云录像清晰度后，重启无效的问题

1.0.7(2019-09-07)
New:
	TciStart() 增加云存储缓冲区大小参数
	有线添加后复位主动解绑
Repair:
	设备无AI服务时通知应用层的expiration值错误
	发生报警时无服务，报警持续期间服务开通，上报的录像起始时间错误
	从SDK里去掉libzbar.a
	  
1.0.6 
	The first release
