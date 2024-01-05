
#include <PPPoS.h>
#include <WiFi.h>
#include <Update.h>
#include <PubSubClient.h>
#include <HTTPClient.h>

#include "SPIFFS.h"
#include "esp_system.h"
#include <String.h>
#include "Rtc_PPP.h"


WiFiClient espClient;
PubSubClient client(espClient);
PPPoS ppp;
HTTPClient http;


#define M66_EN 26

#define MODEM_TX             17
#define MODEM_RX             16

#define PPP_APN   "www"
#define PPP_USER  ""
#define PPP_PASS  ""


String devID = "1002324022";//1002021163
String bin = "/kauvery/gateway_022.bin";
int slave_id = 1;
String Meter_Make = "AUM"; //ELECTRONET  ACCUMAX AUM


String final_unit_litre = "0.0";

char* mqtt_server = "iot.salieabs.in";
String Sub_Topic = "inData/" + devID ;
long contentLength = 0;
bool isValidContentType = false;

String host = "iot.salieabs.in";
int port = 80;


unsigned long currentTime = 0, tmr = 0; // Millis

int wdtTimeout = 120000;  //time in ms to trigger the watchdog
hw_timer_t *timer = NULL;

void IRAM_ATTR resetModule()
{
  ets_printf("reboot\n");
  esp_restart();
}

int Http_state = 1;
int FailCount = 0;
int TransmittedCount = 0;
int PingInterval = 0, PingAddr = 30;
int SignalStrength = 0;
int Rtc_state = 0;

void setup()
{
  Serial.begin(115200);
  Serial.println("    Started");
  initFuntions();
  Init_WDT();
  RTC_Init();
  EepromWrite(10, 0); EepromWrite(20, 0);// ADDRESS , data
  readDeviceInfo();
  RTC_Time();
  readFailed_Count();
  PPP();
  Serial.println("\n");

}

void loop()
{
  timerWrite(timer, 0);
  DateTimeSelection();
  TimeIntervalFn();
  subscribeFn();
  Serial.println("****************************\n");
}
void TimeIntervalFn()
{
  if (tmr == 0 || currentTime - tmr >= PingInterval * 60000) //900000
  {
    tmr = millis();
    // RTC_Time();
    Meter_Selection();
    DataTransmission();
    if (FailCount > 10)
    {
      ESP.restart();
    }
  }
  currentTime = millis();
}
void DateTimeSelection()
{
  if (Rtc_state == 1)
  {
    NtpLocalTime(); dateTime = RTC_Time();
  }
  else
  {
    dateTime = NtpLocalTime();
  }
}
void Meter_Selection()
{
  if (Meter_Make == "ACCUMAX")
  {
    readTotailzer_Accumax();
  }
  else if (Meter_Make == "AUM")
  {
    final_unit_litre =  modbus_read_Aum_Meter(slave_id);
    Serial.println("    Final_unit_litre:" + String(final_unit_litre));
    Serial.println("\n");
  }
  else
  {
    final_unit_litre =  modbus_read_Electronet(slave_id);
    Serial.println("    Final_unit_litre:" + String(final_unit_litre));
    Serial.println("\n");
  }
}
void  readTotailzer_Accumax()
{

  String Litre = modbus_read_Accumax(slave_id);
  char value1[20]; Litre.toCharArray(value1, 20); double data_temp = atol(value1);
  final_unit_litre = String((data_temp / 1000));
  Serial.println("    Final_unit_litre:" + String(final_unit_litre));
  Serial.println("\n");

}
void DataTransmission()
{
  digitalWrite(blue, HIGH);
  int Ack = HTTP_Transmit(final_unit_litre, Warning_status, "0");
  if (Ack == 0)
  {
    Serial.print("***  Re");
    Ack =  HTTP_Transmit(final_unit_litre, Warning_status, "0");

    if (Ack == 0)
    {
      String Failed_Data = Json(devID, final_unit_litre, Warning_status, dateTime); FailCount++;
      Serial.println("   Entering into Back Logging..." + String(FailCount));
      writeFile_Data(SPIFFS, (Failed_Data + "\n").c_str());
    }
  }
  else
  {
    BackLog_Retrive();
  }
  digitalWrite(blue, LOW);
}
void BackLog_Retrive()
{
  readFailed_Count();
  if (FailCount > 0)
  {
    Read_FailedData();
  }
  else
  {

  }
}
void Read_FailedData()
{
  Serial.println("\n");
  int RestartedCount = TransmittedCount;
  if (FailCount > 0)
  {
    Serial.println("      Having BackLogs In Memory");
    File file = SPIFFS.open("/BackLog.txt", FILE_READ);
    //     if (file.available() > 0)
    //  {
    //debugLogData =
    for (int i = 1; i <= FailCount; i++)
    {
      timerWrite(timer, 0);
      String ReceivedData =   file.readStringUntil('\n');
      Serial.print(String(i) + "   ");
      if (i <= RestartedCount)
      {
        Serial.println("  This Data is Already Sent ");
        //        DataSegmentation(ReceivedData);
        //        Serial.println("\n");
      }
      else
      {
        TransmittedCount++;
        DataSegmentation(ReceivedData);
        EEPROM.writeInt(20, TransmittedCount); EEPROM.commit();
        Serial.println("\n");
      }
    }

    Serial.println("  Going to Delete Failed Data");
    DeleteFile();
    FailCount = 0;
    TransmittedCount = 0;
    EEPROM.writeInt(20, TransmittedCount); EEPROM.commit();
  }
  else
  {

  }
}
void DataSegmentation(String DataToParse)
{
  DynamicJsonDocument doc1(2000);
  deserializeJson(doc1, DataToParse);
  if (DataToParse.length() > 20)//Should Have A Json Data
  {
    String F_Totalizer = doc1["Totalizer"]; String F_warnStatus = doc1["WarningStatus"]; String F_dTime = doc1["DateTime"];
    Serial.println("    Totalizer : " + F_Totalizer + "     Time : " + F_dTime);
    int F_Ack = HTTP_Transmit(F_Totalizer, F_warnStatus, F_dTime);
    if (F_Ack == 0)
    {
      Serial.print("***  Re");
      F_Ack =  HTTP_Transmit(F_Totalizer, F_warnStatus, F_dTime);
    }

    if (F_Ack == 1)
    {
      Serial.println("  ( Failed Data Sent Done )");
      //TransmitCount++;
    }
  }

}
void subscribeFn()
{
  Serial.print("  SubScribe Topic:"); Serial.println(Sub_Topic);
  if (!client.connected())
  {
    reconnect();
  }
  for (int i = 0; i <= 100; i++)
  {
    client.loop();
    delay(10);
  }
}

int HTTP_Transmit(String Totalizer, String WarningStatus, String Dt)
{
  digitalWrite(blue, HIGH);
  String Jsn = "";

  if ((Dt.length()) > 5)
  {
    Jsn = "/api/postDeviceData?dev=" + devID + "&mr=" + Totalizer + "&warn=" + WarningStatus + "&dt=" + Dt;
  }
  else
  {
    Jsn = "/api/postDeviceData?dev=" + devID + "&mr=" + Totalizer + "&warn=" + WarningStatus;
  }

  WiFiClient client;
  Serial.print("Data Transmitting to Server.....");
  String httpRequestData = "";

  http.begin(client, "iot.salieabs.in", 5003, Jsn , false);
  delay(100);
  int httpResponseCode ;
  if (Http_state == 1)//Only For Testing
  {
    httpResponseCode = http.POST(httpRequestData);
  }
  else
  {
    httpResponseCode = http.GET();

  }

  if (httpResponseCode == 200)
  {
    Serial.println(" Done.. Sent... Success");
    http.end();
    digitalWrite(blue, LOW);
    return 1;
  }
  else
  {
    Serial.println("Status :" + String(httpResponseCode) + "...Failed");
    http.end();
    digitalWrite(blue, LOW);
    return 0;

  }
}


void reconnect()
{
  int state = 0;
  client.setCallback(callback);
  while (!client.connected())
  {
    //Serial.println(mqttUsername); Serial.println(mqttPassword);
    Serial.print("Attempting MQTT connection...");
    if (client.connect(devID.c_str()))
    {
      Serial.println("connected");
      client.subscribe(Sub_Topic.c_str());
    }
    else
    {
      state++;
      Serial.print(state);
      Serial.print(" time failed,");
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      client.disconnect();
      delay(2000);
    }

    if (state >= 3)
    {
      client.disconnect();
      delay(2000);
      digitalWrite(26, LOW);
      delay(2000);
      ESP.restart();
    }
  }

}
void writeFile_Data(fs::FS &fs, const char * message)
{
  //Serial.printf("Appending to file: %s\r\n", path);

  File file = fs.open("/BackLog.txt", FILE_APPEND);
  if (!file) {
    Serial.println("- failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("    - message appended");
  } else {
    Serial.println("- append failed");
  }
  file.close();
}
int DeleteFile()
{
  String  FileName = "/BackLog.txt";
  if (SPIFFS.remove(FileName))
  {
    Serial.println(" File Deleted");
    return 1;
    //Serial.println("\n");
  }
  else
  {
    Serial.println("Delete Failed");
    return 0;
  }
}

void readFailed_Count()
{
  // Serial.print("readFile");
  File file = SPIFFS.open("/BackLog.txt", FILE_READ);
  Serial.print(" Read File Size :  "); Serial.println(String(file.size()));
  String debugLogData = "";
  FailCount = 0;
  int i = 1;
  if (file.available() > 0)
  {
    debugLogData = file.readStringUntil('\n');
    while (debugLogData.length() > 5)
    {
      timerWrite(timer, 0);
      FailCount++;
      debugLogData = file.readStringUntil('\n');
      Serial.println(String(i) + " :" + debugLogData); i++;
      delay(20);
    }
    FailCount = FailCount - 1;
    Serial.println("    Number of Data  : " + String(FailCount));
  }
  else
  {
    Serial.println("  No BackLog Data in Memory");
  }


  delay(250);
  file.close();
}
void mqttConnect()
{

  if (!client.connected())
  {
    reconnect();
  }
  delay(1000);
}
void Init_Mqtt()
{
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

}
void callback(char* topic, byte * payload, unsigned int length)
{
  String messageTemp = "";
  Serial.println("\n");
  Serial.print("Message arrived on topic: ");
  Serial.println(topic);
  for (int i = 0; i < length; i++)
  {
    messageTemp += (char)payload[i];
  }
  Serial.print("Ota Message: "); Serial.println(messageTemp);
  delay(1000);
  String  OTA_Info = messageTemp;
  int Index = OTA_Info.indexOf('_');
  if (OTA_Info == "GOOTA")
  {
    OTA_Info = "";
    messageTemp = "";
    mqttConnect();
    String msg = "OTA Mode Enterted From ID:" + devID;
    int a = client.publish(Sub_Topic.c_str(), msg.c_str(), true);
    delay(1000);
    client.publish(Sub_Topic.c_str(), "", true);
    delay(1000);
    wdtTimeout = 360000;
    Init_WDT();
    timerWrite(timer, 0);
    execOTA();
  }
  else  if (messageTemp.length() > 140)
  {
    //                  Json Format  {
    //                "date": "9",
    //                "month": "10",
    //                "year": "2022",
    //                "hr": "13",
    //                "min": "10",
    //                "sec": "0"
    //                    }
    DynamicJsonDocument doc1(200);
    deserializeJson(doc1, messageTemp);
    int Date = doc1["date"]; int Month = doc1["month"]; int Year = doc1["year"];
    int Hr = doc1["hr"]; int Min = doc1["min"]; int Sec = doc1["sec"];
    Serial.println(String(Year) + "-" + String(Month) + "-" + String(Date) + " " + String(Hr) + "-" + String(Min) + "-" + String(Sec));

    rtc.adjust(DateTime(Year, Month, Date, Hr, Min, Sec)); // Y M D H M S
    delay(1000);
    String MqttTime = TimeReturn();
    Serial.print("Reset MQTT Time RTC Current Time:");  Serial.println(MqttTime);
    mqttConnect();
    String msg = "MQTT RTC From ID:" + devID + " Time: " + MqttTime;
    Serial.println("Publish Topic: " + Sub_Topic);
    msg_transmission(msg);
  }
  else if (OTA_Info == "RTC_TIME")
  {
    mqttConnect();
    String MqttTime = TimeReturn();
    String msg = "RTC Time From:" + devID + " Time :" + String(MqttTime);
    Serial.println(msg);
    msg_transmission(msg);
  }
  else if (OTA_Info == "RESTART")
  {
    mqttConnect();
    String msg = "Restarted From ID:" + devID ;//+ " Count :" + String(restart_count);

    msg_transmission(msg);
    delay(1000);

    digitalWrite(M66_EN, LOW); digitalWrite(green, LOW); digitalWrite(blue, LOW);
    ESP.restart();
  }
  else if (OTA_Info == "DELETE")
  {
    //SPIFFS.format();
    int Status = DeleteFile();
    Serial.println("BackLog Files in Memory is Deleted");
    String msg = "Delete Command From ID:" + String(Status) ;//+ " Count :" + String(restart_count);
    msg_transmission(msg);
    FailCount = 0;
  }
  else if (OTA_Info == "HTTP")
  {
    Http_state = !Http_state;
    Serial.println("State : " + String(Http_state));
    String msg = "HTTP State:" + String(Http_state) ;//+ " Count :" + String(restart_count);
    msg_transmission(msg);
  }
  else if (OTA_Info == "FAIL_COUNT")
  {
    mqttConnect();
    String msg = "Fail Data Count :" + String(FailCount) ;//+ " Count :" + String(restart_count);
    msg_transmission(msg);
  }
  else if (OTA_Info == "GET_DATA")
  {
    mqttConnect();
    String Json_Data = Json(devID, final_unit_litre, Warning_status, dateTime);
    String msg = String(Json_Data) ;
    msg_transmission(msg);
  }
  else if (OTA_Info == "CLEAR")
  {
    mqttConnect();
    EepromWrite(10, 0); EepromWrite(20, 0);// ADDRESS , data for Ping Interval and TransmittedCount
    String msg = "Eeprom Clear Done ID:" + String(devID) ;//+ " Count :" + String(restart_count);
    msg_transmission(msg);
  }
  else if (OTA_Info == "BACK_LOG")
  {
    mqttConnect();
    String msg = "Enterted BackLogFn :" + String(devID) ;//+ " Count :" + String(restart_count);
    msg_transmission(msg);
    BackLog_Retrive();
  }
  else if (Index == 4)//PING_5
  {
    mqttConnect();
    String msg = "Done PingInterval :" + String(devID) ;//+ " Count :" + String(restart_count);
    msg_transmission(msg);
    int TimeInvl = (OTA_Info.substring(5, OTA_Info.length()).toInt());
    PingInterval = TimeInvl;
    Serial.println("Ping Interval is : " + String (TimeInvl));
    EEPROM.writeInt(PingAddr, TimeInvl); EEPROM.commit();
    //  BackLog_Retrive();

  }
  else if (OTA_Info == "GET_PING")
  {
    mqttConnect();
    String msg = "PingInterval :" + String(PingInterval) ;//+ " Count :" + String(restart_count);
    msg_transmission(msg);
  }
  else if (OTA_Info == "DO_HTTP")
  {
    mqttConnect();
    Serial.println("Start Transmit a Last Data");
    int Ack = HTTP_Transmit(final_unit_litre, Warning_status, "0");
    String msg = "Done HTTP  :" + String(Ack) ;//+ " Count :" + String(restart_count);
    msg_transmission(msg);
  }

  else if (OTA_Info == "SIGNAL")
  {
    mqttConnect();// Get SignalStrength
    String msg = "SignalStrength :" + String(SignalStrength) ;//+ " Count :" + String(restart_count);
    msg_transmission(msg);

  }
  else if (OTA_Info == "OTA_NAME")
  {
    mqttConnect();
    String msg = "OTA_Filename:" + String(bin) ;//+ " Count :" + String(restart_count);
    msg_transmission(msg);
  }
  else if (OTA_Info == "NTP_TIME")
  {
    mqttConnect();
    String msg = "NTP_TIME:" + String(timeStamp_formatted) ;//+ " Count :" + String(restart_count);
    msg_transmission(msg);
  }
  else if (OTA_Info == "RTC_STATE")//RTC init done or not
  {
    Serial.println("State : " + String(Rtc_state));
    String msg = "RTC State:" + String(Rtc_state) ;//+ " Count :" + String(restart_count);
    msg_transmission(msg);
  }
  else if (OTA_Info == "RTC_TOGGLE")//RTC init done or not
  {
    Rtc_state = !Rtc_state;
    Serial.println("State : " + String(Rtc_state));
    String msg = "RTC Toggle:" + String(Rtc_state) ;//+ " Count :" + String(restart_count);
    msg_transmission(msg);
  }
  else
  {
    Serial.println("Command Received From Client But Nothing To Do");
  }
}
void msg_transmission(String Info)
{
  Serial.println("        " + Info);
  int States =   client.publish(Sub_Topic.c_str(), Info.c_str());
  if (States == 0)
  {
    client.publish(Sub_Topic.c_str(), Info.c_str());
  }
  else
  {
    Serial.println("  Reply Sent");
  }
}
void RTC_Init()
{
  delay(100);
  if (! rtc.begin())
  {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    Rtc_state = 0;
    //  abort();
  }
  else
  {
    Rtc_state = 1;
    Serial.println("RTC Init Done");
  }
}

void MQTT_Initial(String Data)
{
  mqttConnect();
  if (!client.connected())
  {
    reconnect();
  }
  String msg = Data + devID;
  Serial.println(" Data Publish Info Success");//BackLog.txt
  client.publish(Sub_Topic.c_str(), msg.c_str(), true);
  delay(1000);
  client.publish(Sub_Topic.c_str(), "", true);
  delay(1000);

}
void initFuntions()
{
  delay(250);
  EEPROM.begin(512); delay(500);
  pinMode(red, OUTPUT);
  pinMode(green, OUTPUT);
  pinMode(blue, OUTPUT);
  pinMode(M66_EN, OUTPUT);  digitalWrite(red, HIGH); delay(1000);
  digitalWrite(blue, LOW);
  digitalWrite(green, LOW);
  pinMode(MAX485_DE, OUTPUT);
  delay(1000);
  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
  }
  delay(1000);
  TransmittedCount = EEPROM.readInt(20);
  PingInterval = EEPROM.readInt(PingAddr);
}

void Init_WDT()
{

  timer = timerBegin(0, 80, true);                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true);  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000, false); //set time in us
  timerAlarmEnable(timer);
  Serial.println("Init WDT Done");
}
void readDeviceInfo()
{
  Serial.println("*******************My Configurations*********************");
  Serial.print("            My Device ID    :  "); Serial.println(devID);
  Serial.print("            MQTT Server     :  "); Serial.println(mqtt_server);
  Serial.print("            Subscribe Topic :  "); Serial.println(Sub_Topic);
  Serial.print("            Bin File name   :  "); Serial.println(bin);
  Serial.print("            Slave ID        :  "); Serial.println(slave_id);
  Serial.print("            TransmittedCount:  "); Serial.println(TransmittedCount);
  Serial.print("            Ping Interval   :  "); Serial.println(PingInterval);
  Serial.println("********************************************************");
  delay(100);
}


void PPP()
{
  Serial.println("Start Connecting to Internet!");
  timer = timerBegin(0, 80, true);                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true);  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000, false); //set time in us
  timerAlarmEnable(timer);
  // pinMode(26, OUTPUT);
  digitalWrite(M66_EN, LOW);
  delay(2000);
  digitalWrite(M66_EN, HIGH);
  delay(10000);
  Serial2.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  Serial2.setTimeout(10);
  Serial2.setRxBufferSize(2048);
  Serial2.println("AT");
  delay(1000);
  Serial.println(Serial2.readString());
  Serial2.println("AT+CPIN?");
  delay(1000);
  Serial.println(Serial2.readString());
  parseCmd1("AT+CSQ");
  delay(1000);
  Serial.println(Serial2.readString());
  Serial2.println("ATE0");
  delay(1000);
  Serial.println(Serial2.readString());
  ppp.begin(&Serial2);
  Serial.print("Connecting PPPoS");
  ppp.connect(PPP_APN, PPP_USER, PPP_PASS);
  int count = 0;
  while (!ppp.status())
  {
    delay(500);
    Serial.print(".");
    count++;
    if (count > 40)
    {
      ppp.end();
      delay(500);
      ppp.end();
      delay(500);
      ppp.end();
      delay(500);
      ppp.end();
      digitalWrite(M66_EN, LOW);
      ESP.restart();
    }
  }
  Serial.println("OK");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  NtpLocalTime();
  Serial.println("\n");
  Init_Mqtt();
  mqttConnect();

}

void execOTA()
{
  timerWrite(timer, 0);
  Serial.println("Connecting to: " + String(host));
  // Connect to S3
  if (espClient.connect(host.c_str(), port)) {
    // Connection Succeed.
    // Fecthing the bin
    Serial.println("Fetching Bin: " + String(bin));

    // Get the contents of the bin file
    espClient.print(String("GET ") + bin + " HTTP/1.1\r\n" +
                    "Host: " + host + "\r\n" +
                    "Cache-Control: no-cache\r\n" +
                    "Connection: close\r\n\r\n");

    unsigned long timeout = millis();
    while (espClient.available() == 0) {
      if (millis() - timeout > 5000) {
        Serial.println("Client Timeout !");
        espClient.stop();
        MQTT_Initial("OTA Fail Timeout From ID: " );
        return;
      }
    }
    // Once the response is available,
    // check stuff
    while (espClient.available()) {
      // read line till /n
      String line = espClient.readStringUntil('\n');
      // remove space, to check if the line is end of headers
      line.trim();
      if (!line.length()) {
        //headers ended
        break; // and get the OTA started
      }

      // Check if the HTTP Response is 200
      // else break and Exit Update
      if (line.startsWith("HTTP/1.1")) {
        if (line.indexOf("200") < 0) {
          Serial.println("Got a non 200 status code from server. Exiting OTA Update.");
          break;
        }
      }

      // extract headers here
      // Start with content length
      if (line.startsWith("Content-Length: ")) {
        contentLength = atol((getHeaderValue(line, "Content-Length: ")).c_str());
        Serial.println("Got " + String(contentLength) + " bytes from server");
      }

      // Next, the content type
      if (line.startsWith("Content-Type: ")) {
        String contentType = getHeaderValue(line, "Content-Type: ");
        Serial.println("Got " + contentType + " payload.");
        if (contentType == "application/octet-stream") {
          isValidContentType = true;
        }
      }
    }
  } else {
    // Connect to S3 failed
    // May be try?
    // Probably a choppy network?
    Serial.println("Connection to " + String(host) + " failed. Please check your setup");
    // retry??
    // execOTA();
  }

  // Check what is the contentLength and if content type is `application/octet-stream`
  Serial.println("contentLength : " + String(contentLength) + ", isValidContentType : " + String(isValidContentType));

  // check contentLength and content type
  if (contentLength && isValidContentType) {
    // Check if there is enough to OTA Update
    bool canBegin = Update.begin(contentLength);

    // If yes, begin
    if (canBegin) {
      Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");
      // No activity would appear on the Serial monitor
      // So be patient. This may take 2 - 5mins to complete
      size_t written = Update.writeStream(espClient);

      if (written == contentLength) {
        Serial.println("Written : " + String(written) + " successfully");
      } else {
        Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?" );
        // retry??
        // execOTA();
      }

      if (Update.end()) {
        Serial.println("OTA done!");
        if (Update.isFinished())
        {
          Serial.println("Update successfully completed. Rebooting.");
          Serial.println("");

          MQTT_Initial("OTA Done From ID: " );
          delay(2000);
          digitalWrite(blue, LOW);
          ESP.restart();
        } else {
          Serial.println("Update not finished? Something went wrong!");
        }
      } else {
        Serial.println("Error Occurred. Error #: " + String(Update.getError()));
      }
    } else {
      // not enough space to begin OTA
      // Understand the partitions and
      // space availability
      Serial.println("Not enough space to begin OTA");
      MQTT_Initial("OTA Fail No space From ID: " );
    }
  }
  else
  {
    Serial.println("There was no content in the response");
    espClient.stop();
    MQTT_Initial("OTA Fail by No Content From ID: " );
  }
  Serial.println("Exit From OTA");
}


void parseCmd(String command, int dly)
{
  Serial2.println(command);
  delay(dly);
  while (Serial2.available() > 0)
  {
    Serial.print(char(Serial2.read()));
  }
}

String getHeaderValue(String header, String headerName)
{
  return header.substring(strlen(headerName.c_str()));
}
void parseCmd1(String command)
{
  String back = "";
  Serial2.println(command);
  delay(2000);
  while (Serial2.available() > 0)
  {
    back = back + char(Serial2.read());
  }

  Serial.println( back);
  //  Serial.println(back.length());
  //  delay(250);
  if (back.length() >= 31   )
  {
    // DataSegment_Simcard(back);
  }
  else if (back.length() >= 20)
  {
    DataSegment_CSQ(back);
  }

}
void DataSegment_CSQ(String Data)
{
  // Serial.println("Received Data: " + Data);


  //Serial.println(Data.substring(15, 17));
  int Index = Data.indexOf(':');
  //
  String Parse = Data.substring(Index, Index + 1);
  //  int SignalStrength ;//= (Data.substring(15, 17)).toInt();
  // Serial.println(Parse);
  if (Parse.indexOf(',') == ',')
  {
    Serial.println("Wrong Data");
    SignalStrength = 17;
  }
  else
  {
    SignalStrength = (Data.substring(15, 17)).toInt();

  }
  Serial.println("SignalStrength: " + String(SignalStrength));
  //  AddRegsiterValue(24, SignalStrength);// Address Value
}
