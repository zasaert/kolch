/*
ESP32-CAM 2-Axis servo pan tilt controlled by using Joystick
Author : ChungYi Fu (Kaohsiung, Taiwan)  2021-7-3 22:00
https://www.facebook.com/francefu

Servo1(水平旋轉) -> gpio2 (伺服馬達與ESP32-CAM共地外接電源)
Servo2(垂直旋轉) -> gpio13 (伺服馬達與ESP32-CAM共地外接電源)

http://192.168.xxx.xxx             //網頁首頁管理介面
http://192.168.xxx.xxx:81/stream   //取得串流影像       網頁語法 <img src="http://192.168.xxx.xxx:81/stream">
http://192.168.xxx.xxx/capture     //取得影像           網頁語法 <img src="http://192.168.xxx.xxx/capture">
http://192.168.xxx.xxx/status      //取得影像狀態值

//自訂指令格式  http://192.168.xxx.xxx/control?cmd=P1;P2;P3;P4;P5;P6;P7;P8;P9
http://192.168.xxx.xxx/control?ip                        //IP
http://192.168.xxx.xxx/control?mac                       //MAC
http://192.168.xxx.xxx/control?restart                   //重啟電源
http://192.168.xxx.xxx/control?digitalwrite=pin;value    //數位輸出
http://192.168.xxx.xxx/control?analogwrite=pin;value     //類比輸出
http://192.168.xxx.xxx/control?digitalread=pin           //數位讀取
http://192.168.xxx.xxx/control?analogread=pin            //類比讀取
http://192.168.xxx.xxx/control?touchread=pin             //觸碰讀取
http://192.168.xxx.xxx/control?resetwifi=ssid;password   //重設Wi-Fi網路
http://192.168.xxx.xxx/control?restart                   //重啟ESP32-CAM
http://192.168.xxx.xxx/control?flash=value               //閃光燈 value= 0~255
http://192.168.xxx.xxx/control?servo=pin;value           //伺服馬達 value= 0~180
http://192.168.xxx.xxx/control?relay=pin;value           //繼電器 value = 0, 1
http://192.168.xxx.xxx/control?servo2=valueH;valueV      //伺服馬達

http://192.168.xxx.xxx/control?joystick=direction           //伺服馬達

官方指令格式 http://192.168.xxx.xxx/control?var=***&val=***

http://192.168.xxx.xxx/control?var=framesize&val=value          //解析度 value = 10->UXGA(1600x1200), 9->SXGA(1280x1024), 8->XGA(1024x768) ,7->SVGA(800x600), 6->VGA(640x480), 5 selected=selected->CIF(400x296), 4->QVGA(320x240), 3->HQVGA(240x176), 0->QQVGA(160x120), 11->QXGA(2048x1564 for OV3660)
http://192.168.xxx.xxx/control?var=quality&val=value            //畫質 value = 10 ~ 63
http://192.168.xxx.xxx/control?var=brightness&val=value         //亮度 value = -2 ~ 2
http://192.168.xxx.xxx/control?var=contrast&val=value           //對比 value = -2 ~ 2
http://192.168.xxx.xxx/control?var=saturation&val=value         //飽和度 value = -2 ~ 2 
http://192.168.xxx.xxx/control?var=special_effect&val=value     //特效 value = 0 ~ 6
http://192.168.xxx.xxx/control?var=hmirror&val=value            //水平鏡像 value = 0 or 1 
http://192.168.xxx.xxx/control?var=vflip&val=value              //垂直翻轉 value = 0 or 1       // value = 0 or 1 
http://192.168.xxx.xxx/control?var=flash&val=value              //閃光燈 value = 0 ~ 255
http://192.168.xxx.xxx/control?var=servoH&val=value             //伺服馬達1 value= 1700~8000
http://192.168.xxx.xxx/control?var=servoV&val=value             //伺服馬達2 value= 1700~8000
http://192.168.xxx.xxx/control?var=anglestep&val=value          //伺服馬達轉動角度

查詢Client端IP：
查詢IP：http://192.168.4.1/?ip
重設網路：http://192.168.4.1/?resetwifi=ssid;password
*/

//輸入AP端連線帳號密碼
const char *apssid = "ESP32-CAM";
const char *appassword = "";  //AP密碼至少要8個字元以上

int angle1Value1 = 90;  //90度
int angle1Value2 = 90;  //90度
int anglestep = 3;      //預設每次移動角度3度

#include <WiFi.h>
#include <esp32-hal-ledc.h>    //用於控制伺服馬達
#include "soc/soc.h"           //用於電源不穩不重開機
#include "soc/rtc_cntl_reg.h"  //用於電源不穩不重開機

//官方函式庫
#include "esp_camera.h"       //視訊函式庫
#include "esp_http_server.h"  //HTTP Server函式庫
#include "img_converters.h"   //影像格式轉換函式庫

String Feedback = "";  //自訂指令回傳客戶端訊息

//自訂指令參數值
String Command = "";
String cmd = "";
String P1 = "";
String P2 = "";
String P3 = "";
String P4 = "";
String P5 = "";
String P6 = "";
String P7 = "";
String P8 = "";
String P9 = "";

//自訂指令拆解狀態值
byte ReceiveState = 0;
byte cmdState = 1;
byte strState = 1;
byte questionstate = 0;
byte equalstate = 0;
byte semicolonstate = 0;

typedef struct {
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

int motor1Pin1 = 13;
int motor1Pin2 = 12;
int motor2Pin1 = 14;
int motor2Pin2 = 15;


//ESP32-CAM模組腳位設定
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

void right() {
  analogWrite(motor3Pin1, dutyCycle + 20);
  analogWrite(motor3Pin2, LOW);
  analogWrite(motor4Pin1, dutyCycle + 20);
  analogWrite(motor4Pin2, LOW);
  analogWrite(motor1Pin1, LOW);
  analogWrite(motor1Pin2, LOW);
  analogWrite(motor2Pin1, LOW);
  analogWrite(motor2Pin2, LOW);
}
void left() {
  analogWrite(motor1Pin1, dutyCycle + 20);
  analogWrite(motor1Pin2, LOW);
  analogWrite(motor2Pin1, dutyCycle + 20);
  analogWrite(motor2Pin2, LOW);
  analogWrite(motor3Pin1, LOW);
  analogWrite(motor3Pin2, LOW);
  analogWrite(motor4Pin1, LOW);
  analogWrite(motor4Pin2, LOW);
}
void up() {
  analogWrite(motor1Pin1, dutyCycle);
  analogWrite(motor1Pin2, LOW);
  analogWrite(motor2Pin1, dutyCycle);
  analogWrite(motor2Pin2, LOW);
  analogWrite(motor3Pin1, dutyCycle);
  analogWrite(motor3Pin2, LOW);
  analogWrite(motor4Pin1, dutyCycle);
  analogWrite(motor4Pin2, LOW);
}
void stop() {
  analogWrite(motor1Pin1, LOW);
  analogWrite(motor1Pin2, LOW);
  analogWrite(motor2Pin1, LOW);
  analogWrite(motor2Pin2, LOW);
  analogWrite(motor3Pin1, LOW);
  analogWrite(motor3Pin2, LOW);
  analogWrite(motor4Pin1, LOW);
  analogWrite(motor4Pin2, LOW);
}

void down() {
  analogWrite(motor1Pin1, LOW);
  analogWrite(motor1Pin2, dutyCycle);
  analogWrite(motor2Pin1, LOW);
  analogWrite(motor2Pin2, dutyCycle);
  analogWrite(motor3Pin1, LOW);
  analogWrite(motor3Pin2, dutyCycle);
  analogWrite(motor4Pin1, LOW);
  analogWrite(motor4Pin2, dutyCycle);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  //關閉電源不穩就重開機的設定

  Serial.begin(115200);
  Serial.setDebugOutput(true);  //開啟診斷輸出
  Serial.println();

  //視訊組態設定  https://github.com/espressif/esp32-camera/blob/master/driver/include/esp_camera.h
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {  //是否有PSRAM(Psuedo SRAM)記憶體IC
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  //視訊初始化
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  }

  //可自訂視訊框架預設大小(解析度大小)
  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }

  //可動態改變視訊框架大小(解析度大小)
  s->set_framesize(s, FRAMESIZE_QVGA);  //UXGA|SXGA|XGA|SVGA|VGA|CIF|QVGA|HQVGA|QQVGA

  //Servo

  WiFi.mode(WIFI_AP);  //其他模式 WiFi.mode(WIFI_AP); WiFi.mode(WIFI_STA);

  WiFi.softAP((WiFi.localIP().toString() + "_" + (String)apssid).c_str(), appassword);  //設定SSID顯示客戶端IP
  Serial.println("");
  Serial.println("STAIP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("");

  Serial.println("");
  Serial.println("APIP address: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("");

  startCameraServer();
}

void servo_rotate(int channel, int angle) {
  int val = 7864 - angle * 34.59;
  if (val > 7864)
    val = 7864;
  else if (val < 1638)
    val = 1638;
  ledcWrite(channel, val);
}

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len) {
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index) {
    j->len = 0;
  }
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
    return 0;
  }
  j->len += len;
  return len;
}

//影像截圖
static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;

  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  size_t fb_len = 0;
  if (fb->format == PIXFORMAT_JPEG) {
    fb_len = fb->len;
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  } else {
    jpg_chunking_t jchunk = { req, 0 };
    res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
    httpd_resp_send_chunk(req, NULL, 0);
    fb_len = jchunk.len;
  }
  esp_camera_fb_return(fb);
  return res;
}

//影像串流
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) {
          Serial.println("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }

    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      break;
    }
  }

  return res;
}

//指令參數控制
static esp_err_t cmd_handler(httpd_req_t *req) {
  char *buf;  //存取網址後帶的參數字串
  size_t buf_len;
  char variable[128] = {
    0,
  };  //存取參數var值
  char value[128] = {
    0,
  };  //存取參數val值
  String myCmd = "";

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK && httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
      } else {
        myCmd = String(buf);  //如果非官方格式不含var, val，則為自訂指令格式
      }
    }
    free(buf);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  Feedback = "";
  Command = "";
  cmd = "";
  P1 = "";
  P2 = "";
  P3 = "";
  P4 = "";
  P5 = "";
  P6 = "";
  P7 = "";
  P8 = "";
  P9 = "";
  ReceiveState = 0, cmdState = 1, strState = 1, questionstate = 0, equalstate = 0, semicolonstate = 0;
  if (myCmd.length() > 0) {
    myCmd = "?" + myCmd;  //網址後帶的參數字串轉換成自訂指令格式
    for (int i = 0; i < myCmd.length(); i++) {
      getCommand(char(myCmd.charAt(i)));  //拆解自訂指令參數字串
    }
  }

  if (cmd.length() > 0) {
    Serial.println("");
    //Serial.println("Command: "+Command);
    Serial.println("cmd= " + cmd + " ,P1= " + P1 + " ,P2= " + P2 + " ,P3= " + P3 + " ,P4= " + P4 + " ,P5= " + P5 + " ,P6= " + P6 + " ,P7= " + P7 + " ,P8= " + P8 + " ,P9= " + P9);
    Serial.println("");

    //自訂指令區塊  http://192.168.xxx.xxx/control?cmd=P1;P2;P3;P4;P5;P6;P7;P8;P9
    if (cmd == "your cmd") {
      // You can do anything
      // Feedback="<font color=\"red\">Hello World</font>";   //可為一般文字或HTML語法
    } else if (cmd == "ip") {  //查詢APIP, STAIP
      Feedback = "AP IP: " + WiFi.softAPIP().toString();
      Feedback += "<br>";
      Feedback += "STA IP: " + WiFi.localIP().toString();
    } else if (cmd == "mac") {  //查詢MAC位址
      Feedback = "STA MAC: " + WiFi.macAddress();
    } else if (cmd == "restart") {  //重設WIFI連線
      ESP.restart();
    } else if (cmd == "digitalwrite") {  //數位輸出
      ledcDetachPin(P1.toInt());
      pinMode(P1.toInt(), OUTPUT);
      digitalWrite(P1.toInt(), P2.toInt());
    } else if (cmd == "digitalread") {  //數位輸入
      Feedback = String(digitalRead(P1.toInt()));
    } else if (cmd == "analogwrite") {  //類比輸出
      if (P1 == "4") {
        ledcAttachPin(4, 4);
        ledcSetup(4, 5000, 8);
        ledcWrite(4, P2.toInt());
      } else {
        ledcAttachPin(P1.toInt(), 9);
        ledcSetup(9, 5000, 8);
        ledcWrite(9, P2.toInt());
      }
    } else if (cmd == "analogread") {  //類比讀取
      Feedback = String(analogRead(P1.toInt()));
    } else if (cmd == "touchread") {  //觸碰讀取
      Feedback = String(touchRead(P1.toInt()));
    } else if (cmd == "restart") {  //重啟電源
      ESP.restart();
    } else if (cmd == "flash") {  //閃光燈
      ledcAttachPin(4, 4);
      ledcSetup(4, 5000, 8);
      int val = P1.toInt();
      ledcWrite(4, val);
    } else if (cmd == "relay") {  //繼電器
      pinMode(P1.toInt(), OUTPUT);
      digitalWrite(P1.toInt(), P2.toInt());
    } else if (cmd == "resetwifi") {  //重設網路連線
      for (int i = 0; i < 2; i++) {
        WiFi.begin(P1.c_str(), P2.c_str());
        Serial.print("Connecting to ");
        Serial.println(P1);
        long int StartTime = millis();
        while (WiFi.status() != WL_CONNECTED) {
          delay(500);
          if ((StartTime + 5000) < millis()) break;
        }
        Serial.println("");
        Serial.println("STAIP: " + WiFi.localIP().toString());
        Feedback = "STAIP: " + WiFi.localIP().toString();

        if (WiFi.status() == WL_CONNECTED) {
          WiFi.softAP((WiFi.localIP().toString() + "_" + P1).c_str(), P2.c_str());
          for (int i = 0; i < 2; i++) {  //若連不上WIFI設定閃光燈慢速閃爍
            ledcWrite(4, 10);
            delay(300);
            ledcWrite(4, 0);
            delay(300);
          }
          break;
        }
      }
    } else if (cmd == "servo") {
    } else if (cmd == "joystick") {
      
      if (P1 == "N") {
        Serial.println("Up");
        up();
      } else if (P1 == "W") {
        Serial.println("Left");
        left();
      } else if (P1 == "C") {
        Serial.println("Stop");
        stop();
      } else if (P1 == "E") {
        Serial.println("Right");
        right();
      } else if (P1 == "S") {
        Serial.println("Down");
        down();
      } else if (P1 == "NW") {
        Serial.println("Left-Up");
        analogWrite(motor1Pin1, 200);
        analogWrite(motor1Pin2, LOW);
        analogWrite(motor2Pin1, 200);
        analogWrite(motor2Pin2, LOW);
        analogWrite(motor3Pin1, LOW);
        analogWrite(motor3Pin2, LOW);
        analogWrite(motor4Pin1, 90);
        analogWrite(motor4Pin2, LOW);
      } else if (P1 == "NE") {
        Serial.println("Right-Up");
        analogWrite(motor1Pin1, LOW);
        analogWrite(motor1Pin2, LOW);
        analogWrite(motor2Pin1, 90);
        analogWrite(motor2Pin2, LOW);
        analogWrite(motor3Pin1, 200);
        analogWrite(motor3Pin2, LOW);
        analogWrite(motor4Pin1, 200);
        analogWrite(motor4Pin2, LOW);
      } else if (P1 == "SW") {
        Serial.println("Left-Down");
        analogWrite(motor1Pin1, LOW);
        analogWrite(motor1Pin2, 200);
        analogWrite(motor2Pin1, LOW);
        analogWrite(motor2Pin2, 200);
        analogWrite(motor3Pin1, LOW);
        analogWrite(motor3Pin2, 90);
        analogWrite(motor4Pin1, LOW);
        analogWrite(motor4Pin2, LOW);
      } else if (P1 == "SE") {
        Serial.println("Right-Down");
        analogWrite(motor1Pin1, LOW);
        analogWrite(motor1Pin2, 90);
        analogWrite(motor2Pin1, LOW);
        analogWrite(motor2Pin2, LOW);
        analogWrite(motor3Pin1, LOW);
        analogWrite(motor3Pin2, 200);
        analogWrite(motor4Pin1, LOW);
        analogWrite(motor4Pin2, 200);
      }

      Feedback = String(angle1Value1) + "," + String(angle1Value2);

      //Serial.println("servoH="+String(val_h));
      //Serial.println("servoV="+String(val_v));
    } else {
      Feedback = "Command is not defined";
    }

    if (Feedback == "") Feedback = Command;  //若沒有設定回傳資料就回傳Command值

    const char *resp = Feedback.c_str();
    httpd_resp_set_type(req, "text/html");                        //設定回傳資料格式
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");  //允許跨網域讀取
    return httpd_resp_send(req, resp, strlen(resp));
  } else {
    //官方指令區塊，也可在此自訂指令  http://192.168.xxx.xxx/control?var=xxx&val=xxx
    int val = atoi(value);
    sensor_t *s = esp_camera_sensor_get();
    int res = 0;

    if (!strcmp(variable, "framesize")) {
      if (s->pixformat == PIXFORMAT_JPEG)
        res = s->set_framesize(s, (framesize_t)val);
    } else if (!strcmp(variable, "quality")) res = s->set_quality(s, val);
    else if (!strcmp(variable, "contrast")) res = s->set_contrast(s, val);
    else if (!strcmp(variable, "brightness")) res = s->set_brightness(s, val);
    else if (!strcmp(variable, "hmirror")) res = s->set_hmirror(s, val);
    else if (!strcmp(variable, "vflip")) res = s->set_vflip(s, val);
    } else if (!strcmp(variable, "anglestep")) {
      Serial.println("anglestep=" + String(val));
      anglestep = val;
    } else if (!strcmp(variable, "servoH")) {
      angle1Value1 = val;
      ledcAttachPin(2, 3);
      ledcSetup(3, 50, 16);
      servo_rotate(3, angle1Value1);
      delay(100);

      Serial.println("servoH=" + String(angle1Value1));
    } else if (!strcmp(variable, "servoV")) {
      angle1Value2 = val;
      ledcAttachPin(13, 5);
      ledcSetup(5, 50, 16);
      servo_rotate(5, angle1Value2);
      delay(100);

      Serial.println("servoV=" + String(angle1Value2));
    } else {
      res = -1;
    }

    if (res) {
      return httpd_resp_send_500(req);
    }

    if (buf) {
      Feedback = String(buf);
      const char *resp = Feedback.c_str();
      httpd_resp_set_type(req, "text/html");
      httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
      return httpd_resp_send(req, resp, strlen(resp));  //回傳參數字串
    } else {
      httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
      return httpd_resp_send(req, NULL, 0);
    }
  }
}

//顯示視訊參數狀態(須回傳json格式載入初始設定)
static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];

  sensor_t *s = esp_camera_sensor_get();
  char *p = json_response;
  *p++ = '{';
  p += sprintf(p, "\"servoH\":%d,", angle1Value1);
  p += sprintf(p, "\"servoV\":%d,", angle1Value2);
  p += sprintf(p, "\"anglestep\":%d,", anglestep);
  p += sprintf(p, "\"flash\":%d,", 0);
  p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
  p += sprintf(p, "\"quality\":%u,", s->status.quality);
  p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
  p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
  p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
  p += sprintf(p, "\"vflip\":%u", s->status.vflip);
  *p++ = '}';
  *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

//自訂網頁首頁
static const char PROGMEM INDEX_HTML[] = R"rawliteral(<!doctype html>
<html>
    <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width,initial-scale=1">
        <meta http-equiv="Access-Control-Allow-Headers" content="Origin, X-Requested-With, Content-Type, Accept">
        <meta http-equiv="Access-Control-Allow-Methods" content="GET,POST,PUT,DELETE,OPTIONS">
        <meta http-equiv="Access-Control-Allow-Origin" content="*">
        <title>ESP32 OV2460</title>
        <style>        
            body {
                font-family: Arial,Helvetica,sans-serif;
                background: #181818;
                color: #EFEFEF;
                font-size: 16px
            }
            h2 {
                font-size: 18px
            }
            section.main {
                display: flex
            }
            #menu,section.main {
                flex-direction: column
            }
            #menu {
                display: block;
                flex-wrap: nowrap;
                min-width: 340px;
                background: #363636;
                padding: 8px;
                border-radius: 4px;
                margin-top: -10px;
                margin-right: 10px;
            }
            #content {
                display: flex;
                flex-wrap: wrap;
                align-items: stretch
            }
            figure {
                padding: 0px;
                margin: 0;
                -webkit-margin-before: 0;
                margin-block-start: 0;
                -webkit-margin-after: 0;
                margin-block-end: 0;
                -webkit-margin-start: 0;
                margin-inline-start: 0;
                -webkit-margin-end: 0;
                margin-inline-end: 0
            }
            figure img {
                display: block;
                width: 100%;
                height: auto;
                border-radius: 4px;
                margin-top: 8px;
            }
            @media (min-width: 800px) and (orientation:landscape) {
                #content {
                    display:flex;
                    flex-wrap: nowrap;
                    align-items: stretch
                }
                figure img {
                    display: block;
                    max-width: 100%;
                    max-height: calc(100vh - 40px);
                    width: auto;
                    height: auto
                }
                figure {
                    padding: 0 0 0 0px;
                    margin: 0;
                    -webkit-margin-before: 0;
                    margin-block-start: 0;
                    -webkit-margin-after: 0;
                    margin-block-end: 0;
                    -webkit-margin-start: 0;
                    margin-inline-start: 0;
                    -webkit-margin-end: 0;
                    margin-inline-end: 0
                }
            }
            section#buttons {
                display: flex;
                flex-wrap: nowrap;
                justify-content: space-between
            }
            #nav-toggle {
                cursor: pointer;
                display: block
            }
            #nav-toggle-cb {
                outline: 0;
                opacity: 0;
                width: 0;
                height: 0
            }
            #nav-toggle-cb:checked+#menu {
                display: none
            }
            .input-group {
                display: flex;
                flex-wrap: nowrap;
                line-height: 22px;
                margin: 5px 0
            }
            .input-group>label {
                display: inline-block;
                padding-right: 10px;
                min-width: 47%
            }
            .input-group input,.input-group select {
                flex-grow: 1
            }
            .range-max,.range-min {
                display: inline-block;
                padding: 0 5px
            }
            button {
                display: block;
                margin: 5px;
                padding: 0 12px;
                border: 0;
                line-height: 28px;
                cursor: pointer;
                color: #fff;
                background: #ff3034;
                border-radius: 5px;
                font-size: 16px;
                outline: 0
            }
            button:hover {
                background: #ff494d
            }
            button:active {
                background: #f21c21
            }
            button.disabled {
                cursor: default;
                background: #a0a0a0
            }
            input[type=range] {
                -webkit-appearance: none;
                width: 100%;
                height: 22px;
                background: #363636;
                cursor: pointer;
                margin: 0
            }
            input[type=range]:focus {
                outline: 0
            }
            input[type=range]::-webkit-slider-runnable-track {
                width: 100%;
                height: 2px;
                cursor: pointer;
                background: #EFEFEF;
                border-radius: 0;
                border: 0 solid #EFEFEF
            }
            input[type=range]::-webkit-slider-thumb {
                border: 1px solid rgba(0,0,30,0);
                height: 22px;
                width: 22px;
                border-radius: 50px;
                background: #ff3034;
                cursor: pointer;
                -webkit-appearance: none;
                margin-top: -11.5px
            }
            input[type=range]:focus::-webkit-slider-runnable-track {
                background: #EFEFEF
            }
            input[type=range]::-moz-range-track {
                width: 100%;
                height: 2px;
                cursor: pointer;
                background: #EFEFEF;
                border-radius: 0;
                border: 0 solid #EFEFEF
            }
            input[type=range]::-moz-range-thumb {
                border: 1px solid rgba(0,0,30,0);
                height: 22px;
                width: 22px;
                border-radius: 50px;
                background: #ff3034;
                cursor: pointer
            }
            input[type=range]::-ms-track {
                width: 100%;
                height: 2px;
                cursor: pointer;
                background: 0 0;
                border-color: transparent;
                color: transparent
            }
            input[type=range]::-ms-fill-lower {
                background: #EFEFEF;
                border: 0 solid #EFEFEF;
                border-radius: 0
            }
            input[type=range]::-ms-fill-upper {
                background: #EFEFEF;
                border: 0 solid #EFEFEF;
                border-radius: 0
            }
            input[type=range]::-ms-thumb {
                border: 1px solid rgba(0,0,30,0);
                height: 22px;
                width: 22px;
                border-radius: 50px;
                background: #ff3034;
                cursor: pointer;
                height: 2px
            }
            input[type=range]:focus::-ms-fill-lower {
                background: #EFEFEF
            }
            input[type=range]:focus::-ms-fill-upper {
                background: #363636
            }
            .switch {
                display: block;
                position: relative;
                line-height: 22px;
                font-size: 16px;
                height: 22px
            }
            .switch input {
                outline: 0;
                opacity: 0;
                width: 0;
                height: 0
            }
            .slider {
                width: 50px;
                height: 22px;
                border-radius: 22px;
                cursor: pointer;
                background-color: grey
            }
            .slider,.slider:before {
                display: inline-block;
                transition: .4s
            }
            .slider:before {
                position: relative;
                content: "";
                border-radius: 50%;
                height: 16px;
                width: 16px;
                left: 4px;
                top: 3px;
                background-color: #fff
            }
            input:checked+.slider {
                background-color: #ff3034
            }
            input:checked+.slider:before {
                -webkit-transform: translateX(26px);
                transform: translateX(26px)
            }
            select {
                border: 1px solid #363636;
                font-size: 14px;
                height: 22px;
                outline: 0;
                border-radius: 5px
            }
            .image-container {
                position: relative;
                min-width: 160px
            }
            .close {
                position: absolute;
                right: 5px;
                top: 5px;
                background: #ff3034;
                width: 16px;
                height: 16px;
                border-radius: 100px;
                color: #fff;
                text-align: center;
                line-height: 18px;
                cursor: pointer
            }
            .hidden {
                display: none
            }
        </style>
        <script>
          var JoyStick=function(t,e){var i=void 0===(e=e||{}).title?"joystick":e.title,n=void 0===e.width?0:e.width,o=void 0===e.height?0:e.height,r=void 0===e.internalFillColor?"#00AA00":e.internalFillColor,h=void 0===e.internalLineWidth?2:e.internalLineWidth,a=void 0===e.internalStrokeColor?"#003300":e.internalStrokeColor,d=void 0===e.externalLineWidth?2:e.externalLineWidth,f=void 0===e.externalStrokeColor?"#008000":e.externalStrokeColor,l=void 0===e.autoReturnToCenter||e.autoReturnToCenter,s=document.getElementById(t),c=document.createElement("canvas");c.id=i,0===n&&(n=s.clientWidth),0===o&&(o=s.clientHeight),c.width=n,c.height=o,s.appendChild(c);var u=c.getContext("2d"),g=0,v=2*Math.PI,p=(c.width-(c.width/2+10))/2,C=p+5,w=p+30,m=c.width/2,L=c.height/2,E=c.width/10,P=-1*E,S=c.height/10,k=-1*S,W=m,T=L;function G(){u.beginPath(),u.arc(m,L,w,0,v,!1),u.lineWidth=d,u.strokeStyle=f,u.stroke()}function x(){u.beginPath(),W<p&&(W=C),W+p>c.width&&(W=c.width-C),T<p&&(T=C),T+p>c.height&&(T=c.height-C),u.arc(W,T,p,0,v,!1);var t=u.createRadialGradient(m,L,5,m,L,200);t.addColorStop(0,r),t.addColorStop(1,a),u.fillStyle=t,u.fill(),u.lineWidth=h,u.strokeStyle=a,u.stroke()}"ontouchstart"in document.documentElement?(c.addEventListener("touchstart",function(t){g=1},!1),c.addEventListener("touchmove",function(t){t.preventDefault(),1===g&&t.targetTouches[0].target===c&&(W=t.targetTouches[0].pageX,T=t.targetTouches[0].pageY,"BODY"===c.offsetParent.tagName.toUpperCase()?(W-=c.offsetLeft,T-=c.offsetTop):(W-=c.offsetParent.offsetLeft,T-=c.offsetParent.offsetTop),u.clearRect(0,0,c.width,c.height),G(),x())},!1),c.addEventListener("touchend",function(t){g=0,l&&(W=m,T=L);u.clearRect(0,0,c.width,c.height),G(),x()},!1)):(c.addEventListener("mousedown",function(t){g=1},!1),c.addEventListener("mousemove",function(t){1===g&&(W=t.pageX,T=t.pageY,"BODY"===c.offsetParent.tagName.toUpperCase()?(W-=c.offsetLeft,T-=c.offsetTop):(W-=c.offsetParent.offsetLeft,T-=c.offsetParent.offsetTop),u.clearRect(0,0,c.width,c.height),G(),x())},!1),c.addEventListener("mouseup",function(t){g=0,l&&(W=m,T=L);u.clearRect(0,0,c.width,c.height),G(),x()},!1)),G(),x(),this.GetWidth=function(){return c.width},this.GetHeight=function(){return c.height},this.GetPosX=function(){return W},this.GetPosY=function(){return T},this.GetX=function(){return((W-m)/C*100).toFixed()},this.GetY=function(){return((T-L)/C*100*-1).toFixed()},this.GetDir=function(){var t="",e=W-m,i=T-L;return i>=k&&i<=S&&(t="C"),i<k&&(t="N"),i>S&&(t="S"),e<P&&("C"===t?t="W":t+="W"),e>E&&("C"===t?t="E":t+="E"),t}};
        </script>        
    </head>
    <body>
        <figure>
          <div id="stream-container" class="image-container hidden">
          <div class="close" id="close-stream">×</div>
            <img id="stream" src="" crossorigin="anonymous">
          </div>
          <div id="joy3Div" style="width:200px;height:200px;margin:50px"></div><br>
          <input type="checkbox" id="chkcanvas">Hide control panel
        </figure>
        <section id="buttons">
            <table>
              <tr><td><button id="restart" onclick="try{fetch(document.location.origin+'/control?restart');}catch(e){}">Restart</button></td><td><button id="get-still">Get Still</button></td><td><button id="toggle-stream">Start Stream</button></td></tr>
            </table>
        </section>
        <section class="main">      
            <div id="logo">
                <label for="nav-toggle-cb" id="nav-toggle">&#9776;&nbsp;&nbsp;Toggle settings</label>
            </div>
            <div id="content">
                <div id="sidebar">
                    <input type="checkbox" id="nav-toggle-cb">
                    <nav id="menu">
                        <div class="input-group" id="servo-group">
                            <label for="servoH">Servo H</label>
                            <div class="range-min">0</div>
                            <input type="range" id="servoH" min="0" max="180" value="90" step="1" class="default-action">
                            <div class="range-max">180</div>
                        </div>
                        <div class="input-group" id="servo-group">
                            <label for="servoV">Servo V</label>
                            <div class="range-min">0</div>
                            <input type="range" id="servoV" min="0" max="180" value="90" step="1" class="default-action">
                            <div class="range-max">180</div>
                        </div>
                        <div class="input-group" id="anglestep-group">
                            <label for="step">Angle Step</label>
                            <div class="range-min">1</div>
                            <input type="range" id="anglestep" min="1" max="10" step="1" value="3" class="default-action">
                            <div class="range-max">10</div>
                        </div>                        
                        <div class="input-group" id="flash-group">
                            <label for="flash">Flash</label>
                            <div class="range-min">0</div>
                            <input type="range" id="flash" min="0" max="255" value="0" class="default-action">
                            <div class="range-max">255</div>
                        </div>
                        <div class="input-group" id="framesize-group">
                            <label for="framesize">Resolution</label>
                            <select id="framesize" class="default-action">
                                <option value="10">UXGA(1600x1200)</option>
                                <option value="9">SXGA(1280x1024)</option>
                                <option value="8">XGA(1024x768)</option>
                                <option value="7">SVGA(800x600)</option>
                                <option value="6">VGA(640x480)</option>
                                <option value="5">CIF(400x296)</option>
                                <option value="4" selected="selected">QVGA(320x240)</option>
                                <option value="3">HQVGA(240x176)</option>
                                <option value="0">QQVGA(160x120)</option>
                            </select>
                        </div>
                        <div class="input-group" id="quality-group">
                            <label for="quality">Quality</label>
                            <div class="range-min">10</div>
                            <input type="range" id="quality" min="10" max="63" value="10" class="default-action">
                            <div class="range-max">63</div>
                        </div>
                        <div class="input-group" id="brightness-group">
                            <label for="brightness">Brightness</label>
                            <div class="range-min">-2</div>
                            <input type="range" id="brightness" min="-2" max="2" value="0" class="default-action">
                            <div class="range-max">2</div>
                        </div>
                        <div class="input-group" id="contrast-group">
                            <label for="contrast">Contrast</label>
                            <div class="range-min">-2</div>
                            <input type="range" id="contrast" min="-2" max="2" value="0" class="default-action">
                            <div class="range-max">2</div>
                        </div>
                        <div class="input-group" id="hmirror-group">
                            <label for="hmirror">H-Mirror</label>
                            <div class="switch">
                                <input id="hmirror" type="checkbox" class="default-action" checked="checked">
                                <label class="slider" for="hmirror"></label>
                            </div>
                        </div>
                        <div class="input-group" id="vflip-group">
                            <label for="vflip">V-Flip</label>
                            <div class="switch">
                                <input id="vflip" type="checkbox" class="default-action" checked="checked">
                                <label class="slider" for="vflip"></label>
                            </div>
                        </div>
                    </nav>
                </div>
            </div>
        </section>
        
        <script>
          document.addEventListener('DOMContentLoaded', function (event) {
            var baseHost = document.location.origin
            var streamUrl = baseHost + ':81'
          
            const hide = el => {
              el.classList.add('hidden')
            }
            const show = el => {
              el.classList.remove('hidden')
            }
          
            const disable = el => {
              el.classList.add('disabled')
              el.disabled = true
            }
          
            const enable = el => {
              el.classList.remove('disabled')
              el.disabled = false
            }
          
            const updateValue = (el, value, updateRemote) => {
              updateRemote = updateRemote == null ? true : updateRemote
              let initialValue
              if (el.type === 'checkbox') {
                initialValue = el.checked
                value = !!value
                el.checked = value
              } else {
                initialValue = el.value
                el.value = value
              }
          
              if (updateRemote && initialValue !== value) {
                updateConfig(el);
              }
            }
          
            function updateConfig (el) {
              let value
              switch (el.type) {
                case 'checkbox':
                  value = el.checked ? 1 : 0
                  break
                case 'range':
                case 'select-one':
                  value = el.value
                  break
                case 'button':
                case 'submit':
                  value = '1'
                  break
                default:
                  return
              }
          
              const query = `${baseHost}/control?var=${el.id}&val=${value}`
          
              fetch(query)
                .then(response => {
                  console.log(`request to ${query} finished, status: ${response.status}`)
                })
            }
          
            document
              .querySelectorAll('.close')
              .forEach(el => {
                el.onclick = () => {
                  hide(el.parentNode)
                }
              })
          
            // read initial values
            fetch(`${baseHost}/status`)
              .then(function (response) {
                return response.json()
              })
              .then(function (state) {
                document
                  .querySelectorAll('.default-action')
                  .forEach(el => {
                    updateValue(el, state[el.id], false)
                  })
              })
          
            const view = document.getElementById('stream')
            const viewContainer = document.getElementById('stream-container')
            const stillButton = document.getElementById('get-still')
            const streamButton = document.getElementById('toggle-stream')
            const closeButton = document.getElementById('close-stream')
          
            const stopStream = () => {
              window.stop();
              streamButton.innerHTML = 'Start Stream';
              viewContainer.style.display = "none";
            }
          
            const startStream = () => {
              view.src = `${streamUrl}/stream`
              streamButton.innerHTML = 'Stop Stream'
              viewContainer.style.display = "block";
            }
          
            // Attach actions to buttons
            stillButton.onclick = () => {
              stopStream()
              view.src = `${baseHost}/capture?_cb=${Date.now()}`
              viewContainer.style.display = "block";
            }
          
            closeButton.onclick = () => {
              stopStream()
              hide(viewContainer)
            }
          
            streamButton.onclick = () => {
              const streamEnabled = streamButton.innerHTML === 'Stop Stream'
              if (streamEnabled) {
                stopStream()
              } else {
                startStream()
              }
            }
          
            // Attach default on change action
            document
              .querySelectorAll('.default-action')
              .forEach(el => {
                el.onchange = () => updateConfig(el)
              })
          
            // Custom actions
          
            const framesize = document.getElementById('framesize')
          
            framesize.onchange = () => {
              updateConfig(framesize)
              if (framesize.value > 5) {
                updateValue(detect, false)
                updateValue(recognize, false)
              }
            }
          })
        </script>
         
        <script type="text/javascript">
          var chkcanvas = document.getElementById('chkcanvas');
          var joy3Div = document.getElementById('joy3Div');
          var joy3Param = { "title": "joystick3" };
          var Joy3 = new JoyStick('joy3Div', joy3Param);
          var cmdState = 0;

          chkcanvas.onchange = function(e){  
            if (chkcanvas.checked)
              joy3Div.style.display = "none";
            else
              joy3Div.style.display = "block";
          }
          
          setInterval(function(){
            if (cmdState == 1) 
              return;
            else
              cmdState = 1;
            controlServo();
          }, 200);
          
          function controlServo() {
            var dir = Joy3.GetDir();
            if (dir!="C") { 
              var target_url = document.location.origin+'/control?joystick='+dir;
              fetch(target_url).then(function(response) {
                return response.text();
              }).then(function(text) {
                var res = text.split(",");
                document.getElementById('servoH').value=res[0];
                document.getElementById('servoV').value=res[1];
                cmdState = 0;
              }).catch(function(error) {
                message.innerHTML= error;
                cmdState = 0;
              });
            }
            else
              cmdState = 0;              
          }
        </script>       
    
    </body>
</html>)rawliteral";

//網頁首頁   http://192.168.xxx.xxx
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

//自訂網址路徑要執行的函式
void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();  //可在HTTPD_DEFAULT_CONFIG()中設定Server Port

  //http://192.168.xxx.xxx/
  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
  };

  //http://192.168.xxx.xxx/status
  httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_handler,
    .user_ctx = NULL
  };

  //http://192.168.xxx.xxx/control
  httpd_uri_t cmd_uri = {
    .uri = "/control",
    .method = HTTP_GET,
    .handler = cmd_handler,
    .user_ctx = NULL
  };

  //http://192.168.xxx.xxx/capture
  httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL
  };

  //http://192.168.xxx.xxx:81/stream
  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };

  Serial.printf("Starting web server on port: '%d'\n", config.server_port);  //Server Port
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    //註冊自訂網址路徑對應執行的函式
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
  }

  config.server_port += 1;  //Stream Port
  config.ctrl_port += 1;    //UDP Port
  Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

//自訂指令拆解參數字串置入變數
void getCommand(char c) {
  if (c == '?') ReceiveState = 1;
  if ((c == ' ') || (c == '\r') || (c == '\n')) ReceiveState = 0;

  if (ReceiveState == 1) {
    Command = Command + String(c);

    if (c == '=') cmdState = 0;
    if (c == ';') strState++;

    if ((cmdState == 1) && ((c != '?') || (questionstate == 1))) cmd = cmd + String(c);
    if ((cmdState == 0) && (strState == 1) && ((c != '=') || (equalstate == 1))) P1 = P1 + String(c);
    if ((cmdState == 0) && (strState == 2) && (c != ';')) P2 = P2 + String(c);
    if ((cmdState == 0) && (strState == 3) && (c != ';')) P3 = P3 + String(c);
    if ((cmdState == 0) && (strState == 4) && (c != ';')) P4 = P4 + String(c);
    if ((cmdState == 0) && (strState == 5) && (c != ';')) P5 = P5 + String(c);
    if ((cmdState == 0) && (strState == 6) && (c != ';')) P6 = P6 + String(c);
    if ((cmdState == 0) && (strState == 7) && (c != ';')) P7 = P7 + String(c);
    if ((cmdState == 0) && (strState == 8) && (c != ';')) P8 = P8 + String(c);
    if ((cmdState == 0) && (strState >= 9) && ((c != ';') || (semicolonstate == 1))) P9 = P9 + String(c);

    if (c == '?') questionstate = 1;
    if (c == '=') equalstate = 1;
    if ((strState >= 9) && (c == ';')) semicolonstate = 1;
  }
}
