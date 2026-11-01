#ifndef TANTRICYPHONS_USB_CONSOLE_H
#define TANTRICYPHONS_USB_CONSOLE_H
/* Core 0 after startup; bounded RX/TX and best-effort output, no audio I/O. */
void usb_console_init(void);
void usb_console_poll(void);
#endif
