#include "eeMem.h"

#ifndef JSONSTRING
#define JSONSTRING

class jsonString
{
public:
  jsonString(const char *pLabel = NULL, int rsv = 0)
  {
    m_cnt = 0;
    if(rsv) s.reserve(rsv);
    s = String("{");
    if(pLabel)
    {
      s += "\"cmd\":\"";
      s += pLabel, s += "\",";
    }
  }
        
  String Close(void)
  {
    s += "}";
    return s;
  }

  template <typename T>
  void Var(const char *key, T iVal)
  {
    if(m_cnt) s += ",";
    s += "\"";
    s += key;
    s += "\":";
    s += iVal;
    m_cnt++;
  }
  
  void Var(const char *key, const char *sVal)
  {
    if(m_cnt) s += ",";
    s += "\"";
    s += key;
    s += "\":\"";
    s += sVal;
    s += "\"";
    m_cnt++;
  }
  
  void Var(const char *key, String sVal)
  {
    if(m_cnt) s += ",";
    s += "\"";
    s += key;
    s += "\":\"";
    s += sVal;
    s += "\"";
    m_cnt++;
  }

  template <typename T>
  void Array(const char *key, T iVal[], int n)
  {
    if(m_cnt) s += ",";
    s += "\"";
    s += key;
    s += "\":[";
    for(int i = 0; i < n; i++)
    {
      if(i) s += ",";
      s += iVal[i];
    }
    s += "]";
    m_cnt++;
  }

  template <typename T>
  void Array3(const char *key, T iVal[][3], int n)
  {
    if(m_cnt) s += ",";
    s += "\"";
    s += key;
    s += "\":[";
    for(int i = 0; i < n; i++)
    {
      if(i) s += ",";
      s += "[";
      s += iVal[i][0];
      s += ",";
      s += iVal[i][1];
      s += ",";
      s += iVal[i][2];
      s += "]";
    }
    s += "]";
    m_cnt++;
  }

  void ArrayCost(const char *key, uint16_t iVal[], int n)
  {
    if(m_cnt) s += ",";
    s += "\"";
    s += key;
    s += "\":[";
    for(int i = 0; i < n; i++)
    {
      if(i) s += ",";
      s += (float)iVal[i]/100;
    }
    s += "]";
    m_cnt++;
  }

  void Array(const char *key, Sensor sns[])
  {
    if(m_cnt) s += ",";
    s += "\"";
    s += key;
    s += "\":[";
    bool bSend = false;
    for(int i = 0; i < SNS_CNT; i++)
    {
      if(sns[i].IP[3])
      {
        if(bSend) s += ",";
        bSend = true;
        s += "[";
        IPAddress ip(sns[i].IP);
        s += "\"";
        s += ip.toString();
        s += "\"";
        s += ",";
        s += sns[i].temp;
        s += ",";
        s += sns[i].rh;
        s += ",";
        s += sns[i].f.f.Weight;
        s += ",\"";
        s += sns[i].szName;
        s += "\"]";
      }
    }
    s += "]";
    m_cnt++;
  }

protected:
  String s;
  int m_cnt;
};
#endif JSONSTRING
