#ifndef WEBSOCKETCLIENT_H
#define WEBSOCKETCLIENT_H_

#include "Arduino.h"
#include <AsyncTCP.h>

enum webEvents
{
  WEBSOCKET_EVENT_ERROR,
  WEBSOCKET_EVENT_CONNECTED,
  WEBSOCKET_EVENT_DISCONNECTED,
  WEBSOCKET_EVENT_DATA_TEXT,
  WEBSOCKET_EVENT_DATA_BINARY,
};

struct WsHeader
{
  uint8_t Opcode:4; // little endien order
  uint8_t Rsvd:3;
  uint8_t Fin:1;
  uint8_t Len:7; // 126/127 = 16/64 bit ExtLen
  uint8_t Mask:1;
  uint8_t ExtLen[2]; // big endien
};

class WebSocketClient
{
public:
  WebSocketClient();
  typedef void (*WebSocketEventHandler)(uint8_t event, char *data, uint16_t length);
  bool connect(char hostname[], char path[] = "/", int port = 80);
  void setCallback(WebSocketEventHandler webSocketEventHandler);
  void send(String data);

private:
  void _onConnect(AsyncClient* client);
  void _onDisconnect(AsyncClient* client);
  void _onData(AsyncClient* client, char* data, size_t len);
  WebSocketEventHandler _webSocketEventHandler;
  String readLine();

#define WSBUF_SIZE 1024
  char *m_pBuffer = NULL;
  int16_t m_bufIdx;
  AsyncClient m_ac;
  bool m_bHandshake;
  bool m_bUpgrade;
  char m_hostname[64];
  char m_path[64];
};

#endif
