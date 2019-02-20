/* Seismic sensor
 *  
 *  - connect to Wifi
 *    - presetup
 *    - or create an access point for you to setup the wifi
 *  - read 3 axis digital accelerometer sensor
 *  - store into SPIFFS memory
 *  - make available these data onto a webserver
 *  - send an email if the data are bigger than the threshold
 *  
 *  ToDo:
 *    - Add sd datalogging
 *    - Stabilise web server (simplify)
 *    - non blocking email sending and SPIFFS writting
 *    - autoreconnect
 *    - auto restart
 *    - save only alarm
 *    - txt module
 *    - faster oled communication
 *    - 
 *  
*/
#include <WiFi.h>
#include <WiFiClient.h>
#include "FS.h"
#include "SPIFFS.h"
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiManager.h> 
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include "Gsender_32.h"

WiFiManager wifiManager;

#include <time.h>        

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <U8x8lib.h>

/* Assign a unique ID to this sensor at the same time */
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

// Initialize the OLED display using Wire library
#define SDA_PIN 4// GPIO21 -> SDA
#define SCL_PIN 15// GPIO22 -> SCL
#define RST_PIN 16
U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(SCL_PIN, SDA_PIN, RST_PIN);

String version = "v1.1";              // Version of this program
String identification = "Test unit";  // Identification of the module, location
const char* ssid     = "WWBYOD";
const char* password = "EiR226*?!nKY";
#define THRESHOLD     1               // m/s2 value of threshold, if movement bigger than that send email

String site_width = "1023";
WiFiClient client;

WebServer server(80); // Start server on port 80 (default for a web-browser, change to your requirements, e.g. 8080 if your Router uses port 80

int       log_time_unit  = 50;  // default is 1-minute between readings, 10=15secs 40=1min 200=5mins 400=10mins 2400=1hr 
int       time_reference = 60;  // Time reference for calculating /log-time (nearly in secs) to convert to minutes
int const table_size     = 72;  // 80 is about the maximum for the available memory and Google Charts, based on 3 samples/hour * 24 * 1 day = 72 displayed, but not stored!
int       index_ptr, timer_cnt, log_interval, log_count, scale_max, scale_min;
String    webpage,time_now,log_time,lastcall,time_str, DataFile = "datalog.txt";
bool      AScale, auto_smooth, AUpdate, log_delete_approved;
float     x, y, z;
float     extrem_x = 0, extrem_y = 0, extrem_z = 0;
float     absExtrem_x,absExtrem_y,absExtrem_z;
float     last_x = 0, last_y = 0, last_z = 0;
int       last_millis = 0;
String    stringX = "X: ",stringY = "Y: ",stringZ = "Z: ";
bool      first_time_after_boot = 1;
int       alarm_cnt=0;

typedef signed short sint16;
typedef struct {
  int     lcnt; // Sequential log count
  String ltime; // Time record of when reading was taken
  sint16 x;  // x values, short unsigned 16-bit integer to reduce memory requirement, saved as x10 more to preserve 0.1 resolution
  sint16 y;  // y values, short unsigned 16-bit integer to reduce memory requirement, saved as x10 more to preserve 0.1 resolution
  sint16 z;  // z values, short unsigned 16-bit integer to reduce memory requirement, saved as x10 more to preserve 0.1 resolution
} record_type;

record_type sensor_data[table_size+1]; // Define the data array

void setup() {
  Serial.begin(115200);
  init_display();
  
  u8x8.setCursor(0, 2);u8x8.clearLine(2);u8x8.print("Connect to Wifi");   
  WiFi.begin(ssid, password);
  if(WiFi.status() != WL_CONNECTED){
    //first parameter is name of access point, second is the password
    u8x8.setCursor(0, 2);u8x8.clearLine(2);u8x8.print("WifiManager");
    wifiManager.autoConnect("WetaSiesmic", "simple123456");
  }
  
  u8x8.setCursor(0, 2);u8x8.clearLine(2);u8x8.print("Start SPIFFS");   
  StartSPIFFS();
  
  u8x8.setCursor(0, 2);u8x8.clearLine(2);u8x8.print("Start Time");   
  StartTime();
  
  Serial.println(F("WiFi connected"));
  u8x8.setCursor(0, 2);u8x8.clearLine(2);u8x8.print("Start Server");   
  server.begin(); 
  Serial.println(F("Webserver started")); // Start the webserver
  Serial.println("Use this URL to connect: http://"+WiFi.localIP().toString()+"/");// Print the IP address
  u8x8.setCursor(0, 7);u8x8.print(WiFi.localIP().toString());   
  //----------------------------------------------------------------------
  server.on("/",      display_x_y_z); // The client connected with no arguments e.g. http:192.160.0.40/
  server.on("/XYZ",    display_x_y_z);
  //server.on("/DV",    display_dial);
  server.on("/AS",    auto_scale);
  server.on("/AU",    auto_update);
  server.on("/Setup", systemSetup);
  server.on("/Help",  help);
  server.on("/LgTU",  logtime_up);
  server.on("/LgTD",  logtime_down);
  server.on("/LogV",  LOG_view);
  server.on("/LogE",  LOG_erase);
  server.on("/LogS",  LOG_stats);
  
  u8x8.setCursor(0, 2);u8x8.clearLine(2);u8x8.print("Server begin..");   
  server.begin();
  Serial.println(F("Webserver started")); // Start the webserver
  u8x8.setCursor(0, 2);u8x8.clearLine(2);u8x8.print("Reading SPIFFS..");   
  
  index_ptr    = 0;     // The array pointer that varies from 0 to table_size  
  log_count    = 0;     // Keeps a count of readings taken
  AScale       = true; // Google charts can AScale axis, this switches the function on/off
  scale_max     = 10;    // Maximum displayed 
  scale_min     = -10;   // Minimum displayed 
  auto_smooth  = false; // If true, transitions of more than 10% between readings are smoothed out, so a reading followed by another that is 10% higher or lower is averaged 
  AUpdate      = true;  // Used to prevent a command from continually auto-updating, for example increase temp-scale would increase every 30-secs if not prevented from doing so.
  lastcall     = "x_y_z";      // To determine what requested the AScale change
  log_interval = log_time_unit*10; // inter-log time interval, default is 5-minutes between readings, 10=15secs 40=1min 200=5mins 400=10mins 2400=1hr 
  timer_cnt    = log_interval + 1; // To trigger first table update, essential
  update_log_time();           // Update the log_time
  log_delete_approved = false; // Used to prevent accidental deletion of card contents, requires two approvals
  reset_array();               // Clear storage array before use 
  prefill_array();             // Load old data from FS back into readings array
  //Serial.println(system_get_free_heap_size()); // diagnostic print to check for available RAM

  u8x8.setCursor(0,2);u8x8.clearLine(2);u8x8.print("Init ADXL345");   
  init_adxl345();
  u8x8.clearLine(2);
}

void loop() {
  server.handleClient();
  /* Get a new sensor event */ 
  sensors_event_t event; 
  accel.getEvent(&event);
  
  x = event.acceleration.x;
  y = event.acceleration.y;
  z = event.acceleration.z;
  
  if(first_time_after_boot){
    // remenber last value
    extrem_x = x;
    extrem_y = y;
    extrem_z = z;
    last_x = extrem_x;
    last_y = extrem_y;
    last_z = extrem_z;
    first_time_after_boot=0;
  }
  
  // record only biggest value (todo : remove 9.8 on the hearth axis)
  if(abs(x)>abs(extrem_x)){
    extrem_x = x;
  }
  if(abs(y)>abs(extrem_y)){
    extrem_y = y;
  }
  if(abs(z)>abs(extrem_z)){
    extrem_z = z;
  }

  time_t now = time(nullptr); 
  time_now = String(ctime(&now)).substring(0,24); // Remove unwanted characters
  if ((time_now != "Thu Jan 01 00:00:00 1970") && (timer_cnt >= log_interval)) { // If time is not yet set, returns 'Thu Jan 01 00:00:00 1970') so wait. 
    timer_cnt = 0;  // log_interval values are 10=15secs 40=1min 200=5mins 400=10mins 2400=1hr
    log_count += 1; // Increase logging event count
    sensor_data[index_ptr].lcnt  = log_count;  // Record current log number, time, temp and humidity readings 
    sensor_data[index_ptr].x  = extrem_x*10;
    sensor_data[index_ptr].y  = extrem_y*10;
    sensor_data[index_ptr].z  = extrem_z*10;
    sensor_data[index_ptr].ltime = calcDateTime(time(&now)); // time stamp of reading 'dd/mm/yy hh:mm:ss'
    File datafile = SPIFFS.open("/"+DataFile, FILE_APPEND);
    time_t now = time(nullptr);
    if (datafile == true) { // if the file is available, write to it
      datafile.println(((log_count<10)?"0":"")+String(log_count)+char(9)+String(x/10,2)+char(9)+String(y/10,2)+char(9)+String(z/10,2)+char(9)+calcDateTime(time(&now))+"."); // TAB delimited
        Serial.println(((log_count<10)?"0":"")+String(log_count)+" New Record Added");
    }
    datafile.close();
    index_ptr += 1; // Increment data record pointer
    if (index_ptr > table_size) { // if number of readings exceeds max_readings (e.g. 100) shift all data to the left and effectively scroll the display left
      index_ptr = table_size;
      for (int i = 0; i < table_size; i++) { // If data table is full, scroll all readings to the left in graphical terms, then add new reading to the end
        sensor_data[i].lcnt  = sensor_data[i+1].lcnt;
        sensor_data[i].x  = sensor_data[i+1].x;
        sensor_data[i].y  = sensor_data[i+1].y;
        sensor_data[i].z  = sensor_data[i+1].z;
        sensor_data[i].ltime = sensor_data[i+1].ltime;
      }
      sensor_data[table_size].lcnt  = log_count;
      sensor_data[table_size].x  = x;
      sensor_data[table_size].y  = y;
      sensor_data[table_size].z  = z;
      sensor_data[table_size].ltime = calcDateTime(time(&now));
    }
    String string=stringX + add_right_padding(extrem_x) + "\n" + stringY + add_right_padding(extrem_y) + "\n" + stringZ + add_right_padding(extrem_z); 
    // display on Oled
    u8x8.setCursor(0, 2);
    u8x8.print(string);
    Serial.println(string);
    // compare biggest value with latest biggest value to see if that moved
    if((abs(extrem_x)-abs(last_x))>THRESHOLD || (abs(extrem_y)-abs(last_y))>THRESHOLD || (abs(extrem_z)-abs(last_z))>THRESHOLD){
      alarm_cnt++;
      Serial.print("Alarm: ");Serial.println(alarm_cnt);
      u8x8.setCursor(0, 6);u8x8.print("Alarm: ");u8x8.print(alarm_cnt);
      
      Gsender *gsender = Gsender::Instance();    // Getting pointer to class instance
      if(gsender->Subject("Seismic alert from " + identification)->Send("siesmic.wetaworkshop@gmail.com", string + "  Alarm cnt: " + alarm_cnt)) {
        Serial.println("Message send.");
      } else {
        Serial.print("Error sending message: ");
        Serial.println(gsender->getError());
      }
    }
    // remenber last value
    last_x = extrem_x;
    last_y = extrem_y;
    last_z = extrem_z;
    // reset extrem value for next slot
    extrem_x = 0;
    extrem_y = 0;
    extrem_z = 0;
    Serial.println(millis()-last_millis); // Used to measure inter-sample timing
  }
  timer_cnt += 1; // Readings set by value of log_interval each 40 = 1min
  last_millis = millis();
  delay(1);     // Delay before next check for a client, adjust for 1-sec repeat interval. Temperature readings take some time to complete.
}

void init_display(){
  // Initialising the UI will init the display too.
  u8x8.begin();
  u8x8.setPowerSave(0);
  delay(1000);
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  u8x8.setCursor(0, 0);
  u8x8.println("Seismic sensor");
}

void init_adxl345(){
  if(!accel.begin())
  {
    /* There was a problem detecting the ADXL345 ... check your connections */
    Serial.println("Ooops, no ADXL345 detected ... Check your wiring!");
    u8x8.setCursor(0, 2);u8x8.clearLine(2);u8x8.println("No ADXL345...");
    while(1);
  }

  /* Set the range to whatever is appropriate for your project */
  accel.setRange(ADXL345_RANGE_16_G);
  // accel.setRange(ADXL345_RANGE_8_G);
  // accel.setRange(ADXL345_RANGE_4_G);
  // accel.setRange(ADXL345_RANGE_2_G);
  
  /* Display some basic information on this sensor */
  displaySensorDetails();
  
  /* Display additional settings (outside the scope of sensor_t) */
  displayDataRate();
  displayRange();
  Serial.println("");
}

void displaySensorDetails(void)
{
  sensor_t sensor;
  accel.getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.print  ("Sensor:       "); Serial.println(sensor.name);
  Serial.print  ("Driver Ver:   "); Serial.println(sensor.version);
  Serial.print  ("Unique ID:    "); Serial.println(sensor.sensor_id);
  Serial.print  ("Max Value:    "); Serial.print(sensor.max_value); Serial.println(" m/s^2");
  Serial.print  ("Min Value:    "); Serial.print(sensor.min_value); Serial.println(" m/s^2");
  Serial.print  ("Resolution:   "); Serial.print(sensor.resolution); Serial.println(" m/s^2");  
  Serial.println("------------------------------------");
  Serial.println("");
  delay(500);
}

void displayDataRate(void)
{
  Serial.print  ("Data Rate:    "); 
  
  switch(accel.getDataRate())
  {
    case ADXL345_DATARATE_3200_HZ:
      Serial.print  ("3200 "); 
      break;
    case ADXL345_DATARATE_1600_HZ:
      Serial.print  ("1600 "); 
      break;
    case ADXL345_DATARATE_800_HZ:
      Serial.print  ("800 "); 
      break;
    case ADXL345_DATARATE_400_HZ:
      Serial.print  ("400 "); 
      break;
    case ADXL345_DATARATE_200_HZ:
      Serial.print  ("200 "); 
      break;
    case ADXL345_DATARATE_100_HZ:
      Serial.print  ("100 "); 
      break;
    case ADXL345_DATARATE_50_HZ:
      Serial.print  ("50 "); 
      break;
    case ADXL345_DATARATE_25_HZ:
      Serial.print  ("25 "); 
      break;
    case ADXL345_DATARATE_12_5_HZ:
      Serial.print  ("12.5 "); 
      break;
    case ADXL345_DATARATE_6_25HZ:
      Serial.print  ("6.25 "); 
      break;
    case ADXL345_DATARATE_3_13_HZ:
      Serial.print  ("3.13 "); 
      break;
    case ADXL345_DATARATE_1_56_HZ:
      Serial.print  ("1.56 "); 
      break;
    case ADXL345_DATARATE_0_78_HZ:
      Serial.print  ("0.78 "); 
      break;
    case ADXL345_DATARATE_0_39_HZ:
      Serial.print  ("0.39 "); 
      break;
    case ADXL345_DATARATE_0_20_HZ:
      Serial.print  ("0.20 "); 
      break;
    case ADXL345_DATARATE_0_10_HZ:
      Serial.print  ("0.10 "); 
      break;
    default:
      Serial.print  ("???? "); 
      break;
  }  
  Serial.println(" Hz");  
}

void displayRange(void)
{
  Serial.print  ("Range:         +/- "); 
  
  switch(accel.getRange())
  {
    case ADXL345_RANGE_16_G:
      Serial.print  ("16 "); 
      break;
    case ADXL345_RANGE_8_G:
      Serial.print  ("8 "); 
      break;
    case ADXL345_RANGE_4_G:
      Serial.print  ("4 "); 
      break;
    case ADXL345_RANGE_2_G:
      Serial.print  ("2 "); 
      break;
    default:
      Serial.print  ("?? "); 
      break;
  }  
  Serial.println(" g");  
}

String add_right_padding(float value){
  String string="";
  if(value<-10.00){
    string = " ";
  }
  else if(value<0.00){
    string = "  ";
  }
  else if(value<10.00){
    string = "   ";
  }
  else if(value<100.00){
    string = "  ";
  }
  string += value;
  return string;
}

void prefill_array(){ // After power-down or restart and if the FS has readings, load them back in
  File datafile = SPIFFS.open("/"+DataFile, FILE_READ);
  while (datafile.available()) { // if the file is available, read from it
    int read_ahead = datafile.parseInt(); // Sometimes at the end of file, NULL data is returned, this tests for that
    if (read_ahead != 0) { // Probably wasn't null data to use it, but first data element could have been zero and there is never a record 0!
      sensor_data[index_ptr].lcnt  = read_ahead ;
      sensor_data[index_ptr].x  = datafile.parseFloat()*10;
      sensor_data[index_ptr].y  = datafile.parseFloat()*10;
      sensor_data[index_ptr].z  = datafile.parseFloat()*10;
      sensor_data[index_ptr].ltime = datafile.readStringUntil('.');
      sensor_data[index_ptr].ltime.trim();
      index_ptr += 1;
      log_count += 1;
    }
    if (index_ptr > table_size) {
      for (int i = 0; i < table_size; i++) {
         sensor_data[i].lcnt  = sensor_data[i+1].lcnt;
         sensor_data[i].x  = sensor_data[i+1].x;
         sensor_data[i].y  = sensor_data[i+1].y;
         sensor_data[i].z  = sensor_data[i+1].z;
         sensor_data[i].ltime = sensor_data[i+1].ltime;
         sensor_data[i].ltime.trim();
      }
      index_ptr = table_size;
    }
  }
  datafile.close();
  // Diagnostic print to check if data is being recovered from SPIFFS correctly
  for (int i = 0; i <= index_ptr; i++) {
    Serial.println(((i<10)?"0":"")+String(sensor_data[i].lcnt)+" "+String(sensor_data[i].x)+" "+String(sensor_data[i].y)+" "+String(sensor_data[i].z)+" "+String(sensor_data[i].ltime));
  }
  datafile.close();
  if (auto_smooth) { // During restarts there can be a discontinuity in readings, giving a spike in the graph, this smooths that out, off by default though
    // At this point the array holds data from the FS, but sometimes during outage and resume, reading discontinuity occurs, so try to correct those.
    float last_x,last_y,last_z;
    for (int i = 1; i < table_size; i++) {
      last_x = sensor_data[i].x;
      last_y = sensor_data[i].y;
      last_z = sensor_data[i].z;
      // Correct next reading if it is more than 10% different from last values
      if ((sensor_data[i+1].x > (last_x * 1.1)) || (sensor_data[i+1].x < (last_x * 1.1))) sensor_data[i+1].x = (sensor_data[i+1].x+last_x)/2; // +/-1% different then use last value
      if ((sensor_data[i+1].y > (last_y * 1.1)) || (sensor_data[i+1].y < (last_y * 1.1))) sensor_data[i+1].y = (sensor_data[i+1].y+last_y)/2; 
      if ((sensor_data[i+1].z > (last_z * 1.1)) || (sensor_data[i+1].z < (last_z * 1.1))) sensor_data[i+1].z = (sensor_data[i+1].z+last_z)/2; 
    }
  }
  Serial.println("Restored data from SPIFFS");
} 

void display_x_y_z() { // Processes a clients request for a graph of the data
  // See google charts api for more details. To load the APIs, include the following script in the header of your web page.
  // <script type="text/javascript" src="https://www.google.com/jsapi"></script>
  // To autoload APIs manually, you need to specify the list of APIs to load in the initial <script> tag, rather than in a separate google.load call for each API. For instance, the object declaration to auto-load version 1.0 of the Search API (English language) and the local search element, would look like: {
  // This would be compressed to: {"modules":[{"name":"search","version":"1.0","language":"en"},{"name":"elements","version":"1.0","packages":["
  // See https://developers.google.com/chart/interactive/docs/basic_load_libs
  log_delete_approved = false; // Prevent accidental SD-Card deletion
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  // https://developers.google.com/loader/ // https://developers.google.com/chart/interactive/docs/basic_load_libs
  // https://developers.google.com/chart/interactive/docs/basic_preparing_data
  // https://developers.google.com/chart/interactive/docs/reference#google.visualization.arraytodatatable and See appendix-A
  // data format is: [field-name,field-name,field-name] then [data,data,data], e.g. [12, 20.5, 70.3]
  webpage += F("<script type=\"text/javascript\" src=\"https://www.google.com/jsapi?autoload={'modules':[{'name':'visualization','version':'1','packages':['corechart']}]}/\"></script>");
  webpage += F("<script type=\"text/javascript\"> google.setOnLoadCallback(drawChart);");
  webpage += F("function drawChart() {");
  webpage += F("var data = google.visualization.arrayToDataTable([");
  webpage += F("['Reading','X','Y','Z'],\n");
  for (int i = 0; i < index_ptr; i=i+2) { // Can't display all data points!!!
     webpage += "['"+String(i)+"',"+String(sensor_data[i].x/10.00F,2) + ","+String(sensor_data[i].y/10.00F,2) + ","+String(sensor_data[i].z/10.00F,2)+"],"; 
  }
  webpage += "]);\n";
//-----------------------------------
  webpage += F("var options = {");
  webpage += F("title:'X,Y & Z',titleTextStyle:{fontName:'Arial', fontSize:20, color: 'Maroon'},");
  webpage += F("legend:{position:'bottom'},colors:['red','blue','green'],backgroundColor:'#F3F3F3',chartArea:{width:'85%',height:'55%'},"); 
  webpage += F("hAxis:{titleTextStyle:{color:'Purple',bold:true,fontSize:16},gridlines:{color:'#333'},showTextEvery:5,title:'");
  webpage += log_time + "'},";
  // Uncomment next line to display x-axis in time units, but Google Charts can't cope with much data, may be just a few readings!
  //webpage += "minorGridlines:{fontSize:8,format:'d/M/YY',units:{hours:{format:['hh:mm a','ha']},minutes:{format:['HH:mm a Z', ':mm']}}},";
  //minorGridlines:{units:{hours:{format:['hh:mm a','ha']},minutes:{format:['HH:mm a Z', ':mm']}}  to display  x-axis in count units
  webpage += F("vAxis:");
  if (AScale) {
    webpage += F("{viewWindowMode:'explicit',gridlines:{color:'black'}, title:'X,Y,Z m/s^2',format:'###.##'},");  
  }
  else {
    webpage += F("{viewWindowMode:'explicit',gridlines:{color:'black'}, title:'X,Y,Z m/s^2',format:'###.##',viewWindow:{min:");
    webpage += String(scale_min)+",max:"+String(scale_max)+"},},";
  } 
  webpage += F("};");
  webpage += F("var chart = new google.visualization.LineChart(document.getElementById('line_chart'));chart.draw(data, options);");
  webpage += F("}");
  webpage += F("</script>");
  webpage += F("<div id='line_chart' style='width:1020px; height:500px'></div>");
  append_page_footer();
  server.send(200, "text/html", webpage);
  webpage = "";
  lastcall = "x_y_z";
}

void reset_array() {
  for (int i = 0; i <= table_size; i++) {
    sensor_data[i].lcnt  = 0;
    sensor_data[i].x  = 0;
    sensor_data[i].y  = 0;
    sensor_data[i].z  = 0;
    sensor_data[i].ltime = "";
  }
}

// After the data has been displayed, select and copy it, then open Excel and Paste-Special and choose Text, then select, then insert graph to view
void LOG_view() {
  File datafile = SPIFFS.open("/"+DataFile, FILE_READ); // Now read data from FS
  if (datafile) {
    if (datafile.available()) { // If data is available and present
      String dataType = "application/octet-stream";
      if (server.streamFile(datafile, dataType) != datafile.size()) {Serial.print(F("Sent less data than expected!")); }
    }
  }
  datafile.close(); // close the file:
  webpage = "";
}  

void LOG_erase() { // Erase the datalog file
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  if (AUpdate) webpage += "<meta http-equiv='refresh' content='30'>"; // 30-sec refresh time and test is needed to stop auto updates repeating some commands
  if (log_delete_approved) {
    if (SPIFFS.exists("/"+DataFile)) {
      SPIFFS.remove("/"+DataFile);
      Serial.println(F("File deleted successfully"));
    }
    webpage += "<h3 style=\"color:orange;font-size:24px\">Log file '"+DataFile+"' has been erased</h3>";
    log_count = 0;
    index_ptr = 0;
    timer_cnt = 2000; // To trigger first table update, essential
    log_delete_approved = false; // Re-enable FS deletion
  }
  else {
    log_delete_approved = true;
    webpage += "<h3 style=\"color:orange;font-size:24px\">Log file erasing is now enabled, repeat this option to erase the log. Graph or Dial Views disable erasing again</h3>";
  }
  append_page_footer();
  server.send(200, "text/html", webpage);
  webpage = "";
}

void LOG_stats(){  // Display file size of the datalog file
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  File datafile = SPIFFS.open("/"+DataFile,FILE_READ); // Now read data from FS
  webpage += "<h3 style=\"color:orange;font-size:24px\">Data Log file size = "+String(datafile.size())+"-Bytes</h3>";
  webpage += "<h3 style=\"color:orange;font-size:24px\">Number of readings = "+String(log_count)+"</h3>";  
  datafile.close();
  append_page_footer();
  server.send(200, "text/html", webpage);
  webpage = "";
}

void auto_scale () { // Google Charts can auto-scale graph axis, this turns it on/off
  if (AScale) AScale = false; else AScale = true;
  if (lastcall == "x_y_z") display_x_y_z();
}

void auto_update () { // Google Charts can auto-scale graph axis, this turns it on/off
  if (AUpdate) AUpdate = false; else AUpdate = true;
  if (lastcall == "x_y_z") display_x_y_z();
}

void logtime_down () {  // Timer_cnt delay values 1=15secs 4=1min 20=5mins 40=10mins 240=1hr, increase the values with this function
  log_interval -= log_time_unit;
  if (log_interval < log_time_unit) log_interval = log_time_unit;
  update_log_time();
  if (lastcall == "x_y_z") display_x_y_z();
}

void logtime_up () {  // Timer_cnt delay values 1=15secs 4=1min 20=5mins 40=10mins 240=1hr, increase the values with this function
  log_interval += log_time_unit;
  update_log_time();
  if (lastcall == "x_y_z") display_x_y_z();
}

void update_log_time() {
  float log_hrs;
  log_hrs = table_size*log_interval/time_reference;
  log_hrs = log_hrs / 60.0; // Should not be needed, but compiler can't calculate the result in-line!
  float log_mins = (log_hrs - int(log_hrs))*60;
  log_time = String(int(log_hrs))+":"+((log_mins<10)?"0"+String(int(log_mins)):String(int(log_mins)))+" Hrs  of readings, ("+String(log_interval)+")secs per reading";
  //log_time += ", Free-mem:("+String(system_get_free_heap_size())+")";
}

void systemSetup() {
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  String IPaddress = WiFi.localIP().toString();
  webpage += F("<h3 style=\"color:orange;font-size:24px\">System Setup, Enter Values Required</h3>");
  webpage += F("<meta http-equiv='refresh' content='200'/ URL=http://");
  webpage += IPaddress + "/Setup>";
  webpage += "<form action='http://"+IPaddress+"/Setup' method='POST'>";
  webpage += "Maximum Value on Graph axis (currently = "+String(scale_max)+"&deg;C<br>";
  webpage += F("<input type='text' name='scale_max_in' value='30'><br>");
  webpage += "Minimum Value on Graph axis (currently = "+String(scale_min)+"&deg;C<br>";
  webpage += F("<input type='text' name='scale_min_in' value='-10'><br>");
  webpage += "Logging Interval (currently = "+String(log_interval)+"-Secs) (1=15secs)<br>";
  webpage += F("<input type='text' name='log_interval_in' value=''><br>");
  webpage += "Auto-scale Graph (currently = "+String(AScale?"ON":"OFF")+"<br>";
  webpage += F("<input type='text' name='auto_scale' value=''><br>");
  webpage += "Auto-update Graph (currently = "+String(AUpdate?"ON":"OFF")+"<br>";
  webpage += F("<input type='text' name='auto_update' value=''><br>");
  webpage += F("<input type='submit' value='Enter'><br><br>");
  webpage += F("</form>");
  append_page_footer();
  server.send(200, "text/html", webpage); // Send a response to the client asking for input
  if (server.args() > 0 ) { // Arguments were received
    for ( uint8_t i = 0; i < server.args(); i++ ) {
      String Argument_Name   = server.argName(i);
      String client_response = server.arg(i);
      if (Argument_Name == "scale_max_in") {
        if (client_response.toInt()) scale_max = client_response.toInt(); else scale_max = 30;
      }
      if (Argument_Name == "scale_min_in") {
        if (client_response.toInt() == 0) scale_min = 0; else scale_min = client_response.toInt();
      }
      if (Argument_Name == "log_interval_in") {
        if (client_response.toInt()) log_interval = client_response.toInt(); else log_interval = 300;
        log_interval = client_response.toInt()*log_time_unit;
      }
      if (Argument_Name == "auto_scale") {
        if (client_response == "ON" || client_response == "on") AScale = !AScale;
      }
      if (Argument_Name == "auto_update") {
        if (client_response == "ON" || client_response == "on") AUpdate = !AUpdate;
      }
    }
  }
  webpage = "";
  update_log_time();
}

void append_page_header() {
  webpage  = "<!DOCTYPE html><head>";
  if (AUpdate) webpage += F("<meta http-equiv='refresh' content='30'>"); // 30-sec refresh time, test needed to prevent auto updates repeating some commands
  webpage += F("<title>Logger</title>");
  webpage += F("<style>ul{list-style-type:none;margin:0;padding:0;overflow:hidden;background-color:#31c1f9;font-size:14px;}");
  webpage += F("li{float:left;}");
  webpage += F("li a{display:block;text-align:center;padding:5px 25px;text-decoration:none;}");
  webpage += F("li a:hover{background-color:#FFFFFF;}");
  webpage += F("h1{background-color:#31c1f9;}");
  webpage += F("body{width:");
  webpage += site_width;
  webpage += F("px;margin:0 auto;font-family:arial;font-size:14px;text-align:center;color:#ed6495;background-color:#F7F2Fd;}");
  webpage += F("</style></head><body><h1>Data Logger ");
  webpage += version + "</h1>";
}

void append_page_footer(){ // Saves repeating many lines of code for HTML page footers
  webpage += F("<ul>");
  webpage += F("<li><a href='/XYZ'>X/Y/Z m/s^2</a></li>");
  //webpage += F("<li><a href='/DV'>Dial</a></li>");
  webpage += F("<li><a href='/LgTU'>Records&dArr;</a></li>");
  webpage += F("<li><a href='/LgTD'>Records&uArr;</a></li>");
  webpage += F("<li><a href='/AS'>AutoScale(");
  webpage += String((AScale?"ON":"OFF"))+")</a></li>";
  webpage += F("<li><a href='/AU'>Refresh(");
  webpage += String((AUpdate?"ON":"OFF"))+")</a></li>";
  webpage += F("<li><a href='/Setup'>Setup</a></li>");
  webpage += F("<li><a href='/Help'>Help</a></li>");
  webpage += F("<li><a href='/LogS'>Log Size</a></li>");
  webpage += F("<li><a href='/LogV'>Log View</a></li>");
  webpage += F("<li><a href='/LogE'>Log Erase</a></li>");
  webpage += F("</ul><footer><p ");
  webpage += F("style='background-color:#a3b2f7'>&copy;");
  webpage += F("Weta Workshop 2018"); + F("</p>\n");
  webpage += F("</footer></body></html>");
}

String calcDateTime(int epoch){ // From UNIX time becuase google charts can use UNIX time
  int seconds, minutes, hours, dayOfWeek, current_day, current_month, current_year;
  seconds      = epoch;
  minutes      = seconds / 60; // calculate minutes
  seconds     -= minutes * 60; // calculate seconds
  hours        = minutes / 60; // calculate hours
  minutes     -= hours   * 60;
  current_day  = hours   / 24; // calculate days
  hours       -= current_day * 24;
  current_year = 1970;         // Unix time starts in 1970
  dayOfWeek    = 4;            // on a Thursday 
  while(1){
    bool     leapYear   = (current_year % 4 == 0 && (current_year % 100 != 0 || current_year % 400 == 0));
    uint16_t daysInYear = leapYear ? 366 : 365;
    if (current_day >= daysInYear) {
      dayOfWeek += leapYear ? 2 : 1;
      current_day   -= daysInYear;
      if (dayOfWeek >= 7) dayOfWeek -= 7;
      ++current_year;
    }
    else
    {
      dayOfWeek  += current_day;
      dayOfWeek  %= 7;
      /* calculate the month and day */
      static const uint8_t daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      for(current_month = 0; current_month < 12; ++current_month) {
        uint8_t dim = daysInMonth[current_month];
        if (current_month == 1 && leapYear) ++dim; // add a day to February if a leap year
        if (current_day >= dim) current_day -= dim;
        else break;
      }
      break;
    }
  }
  current_month += 1; // Months are 0..11 and returned format is dd/mm/ccyy hh:mm:ss
  current_day   += 1;
  String date_time = (current_day<10?"0"+String(current_day):String(current_day)) + "/" + (current_month<10?"0"+String(current_month):String(current_month)) + "/" + String(current_year).substring(2) + " ";
  date_time += ((hours   < 10) ? "0" + String(hours): String(hours)) + ":";
  date_time += ((minutes < 10) ? "0" + String(minutes): String(minutes)) + ":";
  date_time += ((seconds < 10) ? "0" + String(seconds): String(seconds));
  return date_time;
}

void help() {
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  webpage += F("<section style=\"font-size:14px\">");
  webpage += F("Temperature&Humidity - display temperature and humidity>");
  webpage += F("Temperature&Dewpoint - display temperature and dewpoint<br>");
  webpage += F("Dial - display temperature and humidity values<br>");
  webpage += F("Max&deg;C&uArr; - increase maximum y-axis by 1&deg;C;<br>");
  webpage += F("Max&deg;C&dArr; - decrease maximum y-axis by 1&deg;C;<br>");
  webpage += F("Min&deg;C&uArr; - increase minimum y-axis by 1&deg;C;<br>");
  webpage += F("Min&deg;C&dArr; - decrease minimum y-axis by 1&deg;C;<br>");
  webpage += F("Logging&dArr; - reduce logging rate with more time between log entries<br>");
  webpage += F("Logging&uArr; - increase logging rate with less time between log entries<br>");
  webpage += F("Auto-scale(ON/OFF) - toggle graph Auto-scale ON/OFF<br>");
  webpage += F("Auto-update(ON/OFF) - toggle screen Auto-refresh ON/OFF<br>");
  webpage += F("Setup - allows some settings to be adjusted<br><br>");
  webpage += F("Log Size - display log file size in bytes<br>");
  webpage += F("View Log - stream log file contents to the screen, copy and paste into a spreadsheet using paste special, text<br>");
  webpage += F("Erase Log - erase log file, needs two approvals using this function. Graph display functions reset the initial erase approval<br><br>");
  webpage += F("</section>");
  append_page_footer();
  server.send(200, "text/html", webpage);
  webpage = "";
}

////////////// SPIFFS Support ////////////////////////////////
// For ESP8266 See: http://esp8266.github.io/Arduino/versions/2.0.0/doc/filesystem.html
void StartSPIFFS(){
  boolean SPIFFS_Status;
  SPIFFS_Status = SPIFFS.begin(true);
  if (SPIFFS_Status == false)
  { // Most likely SPIFFS has not yet been formated, so do so
    SPIFFS.begin(true);
      File datafile = SPIFFS.open("/"+DataFile, FILE_READ);
      if (!datafile || !datafile.isDirectory()) {
        Serial.println("SPIFFS failed to start..."); // If ESP32 nothing more can be done, so delete and then create another file
        SPIFFS.remove("/"+DataFile); // The file is corrupted!!
        datafile.close();
      } else Serial.println("SPIFFS Started successfully...");
  } else Serial.println("SPIFFS Started successfully...");
}

void StartTime(){
  configTime(0, 0, "0.nz.pool.ntp.org", "time.nist.gov");
  setenv("TZ", "NZST-12NZDT,M9.5.0,M4.1.0/3",1); // Change for your location
  UpdateLocalTime();
}

void UpdateLocalTime(){
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
  }
  //See http://www.cplusplus.com/reference/ctime/strftime/
  Serial.println(&timeinfo, "%a %b %d %Y   %H:%M:%S"); // Displays: Saturday, June 24 2017 14:05:49
  char output[50];
  strftime(output, 50, "%a %d-%b-%y  (%H:%M:%S)", &timeinfo);
  time_str = output;
}

String GetTime(){
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time - trying again");
  }
  //See http://www.cplusplus.com/reference/ctime/strftime/
  Serial.println(&timeinfo, "%a %b %d %Y %H:%M:%S"); // Displays: Saturday, June 24 2017 14:05:49
  char output[50];
  strftime(output, 50, "%d/%m/%y %H:%M:%S", &timeinfo); //Use %m/%d/%y for USA format
  time_str = output;
  Serial.println(time_str);
  return time_str; // returns date-time formatted like this "11/12/17 22:01:00"
}
