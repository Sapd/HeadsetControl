#define ID_LOGITECH_G633 0x0a5c
#define ID_LOGITECH_G635 0x0a89
#define ID_LOGITECH_G933 0x0a5b
#define ID_LOGITECH_G935 0x0a87
#define ID_LOGITECH_G733 0x0ab5

extern void g633_635_733_933_935_init(struct device* device, char* name);
extern int g633_635_733_933_935_request_battery(hid_device* device_handle);
extern int g633_635_733_933_935_send_sidetone(hid_device* device_handle, uint8_t num);
extern int g633_635_733_933_935_lights(hid_device* device_handle, uint8_t on);