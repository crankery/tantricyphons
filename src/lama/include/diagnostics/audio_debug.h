#ifndef LAMA_AUDIO_DEBUG_H
#define LAMA_AUDIO_DEBUG_H
#define DEBUG_UART_TX_PIN 0
#define DEBUG_UART_BAUD 115200

void audio_debug_init(void);
/* Core 0, after sound_core_start(). Never call from an IRQ or core 1. */
void audio_debug_poll(void);
#endif
