// --- CẤU HÌNH BLYNK ---
#define BLYNK_TEMPLATE_ID   "TMPL6B4RebayF"
#define BLYNK_TEMPLATE_NAME "ESMART"
#define BLYNK_AUTH_TOKEN    "Eu00rMy4Jd7S2XVyWmoaHktjTUBPbaxj"

#define BLYNK_PRINT Serial
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <Servo.h>

// Thông tin Wi-Fi của bạn
char ssid[] = "Xiaomi 13 Ultra";
char pass[] = "123456789";

// --- KHAI BÁO CHÂN KẾT NỐI ---
const int trigPin = D7;  
const int echoPin = D6;  
const int fireServoPin = D5;  // Servo bắn
const int aimServoPin = D8;   // Servo xoay

const int btnModePin     = D2;    // Nút đổi chế độ (Xoay vòng 0 -> 1 -> 2 -> 0)
const int btnFireModePin = D1;    
const int joystickBtnPin = D3;    // Nút nhấn trên Joystick (SW) dùng làm nút BẮN
const int btnPowerPin    = D4;    // Nút nguồn phần cứng
const int joystickPin    = A0;    // Cảm biến Joystick (Trục VRX)

Servo fireServo;
Servo aimServo;
BlynkTimer timer;

// --- BIẾN TRẠNG THÁI HỆ THỐNG ---
bool isSystemOn = true;         
int currentMode = 0;            // 0: Tự động, 1: Thủ công 1 (Tự động bắn), 2: Thủ công 2 (Bắn nút)
bool fireTriggerRequested = false; 

// Biến cho chế độ Tự động
int radarAngle = 90;           
int radarDirection = 1;       
unsigned long lastScanTime = 0;
const int scanInterval = 25;  

// Biến xử lý bắn 1 lần không dùng delay
bool isFiring = false;
unsigned long fireStartTime = 0;
int fireStateStep = 0; 

int currentDistance = 400;
bool autoFiredInZone = false;

int currentAimAngle = 90; 
unsigned long lastJoystickMoveTime = 0;

// Biến quản lý độ trễ 4 giây cho việc quay về/điều khiển
unsigned long lastSliderActionTime = 0;
const unsigned long actionDelay4s = 4000; // 4 giây

bool lastBtnModeState     = HIGH;
bool lastJoystickBtnState = HIGH;
bool lastPowerState       = HIGH;

unsigned long lastDebounceTimeMode = 0;
unsigned long lastDebounceTimeFire = 0;
unsigned long lastDebounceTimePower = 0;
const unsigned long debounceDelay = 200; 

void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  
  pinMode(btnModePin, INPUT_PULLUP);
  pinMode(btnFireModePin, INPUT_PULLUP);
  pinMode(joystickBtnPin, INPUT_PULLUP); 
  pinMode(btnPowerPin, INPUT_PULLUP);  

  aimServo.attach(aimServoPin);
  aimServo.write(90);    

  fireServo.attach(fireServoPin);
  fireServo.write(0);
  delay(300);
  fireServo.detach(); 

  Serial.begin(115200);
  delay(1000);

  WiFi.begin(ssid, pass);
  Blynk.config(BLYNK_AUTH_TOKEN);

  timer.setInterval(1000L, sendDataToBlynk); 
  Serial.println("--- HỆ THỐNG HOÀN CHỈNH ĐÃ SẴN SÀNG (ĐÃ CÓ DELAY 4S) ---");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }
  timer.run();

  // 1. NÚT NGUỒN PHẦN CỨNG
  bool currentPowerState = digitalRead(btnPowerPin);
  if (currentPowerState == LOW && lastPowerState == HIGH) {
    if (millis() - lastDebounceTimePower > debounceDelay) {
      isSystemOn = !isSystemOn;
      handleSystemPowerState();
      lastDebounceTimePower = millis();
    }
  }
  lastPowerState = currentPowerState;

  if (!isSystemOn) return; 

  // 2. NÚT ĐỔI CHẾ ĐỘ CỨNG (XOAY VÒNG 0 -> 1 -> 2 -> 0)
  bool currentBtnModeState = digitalRead(btnModePin);
  if (currentBtnModeState == LOW && lastBtnModeState == HIGH) {
    if (millis() - lastDebounceTimeMode > debounceDelay) {
      currentMode = (currentMode + 1) % 3; 
      radarAngle = 90;
      
      Serial.print("[CHẾ ĐỘ CỨNG] Đã chuyển sang: ");
      if (currentMode == 0) Serial.println("0 - TỰ ĐỘNG");
      else if (currentMode == 1) Serial.println("1 - THỦ CÔNG 1 (Ngắm + Tự động bắn NGAY)");
      else if (currentMode == 2) Serial.println("2 - THỦ CÔNG 2 (Ngắm + Bắn nút)");

      lastDebounceTimeMode = millis();
    }
  }
  lastBtnModeState = currentBtnModeState;

  // NÚT BẮN THỦ CÔNG (Nút trên Joystick SW)
  bool currentJoystickBtnState = digitalRead(joystickBtnPin);
  if (currentJoystickBtnState == LOW && lastJoystickBtnState == HIGH) {
    if (millis() - lastDebounceTimeFire > debounceDelay) {
      if (currentMode == 1 || currentMode == 2) {
        fireTriggerRequested = true;
      }
      lastDebounceTimeFire = millis();
    }
  }
  lastJoystickBtnState = currentJoystickBtnState;

  // ĐO KHOẢNG CÁCH SIÊU TỐC LIÊN TỤC KHÔNG ĐỘ TRỄ
  currentDistance = getDistance();

  // 3. PHÂN LUỒNG HOẠT ĐỘNG THEO 3 CHẾ ĐỘ
  if (currentMode == 0) {
    runAutoMode();
  } else if (currentMode == 1) {
    runManualMode1(); // Ngắm + Tự động bắn ngay
  } else if (currentMode == 2) {
    runManualMode2(); // Ngắm + Bắn nút
  }

  handleNonBlockingFire();

  if (fireTriggerRequested && !isFiring) {
    startFiring();
    fireTriggerRequested = false;
  }
}

// ================= CHẾ ĐỘ 0: TỰ ĐỘNG =================
void runAutoMode() {
  if (isFiring) return; 

  if (currentDistance > 0 && currentDistance <= 30) {
    aimServo.write(radarAngle); 
    startFiring();
    return; 
  }

  if (millis() - lastScanTime >= scanInterval) {
    lastScanTime = millis();
    aimServo.write(radarAngle);
    radarAngle += radarDirection;
    
    if (radarAngle >= 180) {
      radarAngle = 180;
      radarDirection = -1; 
    } else if (radarAngle <= 0) {
      radarAngle = 0;
      radarDirection =  1; 
    }
  }
}

// ================= CHẾ ĐỘ 1: THỦ CÔNG 1 (Ngắm + Tự động bắn NGAY) =================
void runManualMode1() {
  updateAimFromJoystick();
  aimServo.write(currentAimAngle);

  if (currentDistance > 0 && currentDistance <= 30) {
    if (!autoFiredInZone && !isFiring) {
      startFiring();
      autoFiredInZone = true; 
    }
  } else {
    autoFiredInZone = false; 
  }
}

// ================= CHẾ ĐỘ 2: THỦ CÔNG 2 (Ngắm + Bắn nút) =================
void runManualMode2() {
  updateAimFromJoystick();
  aimServo.write(currentAimAngle);
}

void updateAimFromJoystick() {
  int rawVal = analogRead(joystickPin); 
  if (rawVal >= 350 && rawVal <= 650) return; 

  if (millis() - lastJoystickMoveTime >= 15) {
    if (rawVal > 650) currentAimAngle += 2;
    else if (rawVal < 350) currentAimAngle -= 2;
    
    currentAimAngle = constrain(currentAimAngle, 0, 180);
    lastJoystickMoveTime = millis();
  }
}

void startFiring() {
  if (!isFiring) {
    isFiring = true;
    fireStateStep = 1;
    fireStartTime = millis();
    fireServo.attach(fireServoPin); 
    fireServo.write(90);            
  }
}

void handleNonBlockingFire() {
  if (!isFiring) return;
  unsigned long currentMillis = millis();

  if (fireStateStep == 1 && (currentMillis - fireStartTime >= 200)) { 
    fireServo.write(0); 
    fireStateStep = 2;
    fireStartTime = currentMillis;
  } 
  else if (fireStateStep == 2 && (currentMillis - fireStartTime >= 150)) {
    fireServo.detach(); 
    isFiring = false; 
    fireStateStep = 0;
  }
}

// ĐO KHOẢNG CÁCH SIÊU TỐC KHÔNG DELAY
int getDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 10000);
  if (duration == 0) return 400;
  return duration * 0.0343 / 2;
}

void handleSystemPowerState() {
  if (!isSystemOn) {
    fireServo.detach();   
    aimServo.write(90);   
    isFiring = false;
    fireStateStep = 0;
    fireTriggerRequested = false;
  }
}

void sendDataToBlynk() {
  Blynk.virtualWrite(V0, currentDistance);          
  Blynk.virtualWrite(V1, currentMode);              
  Blynk.virtualWrite(V2, isSystemOn ? 1 : 0);       
}

// Slider web V3 (Có áp dụng bộ lọc độ trễ 4 giây trước khi cho phép lệnh quay về/thực thi tiếp)
BLYNK_WRITE(V3) {
  if (currentMode == 1 || currentMode == 2) {
    // Kiểm tra nếu chưa qua thời gian 4 giây kể từ lần chỉnh trước thì bỏ qua lệnh mới
    if (millis() - lastSliderActionTime < actionDelay4s) {
      return; 
    }
    
    int sliderVal = param.asInt();
    currentAimAngle = sliderVal; 
    aimServo.write(currentAimAngle); 
    
    // Ghi nhận mốc thời gian vừa thực hiện thao tác
    lastSliderActionTime = millis();
  }
}

BLYNK_WRITE(V4) {
  if (currentMode == 2 && param.asInt() == 1) {
    fireTriggerRequested = true;
  }
}

BLYNK_WRITE(V5) {
  if (param.asInt() == 1) {
    currentMode = (currentMode + 1) % 3; 
    radarAngle = 90;
  }
}

BLYNK_WRITE(V6) {
  isSystemOn = (param.asInt() == 1);
  handleSystemPowerState();
}x
