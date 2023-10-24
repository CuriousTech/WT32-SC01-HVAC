#ifndef FORECAST_H
#define FORECAST_H

#include <Arduino.h>
#include <AsyncTCP.h>

#define FC_CNT 74

struct forecastItem
{
  int16_t temp;
  int16_t humidity;
  char    icon[4];
  int16_t rain[4];
  uint8_t res[4];
};

struct forecastData
{
  uint32_t Date;
  uint32_t loadDate;
  uint16_t Freq;
  forecastItem Data[FC_CNT];
};

struct forecastDataOld
{
  uint32_t Date;
  uint32_t loadDate;
  uint16_t Freq;
  int8_t   Data[FC_CNT];
};


enum FCS_Status
{
  FCS_Idle,
  FCS_Busy,
  FCS_Done,
  FCS_ConnectError,
  FCS_Fail,
  FCS_MemoryError,
};

class ForecastRead
{
public:
  ForecastRead(void);
  void start(IPAddress serverIP, uint16_t port, forecastData *pfd, bool bCelcius);
  int checkStatus();
private:
  void _onConnect(AsyncClient* client);
  void _onDisconnect(AsyncClient* client);
  void _onData(AsyncClient* client, char* data, size_t len);
  int makeroom(uint32_t newTm);

  IPAddress m_serverIP;
  AsyncClient m_ac;
  forecastData *m_pfd;
#define FCBUF_SIZE 1200
  char *m_pBuffer = NULL;
  int m_bufIdx;
  bool m_bDone = false;
  bool m_bCelcius;
  int m_status;
};
#endif // FORECAST_H
