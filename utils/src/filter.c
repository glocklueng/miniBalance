/**
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie control firmware
 *
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
 * filter.h - Filtering functions
 */
#include "filter.h"
#include "imu.h"

/**
 * IIR filter the samples.
 */
int16_t iirLPFilterSingle(int32_t in, int32_t attenuation,  int32_t* filt)
{
  int32_t inScaled;
  int32_t filttmp = *filt;
  int16_t out;

  if (attenuation > (1<<IIR_SHIFT))
  {
    attenuation = (1<<IIR_SHIFT);
  }
  else if (attenuation < 1)
  {
    attenuation = 1;
  }

  // Shift to keep accuracy
  inScaled = in << IIR_SHIFT;
  // Calculate IIR filter
  filttmp = filttmp + (((inScaled-filttmp) >> IIR_SHIFT) * attenuation);
  // Scale and round
  out = (filttmp >> 8) + ((filttmp & (1 << (IIR_SHIFT - 1))) >> (IIR_SHIFT - 1));
  *filt = filttmp;

  return out;
}

static float K1 = 0.02; 
static float angle = 0, angle_dot; 	
static float Q_angle = 0.001;			// ����������Э����
static float Q_gyro = 0.003;			// 0.03 ����������Э���� ����������Э����Ϊһ��һ�����о���
static float R_angle = 0.5;				// ����������Э���� �Ȳ���ƫ��
static float dt = IMU_UPDATE_DT;	//                 
static char  C_0 = 1;
static float Q_bias, Angle_err;
static float PCt_0, PCt_1, E;
static float K_0, K_1, t_0, t_1;
static float Pdot[4] = {0, 0, 0, 0};
static float PP[2][2] = {{1, 0}, {0, 1}};

float Kalman_Filter(float Accel,float Gyro)
{
	angle += (Gyro - Q_bias) * dt; //�������
	Pdot[0] = Q_angle - PP[0][1] - PP[1][0]; // Pk-����������Э�����΢��

	Pdot[1] = -PP[1][1];
	Pdot[2] = -PP[1][1];
	Pdot[3] = Q_gyro;
	
	PP[0][0] += Pdot[0] * dt;   	// Pk-����������Э����΢�ֵĻ���
	PP[0][1] += Pdot[1] * dt;   	// =����������Э����
	PP[1][0] += Pdot[2] * dt;
	PP[1][1] += Pdot[3] * dt;
		
	Angle_err = Accel - angle;		//zk-�������
	
	PCt_0 = C_0 * PP[0][0];
	PCt_1 = C_0 * PP[1][0];
	
	E = R_angle + C_0 * PCt_0;
	
	K_0 = PCt_0 / E;
	K_1 = PCt_1 / E;
	
	t_0 = PCt_0;
	t_1 = C_0 * PP[0][1];

	PP[0][0] -= K_0 * t_0;				//����������Э����
	PP[0][1] -= K_0 * t_1;
	PP[1][0] -= K_1 * t_0;
	PP[1][1] -= K_1 * t_1;
		
	angle	+= K_0 * Angle_err;	 //�������
	Q_bias	+= K_1 * Angle_err;	 //�������
	angle_dot   = Gyro - Q_bias;	 //���ֵ(�������)��΢��=���ٶ�
	
	return angle;
}

float Complementary_Filter(float angle_m, float gyro_m)
{
	angle = K1 * angle_m+ (1-K1) * (angle + gyro_m * dt);
	
	return angle;
}
