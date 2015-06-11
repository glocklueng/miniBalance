/**
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
 *
 */
#include <stdlib.h>
#include <math.h>

#include "FreeRTOS.h"
#include "task.h"

#include "config.h"
#include "system.h"
//#include "pm.h"
#include "commander_balance.h"
#include "stabilizer_balance.h"
#include "controller_balance.h"
#include "sensfusion6.h"
#include "imu.h"
#include "motors.h"
#include "encoder.h"
//#include "log.h"
#include "pid.h"
#include "ledseq.h"
//#include "param.h"
////#include "ms5611.h"
//#include "lps25h.h"
//#include "debug.h"

#define ATTITUDE_UPDATE_RATE_DIVIDER  1
#define FUSION_UPDATE_DT  (float)(1.0 / (IMU_UPDATE_FREQ / ATTITUDE_UPDATE_RATE_DIVIDER))

#define	KALMAN_FILTER
//#define COMPLEMENTARY_FILTER

#define LOGGING_ENABLED
#ifdef LOGGING_ENABLED
  #define PRIVATE
#else
  #define PRIVATE static
#endif

PRIVATE Axis3f gyro; // Gyro axis data in deg/s
PRIVATE Axis3f acc;  // Accelerometer axis data in mG

PRIVATE float eulerRollActual;
PRIVATE float eulerPitchActual;
PRIVATE float eulerYawActual;

extern PidObject pidVelocity;
extern PidObject pidTurn;
	
	
int16_t motorLeftOutput, motorRightOutput;
int32_t encoderLeft, encoderRight;
float angleBalance, gyroBalance, gyroTurn;							//ƽ����� ƽ�������� ת��������
uint8_t flagForward, flagBackward, flagLeft, flagRight;	//����ң����صı���
uint8_t flagStop = 1;																		//ֹͣ��־λ Ĭ��ֹͣ
int32_t balancePWM, velocityPWM, dirPWM;
uint16_t batteryVoltage;

static bool isInit;

static void stabilizerTask(void* param);

static int32_t balanceControl(const float angle, const float gyro);
static int32_t velocityControl(const int32_t encoderLeft, const int32_t encoderRight);
static int32_t dirControl(const int32_t encoderLeft, const int32_t encoderRight, const float gyroTurn);
static uint8_t motorTurnOff(const float angle, const int voltage);
static uint16_t limitThrust(int32_t value);


void stabilizerInit(void)
{
	motorsInit();
	encoderInit();
	imu6Init();
	sensfusion6Init();
	controllerInit();
	
	xTaskCreate(stabilizerTask, (const char *)"STABILIZER",
              2*configMINIMAL_STACK_SIZE, NULL, /*Piority*/2, NULL);

  isInit = true;
}

bool stabilizerTest(void)
{
  bool pass = true;

	if (!isInit)
  {
    pass = false;
  }
	
  pass &= motorsTest();
	pass &= encoderTest();
  pass &= imu6Test();
  pass &= sensfusion6Test();
  pass &= controllerTest();

  return pass;
}

static void stabilizerTask(void* param)
{
	uint32_t attitudeCounter = 0;
  uint32_t lastWakeTime;
	
	vTaskSetApplicationTaskTag(0, (void *)TASK_STABILIZER_ID_NBR);
	
  /* Wait for the system to be fully started to start stabilization loop */
  systemWaitStart();
	
	lastWakeTime = xTaskGetTickCount();
	
	while (1)
	{
		vTaskDelayUntil(&lastWakeTime, F2T(IMU_UPDATE_FREQ));
		
		imu6Read(&gyro, &acc);
		if (imu6IsCalibrated())	
		{
			if (++attitudeCounter >= ATTITUDE_UPDATE_RATE_DIVIDER)
			{
				sensfusion6UpdateQ(gyro.x, gyro.y, gyro.z, acc.x, acc.y, acc.z, FUSION_UPDATE_DT);
				sensfusion6GetEulerRPY(&eulerRollActual, &eulerPitchActual, &eulerYawActual);
				attitudeCounter = 0;
			}
			#if defined(KALMAN_FILTER)
				angleBalance = Kalman_Filter(eulerPitchActual, gyro.y);
			#elif defined(COMPLEMENTARY_FILTER)
				angleBalance = Complementary_Filter(eulerPitchActual, gyro.y);	
			#else
				angleBalance = eulerPitchActual;
			#endif
			gyroBalance = gyro.y;
			gyroTurn = gyro.z;
			
			/* Direction Command */
			comamndGetControl(&flagForward, &flagBackward, &flagLeft, &flagRight);
			flagStop = ~(flagForward || flagBackward || flagLeft || flagRight);

			/* Balance Control */
			// encoder
			encoderCalculate(encoderLeft, encoderRight);			
			balancePWM = balanceControl(angleBalance, gyroBalance);
			velocityPWM = velocityControl(encoderLeft, encoderRight);
			dirPWM = dirControl(encoderLeft, encoderRight, gyroTurn);

			/* Motors Output */
			motorLeftOutput = limitThrust(balancePWM - velocityPWM + dirPWM);
			motorRightOutput = limitThrust(balancePWM - velocityPWM - dirPWM);

			if(motorTurnOff(angleBalance, batteryVoltage) == 0)
			{
				// motor direction
				if(motorLeftOutput < 0)
					motorsSetDir(MOTOR_LEFT, MOTOR_BACKWARD);
				else
					motorsSetDir(MOTOR_LEFT, MOTOR_FORWARD);
				
				if(motorRightOutput < 0)
					motorsSetDir(MOTOR_RIGHT, MOTOR_BACKWARD);
				else
					motorsSetDir(MOTOR_RIGHT, MOTOR_FORWARD);
				// PWM output
				motorsSetRatio(MOTOR_LEFT, abs(motorLeftOutput));
				motorsSetRatio(MOTOR_RIGHT, abs(motorRightOutput));
			}
		}	
	}
}

static int32_t balanceControl(const float angle,const float gyro)
{  
   static float bias;
	 int balance;
	 bias = angle + 3;																	//===���ƽ��ĽǶ���ֵ �ͻ�е���
	 balance = 300*bias + gyro/2;												//===����ֱ���������
	 return balance;
}

#if 1
static int32_t velocityControl(const int32_t encoderLeft, const int32_t encoderRight)
{
	static int Velocity, Encoder_Least, Encoder,Movement;
	static long Encoder_Integral;

	/*************** ң��ǰ�����˲� ***************/
	if(1 == flagForward) Movement = 70;								//===���ǰ����־λ��1 λ��Ϊ��
	else if(1 == flagBackward) Movement = -70;				//===������˱�־λ��1 λ��Ϊ��
	else  Movement = 0;

	/***************** �ٶ�PI���� ******************/	
	Encoder_Least = encoderLeft + encoderRight;				//===��ȡ�����ٶ�ƫ��
	Encoder *= 0.7;																		//===һ�׵�ͨ�˲���       
	Encoder += Encoder_Least * 0.3;										//===һ�׵�ͨ�˲���    

	Encoder_Integral += Encoder;                                  //===���ֳ�λ�� ����ʱ�䣺5ms
	Encoder_Integral = Encoder_Integral + Movement;								//===����ң�������ݣ�����ǰ������
	if(Encoder_Integral > 18000)  	Encoder_Integral = 18000;			//===�����޷�
	if(Encoder_Integral < -18000)	Encoder_Integral = -18000;			//===�����޷�	
	Velocity = Encoder * 40 + Encoder_Integral / 5;								//===�ٶȿ���	
	if(motorTurnOff(angleBalance, batteryVoltage) == 1)   Encoder_Integral = 0;    //===����رպ��������
	return Velocity;
}
#else 
static int32_t velocityControl(const int32_t encoderLeft, const int32_t encoderRight)
{

}


#endif
static int32_t dirControl(const int32_t encoderLeft, const int32_t encoderRight, const float gyroTurn)//ת�����
{
	static int Turn_Target,Turn,Encoder_temp,Turn_Convert=3,Turn_Count;
	int Turn_Bias, Turn_Amplitude = 110/2 + 20;     //===Way_AngleΪ�˲�����������1ʱ������DMP��ȡ��̬��Turn_Amplitudeȡ�󣬿������ͻ����ǣ�ȡС����Ϊ�������˲��㷨Ч���Բ
	static long Turn_Bias_Integral;
	/*************** ң��������ת�� ***************/
	if(1==flagLeft || 1==flagRight)									//��һ������Ҫ�Ǹ�����תǰ���ٶȵ�����ת����ʼ�ٶȣ�����С������Ӧ��
	{
		if(++Turn_Count == 1)
			Encoder_temp = abs(encoderLeft - encoderRight);

		Turn_Convert = 50 / Encoder_temp;
		if(Turn_Convert < 1)
			Turn_Convert = 1;
		if(Turn_Convert > 4)
			Turn_Convert = 4;
	}	
	else
	{
		Turn_Convert=1;
		Turn_Count=0;
		Encoder_temp=0;
	}
	
	if(1==flagLeft)	           Turn_Target += Turn_Convert;
	else if(1==flagRight)	     Turn_Target -= Turn_Convert;
	else Turn_Target=0;//
	
	if(Turn_Target>Turn_Amplitude)  Turn_Target=Turn_Amplitude;		//===ת���ٶ��޷�
	if(Turn_Target<-Turn_Amplitude) Turn_Target=-Turn_Amplitude;

	/*************** ת��PD������ ****************/
	Turn_Bias = encoderLeft - encoderRight - Turn_Target;					//===����ת��ƫ��  
	Turn = Turn_Bias*55 - gyroTurn/2;																//===���Z�������ǽ���PID����
	if(motorTurnOff(angleBalance, batteryVoltage) == 1)   Turn_Bias_Integral=0;//===����رպ��������
	return Turn;
}

static uint8_t motorTurnOff(float angle, int voltage)
{
	u8 temp;

	/*
	 * �رյ��: ��Ǵ���40��, Flag_Stop��1, ��ѹ����11.1V
	 */
	if(angle<-40 || angle>40 || 1==flagStop || voltage<1110)
	{
		temp=1;
		motorsSetDir(MOTOR_LEFT, MOTOR_STOP);
		motorsSetDir(MOTOR_RIGHT, MOTOR_STOP);
	}
	else
		temp=0;
	
	return temp;			
}

static uint16_t limitThrust(int32_t value)
{
  if(value > UINT16_MAX)
  {
    value = UINT16_MAX;
  }
  else if(value < -UINT16_MAX)
  {
    value = -UINT16_MAX;
  }

  return (int16_t)value;
}
