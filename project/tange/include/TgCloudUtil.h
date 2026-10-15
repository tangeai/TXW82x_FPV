/** \file TgCloudUtil.h
 *
 *
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct tgJSON;

/** 温度单位 */
typedef enum { 
    TEMP_C,  ///< Celcius
    TEMP_F   ///< Farenheit
} TEMPTYPE; 
/** 生成 ECEVENT_TEMPERATURE_H/ECEVENT_TEMPERATURE_L 事件的额外参数 
 * @param type 温度单位
 * @param temper 温度
 * @param obuf 输出的json格式字符串缓冲区
 * @param size 输入时, size 为缓冲区大小
 * @return json字符串长度+1。如果大于等于size, 意味obuf缓冲区不足，要重新分配大小至少为返回值的空间并再次调用
 * @see MKEVTDAT_Temperatur
 */
int MKEVTDATA_Temperatur(TEMPTYPE type, float temper, char *obuf, int size);

/** 返回json对象表示的温度事件参数.
 * @see MKEVTDATA_Temperatur
 */
struct tgJSON *MKEVTDAT_Temperatur(TEMPTYPE type, float temper);

/** 生成 ECEVENT_HUMIDITY_H/ECEVENT_HUMIDITY_L 事件的额外参数
 * @param humid 湿度: 0~100
 * @param obuf 输出的json格式字符串缓冲区
 * @param size 输入时, size 为缓冲区大小
 * @return json字符串长度+1。如果大于等于size, 意味obuf缓冲区不足，要重新分配大小至少为返回值的空间并再次调用
 * @see MKEVTDAT_Humidity
 */
int MKEVTDATA_Humidity(int humid, char *obuf, int size);

/** 返回json对象表示的湿度参数
 * @param humid 湿度: 0~100
 */
struct tgJSON *MKEVTDAT_Humidity(int humid);

/** ECEVENT_SITPOSE 事件参数
 *  @param sens 坐姿检测灵敏度参数。0(最灵敏)|1|2(最准确)
 */
struct tgJSON *MKEVTDAT_SitPoseSens(int sens);

/** 生成喂食事件的数据 */
int MKEVTDATA_Feeding(int nServings, char *obuf, int size);

/** 上报喂食事件
 * @param isManually 手动还是自动喂食
 * @param nServing   喂食份数
 * @param pic        图片
 * @param pic_len    图片长度
 * @return <0: 错误码
 */
int TcuSendFeedingEvent(int isManually, int nServing, void *pic, int pic_len);

/** 报告信号强度 */
void TcuReportSignal(time_t t, int sig_lvl);

//length: >0 data is a pointer to content
//        =0 data is the file path
int TcuCalcMd5(const char *path_or_data, long len, unsigned char digest[16]);
int TcuCalcSha256(const char *path_or_data, long len, unsigned char digest[32]);

/** 对保存在云端的设备属性的操作 */
typedef enum {
    PROPACT_UPDATE = 1, ///< 更新属性
    PROPACT_DELETE = 2  ///< 删除属性
} EPROPACTION;

/** 编辑保存在云端的属性值.
 * @param act 动作
 * @param jprops 要更新或删除的属性，不为能NULL. 删除时属性值被忽略. jprops由本接口释放
 * @param verNo 版本号
 * @return 0:ok; !=0:错误码
 * @sa https://tange-ai.feishu.cn/docx/YTqwdQl1MoVytBxQjBmctBitn5f
 */
TG_PUBLIC int TcuUpdateProperties(EPROPACTION act, struct tgJSON *jprops, int verNo);

/** 编辑保存在云端的属性值.
 * @param sprops 属性的json格式字符串, 不能为NULL. 删除时属性值被忽略. 
 * @param verNo 版本号
 * @return 0:ok; !=0:错误码
 * @note 应用想使用自己的json工具时使用本接口.
 * @sa https://tange-ai.feishu.cn/docx/YTqwdQl1MoVytBxQjBmctBitn5f
 */
TG_PUBLIC int TcuUpdatePropertiesS(EPROPACTION act, const char *sprops, int verNo);


/** 获取保存在云端的属性.
 * @param jprops 要获取的属性. NULL为全部属性. jprops由本接口释放
 * @param verNo 版本号. 输入时为要获取的版本号,-1 为最新版本; 输出时为返回的版本号
 * @param ppJData 返回属性值. 要调用 tgJSON_Delete() 释放
 * @return 0:ok; !=0:错误码
 * @sa https://tange-ai.feishu.cn/docx/YTqwdQl1MoVytBxQjBmctBitn5f
 */
TG_PUBLIC int TcuGetProperties(struct tgJSON *jprops, int *verNo, struct tgJSON **ppJData);

/** 获取保存在云端的属性.
 * @param sprops 要获取的属性的json字符串表示. NULL为全部属性. 
 * @param verNo 版本号. 输入时为要获取的版本号,-1 为最新版本; 输出时为返回的版本号
 * @param ppJData 返回的json格式表示的属性值.
 * @return 0:ok; !=0:错误码
 * @note 应用想使用自己的json工具时使用本接口.
 */
TG_PUBLIC int TcuGetPropertiesS(const char *sprops, int *verNo, char **ppJData);

/** 注册qrcode功能.
 * @note 从rev354开始, qrcode功能作为外部模块存在, 不再默认可用. 开发者需要在使用前注册。 \n
 *      此改为是为了减小不需要此功能的应用的代码体积，同时统一库的发布内容.
 * */
void qrInit();

#if 0
/** 按分类上传用户自定义数据.
 * @param scene 分类. App端用分类进行检索. 水质数据使用 "water_quality"
 * @param jdata 数据。本调用会释放jdata对象的空间
 * @return 0:ok; <0:错误码
 */
int TcuUploadCustomData(const char *scene, time_t t, struct tgJSON *jdata);
#endif

/** 按分类上传用户自定义数据.
 * @param scene 分类. App端用分类进行检索. 水质数据使用 "water_quality"
 * @param str 用户数据, 自定义的字符串, 平台不解析.
 * @return 0:ok; <0:错误码
 */
TG_PUBLIC int TcuUploadCustomDataS(const char *scene, time_t, const char *str);

#ifdef __cplusplus
} /* extern "C" */
#endif
