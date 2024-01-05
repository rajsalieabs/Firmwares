
#include <ArduinoJson.h>
#include "RTClib.h"
#include "time.h"
#include <EEPROM.h>
#include <ModbusMaster.h>
#include <SoftwareSerial.h>
ModbusMaster node;
SoftwareSerial swSer;

#define MAX485_DE      27
#define red   14
#define green 18
#define blue  13



int Tot_Addr = 10;
int Modbus_state;
String Warning_status = "";

RTC_DS1307 rtc;
String timestamp = "";
String timeStamp_formatted = "";
String timemin = "";
String dateTime = "";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800;
const int   daylightOffset_sec = 0;



String  RTC_Time();
String TimeReturn();
String NtpLocalTime();
void preTransmission();
void postTransmission();
String modbus_read_Accumax(int slave_id);
float read_register(int addr);

unsigned long Eepromread(int len);
void EepromWrite(int address, unsigned long data);
String Json(String DevID, String totalizer, String ReadStatus, String recordTime);

String modbus_read_Electronet(int slave_id);
String Eepromread_String(int len);
void EepromWrite_String(int address, String data);
String modbus_read_Aum_Meter(int slave_id);
float read_register_aum(int addr);

void preTransmission()
{
  digitalWrite(MAX485_DE, 1);
}

void postTransmission()
{
  digitalWrite(MAX485_DE, 0);
}

void modbusEnable(int slave_id1)
{
  swSer.begin(9600, SWSERIAL_8N1, 4, 15);//4,15
  delay(1000);
  node.begin(slave_id1, swSer);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
  delay(50);
}
String modbus_read_Accumax(int slave_id)
{
  Serial.println("\n");
  float t1, t2;
  unsigned long prv_totalizer = Eepromread(Tot_Addr);
  String Total_Litre_Data = "";

  for (int i = 1; i <= 1; i++)
  {
    modbusEnable(slave_id);
    Serial.print("Read Accumax Meter....");

    t1 = read_register(2);
    t2 = read_register(4);

    if (Modbus_state < 1)
    {
      delay(500);
      swSer.end();
      digitalWrite(green, LOW);
      unsigned long totalizer = ((65535 * t2) + t1);
      //Serial.print("Totalizer value:"); Serial.println(totalizer);
      if (prv_totalizer <= totalizer)
      {
        Warning_status = "Modbus_read";
        EepromWrite(Tot_Addr, totalizer);
        Total_Litre_Data = String(totalizer);
        Serial.println("Done..");
        //Serial.print("\tCurrent Totalizer value:"); Serial.println(Total_Litre_Data);
      }
      else
      {
        Warning_status = "Modbus_Negative";
        Total_Litre_Data = String(prv_totalizer);
        Serial.println("Negative Data....");
        //Serial.print("\tPrevious Totalizer value:"); Serial.println(Total_Litre_Data);
      }
      break;
    }

    else
    {
      delay(500);
      swSer.end();
      Warning_status = "Modbus_Failed"; Modbus_state = 0;
      Total_Litre_Data = String(prv_totalizer);
      Serial.println("Failed..");
    }
  }
  return Total_Litre_Data;
}
float read_register(int addr)
{
  swSer.listen();
  delay(1000);
  uint16_t result; int32_t Buffer[2]; float temp_data;

  result = node.readHoldingRegisters(addr, 2);
  if (result == node.ku8MBSuccess)
  {
    digitalWrite(green, HIGH);
    Buffer[0] = node.getResponseBuffer(0);
    Buffer[1] = node.getResponseBuffer(1);
    unsigned long  Result = ((Buffer[0] & 0xFFFFUL) << 16) | ((Buffer[1]));
    temp_data = *(float*)&Result;
    //    Serial.print("Temp Data:  ");
    //    Serial.println(temp_data);
    delay(1000);

  }
  else
  {
    //Serial.println("Modbus Failed");
    Modbus_state++;
    temp_data = 0.0;
  }

  digitalWrite(green, LOW);
  return temp_data;
}


unsigned long Eepromread(int len)
{
  Serial.println("EepromRead");
  unsigned long data_temp = 0;
  data_temp = EEPROM.readULong(len);
  return data_temp;
}
void EepromWrite(int address, unsigned long data)
{
  //  Serial.println("EepromWrite");  delay(1000);
  EEPROM.writeULong(address, data);
  EEPROM.commit();
  delay(500);
}
String Json(String DevID, String totalizer, String ReadStatus, String recordTime)
{
  String  FullData = "";
  DynamicJsonDocument doc(2000);
  doc["DeviceID"] = DevID;
  doc["Totalizer"] = totalizer;
  doc["WarningStatus"] = ReadStatus;
  doc["DateTime"] = recordTime;
  serializeJson(doc, FullData);
  return FullData;

}
String modbus_read_Electronet(int slave_id)
{

  double t1 = 0, t2 = 0;
  String prv_totalizer_str = Eepromread_String(Tot_Addr);//Serial.print("EEPROM Data:");Serial.println(prv_totalizer);

  double prv_totalizer = prv_totalizer_str.toDouble();

  //Serial.print("EEPROM Data:"); Serial.println(prv_totalizer);
  String Total_Litre_Data = "";

  for (int i = 1; i <= 5; i++)
  {
    modbusEnable(slave_id);
    Serial.print("Read Electronet Meter....");

    t1 = read_register(2);
    delay(100);
    if (Modbus_state < 1)
    {
      delay(500);
      swSer.end();
      double totalizer =  t1;
      if (  totalizer >= prv_totalizer)
      {
        Serial.println("Done..");
        Total_Litre_Data = String(totalizer);
        EepromWrite_String(Tot_Addr, Total_Litre_Data);
        Warning_status = "Modbus_read";
      }
      else
      {
        Total_Litre_Data = String(prv_totalizer);
        Warning_status = "Modbus_Negative";  Serial.println("Negative Data....");
      }
      break;
    }
    else
    {
      delay(500);
      swSer.end();
      Warning_status = "Modbus_Failed"; Modbus_state = 0;
      Total_Litre_Data = String(prv_totalizer);
      Serial.println("Failed..");
    }
  }

  digitalWrite(green, LOW);
  return Total_Litre_Data;
}

String Eepromread_String(int len)
{
  Serial.println("EepromRead");
  String data_temp = "";
  data_temp = EEPROM.readString(len);
  return data_temp;
}
void EepromWrite_String(int address, String data)
{
  EEPROM.writeString(address, data);
  EEPROM.commit();
  delay(500);
}
String modbus_read_Aum_Meter(int slave_id)
{

  float totalizer = 0;
  String  prv_totalizer_str = Eepromread_String(Tot_Addr);

  String Total_Litre_Data = "";
  double prv_totalizer = prv_totalizer_str.toDouble();

  for (int i = 1; i <= 5; i++)
  {
    modbusEnable(slave_id);
    Serial.print("Read AUM Meter....");

    totalizer = read_register_aum(115);//Positive Totalizer
    delay(100);
    if (Modbus_state < 1)
    {
      delay(500);
      swSer.end();
      if (prv_totalizer <= totalizer)
      {
        Total_Litre_Data = String(totalizer);
        EepromWrite_String(Tot_Addr, Total_Litre_Data);
        Warning_status = "Modbus_read";  Serial.println("Done..");
      }
      else
      {
        Total_Litre_Data = String(prv_totalizer);
        Warning_status = "Modbus_Negative"; Serial.println("Negative Data....");

      }
      break;
    }
    else
    {
      swSer.end();
      delay(100);
      Modbus_state = 0;
      Total_Litre_Data = String(prv_totalizer);
      Warning_status = "Modbus_Failed"; Serial.println("Failed..");
    }
  }
  digitalWrite(green, LOW);
  return Total_Litre_Data;

}
float read_register_aum(int addr)
{
 
  swSer.listen();
  delay(100);
  uint8_t result;
  int32_t Buffer[2];
  result = node.readHoldingRegisters((addr - 1), 2);
  if (result == node.ku8MBSuccess)
  {
    digitalWrite(green, HIGH);
    Buffer[0] = node.getResponseBuffer(0);
    Buffer[1] = node.getResponseBuffer(1);
    unsigned long  Result = ((Buffer[1] & 0xFFFFUL) << 16) | ((Buffer[0]));
    float temp_data = *(float*)&Result;
//    Serial.print("    Temp Data:  ");
//    Serial.println(temp_data);
    delay(100);
    return temp_data;
  }
  else
  {
    Modbus_state++;
    //Serial.println("    Modbus Failed");
    return 0;
  }
 
}
String  RTC_Time()
{

  String Time = "", Date = "";

  DateTime now = rtc.now();
  delay(1000);
  Date = now.timestamp(DateTime::TIMESTAMP_DATE);
  String  Time1 = now.timestamp(DateTime::TIMESTAMP_TIME);
  Time = Date + " " + Time1;
  Serial.print("      RTC : ");  Serial.println(Time);
  Time = ""; Time = Date + "%20" + Time1;
  return Time;
  
}
String TimeReturn()
{
  DateTime now = rtc.now();
  delay(1000);
  String  Date1 = now.timestamp(DateTime::TIMESTAMP_DATE);
  String  Time1 = now.timestamp(DateTime::TIMESTAMP_TIME);
  String Time = Date1 + " " + Time1;
  return Time;
}

String NtpLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    timestamp = "0000-00-00 00:00:00.000";
    timeStamp_formatted = "00000000000000000";
    // ESP.restart();
    return timestamp;
  }
  char Hour[3];  strftime(Hour, 3, "%H", &timeinfo);
  char Mint[3];  strftime(Mint, 3, "%M", &timeinfo);
  char Sec[3]; strftime(Sec, 3, "%S", &timeinfo);
  char Year[5]; strftime(Year, 5, "%Y", &timeinfo);
  char Month[3]; strftime(Month, 3, "%m", &timeinfo);
  char Date[5];  strftime(Date, 3, "%d", &timeinfo);
  timemin = String(Mint);
  timestamp =  String(Year) + "-" + String(Month) + "-" + String(Date) + "%20" + String(Hour) + ":" + String(Mint) + ":" + String(Sec) ;
  timeStamp_formatted = String(Year) + "-" + String(Month) + "-" + String(Date) + "%20" + String(Hour) + ":" + String(Mint) + ":" + String(Sec) ;
  Serial.print("NTP : " + String(timestamp));
  return timeStamp_formatted;
}
