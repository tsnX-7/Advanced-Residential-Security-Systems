#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include "Arduino.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"
#include <SPIFFS.h>
#include <FS.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"


#define PHOTO   2
#define LED     4
#define LOCK    12
#define PIR     13
#define BUTTON  14
#define RED     15


const char* ssid = "..";
const char* password = "*****";                              //personal credential
#define AUTH "95rh-thRZeU87PyO4sVuJHH0lZxOs9w5"              //sent by Blynk
#define API_KEY "AIzaSyAwH1y5e8oDb4g6qvPVdI93I0jR_AvS74U"
#define USER_EMAIL "tasin.ti07@gmail.com"                    // Insert Authorized Email
#define USER_PASSWORD "*****"                                //Corresponding Password 
#define STORAGE_BUCKET_ID "smart-lock-90ed4.appspot.com"     // Insert Firebase storage bucket ID
#define FILE_PHOTO "/data/photo.jpg"                         // Photo File Name to save in SPIFFS


//Define Firebase Data objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig configF;


String local_IP;
int cnt = 0;
int cnt_pir = 0;
bool taskCompleted = false;
bool motionDetected = false;
boolean takeNewPhoto = true;


void startCameraServer();


void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("Error occurred while mounting SPIFFS!");
    ESP.restart();
  }
  else {
    delay(500);
    Serial.println("SPIFFS mounted successfully!");
  }
}

// Check if photo capture was successful
bool checkPhoto( fs::FS &fs ) {
  File f_pic = fs.open( FILE_PHOTO );
  unsigned int pic_sz = f_pic.size();
  return ( pic_sz > 100 );
}



void capturePhotoSaveSpiffs( void ) {
  camera_fb_t * fb = NULL;
  bool ok = 0;                                                // Boolean indicating if the picture has been taken correctly
  taskCompleted = false;

  do {
    Serial.println("Taking a photo...");                      // Take a photo with the camera
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      return;
    }
    Serial.printf("Picture file name: %s\n", FILE_PHOTO);     // Photo file name

    // Insert the data in the photo file
    File file = SPIFFS.open(FILE_PHOTO, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file in writing mode");
    }
    else {
      file.write(fb->buf, fb->len); // payload (image), payload length
      Serial.print("The picture has been saved in ");
      Serial.print(FILE_PHOTO);
      Serial.print(" - Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    file.close();
    esp_camera_fb_return(fb);
    ok = checkPhoto(SPIFFS);    // check if file has been correctly saved in SPIFFS

  } while (!ok);


  if (Firebase.ready() && !taskCompleted) {
    taskCompleted = true;
    Serial.print("Uploading picture... ");
    const char* photoname = ("/data/IMG_" + (String)cnt + ".jpg").c_str();

    if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, FILE_PHOTO /* path to local file */, mem_storage_type_flash, photoname /* path of remote file stored in the bucket */, "image/jpeg")) {
      Serial.printf("\nDownload URL: %s\n", fbdo.downloadURL().c_str());
    }
    else {
      Serial.println(fbdo.errorReason());
    }
  }
  else {
    Serial.println("Firebase not responding correctly!");
  }
}



void takePhoto()
{
  digitalWrite(LED, HIGH);
  delay(200);
  uint32_t randomNum = random(50000);
  Serial.println("http://" + local_IP + "/capture?_cb=" + (String)randomNum);
  Blynk.setProperty(V1, "urls", "http://" + local_IP + "/capture?_cb=" + (String)randomNum);
  cnt += 1;
  capturePhotoSaveSpiffs();
  digitalWrite(LED, LOW);
}



void setup() {
  Serial.begin(115200);
  pinMode(LOCK, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(BUTTON, INPUT);
  pinMode(RED, OUTPUT);
  pinMode(PIR, INPUT_PULLDOWN);
  digitalWrite(PIR, LOW);
  Serial.setDebugOutput(true);
  Serial.println();

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

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality for larger pre-allocated frame buffer.
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();              // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);                                // flip it back
    s->set_brightness(s, 1);                           // up the brightness just a bit
    s->set_saturation(s, -2);                          // lower the saturation
  }
  s->set_framesize(s, FRAMESIZE_QVGA);                 // drop down frame size for higher initial frame rate


  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected successfully");


  initSPIFFS();
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  startCameraServer();

  Serial.print("Camera Ready! Use 'http://");           //local ip addr to operate esp32 cam module
  Serial.print(WiFi.localIP());
  local_IP = WiFi.localIP().toString();
  Serial.println("' to connect");
  Blynk.begin(AUTH, ssid, password, "blynk.cloud", 80);

  configF.api_key = API_KEY;
  auth.user.email = USER_EMAIL;                         //Assign the user sign in credentials
  auth.user.password = USER_PASSWORD;
  configF.token_status_callback = tokenStatusCallback;  //Assign the callback function for the long running token generation task
  Firebase.begin(&configF, &auth);
  Firebase.reconnectWiFi(true);

}



//main code, runs repeatedly
void loop() {
  Blynk.run();
  digitalWrite(PIR, LOW);
  if (digitalRead(BUTTON) == HIGH) {
    Serial.println("Send Notification");
    Blynk.logEvent("BUTTON", "Someone is at the door...");
    takePhoto();
  }
  if (digitalRead(PHOTO) == HIGH) {
    Serial.println("Capture Photo");
    takePhoto();
    delay(500);
    digitalWrite(PHOTO, LOW);
  }
  if (digitalRead(LOCK) == HIGH) {
    Serial.println("Unlock Door");
    digitalWrite(RED, LOW);
  }
  if (digitalRead(LOCK) == LOW) {
    digitalWrite(RED, HIGH);
  }

  int motion = digitalRead(PIR);                            //read the PIR sensor state
  if (motion == HIGH) cnt_pir += 1;
  if (motion == HIGH && !motionDetected && cnt_pir > 5) {   //if motion detected and not already detected
    Serial.println("Mon detected!");
    Blynk.logEvent("PIR", "Motion detected!");              //send notification to Blynk app
    motionDetected = true;                                  //set the flag to true
  }
  else if (motion == LOW && motionDetected) {               //if no motion detected and already detected
    Serial.println("Motion stopped.");
    motionDetected = false;                                 //set the flag to false
  }
}
