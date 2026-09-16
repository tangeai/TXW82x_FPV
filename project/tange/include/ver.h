/* 说明:
 *
 *  - 从Rev361起，版本号扩展为5位(以上)。低2位为修订号(_TCISDKVER_REVISION_)，其余为主版本号(_TCISDKVER_MAJOR_).
 *  - 修订号为奇数的为beta版，偶数为正式版
 *
 *  - 在分支上做了修改，只增加 _TCISDKVER_REVISION_
 *  - 在主线上的修改，正式发布前可以修改修订号，发布大版本时修订号归0增加 TCISDKVER_MAJOR_.
 *  - 每次发布(须同时提交代码)后, 将修订号加1(为奇数), 表示进入下一个开发周期.
 *
 */

//主版本号
#define _TCISDKVER_MAJOR_ 369
//用于分支的版本号 0~99(0表示为主分支, >0为表示分枝上的修改)
#define _TCISDKVER_REVISION_ 6

#define TCISDKVERSION (_TCISDKVER_MAJOR_*100 + _TCISDKVER_REVISION_)
