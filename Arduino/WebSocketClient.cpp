// Lean ESP32 Async WebSocket client specifically for ESPAsyncWebServer. Not actually encrypted.

#include "WebSocketClient.h"

WebSocketClient::WebSocketClient()
{
  m_ac.onConnect([](void* obj, AsyncClient* c) { (static_cast<WebSocketClient*>(obj))->_onConnect(c); }, this);
  m_ac.onDisconnect([](void* obj, AsyncClient* c) { (static_cast<WebSocketClient*>(obj))->_onDisconnect(c); }, this);
  m_ac.onData([](void* obj, AsyncClient* c, void* data, size_t len) { (static_cast<WebSocketClient*>(obj))->_onData(c, static_cast<char*>(data), len); }, this);
}

bool WebSocketClient::connect(char hostname[], char path[], int port)
{
  if(m_ac.connected()  || m_ac.connecting())
    return true;
  strncpy(m_hostname, hostname, sizeof(m_hostname));
  strncpy(m_path, path, sizeof(m_path));
  if(!m_ac.connect(hostname, port))
    return false;

  if(m_pBuffer == NULL)
  {
    m_pBuffer = new char[WSBUF_SIZE];
    if(m_pBuffer) m_pBuffer[0] = 0;
  }
  m_bufIdx = 0;

  m_bUpgrade = false;
  m_bHandshake = false;
	return true;
}

void WebSocketClient::_onConnect(AsyncClient* client)
{
  (void)client;

  if(!m_bUpgrade) // only upgrade first connection
  {
    m_bUpgrade = true;
    String head = "GET ";
    head += m_path;
    head += " HTTP/1.1\n"
            "Upgrade: WebSocket\n"
            "Connection: Upgrade\n"
            "Host: ";
    head += client->remoteIP().toString();
    head += "\nOrigin: ESP32WebSocketClient\n";
    head += "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\n"; // fake key
    head += "Sec-WebSocket-Version: 13\n\n";
    m_ac.add(head.c_str(), head.length());
    return;
  }
}

void WebSocketClient::_onDisconnect(AsyncClient* client)
{
  (void)client;
  if(_webSocketEventHandler)
    _webSocketEventHandler(WEBSOCKET_EVENT_DISCONNECTED, "", 0);
}

// build file in chunks
void WebSocketClient::_onData(AsyncClient* client, char* data, size_t len)
{
  if(!m_bHandshake) // first packet should be handshake
  {
    m_bHandshake = true;

    bool bGood = ( strncmp(data, "HTTP/1.1 101", 12) == 0);

    if(_webSocketEventHandler)
      _webSocketEventHandler(bGood ? WEBSOCKET_EVENT_CONNECTED:WEBSOCKET_EVENT_ERROR, data, len);
    if(!bGood)
      client->close();
    return;
  }

  WsHeader *hdr = (WsHeader *)data;
  uint16_t tlen = hdr->ExtLen[0] << 8 | hdr->ExtLen[1];

  static bool bText;

  switch(hdr->Opcode)
  {
    case 1: // text
      bText = true;
      m_bufIdx = 0;
      break;
    case 2: // bin
      bText = false;
      m_bufIdx = 0;
      break;
    case 0: // continuation
      break;
  }

  if(m_pBuffer == NULL || m_bufIdx + tlen >= WSBUF_SIZE + 1)
     return;
  memcpy(m_pBuffer + m_bufIdx, data + 4, tlen);
  m_bufIdx += tlen;
  m_pBuffer[m_bufIdx] = 0; // null terminate text

  if(hdr->Fin)
    if(_webSocketEventHandler)
      _webSocketEventHandler(bText ? WEBSOCKET_EVENT_DATA_TEXT:WEBSOCKET_EVENT_DATA_BINARY, m_pBuffer, m_bufIdx);
}

void WebSocketClient::setCallback(WebSocketEventHandler webSocketEventHandler)
{
  _webSocketEventHandler = webSocketEventHandler;
}

void WebSocketClient::send(String data)
{
  WsHeader hdr;

  hdr.Fin = 1;
  hdr.Opcode = 1;
  hdr.Rsvd = 0;
  hdr.Mask = 0;
  hdr.Len = 126;
  hdr.ExtLen[0] = data.length() >> 8;
  hdr.ExtLen[1] = data.length() & 0xFF;

  m_ac.add((char *)&hdr, sizeof(hdr) );
  m_ac.add(data.c_str(), data.length() );
  m_ac.send();
}
