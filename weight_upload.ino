/*
 * weight_upload
 *
 * Author:   Makoto Uju (ainehanta), Hiromasa Ihara (taisyo)
 * Created:  2016-07-04
 */

#include <Ethernet.h>
#include <EthernetUdp.h>

#include <PFFIAPUploadAgent.h>
#include <TimeLib.h>
#include <SerialCLI.h>
#include <HX711.h>
#include <NTP.h>

#define CHANNEL0_SCALE_CALIB -430.6142
#define CHANNEL1_SCALE_CALIB -457.5805
#define CHANNEL2_SCALE_CALIB -449.0159
#define CHANNEL3_SCALE_CALIB -489.1717

#define CHANNEL0_SCALE_SDA 3
#define CHANNEL0_SCALE_SCK 2
#define CHANNEL1_SCALE_SDA 5
#define CHANNEL1_SCALE_SCK 4
#define CHANNEL2_SCALE_SDA 7
#define CHANNEL2_SCALE_SCK 6
#define CHANNEL3_SCALE_SDA 9
#define CHANNEL3_SCALE_SCK 8

#define SEND_SEC 60

//cli
SerialCLI commandline(Serial);
//  ethernet
MacEntry mac("MAC", "B0:12:66:01:02:89", "mac address");
//  ip
BoolEntry dhcp("DHCP", "true", "DHCP enable/disable");
IPAddressEntry ip("IP", "192.168.13.127", "IP address");
IPAddressEntry gw("GW", "192.168.13.1", "default gateway IP address");
IPAddressEntry sm("SM", "255.255.255.0", "subnet mask");
IPAddressEntry dns_server("DNS", "8.8.8.8", "dns server");
//  ntp
StringEntry ntp("NTP", "ntp.nict.jp", "ntp server");
//  fiap
StringEntry host("HOST", "202.15.110.21", "host of ieee1888 server end point");
IntegerEntry port("PORT", "80", "port of ieee1888 server end point");
StringEntry path("PATH", "/axis2/services/FIAPStorage", "path of ieee1888 server end point");
StringEntry prefix("PREFIX", "http://j.kisarazu.ac.jp/OpenLaboD/Frame/Weight/", "prefix of point id");
StringEntry timezone("TIMEZONE", "+09:00", "timezone");
//  debug
int debug = 1;

//ntp
NTPClient ntpclient;

//fiap
FIAPUploadAgent fiap_upload_agent;
char timezone_p[16] = "+09:00";
char channel0_weight_str[16];
char channel1_weight_str[16];
char channel2_weight_str[16];
char channel3_weight_str[16];
char total_weight_str[16];

struct fiap_element fiap_elements [] = {
  { "CHANNEL0", channel0_weight_str, 0, 0, 0, 0, 0, 0, timezone_p, },
  { "CHANNEL1", channel1_weight_str, 0, 0, 0, 0, 0, 0, timezone_p, },
  { "CHANNEL2", channel2_weight_str, 0, 0, 0, 0, 0, 0, timezone_p, },
  { "CHANNEL3", channel3_weight_str, 0, 0, 0, 0, 0, 0, timezone_p, },
  { "TOTAL", total_weight_str, 0, 0, 0, 0, 0, 0, timezone_p, },
};

//sensor
HX711 channel0_scale(CHANNEL0_SCALE_SDA, CHANNEL0_SCALE_SCK);
HX711 channel1_scale(CHANNEL1_SCALE_SDA, CHANNEL1_SCALE_SCK);
HX711 channel2_scale(CHANNEL2_SCALE_SDA, CHANNEL2_SCALE_SCK);
HX711 channel3_scale(CHANNEL3_SCALE_SDA, CHANNEL3_SCALE_SCK);

float weights[4];

void enable_debug()
{
  debug = 1;
}

void disable_debug()
{
  debug = 0;
}

void setup()
{
  pinMode(22, OUTPUT);
  pinMode(23, OUTPUT);
  digitalWrite(22, HIGH);
  
  int ret;
  commandline.add_entry(&mac);

  commandline.add_entry(&dhcp);
  commandline.add_entry(&ip);
  commandline.add_entry(&gw);
  commandline.add_entry(&sm);
  commandline.add_entry(&dns_server);

  commandline.add_entry(&ntp);

  commandline.add_entry(&host);
  commandline.add_entry(&port);
  commandline.add_entry(&path);
  commandline.add_entry(&prefix);

  commandline.add_command("debug", enable_debug);
  commandline.add_command("nodebug", disable_debug);

  commandline.begin(9600, "SerialCLI Sample");

  //ethernet & ip connection
  if(dhcp.get_val() == 1){
    ret = Ethernet.begin(mac.get_val());
    if(ret == 0) {
      restart("Failed to configure Ethernet using DHCP", 10);
    }
  }else{
    Ethernet.begin(mac.get_val(), ip.get_val(), dns_server.get_val(), gw.get_val(), sm.get_val()); 
  }

  // fetch time
  uint32_t unix_time = 0;
  ntpclient.begin();
  ret = ntpclient.getTime(ntp.get_val(), &unix_time);
  if(ret < 0){
    restart("Failed to configure time using NTP", 10);
  }
  setTime(unix_time + (9 * 60 * 60));

  // fiap
  fiap_upload_agent.begin(host.get_val().c_str(), path.get_val().c_str(), port.get_val(), prefix.get_val().c_str());

  // sensor
  channel0_scale.set_scale(CHANNEL0_SCALE_CALIB);
  channel1_scale.set_scale(CHANNEL1_SCALE_CALIB);
  channel2_scale.set_scale(CHANNEL2_SCALE_CALIB);
  channel3_scale.set_scale(CHANNEL3_SCALE_CALIB);
  
  digitalWrite(23, HIGH);
  
  Serial.println("Measurement will be starting at 1 min after.");
  Serial.println("Please remove all weights from scale.");
//  wait(1);

  delay(10000);
  channel0_scale.tare();
  channel1_scale.tare();
  channel2_scale.tare();
  channel3_scale.tare();
  delay(5000);
  channel0_scale.tare();
  channel1_scale.tare();
  channel2_scale.tare();
  channel3_scale.tare();
  
  digitalWrite(23, LOW);
}

void loop()
{
  static unsigned long old_epoch = 0, epoch;

  commandline.process();
  epoch = now();
  if(dhcp.get_val() == 1){
    Ethernet.maintain();
  }

  if(epoch != old_epoch){
    char buf[32];

    channel0_scale.power_up();
    channel1_scale.power_up();
    channel2_scale.power_up();
    channel3_scale.power_up();

    weights[0] = channel0_scale.get_units();
    weights[1] = channel1_scale.get_units();
    weights[2] = channel2_scale.get_units();
    weights[3] = channel3_scale.get_units();
    
    dtostrf(weights[0], 4, 1, channel0_weight_str);
    dtostrf(weights[1], 4, 1, channel1_weight_str);
    dtostrf(weights[2], 4, 1, channel2_weight_str);
    dtostrf(weights[3], 4, 1, channel3_weight_str);
    dtostrf(weights[0]+weights[1]+weights[2]+weights[3], 4, 1, total_weight_str);
    
    sprintf(buf, "channel0 weight = %s", channel0_weight_str);
    debug_msg(buf);
    sprintf(buf, "channel1 weight = %s", channel1_weight_str);
    debug_msg(buf);
    sprintf(buf, "channel2 weight = %s", channel2_weight_str);
    debug_msg(buf);
    sprintf(buf, "channel3 weight = %s", channel3_weight_str);
    debug_msg(buf);
    sprintf(buf, "total weight = %s", total_weight_str);
    debug_msg(buf);

    if(epoch % 60 == 0){
      debug_msg("uploading...");

      weights[0] = channel0_scale.get_units(5);
      weights[1] = channel1_scale.get_units(5);
      weights[2] = channel2_scale.get_units(5);
      weights[3] = channel3_scale.get_units(5);
      
      dtostrf(weights[0], 4, 1, channel0_weight_str);
      dtostrf(weights[1], 4, 1, channel1_weight_str);
      dtostrf(weights[2], 4, 1, channel2_weight_str);
      dtostrf(weights[3], 4, 1, channel3_weight_str);
      dtostrf(weights[0]+weights[1]+weights[2]+weights[3], 4, 1, total_weight_str);

      for(int i = 0; i < sizeof(fiap_elements)/sizeof(fiap_elements[0]); i++){
        fiap_elements[i].year = year();
        fiap_elements[i].month  = month();
        fiap_elements[i].day = day();
        fiap_elements[i].hour = hour();
        fiap_elements[i].minute = minute();
        fiap_elements[i].second = second();
      }
      int ret = fiap_upload_agent.post(fiap_elements, sizeof(fiap_elements)/sizeof(fiap_elements[0]));
      if(ret == 0){
        debug_msg("done");
      }else{
        debug_msg("failed");
        Serial.println(ret);
      }
    }
  }

  channel0_scale.power_down();
  channel1_scale.power_down();
  channel2_scale.power_down();
  channel3_scale.power_down();

  old_epoch = epoch;
}

void debug_msg(String msg)
{
  if(debug == 1){
    Serial.print("[");
    print_time();
    Serial.print("]");
    Serial.println(msg);
  }
}

void print_time()
{
  char print_time_buf[32];
  sprintf(print_time_buf, "%04d/%02d/%02d %02d:%02d:%02d",
      year(), month(), day(), hour(), minute(), second());
  Serial.print(print_time_buf);
}

void restart(String msg, int restart_minutes)
{
  Serial.println(msg);
  Serial.print("This system will restart after ");
  Serial.print(restart_minutes);
  Serial.print("minutes.");

  unsigned int start_ms = millis();
  while(1){
    commandline.process();
    if(millis() - start_ms > restart_minutes*60UL*1000UL){
      commandline.reboot();
    }
  }
}

void wait(int minutes) {
  Serial.println();
  Serial.print("Waiting");
  unsigned int start_ms = millis();
  unsigned int start_s  = now();
  while(1){
    commandline.process();
    if(millis() - start_ms > minutes*60UL*1000UL){
      Serial.println();
      break;
    }
  }
}

