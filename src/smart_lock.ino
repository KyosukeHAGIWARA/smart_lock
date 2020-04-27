#include <Time.h>
#include <TimeLib.h>
#include <Servo.h>
#include <Adafruit_NeoPixel.h>
#include "RCS620S.h"
#include <inttypes.h>
#include <string.h>

// 各種定数
const int COMMAND_TIMEOUT = 400; // リーダのタイムアウト
const int POLLING_INTERVAL = 500; // 次回のpollingまでのクール時間
const int AUTO_LOCK_TIME = 600; // ドア閉からオートロックがかかるまでの時間 10分(600秒) MAXは1時間
const int LUCT_DEFAULT = 1410065407; // LUCTのデフォルト値

// サーボ動作角定数
const int ANGLE_OPENNEUTRAL = 120; // 開状態時のニュートラル
const int ANGLE_CLOSENEUTRAL = 60; // 閉状態時のニュートラル
const int ANGLE_OPEN = 150; // 開
const int ANGLE_CLOSE = 30; // 閉

// 登録するIDmのリスト
const char suica[] PROGMEM = "0000000000000001";
const char pasmo[] PROGMEM = "0000000000000002";
const char* const IDms[] PROGMEM = {  
    suica
    , pasmo
};

// 各種ピン番号アサイン
const int FET_PIN = 5; // サーボの電源供給FETのゲート
const int SERVO_PIN = 6; // サーボ信号線
const int SW_LOCK_PIN = 8; //施錠ボタン
const int SW_UNLOCK_PIN = 9; // 開錠ボタン
const int LED_PIN = 10; // インジケータ用のLED信号線
const int SW_DOOR_PIN = 12; // ドア開閉監視のリードスイッチ

// 各種インスタンス
Servo servo;
Adafruit_NeoPixel led1(1, LED_PIN, NEO_GRBW + NEO_KHZ800);
RCS620S reader;

bool is_locked; // ロック状態を保持
int num_felica; // 登録済みカードの枚数
time_t luct; // last_unlock_close_time 最後に開錠ドア閉状態になった時刻;


// セットアップ関数
void setup() {
    // 登録済みカードの枚数をカウント
    num_felica = sizeof(IDms) / sizeof(char*);

    // FETの初期化
    pinMode(FET_PIN, OUTPUT);   // MOSFET
    // SWの初期化
    pinMode(SW_LOCK_PIN, INPUT_PULLUP);
    pinMode(SW_UNLOCK_PIN, INPUT_PULLUP);
    pinMode(SW_DOOR_PIN, INPUT_PULLUP);

    //LEDの初期化
    pinMode(LED_PIN, OUTPUT);
    led1.begin();           // INITIALIZE NeoPixel led1 object (REQUIRED)/
    led1.show();            // Turn OFF all pixels ASAP
    led1.setBrightness(25); // Set BRIGHTNESS to about 1/10 (max = 255)

    // サーボの初期化
    servo.attach(SERVO_PIN);
    if (digitalRead(SW_DOOR_PIN) == HIGH){ 
      // 開いている
      door_unlock();
    }else{ 
      // 閉まっている
      door_lock();
    }
    
    // 時刻を初期化(相対時刻しか使わないので適当な値で開始)
    setTime(0, 0, 0, 1, 4, 2020);
    luct = now();
    
    // NFCリーダ RC-S620/Sの初期化
    Serial.begin(115200);      // シリアル通信を開始 RC-S620/S
    int ret = reader.initDevice();
    while (!ret) {}             // blocking
  
}

//　ループ関数
void loop() {

    // オートロックのロジック
    if (!is_locked && (digitalRead(SW_DOOR_PIN)==LOW)){
        // 開錠でドア閉な状態
        if (luct == LUCT_DEFAULT){
            // 初めて開錠ドア閉状態になったのでluct更新して計測開始
            luct = now();
        }else if((3600 > now() - luct) && (now() - luct > AUTO_LOCK_TIME)){
            // 指定秒数経ったのでオートロック
            flash_color(led1.Color(0, 255, 0), 2); // Red Flash
            door_lock();
        }else{
            // オートロックまでの待機時間中
        }
    }else{
        // 開錠ドア閉状態以外なのでluctをリセット
        luct = LUCT_DEFAULT;
    }


    // 開錠施錠状態でLED色を変化
    if (is_locked){
        // ロックされてれば赤
        change_color(led1.Color(0, 255, 0)); // Red
    }else{
        // ロックされてなければ青
        change_color(led1.Color(0, 0, 255)); // Blue
    }
    

    // 各種スイッチ入力を監視
    int v = digitalRead(SW_UNLOCK_PIN);
    if (is_locked && (v == LOW) ){
        // ロックされてて開錠ボタンが押された ので開錠する
        flash_color(led1.Color(0, 0, 255), 2); // Blue Flash
        door_unlock();
    }
    v = digitalRead(SW_LOCK_PIN);
    if (!is_locked && (v ==  LOW)){
        // ロックされておらず施錠ボタンが押された ので施錠する
        flash_color(led1.Color(0, 255, 0), 2); // Red Flash
        door_lock();
    }
    v = digitalRead(SW_DOOR_PIN);
    if (v == HIGH){ // 
        if(is_locked){
          // ロックされてるはずなのに扉が開いてる
          // 外から物理鍵で開けた場合とか 
          // ので開錠する
          flash_color(led1.Color(255, 0, 255), 10); // BlueWhite Flash
          door_unlock();
        }
    }

    // リーダのPollingロジック
    reader.timeout = COMMAND_TIMEOUT;
    int ret = reader.polling();
    if(ret) {
        // 以下カードがかざされたときの処理
        
        String IDm = "";
        char buf[30];
        // IDmを取得する
        for(int i = 0; i < 8; i++){
            sprintf(buf, "%02X", reader.idm[i]);
            IDm += buf;
        }

        // 登録済みかを調べて分岐
        if(is_registered_card(IDm)){
            // 登録済みのIDmカード
            flash_color(led1.Color(255, 0, 0), 3); // Green Flash
            if(is_locked){
                door_unlock();
            }else{
                door_lock();
            }
        }else{
            // 未登録のIDmカード  
            flash_color(led1.Color(0, 255, 255), 3); // Red Flash
        }
    }
    reader.rfOff();

    //一定秒数待つ
    delay(POLLING_INTERVAL);

}

// 開錠する
void door_unlock(){
    change_color(led1.Color(255, 255, 0)); // Green サーボ動作中は緑色
    digitalWrite(FET_PIN, HIGH); // FETオンにしてサーボに電源供給
    servo.write(ANGLE_OPEN);
    delay(1000);
    servo.write(ANGLE_OPENNEUTRAL);
    delay(1000);
    digitalWrite(FET_PIN, LOW);　// FETをオフ
    is_locked = false;
    change_color(led1.Color(0, 0, 0));
}

// 施錠する
void door_lock(){
    change_color(led1.Color(255, 255, 0)); // Green　サーボ動作中は緑色
    digitalWrite(FET_PIN, HIGH);　// FETオンにしてサーボに電源供給
    servo.write(ANGLE_CLOSE);
    delay(1000);
    servo.write(ANGLE_CLOSENEUTRAL);
    delay(1000);
    digitalWrite(FET_PIN, LOW);　// FETをオフ
    is_locked = true;
    change_color(led1.Color(0, 0, 0));
}

// カードIDmを登録済みリストと照合してboolを返す
bool is_registered_card(String IDm){
    char  IDm_buf[17];
    for (int i = 0; i < num_felica; i++) {
        strcpy_P(IDm_buf, (char*) pgm_read_word(&IDms[i]));
        if (IDm.compareTo(IDm_buf) == 0) {
            return true;
        }
    }
    return false;
}

// LEDの色をセットする
void change_color(uint32_t color) {
    led1.setPixelColor(0, color);         //  Set pixel's color (in RAM)
    led1.show();                          //  Update led1 to match  
}

// LEDを指定色で指定回数高速点滅する
void flash_color(uint32_t color, int flash_num) {
    for (int i = 0; i < flash_num; i++) {
        led1.setPixelColor(0, color);         //  Set pixel's color (in RAM)
        led1.show();                          //  Update led1 to match  
        delay(50);
        led1.setPixelColor(0, led1.Color(0, 0, 0));         //  Set pixel's color (in RAM)
        led1.show();                          //  Update led1 to match  
        delay(50);
    }

}