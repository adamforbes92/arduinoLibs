/*
  Title:
    03.Text_options.ino

  Description:
    Demonstrates how to set different font options.

  Notes:
    *Setting a font option will make it persist through all following text commands, there is no need to call it before every text writing command.
    *Options:
      * setFont
      * setTextTransparency
      * setTextAlignment
      * setLineSpacing
      
    *The color option is universal for all drawing commands:
      * setDrawColor
      
    *If no option is changed by the user, the following defaults are used:
      * font = TLBFISLib::STANDARD
      * transparency = TLBFISLib::OPAQUE
      * alignment = TLBFISLib::LEFT
      * spacing = 1
      * color = TLBFISLib::NORMAL

    *This demo shows all three text fonts (STANDARD/COMPACT/GRAPHICS) by looping through three pages, the current screen being stored by the "page" variable.
    The new screen is only drawn once when the page changes from its previous value ("prev_page").
*/

//Include the FIS library.
#include <TLBFISLib.h>

//Include the SPI library.
#include <SPI.h>

//18 clk
//23 mosi data
//19 ena

#define ENA_PIN 19
//Hardware configuration
#define SPI_INSTANCE SPI

//Define the function to be called when the library needs to send a byte.
void sendFunction(uint8_t data) {
  SPI_INSTANCE.beginTransaction(SPISettings(130000, MSBFIRST, SPI_MODE3));
  SPI_INSTANCE.transfer(data);
  SPI_INSTANCE.endTransaction();
}

//Define the function to be called when the library is initialized by begin().
void beginFunction() {
  //SPI_INSTANCE.setClockDivider(SPI_CLOCK_DIV256);
  SPI_INSTANCE.begin();
}

//Create an instance of the FIS library.
TLBFISLib FIS(ENA_PIN, sendFunction, beginFunction);

//For displaying different pages
uint8_t page = 0, prev_page = -1;

//For non-blocking delays
unsigned long page_timer = 0;
#define PAGE_TIME 5000  //how many milliseconds to wait on a page (5000 = 5 seconds)

void setup() {
  //Here, the custom error function is declared directly, as a lambda function.
  //In the example "02.Custom_functions" you can see how to define it as a separate function.
  FIS.errorFunction(
    [](unsigned long duration) {
      //Errors are measured in milliseconds, to offer the possibility of differentiating between events.
      //Here, this value won't be used, so cast it to void to avoid a compiler warning.
      (void)duration;

      //Initialize the screen.
      FIS.initScreen();

      //Set "prev_page" to a value different than "page", so a page is redrawn immediately.
      prev_page = -1;
    });

  //Start the library and initialize the screen.
  FIS.begin();
  FIS.initScreen(TLBFISLib::FULLSCREEN);

  //When using the writeMultiLineText() function, which splits strings on multiple lines when encountering a newline character, the space left betweem rows is
  //adjusted using the setLineSpacing() function.
  //Set the line spacing for writeMultiLineText() to 3 pixels.
  //This will not be changed anywhere else in this program, so it can just be set once.
  FIS.setLineSpacing(0);
}

void loop() {
  //Maintain the connection.
  FIS.update();
  //When starting to draw a new page, clear the screen.
  //FIS.clear();

  //Show different info on the screen, depending on which page is currently selected.

  //Align the text to the right for the title.
  FIS.setTextAlignment(TLBFISLib::CENTER);

  //Set the COMPACT font.
  FIS.setFont(TLBFISLib::COMPACT);
  char buf[16];
  sprintf(buf, "BLOCK %d", page);
  page++;
  //Write the page title at position X0, Y1.
  FIS.writeText(0, 1, buf);

  //Draw a horizontal line under the title, at position X0, Y9, with a length of 64 pixels.
  FIS.drawLine(0, 9, 64);

  //Align the text to the center for the following message.
  FIS.setTextAlignment(TLBFISLib::LEFT);

  //Write text, spanning multiple lines, with the first line starting at position X0, Y15.
  FIS.writeMultiLineText(0, 15, "The\nCOMPACT\nCOMPACT\nARE\nYOU\nA\nCUNT");
  delay(100);
}
