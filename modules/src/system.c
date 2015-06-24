/*
 * Copyright (C) 2011-2012 Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * system.c - Top level module implementation
 */
#define DEBUG_MODULE "SYS"

#include <stdbool.h>

/* FreeRtos includes */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "ledseq.h"
#include "pm.h"
#include "system.h"

#include "uart_syslink.h"
#include "comm.h"
#include "stabilizer_balance.h"

/* Private variable */
static bool canFly;

static bool isInit;

/* System wide synchronisation */
xSemaphoreHandle canStartMutex;


/* Private functions */
static void systemTask(void *arg);

/* Public functions */
void systemLaunch(void)
{
  xTaskCreate(systemTask, (const char *)"SYSTEM",
              2*configMINIMAL_STACK_SIZE, NULL, /*Piority*/ 2, NULL);

}

/* This must be the first module to be initialized! */
void systemInit(void)
{
  if(isInit)
    return;
 
  canStartMutex = xSemaphoreCreateMutex();
  xSemaphoreTake(canStartMutex, portMAX_DELAY);
	
	adcInit();
	ledseqInit();
	pmInit();
	
  isInit = true;
}

bool systemTest()
{
  bool pass=isInit;
  
  pass &= adcTest();
  pass &= ledseqTest();
  pass &= pmTest();
//  pass &= workerTest();
  
  return pass;
}

void systemTask(void *arg)
{
  bool pass = true;

  /* Init the high-levels modules */
  systemInit();
	
	uartInit();
	commInit();
	stabilizerInit();
	
	//Test the modules
  pass &= systemTest();
	pass &= commTest();
//	pass &= commanderTest();
	pass &= stabilizerTest();
	
	if (pass)
	{
		systemStart();
		ledseqRun(LED_RED, seq_alive);
    ledseqRun(LED_GREEN, seq_testPassed);
	}
	else
  {
    if (systemTest())
    {
      while(1)
      {
        ledseqRun(LED_RED, seq_testPassed); //Red passed == not passed!
        vTaskDelay(M2T(2000));
      }
    }
    else
    {
      ledInit();
      ledSet(LED_RED, true);
    }
  }
	
	pmSetChargeState(charge500mA);
	
	//Should never reach this point!
	while(1)
    vTaskDelay(portMAX_DELAY);
	
}


/* Global system variables */
void systemStart(void)
{
  xSemaphoreGive(canStartMutex);
}

void systemWaitStart(void)
{
  //This permits to guarantee that the system task is initialized before other
  //tasks waits for the start event.
  while(!isInit)
    vTaskDelay(2);

  xSemaphoreTake(canStartMutex, portMAX_DELAY);
  xSemaphoreGive(canStartMutex);
}

void systemSetCanFly(bool val)
{
  canFly = val;
}

bool systemCanFly(void)
{
  return canFly;
}
