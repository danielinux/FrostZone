#include <stdint.h>
#include <stdbool.h>
#if !CONFIG_USB_NET && CONFIG_TCPIP == 1

typedef void tusb_desc_interface_t;
typedef void tusb_control_request_t;
typedef int xfer_result_t;

void netd_init(void)
{
}

bool netd_deinit(void)
{
    return true;
}

void netd_reset(uint8_t rhport)
{
    (void)rhport;
}

uint16_t netd_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len)
{
    (void)rhport;
    (void)itf_desc;
    (void)max_len;
    return 0;
}

bool netd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    (void)rhport;
    (void)stage;
    (void)request;
    return false;
}

bool netd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    (void)rhport;
    (void)ep_addr;
    (void)result;
    (void)xferred_bytes;
    return false;
}

void netd_report(uint8_t *buf, uint16_t len)
{
    (void)buf;
    (void)len;
}

#endif /* !CONFIG_USB_NET */
