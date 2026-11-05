#pragma once

#define USE_LR1121
#define LORA_SCK 25
#define LORA_MOSI 32
#define LORA_MISO 33
#define LORA_CS 27
#define LORA_RESET 15
#define LR1121_SPI_NSS_PIN 27
#define LR1121_IRQ_PIN 37
#define LR1121_NRESET_PIN 15
#define LR1121_BUSY_PIN 36
#define LR11X0_DIO_AS_RF_SWITCH
#define LR1110_MAX_POWER 5
#define LR1120_MAX_POWER 5
#define BUTTON_PIN 14
#define BUTTON_NEED_PULLUP
#define NOMAD_BUTTON_B 12
#define NOMAD_FAN_PIN 2
#define NOMAD_RGB_PIN 22
#define NOMAD_APC2_PIN 26
// The C3 backpack is not a Meshtastic serial transport or an upload target.
#define NOMAD_BACKPACK_RX 18
#define NOMAD_BACKPACK_TX 5
#define NOMAD_BACKPACK_BOOT 23
#define NOMAD_BACKPACK_EN 19
#undef GPS_RX_PIN
#undef GPS_TX_PIN
