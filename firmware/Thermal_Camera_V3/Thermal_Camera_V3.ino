/*

Thermal Camera App V1.0 by turbo2ltr

FORKED FROM: 
	Makerfabs MLX90640 Camera demo
	Author  : Vincent
	Version : 3.0

TURBO2LTR CHANGES
* Removed wifi option and related code
* removed option and related code for non-interpolated output
* removed unused SD Card functions
* added min/max/mean display
* upper and lower bounds of the color map are no longer hard coded. 
	They are now auto-scaling based on the min/max temperatures of the frame to get the best color range
* Added button to lock upper and lower bounds (i.e. turn off auto-scaling)
* Added crosshairs and temp readout in center of the image
* Added button to save screenshot BMP to SD card.  (http://www.technoblogy.com/show?398X)

Useful links
https://github.com/Makerfabs/ESP32-S2-MLX90640-Touch-Screen-Thermal-Camera
https://learn.adafruit.com/adafruit-gfx-graphics-library/graphics-primitives
https://github.com/lovyan03/LovyanGFX
http://www.technoblogy.com/list?39BW


*/

#define LGFX_USE_V1

#include <LovyanGFX.hpp>
#include <Wire.h>
#include <Adafruit_MLX90640.h>
#include "color.h"
#include "FT6236.h"
#include "lcd_set.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"


LGFX lcd;

#define SPI_MOSI 			2 
#define SPI_MISO 			41
#define SPI_SCK 			42
#define SD_CS 				1 

#define I2C_SCL 			39
#define I2C_SDA 			38

#define MLX_I2C_ADDR 		0x33

Adafruit_MLX90640 	mlx;

// TOUCH RELATED
const int i2c_touch_addr = TOUCH_I2C_ADD;
#define get_pos ft6236_pos
int touch_flag_lock = 0;
int touch_flag_screenshot = 0;
int pos[2] = {0, 0};

// BUTTON BOUNDS
#define BTN_LOCK_LEFT	210
#define BTN_LOCK_TOP	400
#define BTN_LOCK_RIGHT	300
#define BTN_LOCK_BOTTOM	450
#define BTN_LOCK_COLOR 	TFT_GREEN
#define BTN_LOCK_TXT 	TFT_BLACK

#define BTN_SS_LEFT		20
#define BTN_SS_TOP		400
#define BTN_SS_RIGHT	110
#define BTN_SS_BOTTOM	450
#define BTN_SS_COLOR 	TFT_GREEN
#define BTN_SS_TXT 		TFT_BLACK

#define MINTEMP 		16			// just initial values, now auto-scales
#define MAXTEMP 		37			// just initial values, now auto-scales

#define MLX_MIRROR 		0 			// Set 1 when the camera is facing the same direction as the screen
#define FILTER_ENABLE 	1			// Set to 1 averages the previous and current values to smooth the display a bit

float range_lower = MINTEMP;		// store the dynamic lower bound of the range
float range_upper = MAXTEMP;		// store the dynamic upper bound of the range
float range_avg;					// store the average temperature of the frame

float frame[32 * 24]; 				// buffer for full frame of temperatures
float *temp_frame = NULL;			
uint16_t *inter_p = NULL;			// stores 320x240 interpolation of the 32x24 sensor data


uint32_t runtime = 0;				// stores millis to trigger statistic data update
bool lock_bounds = false;			// if set to true, inhibits auto-scaling of color mapping

File bmpFile;


// *********************
// SETUP
// *********************
void setup(void)
{
    pinMode(LCD_CS, OUTPUT);
    pinMode(LCD_BLK, OUTPUT);

    digitalWrite(LCD_CS, LOW);
    digitalWrite(LCD_BLK, HIGH);

    Serial.begin(115200);
    Serial.println(ESP.getFreeHeap());

    lcd.init();
    lcd.setRotation(0);
    display_ui();								// draw static parts of display

	// SPI SD INIT
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    if (SD_init())
        Serial.println(F("SD init error"));
   
    //I2C init (thermal sensor)
    Wire.begin(I2C_SDA, I2C_SCL);
    byte error, address;

    Wire.beginTransmission(MLX_I2C_ADDR);
    error = Wire.endTransmission();

    if (error == 0)
    {
        Serial.print(F("I2C device found at address 0x"));
        Serial.print(MLX_I2C_ADDR, HEX);
        Serial.println(F("  !"));
    }
    else if (error == 4)
    {
        Serial.print(F("Unknown error at address 0x"));
        Serial.println(MLX_I2C_ADDR, HEX);
    }

    Serial.println(F("Adafruit MLX90640 Simple Test"));
    if (!mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire))
    {
        Serial.println(F("MLX90640 not found!"));
        while (1)
            delay(10);
    }

    // Configure thermal sensor
    mlx.setMode(MLX90640_CHESS);
    mlx.setResolution(MLX90640_ADC_18BIT);
    mlx90640_resolution_t res = mlx.getResolution();
    mlx.setRefreshRate(MLX90640_16_HZ);
    mlx90640_refreshrate_t rate = mlx.getRefreshRate();
    Wire.setClock(1000000); // max 1 MHz

    Serial.println(ESP.getFreeHeap());

    // INIT IMAGE STORAGE
    
    inter_p = (uint16_t *)malloc(320 * 240 * sizeof(uint16_t));
    if (inter_p == NULL)
    {
        Serial.println(F("inter_p Malloc error"));
    }

    for (int i = 0; i < 320 * 240; i++)
    {
        *(inter_p + i) = 0x480F;
    }

    Serial.println(ESP.getFreeHeap());
    

    // INIT SENSOR DATA STORAGE
    temp_frame = (float *)malloc(32 * 24 * sizeof(float));
    if (temp_frame == NULL)
        Serial.println(F("temp_frame Malloc error"));

    for (int i = 0; i < 32 * 24; i++)
        temp_frame[i] = MINTEMP;
 
    Serial.println(ESP.getFreeHeap());
    Serial.println(F("All init over."));
}



void loop()
{
    //Get a frame
    if (mlx.getFrame(frame) != 0)
    {
        Serial.println(F("Get frame failed"));
        return;
    }

    filter_frame(frame, temp_frame);							// flips image if needed (MLX_MIRROR), averages previous frame if FILTER_ENABLE is 1, puts it in temp_frame
	
	updateMeanMinMax(frame, 32 * 24 - 1);						// collect statistics for the frame

    interpolation(temp_frame, inter_p);							// interpolates 32*24 sensor data to 320*240 16 bit color coded LCD image, puts it in inter_p
    lcd.pushImage(0, 0, 320, 240, (lgfx::rgb565_t *)inter_p);	// write the interpolated image data to the LCD

    // draw crosshairs
    lcd.drawFastVLine(160, 110, 20, TFT_WHITE);
    lcd.drawFastHLine(150, 120, 20, TFT_WHITE);
	
	// draw the current temp
    lcd.setTextSize(2);
    lcd.setTextColor(TFT_WHITE);
    lcd.setCursor(140, 135);									// 160,120 is the center
    lcd.printf("%3.1f",_f(temp_frame[400]) );					// 400 = 32x12 + 16

        
	// See if we had a touch event
    if (get_pos(pos))
    {
        if((pos[0] + pos[1]) != 0)
        	Serial.println((String) "x=" + pos[0] + ",y=" + pos[1]);
        // check the bounds of the lock button
        if (pos[0] > BTN_LOCK_LEFT && pos[0] < BTN_LOCK_RIGHT  &&  pos[1] > BTN_LOCK_TOP &&  pos[1] < BTN_LOCK_BOTTOM)
            touch_flag_lock = 1;

        // check the bounds of the screenshot button    
        if (pos[0] > BTN_SS_LEFT && pos[0] < BTN_SS_RIGHT  &&  pos[1] > BTN_SS_TOP &&  pos[1] < BTN_SS_BOTTOM)
            touch_flag_screenshot = 1;
    }

    if ((millis() - runtime) > 250)
    { // update ever 250 ms
        lcd.fillRect(0, 270, 319, (400-270), TFT_BLACK);		// clean the screen

		// draw out the statistical data
        lcd.setTextSize(2);
        lcd.setTextColor(TFT_WHITE);			
       
       	lcd.setCursor(20, 280);
        lcd.println("MIN");        
        lcd.setCursor(125, 280);
        lcd.println("AVG");        
        lcd.setCursor(245, 280);
        lcd.println("MAX");

		lcd.setTextSize(3);
        lcd.setCursor(0, 300);
        lcd.printf("%3.1f", _f(range_lower));
		lcd.setCursor(115, 300);
		lcd.printf("%3.1f", _f(range_avg));
		lcd.setCursor(230, 300);
		lcd.printf("%3.1f", _f(range_upper));
        

		// Process any touch events	
        if (touch_flag_lock == 1)
        {
            touch_flag_lock = 0;
			lock_bounds = !lock_bounds;				// toggle

			drawLockButton();
        }

        if (touch_flag_screenshot == 1)
        {
            touch_flag_screenshot = 0;
			bmpSave();
        }

        runtime = millis();
    }
}


//Filter temperature data and change camera direction
void filter_frame(float *in, float *out)
{
    if (MLX_MIRROR == 1)
    {
        for (int i = 0; i < 32 * 24; i++)
        {
            if (FILTER_ENABLE == 1)
                out[i] = (out[i] + in[i]) / 2;				// average the current frame with the previous frame
            else
                out[i] = in[i];
        }
    }
    else
    {
        for (int i = 0; i < 24; i++)
            for (int j = 0; j < 32; j++)
            {
                if (FILTER_ENABLE == 1)
                    out[32 * i + 31 - j] = (out[32 * i + 31 - j] + in[32 * i + j]) / 2;		// average the current frame with the previous frame
                else
                    out[32 * i + 31 - j] = in[32 * i + j];
            }
    }
}



//Transform 32*24 to 320 * 240 pixel using interpolation
void interpolation(float *data, uint16_t *out)
{
    for (uint8_t h = 0; h < 24; h++)
    {	// for each row of sensor data
        for (uint8_t w = 0; w < 32; w++)
        { // for each column of sensor data

        	// put the sensor data in the right spot in the 320x240 array, and map the value based on the current frame's min and max values
            out[h * 10 * 320 + w * 10] = map_f(data[h * 32 + w], range_lower, range_upper);		
        }
    }
    
    for (int h = 0; h < 240; h += 10)
    { // for each row (of 10 px)
        for (int w = 1; w < 310; w += 10)
        { // for each column (of 10 px)
            for (int i = 0; i < 9; i++)
            { // for each of the 10 px
                out[h * 320 + w + i] = (out[h * 320 + w - 1] * (9 - i) + out[h * 320 + w + 9] * (i + 1)) / 10;		// interpolate the 10 pixels between the two column data points
            }
        }
        for (int i = 0; i < 9; i++)
        {
            out[h * 320 + 311 + i] = out[h * 320 + 310];	// last 10 PX on right side have no data to interpolate to so just fill it with the last actual value
        }
    }
    
    for (int w = 0; w < 320; w++)
    { // for each column
        for (int h = 1; h < 230; h += 10)
        { // for each row of 10 px
            for (int i = 0; i < 9; i++)
            { // for each of the 10 row pixels
                out[(h + i) * 320 + w] = (out[(h - 1) * 320 + w] * (9 - i) + out[(h + 9) * 320 + w] * (i + 1)) / 10;  // interpolate each pixel vertically between the two rows we previously interpolated
            }
        }
        for (int i = 0; i < 9; i++)
        { // last 10 pixels on the bottom have no data to interolate between to so just fill in with the last actual value.
            out[(231 + i) * 320 + w] = out[230 * 320 + w]; 	
        }
    }
    
    
    for (int h = 0; h < 240; h++)
    { // for each row
        for (int w = 0; w < 320; w++)
        { // for each column
            out[h * 320 + w] = camColors[out[h * 320 + w]];		// calculate the pixel color.
        }
    }
    
}

//float to 0,255
int map_f(float in, float a, float b)
{
    if (in < a)
        return 0;

    if (in > b)
        return 255;

    return (int)(in - a) * 255 / (b - a);
}


void updateMeanMinMax(float arr[], int N)
{
    // Store the average of the array
    float avg = 0;
    if(!lock_bounds)
	{
	    range_lower = 300;		// max for sensor
	    range_upper = -40;		// min for sensor
	}
 
    // Traverse the array arr[]
    for (int i = 0; i < N; i++) 
    {   // Update avg
        avg += (arr[i] - avg) / (i + 1);

		if(!lock_bounds)
		{
			if(arr[i] > range_upper)
				range_upper = arr[i];
	
			if(arr[i] < range_lower)
				range_lower = arr[i];
		}
    }
 
    range_avg = avg;
    
    if(range_lower == range_upper)
    	range_upper ++;	// make sure they aren't equal.
}


void display_ui()
{
    for (int i = 0; i < 256; i++)
        lcd.drawFastVLine(32 + i, 255, 20, camColors[i]);		// draw rainbow

	drawLockButton();

    // SCREENSHOT BUTTON
    lcd.setTextSize(2);
    lcd.fillRect(BTN_SS_LEFT, BTN_SS_TOP, (BTN_SS_RIGHT - BTN_SS_LEFT), BTN_SS_BOTTOM - BTN_SS_TOP, BTN_SS_COLOR);
    lcd.setCursor(BTN_SS_LEFT + 10, BTN_SS_TOP + 15);
    lcd.setTextColor(BTN_SS_TXT,BTN_SS_COLOR);
    lcd.println("SAVE");

}

void drawLockButton()
{	// LOCK BUTTON
	
	lcd.setTextSize(2);
	lcd.setCursor(BTN_LOCK_LEFT + 10, BTN_LOCK_TOP + 15);
	
	if(lock_bounds)
	{
	    lcd.fillRect(BTN_LOCK_LEFT, BTN_LOCK_TOP, (BTN_LOCK_RIGHT - BTN_LOCK_LEFT), BTN_LOCK_BOTTOM - BTN_LOCK_TOP, BTN_LOCK_COLOR);
	    lcd.setTextColor(BTN_LOCK_TXT,BTN_LOCK_COLOR);    
    	lcd.println("UNLOCK");
	}
	else
	{
		lcd.fillRect(BTN_LOCK_LEFT, BTN_LOCK_TOP, (BTN_LOCK_RIGHT - BTN_LOCK_LEFT), BTN_LOCK_BOTTOM - BTN_LOCK_TOP, BTN_LOCK_COLOR);
		lcd.setTextColor(BTN_LOCK_TXT,BTN_LOCK_COLOR);
		lcd.println("LOCK");
	}
    

}

float _f(float c)
{ // convert to f
	return (c*1.8) + 32.0;
}


void setStatus(String txt)
{ // put a status message at the bottom of the screen
	
	lcd.fillRect(0, 460, 320, 20, TFT_BLACK);		// clean the screen
	lcd.setTextSize(1);
	lcd.setCursor(4,465);
	lcd.print(txt);	
}

void bmpSave () 
{ // save the current image to a BMP on the SD card
	
	uint16_t width = 320, height = 240;
	char filename[12] = "/image1.bmp";
	setStatus("Saving screenshot, please wait...");
	
	SD_init();

	// get a fresh file name
	while (SD.exists(filename)) 
		filename[6]++; 			// this will fail after 10

	Serial.print(F("Attempt to open: "));	 
	Serial.println(filename);	 
	
	bmpFile = SD.open(filename, FILE_WRITE);
	
	// On error return
	if (!bmpFile) 
	{
		Serial.println(F("Failed to Open BMP"));
		setStatus("FAILED: Check SD Card");
		return;
	}
	else
	{
		// generate a BMP
		// File header: 14 bytes
		bmpFile.write('B'); bmpFile.write('M');
		writeFour(14+40+12+width*height*2); // File size in bytes
		writeFour(0);
		writeFour(14+40+12);                // Offset to image data from start
		
		// Image header: 40 bytes
		writeFour(40);                      // Header size
		writeFour(width);                   // Image width
		writeFour(height);                  // Image height
		writeTwo(1);                        // Planes
		writeTwo(16);                       // Bits per pixel
		writeFour(3);                       // Compression (BI_BITFIELDS)
		writeFour(0);                       // Image size (0 for uncompressed)
		writeFour(0);                       // Preferred X resolution (ignore)
		writeFour(0);                       // Preferred Y resolution (ignore)
		writeFour(0);                       // Colour map entries (ignore)
		writeFour(0);                       // Important colours (ignore)

		// Colour masks: 12 bytes  RGB565
		writeFour(0b1111100000000000);      // Red
		writeFour(0b0000011111100000);      // Green
		writeFour(0b0000000000011111);      // Blue


		// Image data: width * height * 2 bytes
		// we already have the data stored in inter_p which is 16 bit RGB565 encoded and 
		// the start of the array is the top left corner of the image.
		// But BMP data is 16 bit little endian, and the start of the data is the bottom left corner.
		// so we need to swap the endianness and swap the top for the bottom
		// we don't have enough memory to store the whole 320x240x16bit image and
		// it's very slow writing single bytes to the SD card, so we temporarily store each line 
		// and swap the data in-place from each end of the array.
		
		uint8_t endianed[320*2];
		uint16_t temp_line[320];
		uint16_t indx = 0;
		uint32_t ptr;
		
		uint8_t y_upper = 0;
		uint8_t y_lower = height-1;

		while(y_upper != y_lower && y_upper < y_lower)
		{ // until we meet in the middle...
			
			memcpy((uint8_t *) temp_line, (uint8_t *)inter_p + (y_upper*640),  640);   //  Store the upper line  (to,from,size, in bytes)

			// convert the lower line
			indx = 0;
			for (int x=0; x<width; x++) 
			{ // swap endienness and store
				ptr = (y_lower*320)+x;
				endianed[indx] = (inter_p[ptr] & 0xff);			// lsb
				indx++;
				endianed[indx] = (inter_p[ptr] >> 8) & 0xff;	// msb
				indx++;
			}
			
			// write the endiened data from the lower line to the upper line
			memcpy((uint8_t*)inter_p + (y_upper*640), endianed, 640);   		// to, from, size

			// swap the endiendness of the saved upper line and save to the lower spot
			indx = 0;
			for (int x=0; x<width; x++) 
			{
				endianed[indx] = (temp_line[x] & 0xff);		// lsb
				indx++;
				endianed[indx] = (temp_line[x] >> 8) & 0xff;	// msb
				indx++;
			}
			// write the swapped data to the lower line
			memcpy((uint8_t*)inter_p + (y_lower*640), endianed, 640);   // to, from, size

			// go to the next top line and the previous bottom line
			y_upper ++;
			y_lower --;
			
		}

		bmpFile.write((uint8_t*)inter_p, 320*240*2);		// write the whole array all at once

		// Close the file
		bmpFile.close();

		Serial.println(F("Saved Screenshot"));
		setStatus((String) "Saved: " + (String) filename);
	}
}


// Write two bytes, least significant byte first
void writeTwo (uint16_t word) 
{
	bmpFile.write(word & 0xFF); 
	bmpFile.write((word >> 8) & 0xFF);
}

// Write four bytes, least significant byte first
void writeFour (uint32_t word) 
{
	bmpFile.write(word & 0xFF); 
	bmpFile.write((word >> 8) & 0xFF);
	bmpFile.write((word >> 16) & 0xFF); 
	bmpFile.write((word >> 24) & 0xFF);
}


int SD_init()
{
    if (!SD.begin(SD_CS))
    {
        Serial.println(F("Card Mount Failed"));
        return 1;
    }
    uint8_t cardType = SD.cardType();

    if (cardType == CARD_NONE)
    {
        Serial.println(F("No SD card attached"));
        return 1;
    }

    Serial.print(F("SD Card Type: "));
    switch(cardType)
    {
    	case CARD_MMC:
    		Serial.println(F("MMC"));
    	break;
    	case CARD_SD:
        	Serial.println(F("SDSC"));
    	break;
    	case CARD_SDHC:
    		Serial.println(F("SDHC"));
    	break;
    	default:
    		Serial.println(F("UNKNOWN"));
    }
   
    //uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    //Serial.printf("SD Card Size: %lluMB\n", cardSize);
   
    return 0;
}
