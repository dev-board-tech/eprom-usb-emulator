#include <Arduino.h>
#include <arduFPGA-app-common-arduino.h>
#include <SPI.h>

/*
 * FLASH Partitions:
 * 0x0-0x2FFFF = FPGA design.
 * 0x30000-0x40000 = application code.
 */
 #define FLASH_PARTITION_FPGA_START (0x0)
 #define FLASH_PARTITION_FPGA_SIZE  (0x20000)

 #define FLASH_PARTITION_APP_START  (FLASH_PARTITION_FPGA_START + FLASH_PARTITION_FPGA_SIZE)
 #define FLASH_PARTITION_APP_SIZE   (0x8000)

/*
 * We are on a FPGA, the direction and the state of the pins are already set up, and the pin can be shared used as an input and as output at the same time.
 */

#define SPI_CS_PIN        8
#define VCC_DETECT_PIN    8
/*
 * When design is reset, the PB5 is set to '1, in FLASH write mode running the boot-loader.
 * After the application is loaded we need to make PB5 = '0, will disable the FLASH writting and will trigger a CPU reset.
 * When FLASH write is disabled the CPU will run code from application memory.
 */
#define FLASH_WRITE_EN    9

#define FLASH_WRITE_DATA_LOW    PORTC
#define FLASH_WRITE_DATA_HIGH   PORTD

enum {
  BOOT_LOADER_WRITE_MODE_NONE,
  BOOT_LOADER_WRITE_MODE_DIRECT,
  BOOT_LOADER_WRITE_MODE_FLASH,
  BOOT_LOADER_WRITE_MODE_DESIGN,
}bootLoaderWriteMode_e = BOOT_LOADER_WRITE_MODE_NONE;


sTimer waitEnterBootLoader;

uint8_t dataBuff[256 + 4] = {0};
char rcvBuf[(sizeof(dataBuff) * 2) + 1] = {0};
int rcvBufPtr = 0;
int writePtr = 0;

Flash25 spiFlash = Flash25(&SPI, SPI_CS_PIN);

void executeCommand(char *cmd);

void setup() {
  Serial1.begin(115200);
  //while(digitalRead(FLASH_WRITE_EN) == LOW) {
  //  Serial1.println("STUCK");
  //}
  digitalWrite(FLASH_WRITE_EN, LOW);
  waitEnterBootLoader.SetInterval(1000);
  waitEnterBootLoader.Start();
  Serial1.println("BOOT_LOADER_STARTED");
  SPI.begin();
}

void loop() {
  if (waitEnterBootLoader.Tick()) {
    spiFlash.read(FLASH_PARTITION_APP_START, dataBuff, 4);
    uint32_t i = uni_8_to_32(dataBuff[3], dataBuff[2], dataBuff[1], dataBuff[0]);
    if (0xFFFFFFFF == i) {
      waitEnterBootLoader.Stop();
      asm("jmp 0");
    }
    spiFlashToPgmRam();
    jumpToApp();
  }
  if (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n' || c == '\r') {
      rcvBufPtr = 0;
      executeCommand(rcvBuf);
    } else {
      if (sizeof(rcvBuf) > rcvBufPtr) {
        rcvBuf[rcvBufPtr] = c;
        rcvBufPtr++;
        rcvBuf[rcvBufPtr] = '\0';
      }
    }
  }
}

void executeCommand(char *cmd) {
  if (BOOT_LOADER_WRITE_MODE_NONE == bootLoaderWriteMode_e) {
    if (!strcmp_P(cmd, PSTR("ENTER-BOOT-LOADER-DIRECT"))) {
      waitEnterBootLoader.Stop();
      Serial1.println("ENTER-BOOT-LOADER-DIRECT-OK");
      bootLoaderWriteMode_e = BOOT_LOADER_WRITE_MODE_DIRECT;
    } else if (!strcmp_P(cmd, PSTR("ENTER-BOOT-LOADER-FLASH"))) {
      waitEnterBootLoader.Stop();
      Serial1.println("ENTER-BOOT-LOADER-FLASH-OK");
      writePtr = 0;
      spiFlash.eraseBlock(FLASH_PARTITION_APP_START);
      bootLoaderWriteMode_e = BOOT_LOADER_WRITE_MODE_FLASH;
    } else if (!strcmp_P(cmd, PSTR("ENTER-BOOT-LOADER-DESIGN"))) {
      waitEnterBootLoader.Stop();
      Serial1.println("ENTER-BOOT-LOADER-DESIGN-OK");
      writePtr = 0;
      spiFlash.eraseBlock(FLASH_PARTITION_FPGA_START);
      spiFlash.eraseBlock(FLASH_PARTITION_FPGA_START + 0x10000);
      bootLoaderWriteMode_e = BOOT_LOADER_WRITE_MODE_DESIGN;
    }
  }
  rcvBufPtr = 0;
  memset(rcvBuf, 0, sizeof(rcvBuf));
  delay(100);
  while(1) {
    if(Serial1.available()) {
      char c = Serial1.read();
      if('\n' == c || '\r' == c) {
        if(!strcmp_P(rcvBuf, PSTR("DONE"))) {
          Serial1.print("K");
          if (BOOT_LOADER_WRITE_MODE_DESIGN == bootLoaderWriteMode_e) {
            Serial1.println("Push the reset BTN");
            while(1);
          }
          if (BOOT_LOADER_WRITE_MODE_FLASH == bootLoaderWriteMode_e) {
            spiFlashToPgmRam();
          }
          jumpToApp();
        } else {
          int recv_len = util_get_bin_from_hex_buf(dataBuff, rcvBuf, sizeof(dataBuff));
          if(4 >= recv_len) {
            Serial1.print("e");
            rcvBufPtr = 0;
            continue;
          }
          uint16_t chk = 0;
          for (int i = 0; i < recv_len - 4; i++) {
            chk += dataBuff[i];
          }
          uint16_t chkrp = (dataBuff[recv_len - 4] << 8) + dataBuff[recv_len - 3];
          uint16_t chkrn = 0 - ((dataBuff[recv_len - 2] << 8) + dataBuff[recv_len - 1]);
          if(!recv_len || chkrp != chkrn || chkrp != chk) {
            Serial1.print("e");
            rcvBufPtr = 0;
          } else {
            recv_len -= 4;
            if(BOOT_LOADER_WRITE_MODE_DIRECT == bootLoaderWriteMode_e) {
              for(int i = 0; i < recv_len; i+=2) {
                FLASH_WRITE_DATA_HIGH = dataBuff[i];
                FLASH_WRITE_DATA_LOW = dataBuff[i+1];
              }
            } else if(BOOT_LOADER_WRITE_MODE_FLASH == bootLoaderWriteMode_e) {
              spiFlash.write(FLASH_PARTITION_APP_START + writePtr, dataBuff, recv_len);
              writePtr += recv_len;
            } else if(BOOT_LOADER_WRITE_MODE_DESIGN == bootLoaderWriteMode_e) {
              spiFlash.write(FLASH_PARTITION_FPGA_START + writePtr, dataBuff, recv_len);
              writePtr += recv_len;
            }
            Serial1.print("k");
          }
        }
        rcvBufPtr = 0;
      } else {
        if(sizeof(rcvBuf) > rcvBufPtr) {
          rcvBuf[rcvBufPtr] = c;
          rcvBufPtr++;
          rcvBuf[rcvBufPtr] = '\0';
        }
      }
    }
  }
}

void spiFlashToPgmRam() {
  for (int j = 0; j < FLASH_PARTITION_APP_SIZE; j += 256) {
    spiFlash.read(FLASH_PARTITION_APP_START + j, dataBuff, 256);
    for (int i = 0; i < 256; i += 2) {
      FLASH_WRITE_DATA_HIGH = dataBuff[i];
      FLASH_WRITE_DATA_LOW = dataBuff[i+1];
    }
  }
}

void jumpToApp() {
  asm("cli");
  digitalWrite(FLASH_WRITE_EN, HIGH);
}
