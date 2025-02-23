#include "VAGFISWriter.h"

#define FIS_DATA 25
#define FIS_CLK 26
#define FIS_ENA 27  //if this pin did not have HW interrupt, force mode must by set in initialization of VAGFISWRITER (4th parameter = 1)

//HW interrupt on FIS_ENA pin
VAGFISWriter fisWriter(FIS_CLK, FIS_DATA, FIS_ENA, 1);
//no HW interrupt on FIS_ENA pin
//VAGFISWriter fisWriter( FIS_CLK, FIS_DATA, FIS_ENA,0);

void setup() {
  fisWriter.begin();
  fisWriter.reset();
}
long timex = 0;
void loop() {

  fisWriter.sendRadioMsg("TEST12345678TEST");

  while (1) {
    //no HW interrupt on FIS_ENA pin
    //we must time outputing packets to cluster
    //delay(1500);// should be send each second
    fisWriter.sendRadioData();
  }
}
