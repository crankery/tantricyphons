#ifndef _LAMA_DOT_H
#define _LAMA_DOT_H

#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS 17
#define PIN_SCK 18
#define PIN_MOSI 19

#define I2C_PORT i2c0
#define I2C_SDA 8
#define I2C_SCL 9

#define SAMPLE_BUFFER_SIZE 256
#ifndef SAMPLE_RATE
#define SAMPLE_RATE 44100
#endif

#endif // _LAMA_DOT_H