/*
 * weight_upload
 *
 * Author:   Makoto Uju (ainehanta), Hiromasa Ihara (taisyo)
 * Created:  2016-07-04
 */

#include <Ethernet.h>
#include <EthernetUdp.h>

#include <PFFIAPUploadAgent.h>
#include <Time.h>
#include <SerialCLI.h>
#include <HX711.h>
#include <NTP.h>
#include <Wire.h>

//cli
SerialCLI commandline(Serial);
//  ethernet
MacEntry mac("MAC", "12:34:56:78:9A:BC", "mac address");
//  ip
BoolEntry dhcp("DHCP", "true", "DHCP enable/disable");
IPAddressEntry ip("IP", "192.168.0.2", "IP address");
IPAddressEntry gw("GW", "255.255.255.0", "default gateway IP address");
IPAddressEntry sm("SM", "192.168.0.1", "subnet mask");
IPAddressEntry dns_server("DNS", "8.8.8.8", "dns server");
//  ntp
StringEntry ntp("NTP", "ntp.nict.jp", "ntp server");
//  fiap
StringEntry host("HOST", "fiap-dev.gutp.ic.i.u-tokyo.ac.jp", "host of ieee1888 server end point");
IntegerEntry port("PORT", "80", "port of ieee1888 server end point");
StringEntry path("PATH", "/axis2/services/FIAPStorage", "path of ieee1888 server end point");
StringEntry prefix("PREFIX", "http://j.kisarazu.ac.jp/OpenLaboD/Chanba/Weight", "prefix of point id");
StringEntry timezone("TIMEZONE", "+09:00", "timezone");
//  debug
int debug = 0;

//ntp
NTPClient ntpclient;

//fiap
FIAPUploadAgent fiap_upload_agent;
char *timezone_p;
char weight_bottom1_str[16];
char weight_bottom2_str[16];
char weight_top_str[16];
struct fiap_element fiap_elements [] = {
  { "Bottom1", weight_bottom1_str, 0, 0, 0, 0, 0, 0, timezone_p, },
  { "Bottom2", weight_bottom2_str, 0, 0, 0, 0, 0, 0, timezone_p, },
  { "Top", weight_top_str, 0, 0, 0, 0, 0, 0, timezone_p, },
};

//sensor
HX711 scale_bottom1(A1, A0);
HX711 scale_bottom2(A3, A2);
HX711 scale_top(A5, A4);

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

  // ethernet & ip connection
  if(dhcp.get_val() == 1){
    ret = Ethernet.begin(mac.get_val());
    if(ret == 0) {
      restart("Failed to configure Ethernet using DHCP", 10);
    }
  }else{
    Ethernet.begin(mac.get_val(), ip.get_val(), dns_server.get_val(), gw.get_val(), sm.get_val()); 
  }

  // fetch time
  uint32_t unix_time;
  ntpclient.begin();
  ret = ntpclient.getTime(ntp.get_val(), &unix_time);
  if(ret < 0){
    restart("Failed to configure time using NTP", 10);
  }
  setTime(unix_time + (9 * 60 * 60));

  // fiap
  fiap_upload_agent.begin(host.get_val(), path.get_val(), port.get_val(), prefix.get_val());

  // sensor
  scale_bottom1.set_scale(2240);
  scale_bottom2.set_scale(2240);
  scale_top.set_scale(2240);

  Serial.println("Measurement will be starting at 2 min after.");
  Serial.println("Please remove all weights from scale.");
  wait(2);

  scale_bottom1.tare();
  scale_bottom2.tare();
  scale_top.tare();
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
    sprintf(buf, "bottom1 weight = %f", scale_bottom1.get_units(5));
    debug_msg(buf);
    sprintf(buf, "bottom2 weight = %f", scale_bottom2.get_units(5));
    debug_msg(buf);
    sprintf(buf, "top weight = %f", scale_top.get_units(5));
    debug_msg(buf);

    if(epoch % 60 == 0){
      debug_msg("uploading...");
      sprintf(weight_bottom1_str, "%f", scale_bottom1.get_units(5));
      sprintf(weight_bottom2_str, "%f", scale_bottom2.get_units(5));
      sprintf(weight_top_str, "%f", scale_top.get_units(5));
      timezone_p = (char*)timezone.get_val().c_str();

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
      }
      Serial.println(ret);
    }
  }

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
  unsigned int start_ms = millis();
  while(1){
    commandline.process();
    if(millis() - start_ms > minutes*60UL*1000UL){
      break;
    }
  }
}

