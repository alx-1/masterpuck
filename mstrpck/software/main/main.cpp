#include <ableton/Link.hpp>
#include <driver/gpio.h>
#include <driver/timer.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <protocol_examples_common.h>
#include "driver/uart.h"
#include <stdio.h>
#include "esp_timer.h"
#include "esp_sleep.h"

#include <chrono> // essai pour setTempo()

extern "C" {
#include <string.h> // De l'exemple station_example_main
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include <stdbool.h> 
#include "lwip/err.h"
#include "lwip/sys.h"
#include <stdlib.h> // De l'exemple smart_config
#include "esp_wpa2.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "freertos/queue.h"
#include "esp_log.h"
}

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#define LED GPIO_NUM_2 
#define PRINT_LINK_STATE false
#define USE_TOUCH_PADS // touch_pad_7 (GPIO_NUM_27), touch_pad_8 (GPIO_NUM_33), touch_pad_9 (GPIO_NUM_32)
#define USE_I2C_DISPLAY // SDA GPIO_NUM21, SCL GPIO_NUM_22

// Serial midi
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_TXD  (GPIO_NUM_4)
#define ECHO_TEST_RXD  (GPIO_NUM_5)
#define BUF_SIZE (1024)
#define MIDI_TIMING_CLOCK 0xF8
#define MIDI_START 0xFA
#define MIDI_STOP 0xFC
#define MIDI_SONG_POSITION_POINTER 0xF2

// Global
static void periodic_timer_callback(void* arg);
esp_timer_handle_t periodic_timer;

bool startStopCB = false; // l'état du callback 
bool startStopState = false; // l'état local
bool changePiton = false; 
bool changeLink = false;
bool tempoINC = false; // si le tempo doit être augmenté
bool tempoDEC = false; // si le tempo doit être réduit
double newBPM; // pour tenter d'envoyer à setTempo();
double curr_beat_time;
double prev_beat_time;

/////////////////// I2C Display //////////////////
#if defined USE_I2C_DISPLAY
extern "C" {
#include "ssd1306.h"
#include "ssd1306_draw.h"
#include "ssd1306_font.h"
#include "ssd1306_default_if.h"

static const int I2CDisplayAddress = 0x3C;
static const int I2CDisplayWidth = 128;
static const int I2CDisplayHeight = 64;
static const int I2CResetPin = -1;

struct SSD1306_Device I2CDisplay;

bool DefaultBusInit( void ) {  
        assert( SSD1306_I2CMasterInitDefault( ) == true );
        assert( SSD1306_I2CMasterAttachDisplayDefault( &I2CDisplay, I2CDisplayWidth, I2CDisplayHeight, I2CDisplayAddress, I2CResetPin ) == true );
    return true;
}

void FontDisplayTask( void* Param ) {
    struct SSD1306_Device* Display = ( struct SSD1306_Device* ) Param;

    if ( Param != NULL ) {

        SSD1306_SetFont( Display, &Font_droid_sans_mono_13x24);
        SSD1306_Clear( Display, SSD_COLOR_BLACK );
        SSD1306_FontDrawAnchoredString( Display, TextAnchor_North, "BPM", SSD_COLOR_WHITE );
        SSD1306_FontDrawAnchoredString( Display, TextAnchor_Center, "66.6", SSD_COLOR_WHITE );
        SSD1306_Update( Display );
    }

    vTaskDelete( NULL );
}
    
void SetupDemo( struct SSD1306_Device* DisplayHandle, const struct SSD1306_FontDef* Font ) {
    SSD1306_Clear( DisplayHandle, SSD_COLOR_BLACK );
    SSD1306_SetFont( DisplayHandle, Font );
}

void SayHello( struct SSD1306_Device* DisplayHandle, const char* HelloText ) {
    SSD1306_FontDrawAnchoredString( DisplayHandle, TextAnchor_Center, HelloText, SSD_COLOR_WHITE );
    SSD1306_Update( DisplayHandle );
}
     
} 
#endif ////////////// END I2C Display init /////////////////////////


#if defined USE_TOUCH_PADS
extern "C" {
#include "driver/touch_pad.h" 
#include "soc/rtc_periph.h"
#include "soc/sens_periph.h"

static const char *TOUCH_TAG = "Touch pad";

#define TOUCH_THRESH_NO_USE   (0)
#define TOUCH_THRESH_PERCENT  (80)
#define TOUCHPAD_FILTER_TOUCH_PERIOD (10)

static bool s_pad_activated[2];
static uint32_t s_pad_init_val[2];

static void tp_example_set_thresholds(void)
{
    uint16_t touch_value;
     for (int i = 7; i < 10; i++) {
        touch_pad_read_filtered((touch_pad_t)i, &touch_value);
        s_pad_init_val[i] = touch_value;
        ESP_LOGI(TOUCH_TAG, "test init: touch pad [%d] val is %d", i, touch_value); //set interrupt threshold.
        ESP_ERROR_CHECK(touch_pad_set_thresh((touch_pad_t)i, touch_value * 2 / 3));
     }
}
static void tp_example_read_task(void *pvParameter) {
    
    static int show_message;
    
 while (1) {
     
    touch_pad_intr_enable();
    for (int i = 7; i < 10; i++) {
        if (s_pad_activated[7] == true) {
        //ESP_LOGI(TOUCH_TAG, "T%d activated!", 7);  // Wait a while for the pad being released
        tempoINC = true; // pour que le audio loop le prenne en compte
        vTaskDelay(300 / portTICK_PERIOD_MS);  // Clear information on pad activation
        s_pad_activated[7] = false; // Reset the counter triggering a message // that application is running
        show_message = 1;
        } else if (s_pad_activated[8] == true) {
        //ESP_LOGI(TOUCH_TAG, "T%d activated!", 8);  // Wait a while for the pad being released
        tempoDEC = true; // pour que le audio loop le prenne en compte
        vTaskDelay(300 / portTICK_PERIOD_MS);  // Clear information on pad activation
        s_pad_activated[8] = false; // Reset the counter triggering a message // that application is running
        show_message = 1;

        }
        else if (s_pad_activated[9] == true) {
        ESP_LOGI(TOUCH_TAG, "T%d piton!", 9);  // Wait a while for the pad being released
        startStopState = !startStopState; // inverse notre état, on veut changer localement
        changePiton = true;
        ESP_LOGI(TOUCH_TAG, "startStopState : %i ", startStopState);
        ESP_LOGI(TOUCH_TAG, "changePiton : %i ", changePiton);
        vTaskDelay(300 / portTICK_PERIOD_MS);  // Clear information on pad activation
        s_pad_activated[9] = false; // Reset the counter triggering a message // that application is running
        show_message = 1;
        }
    }
        
    vTaskDelay(10 / portTICK_PERIOD_MS);
     
    }
}


static void tp_example_rtc_intr(void *arg) { //  Handle an interrupt triggered when a pad is touched. Recognize what pad has been touched and save it in a table.
    uint32_t pad_intr = touch_pad_get_status();
    //clear interrupt
    touch_pad_clear_status();
    for (int i = 7; i < 10; i++) {
        if ((pad_intr >> i) & 0x01) {
            s_pad_activated[i] = true;
        }
    }
}


static void tp_example_touch_pad_init(void) { // Before reading touch pad, we need to initialize the RTC IO.
    for (int i = 7; i < 10; i++) {
        //init RTC IO and mode for touch pad.
        touch_pad_config((touch_pad_t)i, TOUCH_THRESH_NO_USE);
    }
}

}    
#endif


/////////////////// WiFI smart station config //////////////////////
#define EXAMPLE_ESP_WIFI_SSID      "link"
#define EXAMPLE_ESP_WIFI_PASS      "nidieunimaitre"
#define EXAMPLE_ESP_MAXIMUM_RETRY  2
bool goSMART = false;
bool goLINK = false;
static EventGroupHandle_t s_wifi_event_group; /* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_smartcfg_event_group; /* FreeRTOS event group to signal when smart config is done*/
TaskHandle_t xHandle;

#define WIFI_CONNECTED_BIT BIT0 // we are connected to the AP with an IP
#define WIFI_FAIL_BIT      BIT1  // we failed to connect after the maximum amount of retries 

static const int CONNECTED_BIT      = BIT0;  // est-ce nécessaire ?
static const int ESPTOUCH_DONE_BIT  = BIT1;  // depuis smart_config static const int ESPTOUCH_DONE_BIT = BIT1;

static const char *TAG = "smart wifi station";
static int s_retry_num = 0;

extern "C" {
  static void smartconfig_example_task(void * parm);
  } // depuis smart_config

extern "C" { 
  static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
  {
    // station_example
    
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START && goSMART == false) {
        ESP_LOGI(TAG,"tente une connection avec les crédentials en mémoire");
/////////À RECHANGER POUR LE SMART CONFIG !!!
         wifi_config_t wifi_config = { // whatever parce que C, c'est pas C++
      { EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS }
      };
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED && goSMART == false) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP failed....");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP && goSMART == false) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
      
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED && goSMART == true) {
        esp_wifi_connect();
        xEventGroupClearBits(s_smartcfg_event_group, CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED && goSMART == true) {
        ESP_LOGI(TAG, "on a de quoi !");
        esp_wifi_connect();
        xEventGroupClearBits(s_smartcfg_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP && goSMART == true) {
        ESP_LOGI(TAG, "on a de nouveau de quoi !");
        xEventGroupSetBits(s_smartcfg_event_group, CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE && goSMART == true) {
        ESP_LOGI(TAG, "Le Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL && goSMART == true) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD && goSMART == true) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            ESP_LOGI(TAG, "Is bssid_set true ?");
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid)); // ne semble pas copier les valeurs en mémoire ?
        memcpy(password, evt->password, sizeof(evt->password));

        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
        ESP_ERROR_CHECK( esp_wifi_connect() );

    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        ESP_LOGI(TAG, "ESPTOUCH DONE!");
        xEventGroupSetBits(s_smartcfg_event_group, ESPTOUCH_DONE_BIT);
    }
  } // fin extern "C"
}

extern "C" { static void smartconfig_example_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t smtcfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&smtcfg) );
    ESP_LOGI(TAG,"normalement on a démarré le smartconfig");
    while (1) {
        ESP_LOGI(TAG,"et puis là : ...");
        vTaskDelay(35000 / portTICK_PERIOD_MS); // besoin d'un long délai ça l'air
        uxBits = xEventGroupWaitBits(s_smartcfg_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        ESP_LOGI(TAG,"Euh");
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
    #if defined USE_I2C_DISPLAY   
        if ( DefaultBusInit( ) == true ) {
        printf( "BUS Init lookin good...\n" );
        printf( "Drawing.\n" );
        SetupDemo( &I2CDisplay, &Font_droid_sans_mono_13x24 );
        SayHello( &I2CDisplay, "CFG WiFI!" );
        printf( "Done!\n" );
   }
    #endif 
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            goLINK = true;
            esp_smartconfig_stop();
            ESP_LOGI(TAG,"this will print ok ");
            vTaskDelete( xHandle ); // tente de fermer le task correctement
            // ESP_LOGI(TAG,"this will not print as we exit beforehand");
            //vTaskGetRunTimeStats( xHandle );
            
            
        }
        
    }
  } // fin extern "C"
}

extern "C" { void wifi_init_sta(void)
{

  #if defined USE_I2C_DISPLAY   
        if ( DefaultBusInit( ) == true ) {
        printf( "BUS Init lookin good...\n" );
        SetupDemo( &I2CDisplay, &Font_droid_sans_mono_7x13 );
        SayHello( &I2CDisplay, "MSTRPCK" );
   }
   #endif

    s_wifi_event_group = xEventGroupCreate();
    s_smartcfg_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config;
    ESP_ERROR_CHECK(esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config));
    if(wifi_config.sta.ssid){ // on a des infos dans la mémoire NVS
        ESP_LOGI(TAG,"Tout va bien");
        ESP_LOGI(TAG,"MON SSID:%s",wifi_config.sta.ssid);
        ESP_LOGI(TAG,"MON PASSWORD:%s",wifi_config.sta.password);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );

/////////À ENLEVER POUR LE SMART CONFIG !!!
         wifi_config_t wifi_config = { // whatever parce que C, c'est pas C++
      { EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS }
      };
        
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
        ESP_ERROR_CHECK(esp_wifi_start() );
    
    }else{ // on n'a rien encore dans le nvs donc on utilise les identifiants 'EXAMPLE_ESP_WIFI_SSID'...
     
      wifi_config_t wifi_config = { // whatever parce que C, c'est pas C++
      { EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS }
      };

      ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
      ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
      ESP_ERROR_CHECK(esp_wifi_start() );
      }
  
    ESP_LOGI(TAG, "wifi_init_sta finished.");
    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "nous sommes connectés par WiFI");
        
        #if defined USE_I2C_DISPLAY   
        //if ( DefaultBusInit( ) == true ) {
        //printf( "BUS Init lookin good...\n" );
        //printf( "Drawing.\n" );
        SetupDemo( &I2CDisplay, &Font_droid_sans_mono_13x24 );
        SayHello( &I2CDisplay, "WiFI!" );
        //printf( "Done!\n" );
        //}
        #endif 
        
        // to ap SSID:%s password:%s",
        // wifi_config.sta.ssid, wifi_config.sta.password);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 wifi_config.sta.ssid, wifi_config.sta.password);
                 goSMART = true; // pour la suite des choses
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
   // ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
   // ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
    //vEventGroupDelete(s_wifi_event_group);
  } // fin extern "C"
}


/*
  Serial1.begin(31250);

#define MIDI_TIMING_CLOCK 0xF8
#define MIDI_START 0xFA
#define MIDI_STOP 0xFC
#define MIDI_SONG_POSITION_POINTER 0xF2

// START AND SET AT BEGINNING
Serial1.write(MIDI_START);
Serial1.write(MIDI_SONG_POSITION_POINTER);
Serial1.write(0);
Serial1.write(0);

// SEND TIMING CLOCK 24 TIMES PER BEAT
Serial1.write(MIDI_TIMING_CLOCK);*/

unsigned int if_nametoindex(const char* ifName)
{
  return 0;
}

char* if_indextoname(unsigned int ifIndex, char* ifName)
{
  return nullptr;
}

void IRAM_ATTR timer_group0_isr(void* userParam)
{
  static BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  TIMERG0.int_clr_timers.t0 = 1;
  TIMERG0.hw_timer[0].config.alarm_en = 1;

  xSemaphoreGiveFromISR(userParam, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken)
  {
    portYIELD_FROM_ISR();
  }
}

void timerGroup0Init(int timerPeriodUS, void* userParam)
{
  timer_config_t config = {.alarm_en = TIMER_ALARM_EN,
    .counter_en = TIMER_PAUSE,
    .intr_type = TIMER_INTR_LEVEL,
    .counter_dir = TIMER_COUNT_UP,
    .auto_reload = TIMER_AUTORELOAD_EN,
    .divider = 80};

  timer_init(TIMER_GROUP_0, TIMER_0, &config);
  timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
  timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, timerPeriodUS);
  timer_enable_intr(TIMER_GROUP_0, TIMER_0);
  timer_isr_register(TIMER_GROUP_0, TIMER_0, &timer_group0_isr, userParam, 0, nullptr);

  timer_start(TIMER_GROUP_0, TIMER_0);
}

void printTask(void* userParam)
{
  auto link = static_cast<ableton::Link*>(userParam);
  const auto quantum = 4.0;
 
  while (true)
  {
    const auto sessionState = link->captureAppSessionState();
    const auto numPeers = link->numPeers();
    const auto time = link->clock().micros();
    const auto beats = sessionState.beatAtTime(time, quantum);

    std::cout << std::defaultfloat << "| peers: " << numPeers << " | "
              << "tempo: " << sessionState.tempo() << " | " << std::fixed
              << "beats: " << beats << " |" << std::endl;
    vTaskDelay(800 / portTICK_PERIOD_MS);

  }
}

// callbacks
void tempoChanged(double tempo) {
    ESP_LOGI(TAG, "tempochanged");
    double midiClockMicroSecond = ((60000 / tempo) / 24) * 1000;

#if defined USE_I2C_DISPLAY
      char buf[10];
      snprintf(buf, 10 , "%i", (int) round( tempo ) );
      SSD1306_Clear( &I2CDisplay, SSD_COLOR_BLACK );
      SSD1306_SetFont( &I2CDisplay, &Font_droid_sans_mono_13x24);
      SSD1306_FontDrawAnchoredString( &I2CDisplay, TextAnchor_North, "BPM", SSD_COLOR_WHITE );
      SSD1306_FontDrawAnchoredString( &I2CDisplay, TextAnchor_Center, buf, SSD_COLOR_WHITE );
      SSD1306_Update( &I2CDisplay );   
#endif

    esp_timer_handle_t periodic_timer_handle = (esp_timer_handle_t) periodic_timer;
    ESP_ERROR_CHECK(esp_timer_stop(periodic_timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer_handle, midiClockMicroSecond));
}

void startStopChanged(bool state) {
  // received as soon as sent, we can get the state of 'isPlaying' and use that
  // need to wait for phase to be 0 (and deal with latency...)
  startStopCB = state;
  changeLink = true;

  //ESP_LOGI(TAG, "startstopCB is : %i", state);
  //ESP_LOGI(TAG, "changeLink is : %i", state);
  
}

void tickTask(void* userParam)
{
  // connect link
  ableton::Link link(120.0f);
  link.enable(true);
  link.enableStartStopSync(true); // if not no callback for start/stop

  // callbacks
  link.setTempoCallback(tempoChanged);
  link.setStartStopCallback(startStopChanged);

  // debug
  if (PRINT_LINK_STATE)
  {
    xTaskCreate(printTask, "print", 8192, &link, 1, nullptr);
  }
  
  gpio_set_direction(LED, GPIO_MODE_OUTPUT);

  // phase
  while (true)
  { 
    xSemaphoreTake(userParam, portMAX_DELAY);

    const auto state = link.captureAudioSessionState(); 
    //auto mySession = link.captureAppSessionState();

    //const auto phase = state.phaseAtTime(link.clock().micros(), 1); 
    //gpio_set_level(LED, fmodf(phase, 1.) < 0.3); 

    if ( tempoINC == true ) {
      const auto tempo = state.tempo(); // quelle est la valeur de tempo?
      newBPM = tempo + 1;
      ESP_LOGI(TAG, "BPM changed %i", int(newBPM));
      auto mySession = link.captureAppSessionState();
      const auto timez = link.clock().micros();
      mySession.setTempo(newBPM,timez); // setTempo()'s second arg format is : const std::chrono::microseconds atTime
      link.commitAppSessionState(mySession);
      tempoINC = false;
    }

    if ( tempoDEC == true ) {
      const auto tempo = state.tempo(); // quelle est la valeur de tempo?
      newBPM = tempo - 1;
      ESP_LOGI(TAG, "BPM changed %i", int(newBPM));
      auto mySession = link.captureAppSessionState();
      const auto timez = link.clock().micros();
      mySession.setTempo(newBPM,timez); // setTempo()'s second arg format is : const std::chrono::microseconds atTime
      link.commitAppSessionState(mySession);
      tempoDEC = false;
    }

    ////////// test start stop send to other clients /////////
    //ESP_LOGI(TAG, "PRE startStopState is :  %i", startStopState);  
    //ESP_LOGI(TAG, "PRE startStopCB is :  %i", startStopCB); 

    if ( changePiton && startStopState != startStopCB ){ // if local state is different to the SessionState then send to the session state 
        auto mySession = link.captureAppSessionState();
        const auto timez = link.clock().micros();
        mySession.setIsPlaying(startStopState, timez);
        link.commitAppSessionState(mySession);
      }
      
    if ( changeLink && startStopState != startStopCB ){ // if CB state is different to the local startStopState then resync the latter to the former
       startStopState = startStopCB; // resync 
      }

    //ESP_LOGI(TAG, "POST startStopState is :  %i", startStopState);  
    //ESP_LOGI(TAG, "POST startStopCB is :  %i", startStopCB); 
   
    curr_beat_time = state.beatAtTime(link.clock().micros(), 4);
    const double curr_phase = fmod(curr_beat_time, 4);
    if (curr_beat_time > prev_beat_time) {
      const double prev_phase = fmod(prev_beat_time, 4);
      const double prev_step = floor(prev_phase * 1);
      const double curr_step = floor(curr_phase * 1);
      //ESP_LOGI(TAG, "current step : %f", curr_step); 
      if (abs(curr_step - prev_step) > 0.5){

        gpio_set_level(LED, 1); 
      } else {

        gpio_set_level(LED, 0); 
      }
      

      if (prev_phase - curr_phase > 4 / 2 || prev_step != curr_step) {

        if( ( curr_step == 0 && changePiton ) || ( curr_step == 0 && changeLink ) ) {
        // if(curr_step == 0 && startStopState != startStopCB) {

              if(startStopCB) {
                char zedata[] = { MIDI_START };
                uart_write_bytes(UART_NUM_1, zedata, 1);
                uart_write_bytes(UART_NUM_1, 0, 1);
                changeLink = false;
                changePiton = false;
              } else {
                char zedata[] = { MIDI_STOP };
                uart_write_bytes(UART_NUM_1, zedata, 1);
                uart_write_bytes(UART_NUM_1, 0, 1);
                changeLink = false;
                changePiton = false;
              }
            }
         
    //ESP_LOGI(TAG, "POST changeLink :  %i", changeLink);  
    //ESP_LOGI(TAG, "POST changePiton :  %i", changePiton); 
        
      }
    }
    prev_beat_time = curr_beat_time;



    portYIELD();
  }
}

static void periodic_timer_callback(void* arg)
{
    char zedata[] = { MIDI_TIMING_CLOCK };
    uart_write_bytes(UART_NUM_1, zedata, 1);
}


extern "C" void app_main()
{
  //test();
  //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    
    wifi_init_sta();

    if (goSMART == true){
        //ESP_ERROR_CHECK(esp_wifi_stop());
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        //ESP_LOGI(TAG, "STOP STA");
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, &xHandle);
    }

  //if (goLINK == true){
    ESP_LOGI(TAG, "weeeeeiiiiiiird ##################");
    tcpip_adapter_init();
  
  //ESP_ERROR_CHECK(esp_event_loop_create_default());
  //ESP_ERROR_CHECK(example_connect());
  

  // serial
  uart_config_t uart_config = {
    .baud_rate = 31250, // midi speed
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 122,
  };
  uart_param_config(UART_NUM_1, &uart_config);
  uart_set_pin(UART_NUM_1, ECHO_TEST_TXD, ECHO_TEST_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 0, 0, NULL, 0);

  // link timer - phase
  SemaphoreHandle_t tickSemphr = xSemaphoreCreateBinary();
  timerGroup0Init(1000, tickSemphr);
  xTaskCreate(tickTask, "tick", 8192, tickSemphr, 1, nullptr);

  // midi clock
  const esp_timer_create_args_t periodic_timer_args = {
          .callback = &periodic_timer_callback,
          .name = "periodic"
  };
  ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 20833.333333333)); // 120 bpm by default
  
  #if defined USE_TOUCH_PADS
    ESP_LOGI(TOUCH_TAG, "Initializing touch pad");     // Initialize touch pad peripheral, it will start a timer to run a filter
    touch_pad_init();
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);   // If use interrupt trigger mode, should set touch sensor FSM mode at 'TOUCH_FSM_MODE_TIMER'.
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);// Set reference voltage for charging/discharging
    tp_example_touch_pad_init();    // Init touch pad IO
    touch_pad_filter_start(TOUCHPAD_FILTER_TOUCH_PERIOD); // Initialize and start a software filter to detect slight change of capacitance.
    tp_example_set_thresholds();     // Set thresh hold
    touch_pad_isr_register(tp_example_rtc_intr, NULL); // Register touch interrupt ISR
    xTaskCreate(&tp_example_read_task, "touch_pad_read_task", 2048, NULL, 5, NULL); // Start a task to show what pads have been touched
  #endif

  //} // fin de goLINK
}
