#include "sys_config.h"
#include "basic_include.h"
#include "hal/adc.h"
#include "lib/rpc/cpurpc.h"
#include "lib/atcmd/libatcmd.h"
#include "lib/common/atcmd.h"
#include "lib/umac/ieee80211.h"
#include "lib/lmac/lmac.h"
#include "lib/bluetooth/uble/ble_demo.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/sys.h"
#include "lwip/ip_addr.h"
#include "lwip/tcpip.h"
#include "netif/ethernetif.h"

#include "lib/net/dhcpd/dhcpd.h"
#include "lib/net/utils.h"
#include "lib/net/skmonitor/skmonitor.h"
#include "syscfg.h"
#include "app/user_app.h"

__init static void sys_network_add_netif(uint8 devid, char *name, uint8 dhcpc, uint8 setip)
{
    ip_addr_t ipaddr, netmask, gw;
    struct netdev *ndev = (struct netdev *)dev_get(devid);
    if (ndev) {
        ipaddr.addr  = sys_cfgs.ipaddr;
        netmask.addr = sys_cfgs.netmask;
        gw.addr      = sys_cfgs.gw_ip;
        os_printf(KERN_INFO"netif %s setttings: "IPSTR"/"IPSTR"/"IPSTR"!\r\n", name, IP2STR_N(ipaddr.addr), IP2STR_N(netmask.addr), IP2STR_N(gw.addr));
        lwip_netif_add(ndev, name, (setip ? &ipaddr : NULL), (setip ? &netmask : NULL), (setip ? &gw : NULL));
        if (dhcpc) {
            sys_status.dhcpc_done = 0;
            lwip_netif_set_dhcp(ndev, 1);
            os_printf(KERN_INFO"netif %s start dhcp client!\r\n", name);
        }
    } else {
        os_printf(KERN_ERR"No Netdev: %d\r\n", devid);
    }
}

__init void sys_network_init(void)
{
    tcpip_init(NULL, NULL);
    sock_monitor_init();

    sys_network_add_netif(HG_WIFI0_DEVID, "w0", sys_cfgs.dhcpc_en, 1);
    lwip_netif_set_default2("w0");
#if GMAC_EN
    uint8_t mac[6];
    sysctrl_efuse_mac_addr_calc(mac);
    mac[5] |= 3;
    netdev_set_macaddr((struct netdev *)dev_get(HG_GMAC_DEVID), mac);
    sys_network_add_netif(HG_GMAC_DEVID, "e0", sys_cfgs.dhcpc_en, 1);
    lwip_netif_set_default2("e0");
#endif

    sys_status.dhcpc_result.ipaddr  = sys_cfgs.ipaddr;
    sys_status.dhcpc_result.netmask = sys_cfgs.netmask;
    sys_status.dhcpc_result.router  = sys_cfgs.gw_ip;
}

//启动DHCP服务器
void sys_dhcpd_start()
{
    struct dhcpd_param param;

    if (sys_cfgs.dhcpd_en && sys_cfgs.wifi_mode == WIFI_MODE_AP) {
        os_memset(&param, 0, sizeof(param));
        param.start_ip   = sys_cfgs.dhcpd_startip;
        param.end_ip     = sys_cfgs.dhcpd_endip;
        param.netmask    = sys_cfgs.netmask;
        param.lease_time = sys_cfgs.dhcpd_lease_time;
        param.dns1       = sys_cfgs.dhcpd_dns1;
        param.dns2       = sys_cfgs.dhcpd_dns2;
        param.router     = sys_cfgs.dhcpd_router;
        if (dhcpd_start("w0", &param)) {
            os_printf("dhcpd start error\r\n");
        }else{
            os_printf("dhcpd start successful\r\n");
        }
    }
}

/* tcpip_thread 上下文执行: 真正启动 dhcp. 非阻塞回调, 不在调用方等待 */
static void _dhcpc_restart_cb(void *ctx)
{
    struct netif *nif = (struct netif *)ctx;
    if (nif) {
        dhcp_start(nif);
    }
}

void sys_dhcpc_check(void)
{
    static uint8 __loop;
    if (sys_cfgs.dhcpc_en && !sys_status.dhcpc_done) {
        if (__loop++ > 6) {
//            lwip_netif_set_dhcp2("w0", 1);
            /* 原 lwip_netif_set_dhcp2 -> netifapi_dhcp_start 是阻塞调用 (投消息给
             * tcpip_thread 并等返回). 本函数跑在 main_wk 的 sys_main_loop 里,
             * 而 sys_main_loop 还负责喂狗 —— 配网完成后探鸽建大量 socket, tcpip_thread
             * 繁忙时阻塞调用会卡住 main_wk, sys_main_loop 不返回 -> 停止喂狗 -> 看门狗
             * 复位 (BLE 配网时稳定复现). 改用 tcpip_try_callback 异步投递 dhcp_start,
             * 立即返回不阻塞, mbox 满也直接返回, 永不卡住 main_wk。 */
            struct netif *nif = netif_find("w0");
            if (nif) {
                tcpip_try_callback(_dhcpc_restart_cb, nif);
            }

            __loop = 0;
        }
    }
}

