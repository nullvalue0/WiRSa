#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HardwareSerial.h>
#include <Adafruit_SSD1306.h>
#include "SD.h"
#include "arduino-timer.h"

// Debug flags - uncomment to enable verbose debug output
#define PPP_DEBUG    // Enable PPP/LCP/IPCP debug output on Serial

// Pin definitions
#define SWITCH_PIN 0       // GPIO0 (programming mode pin)
#define LED_PIN 2          // Status LED

#define DCD_PIN 33         // DCD Carrier Status
#define RTS_PIN 15         // RTS Request to Send
#define CTS_PIN 27         // CTS Clear to Send
#define DTR_PIN 4          // DTR Data Terminal Ready
#define DSR_PIN 26         // DSR Data Set Ready
#define RI_PIN  25         // RI Ring Indicator

#define SW1_PIN 36         // DOWN
#define SW2_PIN 39         // BACK
#define SW3_PIN 34         // ENTER
#define SW4_PIN 35         // UP

// SD Card Pins
#define SD_CS 5
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK 18

#define RXD2 16
#define TXD2 17

// Mode definitions
#define MODE_MAIN 0
#define MODE_MODEM 1
#define MODE_FILE 2
#define MODE_PLAYBACK 3
#define MODE_SETTINGS 4
#define MODE_SETBAUD 5
#define MODE_LISTFILE 6
#define MODE_ORIENTATION 7
#define MODE_DEFAULTMODE 8
#define MODE_PROTOCOL 9
#define MODE_SERIALCONFIG 10
#define MODE_SLIP 11
#define MODE_PPP 12
#define MODE_WIFI_SETUP 13
#define MODE_WIFI_PASSWORD 14
#define MODE_DIAGNOSTICS 15

// Menu modes
#define MENU_BOTH 0
#define MENU_NUM  1
#define MENU_DISP 2

// Transfer modes
#define XFER_SEND 0
#define XFER_RECV 1

// Display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// EEPROM addresses
#define VERSION_ADDRESS 0
#define VERSION_LEN     2
#define SSID_ADDRESS    2
#define SSID_LEN        32
#define PASS_ADDRESS    34
#define PASS_LEN        63
#define IP_TYPE_ADDRESS 97
#define STATIC_IP_ADDRESS 98
#define STATIC_GW       102
#define STATIC_DNS      106
#define STATIC_MASK     110
#define BAUD_ADDRESS    111
#define ECHO_ADDRESS    112
#define SERVER_PORT_ADDRESS 113
#define AUTO_ANSWER_ADDRESS 115
#define TELNET_ADDRESS  116
#define VERBOSE_ADDRESS 117
#define PET_TRANSLATE_ADDRESS 118
#define FLOW_CONTROL_ADDRESS 119
#define PIN_POLARITY_ADDRESS 120
#define DTR_MODE_ADDRESS 121
#define DIAL0_ADDRESS   200
#define DIAL1_ADDRESS   250
#define DIAL2_ADDRESS   300
#define DIAL3_ADDRESS   350
#define DIAL4_ADDRESS   400
#define DIAL5_ADDRESS   450
#define DIAL6_ADDRESS   500
#define DIAL7_ADDRESS   550
#define DIAL8_ADDRESS   600
#define DIAL9_ADDRESS   650
#define BUSY_MSG_ADDRESS 700
#define BUSY_MSG_LEN    80
#define ORIENTATION_ADDRESS 780
#define DEFAULTMODE_ADDRESS 790
#define SERIALCONFIG_ADDRESS 791
#define USB_DEBUG_ADDRESS 792
#define QUIET_MODE_ADDRESS 793
#define ESC_CHAR_ADDRESS 794

// Port Forwarding (generic, shared by SLIP and PPP)
#define PORTFWD_BASE    950   // Port forwards start here
#define PORTFWD_SIZE    10    // 10 bytes per entry (active, proto, extPort, intPort, intIP)
#define PORTFWD_COUNT   8     // Max 8 port forwards
// Total: 950 + (8 * 10) = 1030

#define LAST_ADDRESS    1030  // Extended for port forwards

// Version
#define VERSIONA 0
#define VERSIONB 1

// Modem constants
#define LISTEN_PORT 23
#define RING_INTERVAL 3000
#define MAX_CMD_LENGTH 256
#define LED_TIME 15
#define TX_BUF_SIZE 256

// ============================================================================
// ZMODEM Protocol Constants (per official spec and modern implementations)
// ============================================================================

// Frame types (complete set)
#define ZRQINIT     0x00  // Request receive init (sender to receiver)
#define ZRINIT      0x01  // Receive init (receiver to sender)
#define ZSINIT      0x02  // Send init sequence (optional)
#define ZACK        0x03  // Acknowledge
#define ZFILE       0x04  // File name from sender
#define ZSKIP       0x05  // Skip this file
#define ZNAK        0x06  // Last packet was garbled
#define ZABORT      0x07  // Abort batch transfers
#define ZFIN        0x08  // Finish session
#define ZRPOS       0x09  // Resume data at this position
#define ZDATA       0x0A  // Data packet(s) follow
#define ZEOF        0x0B  // End of file
#define ZFERR       0x0C  // Fatal read/write error
#define ZCRC        0x0D  // Request/response for file CRC
#define ZCHALLENGE  0x0E  // Security challenge
#define ZCOMPL      0x0F  // Request is complete
#define ZCAN        0x10  // Other end cancelled with CAN chars
#define ZFREECNT    0x11  // Request free bytes on filesystem
#define ZCOMMAND    0x12  // Command from sending program
#define ZSTDERR     0x13  // Output to stderr

// Header format indicators
#define ZPAD        0x2A  // '*' Padding char (begins frames)
#define ZDLE        0x18  // Ctrl-X ZModem escape
#define ZDLEE       0x58  // Escaped ZDLE (ZDLE XOR 0x40)
#define ZBIN        0x41  // 'A' Binary frame (CRC-16)
#define ZHEX        0x42  // 'B' Hex frame (CRC-16)
#define ZBIN32      0x43  // 'C' Binary frame (CRC-32)

// Data subpacket terminators (frame end types)
#define ZCRCE       0x68  // 'h' CRC next, end frame, header follows
#define ZCRCG       0x69  // 'i' CRC next, frame continues nonstop
#define ZCRCQ       0x6A  // 'j' CRC next, frame continues, ZACK expected
#define ZCRCW       0x6B  // 'k' CRC next, end frame, ZACK expected

// Special ZDLE sequences
#define ZRUB0       0x6C  // Translate to 0x7F (DEL)
#define ZRUB1       0x6D  // Translate to 0xFF

// ZRINIT capability flags (ZF0)
#define ZF0_CANFDX  0x01  // Can send/receive true full duplex
#define ZF0_CANOVIO 0x02  // Can receive during disk I/O
#define ZF0_CANBRK  0x04  // Can send break signal
#define ZF0_CANCRY  0x08  // Can decrypt (not used)
#define ZF0_CANLZW  0x10  // Can uncompress (not used)
#define ZF0_CANFC32 0x20  // Can use 32-bit CRC
#define ZF0_ESCCTL  0x40  // Expects ctrl chars escaped
#define ZF0_ESC8    0x80  // Expects 8th bit escaped

// Byte positions in header array
#define ZF0         4     // First flags byte (also ZP3 for positions)
#define ZF1         3     // Second flags byte (also ZP2)
#define ZF2         2     // Third flags byte (also ZP1)
#define ZF3         1     // Fourth flags byte (also ZP0)
#define ZP0         1     // Low order position byte
#define ZP1         2
#define ZP2         3
#define ZP3         4     // High order position byte

// Control characters
#define CAN         0x18  // Cancel character
#define XON         0x11  // Resume transmission
#define XOFF        0x13  // Pause transmission
#define DLE         0x10  // Data Link Escape
#define CR          0x0D  // Carriage return
#define LF          0x0A  // Line feed

// Timing constants
#define ZMODEM_TIMEOUT       10000   // 10 second timeout for data
#define ZMODEM_INIT_TIMEOUT  60000   // 60 second timeout for init
#define ZMODEM_MAX_ERRORS    10      // Max consecutive errors before abort
#define ZMODEM_BLOCK_SIZE    1024    // Standard block size

// Return codes for internal functions
#define ZM_OK           0
#define ZM_TIMEOUT     -1
#define ZM_CANCELLED   -2
#define ZM_CRC_ERROR   -3
#define ZM_ERROR       -4
#define ZM_FRAME_END    1
#define ZM_FRAME_CONT   2

// Debug logging levels
enum ZMDebugLevel {
  ZM_DEBUG_OFF = 0,      // No logging
  ZM_DEBUG_MINIMAL = 1,  // Protocol events, errors, state changes
  ZM_DEBUG_VERBOSE = 2   // Full hex dumps of all bytes
};

// State machine states
enum ZMStateEnum {
  ZM_IDLE,           // No transfer in progress
  ZM_SEND_INIT,      // Waiting for ZRINIT from receiver
  ZM_SEND_ZFILE,     // Sending file info, waiting for ZRPOS
  ZM_SEND_DATA,      // Sending file data
  ZM_SEND_EOF,       // Sent ZEOF, waiting for ZRINIT
  ZM_SEND_FIN,       // Sending ZFIN
  ZM_RECV_INIT,      // Sent ZRINIT, waiting for ZFILE
  ZM_RECV_FILE_INFO, // Received ZFILE, parsing file info
  ZM_RECV_DATA,      // Receiving file data
  ZM_RECV_EOF,       // Received ZEOF
  ZM_DONE,           // Transfer complete
  ZM_STATE_ERROR     // Error state
};

// Telnet constants
#define IAC 0xff
#define DO 0xfd
#define WONT 0xfc
#define WILL 0xfb
#define DONT 0xfe

// Global objects
extern Adafruit_SSD1306 display;
extern WiFiClient tcpClient;
extern WiFiClient consoleClient;  // Telnet console client (separate from call client)
extern WiFiServer tcpServer;
extern WebServer webServer;
extern MDNSResponder mdns;
extern HardwareSerial PhysicalSerial;
extern Timer<16> modem_timer;

// Global variables
extern String build;
extern String cmd;
extern bool cmdMode;
extern bool callConnected;
extern bool consoleConnected;  // Telnet console is connected
extern bool telnet;
extern bool verboseResults;
extern int tcpServerPort;
extern unsigned long lastRingMs;
extern char plusCount;
extern unsigned long plusTime;
extern unsigned long ledTime;
extern uint8_t txBuf[TX_BUF_SIZE];
extern String speedDials[10];
extern byte serialConfig;
extern byte serialSpeed;
extern String ssid, password, busyMsg;
extern bool echo;
extern bool autoAnswer;
extern byte flowControl;
extern byte pinPolarity;
extern byte dtrMode;
extern bool petTranslate;
extern unsigned long connectTime;
extern bool txPaused;
extern int menuMode;
extern int lastMenu;
extern int menuIdx;
extern int menuCnt;
extern int menuStart;
extern bool msgFlag;
extern byte dispOrientation;
extern byte defaultMode;
extern int protocol;
extern String fileName;
extern String files[100];
extern int fileCount;
extern String terminalMode;
extern int xferDirection;
extern String xferMsg;
extern unsigned long xferBytes;
extern unsigned int xferBlock;
extern uint8_t zBuf[4096];  // Increased from 2048 to 4096 to reduce CRC errors from WiFi interrupt delays

// ZModem Context Structure - comprehensive protocol state
struct ZModemContext {
  // State machine
  ZMStateEnum state;

  // File info
  uint32_t filePos;        // Current position in file
  uint32_t fileSize;       // Total file size
  char fileName[64];       // Current file name
  uint32_t fileTime;       // File modification time (seconds since 1970)

  // Protocol state
  bool useCRC32;           // Use CRC-32 vs CRC-16
  bool receive32BitData;   // Receiving 32-bit CRC data
  bool canFullDuplex;      // Receiver supports full duplex
  bool canOverlapIO;       // Receiver can overlap I/O
  bool escapeCtrlChars;    // Escape all control characters
  uint16_t recvBufSize;    // Receiver's buffer size (0 = unlimited)

  // Error handling
  uint8_t errorCount;      // Consecutive errors
  uint8_t maxErrors;       // Maximum errors before abort

  // Timeout handling
  uint32_t lastActivity;   // millis() of last activity
  uint16_t timeout;        // Timeout in ms

  // Cancel detection
  uint8_t canCount;        // Count of consecutive CAN chars (5 = abort)
  uint8_t lastSent;        // Last byte sent (for @ CR escaping)

  // Header parsing
  uint8_t rxdHeader[5];    // Last received header (type + 4 bytes)
  uint32_t rxdHeaderPos;   // Position from last header

  // Debug logging
  ZMDebugLevel debugLevel; // Current debug level
  File* debugFile;         // Debug log file handle (pointer to avoid memset corruption)
};

extern ZModemContext zmCtx;

// Legacy support - alias for compatibility
#define zmState zmCtx

// Constant arrays
extern const int bauds[];
extern const int bits[];
extern const int speedDialAddresses[];
extern const uint8_t PROGMEM wifi_symbol[];
extern const uint8_t PROGMEM phon_symbol[];
extern String baudDisp[];
extern String bitsDisp[];
extern String mainMenuDisp[];
extern String settingsMenuDisp[];
extern String orientationMenuDisp[];
extern String playbackMenuDisp[];
extern String fileMenuDisp[];
extern String protocolMenuDisp[];
extern String defaultModeDisp[];
extern String resultCodes[];

// CRC tables
extern const uint16_t crc16_ccitt_table[256];
extern const uint32_t crc32tab[256];

// Button state variables (not macros) for proper debouncing
extern bool BTNUP;
extern bool BTNDN;
extern bool BTNEN;
extern bool BTNBK;

#endif // GLOBALS_H
