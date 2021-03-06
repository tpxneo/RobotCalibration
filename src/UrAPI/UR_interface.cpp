#include "UR_interface.h"
#include <cmath>
#include <iostream>
#include <fstream>
#include <iomanip>
using namespace std;

namespace {
	const double ZERO_THRESH = 0.00000001;
	int SIGN(double x) {
		return (x > 0) - (x < 0);
	}
	const double PI = 3.1415926535;

	const double d1 = 0.089159;
	const double a2 = -0.42500;
	const double a3 = -0.39225;
	const double d4 = 0.10915;
	const double d5 = 0.09465;
	const double d6 = 0.0823;
}


UR_interface::UR_interface()
{
	connectedFlag = false;
}

UR_interface::~UR_interface(void)
{
	modbus_close(mb);
	modbus_free(mb);
	delete sender;
	delete sServer;
	delete server;
	//delete dashboard_sender;
}


bool UR_interface::isConnected()
{
	return connectedFlag;
}


bool UR_interface::connect_robot(const std::string ip, int port)
{

	sender = new SocketClient(ip, port); //默认30003端口
	//dashboard_sender = new SocketClient(ip,29999);
	mb = modbus_new_tcp(ip.c_str(), MODBUS_TCP_DEFAULT_PORT);

	if (mb == NULL)
	{
		fprintf(stderr, "Unable to allocate libmodbus context\n");
		return false;
	}

	if (modbus_connect(mb) == -1)
	{
		fprintf(stderr, "Connection failed:%s\n", modbus_strerror(errno));
		modbus_free(mb);
		return false;
	}
	else
	{
		connectedFlag = true;
	}
	return true;
}

void UR_interface::disconnect_robot()
{
	if (!connectedFlag)  return;
	modbus_close(mb);
	modbus_free(mb);
}

void UR_interface::GetJointAngle(double joint_angle[6])
{
	uint16_t reg[6];

	modbus_read_registers(mb, 270, 6, reg);

	for (int i = 0; i < 6; i++)
	{
		joint_angle[i] = (double)reg[i];

		if (joint_angle[i] > 32768)
			joint_angle[i] = joint_angle[i] - 65536;

		joint_angle[i] = joint_angle[i] / 1000;  //单位是弧度
	}

	if (joint_angle[0] < -25 / 57.3)
		joint_angle[0] += 2 * PI;
	if (joint_angle[0] > 285 / 57.3)
		joint_angle[0] -= 2 * PI;
	if (joint_angle[1] > 5 / 57, 3)
		joint_angle[1] -= 2 * PI;

	for (int i = 2; i < 6; i++)
	{
		if (joint_angle[i] > PI)
			joint_angle[i] -= 2 * PI;
		if (joint_angle[i] < -PI)
			joint_angle[i] += 2 * PI;
	}
}

void UR_interface::GetTCPPos(double pos[6])
{
	uint16_t reg[6];

	modbus_read_registers(mb, 400, 6, reg); //查询地址400~405(10进制)6个寄存器中的数据

	for (int i = 0; i < 6; i++)
	{
		pos[i] = (double)reg[i];

		if (pos[i] > 32768)
			pos[i] = pos[i] - 65536;
		if (i < 3)
			pos[i] = pos[i] / 10000;     //注意！单位是m
		else
			pos[i] = pos[i] / 1000;
		cout << pos[i] << endl;
	}
}


void UR_interface::axisangle_2_matrix(const double UR_AxisAngle[3], double mat[3][3])
{
	double angle = 0;
	double c = 0;
	double s = 0;
	double t = 0;
	double x = 0;
	double y = 0;
	double z = 0;

	for (int i = 0; i < 3; i++)
	{
		angle += UR_AxisAngle[i] * UR_AxisAngle[i];
	}
	angle = sqrt(angle); //计算等效转角angle

	x = UR_AxisAngle[0] / angle;
	y = UR_AxisAngle[1] / angle;    // (x,y,z)为单位向量
	z = UR_AxisAngle[2] / angle;

	c = cos(angle);
	s = sin(angle);
	t = 1 - c;

	mat[0][0] = t*x*x + c;
	mat[0][1] = t*x*y - z*s;
	mat[0][2] = t*x*z + y*s;
	mat[1][0] = t*x*y + z*s;
	mat[1][1] = t*y*y + c;         //参考《机器人学导论》英文版第3版 P47 公式2.80
	mat[1][2] = t*y*z - x*s;
	mat[2][0] = t*x*z - y*s;
	mat[2][1] = t*y*z + x*s;
	mat[2][2] = t*z*z + c;
}

void UR_interface::UR6params_2_matrix(const double UR_6params[6], double mat[4][4])
{
	double UR_AxisAngle[3] = { 0,0,0 };
	double UR_RotationMatrix[3][3];

	for (int i = 0; i < 3; i++)
		UR_AxisAngle[i] = UR_6params[3 + i];   // UR_6params最后三个分量为axisangle

	axisangle_2_matrix(UR_AxisAngle, UR_RotationMatrix);

	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			mat[i][j] = UR_RotationMatrix[i][j];
		}
		mat[3][i] = 0;
		mat[i][3] = UR_6params[i];
	}

	mat[3][3] = 1;
}


void UR_interface::matrix_2_axisangle(const double mat[3][3], double UR_AxisAngle[3])
{
	double angle = 0;
	double temp1 = 0;
	double temp2 = 0;
	double temp3 = 0;
	double x = 0;
	double y = 0;
	double z = 0;

	//need to be modified later
	double epsilon1 = 0.01;
	double epsilon2 = 0.1;
	//end 

	/*singularity found,first check for identity matrix which must have +1 for all terms
	  in leading diagonaland zero in other terms
	*/

	if ((abs(mat[0][1] - mat[1][0]) < epsilon1)
		&& (abs(mat[0][2] - mat[2][0]) < epsilon1)        //矩阵对称性检查（angle为0°或180°时矩阵为对称阵）
		&& (abs(mat[1][2] - mat[2][1]) < epsilon1))
		/*singularity found,first check for identity matrix which must have +1 for all terms
					 in leading diagonaland zero in other terms
		*/

	{
		if ((abs(mat[0][1] + mat[1][0]) < epsilon2)
			&& (abs(mat[0][2] + mat[2][0]) < epsilon2)
			&& (abs(mat[1][2] + mat[2][1]) < epsilon2)
			&& (abs(mat[0][0] + mat[1][1] + mat[2][2] - 3) < epsilon2))   //m近似为单位矩阵

		{
			for (int i = 0; i < 3; i++)
			{
				UR_AxisAngle[i] = 0;             //等效转角angle为0°

			}
			return;
		}


		// otherwise this singularity is angle = 180
		angle = PI;
		double xx = (mat[0][0] + 1) / 2;
		double yy = (mat[1][1] + 1) / 2;
		double zz = (mat[2][2] + 1) / 2;
		double xy = (mat[0][1] + mat[1][0]) / 4;
		double xz = (mat[0][2] + mat[2][0]) / 4;
		double yz = (mat[1][2] + mat[2][1]) / 4;

		if ((xx > yy) && (xx > zz))
		{ // m[0,0) is the largest diagonal term
			if (xx < epsilon1)
			{
				x = 0;
				y = 0.7071;
				z = 0.7071;
			}
			else
			{
				x = sqrt(xx);
				y = xy / x;
				z = xz / x;
			}
		}
		else if (yy > zz)
		{
			// m[1,1) is the largest diagonal term
			if (yy < epsilon1)
			{
				x = 0.7071;
				y = 0;
				z = 0.7071;
			}
			else
			{
				y = sqrt(yy);
				x = xy / y;
				z = yz / y;
			}
		}
		else
		{ // m[2,2) is the largest diagonal term so base result on this
			if (zz < epsilon1)
			{
				x = 0.7071;
				y = 0.7071;
				z = 0;
			}
			else
			{
				z = sqrt(zz);
				x = xz / z;
				y = yz / z;
			}
		}
		// return 180 deg rotation
		UR_AxisAngle[0] = x*PI;
		UR_AxisAngle[1] = y*PI;
		UR_AxisAngle[2] = z*PI;
		return;
	}


	temp1 = pow((mat[2][1] - mat[1][2]), 2) + pow((mat[0][2] - mat[2][0]), 2) + pow((mat[1][0] - mat[0][1]), 2);
	temp1 = sqrt(temp1);   // tmp1 = 2*sin(angle)    根据公式2.80中矩阵元素对称性可以计算出sin(angle)

	if (abs(temp1) < 0.001)
		temp1 = 1;
	// prevent divide by zero, should not happen if matrix is orthogonal and should be
	// caught by singularity test above, but I've left it in just in case
	angle = acos((mat[0][0] + mat[1][1] + mat[2][2] - 1) / 2);

	x = (mat[2][1] - mat[1][2]) / temp1;
	y = (mat[0][2] - mat[2][0]) / temp1;
	z = (mat[1][0] - mat[0][1]) / temp1;   //参考《机器人学导论》英文版第3版 P48 公式2.82
	UR_AxisAngle[0] = x*angle;
	UR_AxisAngle[1] = y*angle;
	UR_AxisAngle[2] = z*angle;
}



void UR_interface::matrix_2_UR6params(const double mat[4][4], double UR_6params[6])
{
	double m3x3[3][3];
	double AxisAngle[3];
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			m3x3[i][j] = mat[i][j];
		}
	}
	matrix_2_axisangle(m3x3, AxisAngle);

	for (int i = 0; i < 3; i++)
	{
		UR_6params[i] = mat[i][3];
		UR_6params[i + 3] = AxisAngle[i];
	}
}



void UR_interface::Movej_joint(const double joint_angle[6], float a, float v, float t, float r) //注意：带默认参数的函数,声明和定义两者默认值只能在一个地方出现，不能同时出现。
{
	stringstream temp;   //创建一个流
	string cmd;
	temp << "movej([" << joint_angle[0] << "," << joint_angle[1] << "," << joint_angle[2] << "," << joint_angle[3] << "," << joint_angle[4] << "," << joint_angle[5] << "],"
		<< a << "," << v << "," << t << "," << r << ")";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::Movej_pose(const double tcp_pose[6], float a, float v, float t, float r)
{
	stringstream temp;
	string cmd;
	temp << "movej(p[" << tcp_pose[0] << "," << tcp_pose[1] << "," << tcp_pose[2] << "," << tcp_pose[3] << "," << tcp_pose[4] << "," << tcp_pose[5] << "],"
		<< a << "," << v << "," << t << "," << r << ")";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::Movel_pose(const double tcp_pose[6], float a, float v, float t, float r)
{
	stringstream temp;
	string cmd;
	temp << "movej(p[" << tcp_pose[0] << "," << tcp_pose[1] << "," << tcp_pose[2] << "," << tcp_pose[3] << "," << tcp_pose[4] << "," << tcp_pose[5] << "],"
		<< a << "," << v << "," << t << "," << r << ")";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::Movej_TCP(const double relative_pose[6], float a, float v, float t, float r)
{
	stringstream temp;
	string cmd;
	temp << "def f():\n"
		<< "wp1=get_actual_tcp_pose()\n"      
		<< "movej(pose_trans(wp1,p[" << relative_pose[0] << "," << relative_pose[1] << "," << relative_pose[2] << ","
		<< relative_pose[3] << "," << relative_pose[4] << "," << relative_pose[5] << "])," << a << "," << v << "," << t << "," << r << ")\n"
		<< "end\n";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::Movel_TCP(const double relative_pose[6], float a, float v, float t, float r)
{
	stringstream temp;
	string cmd;
	temp << "def f():\n"
		<< "wp1=get_actual_tcp_pose()\n"
		<< "movel(pose_trans(wp1,p[" << relative_pose[0] << "," << relative_pose[1] << "," << relative_pose[2] << ","
		<< relative_pose[3] << "," << relative_pose[4] << "," << relative_pose[5] << "])," << a << "," << v << "," << t << "," << r << ")\n"
		<< "end\n";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::Movej_delta(const double joint_angle[6], float a, float v, float t, float r)
{
	stringstream temp;   //创建一个流
	string cmd;
	temp << "def f():\n"
		<< "b1=get_actual_joint_positions()\n"
		<< "b1[0] = b1[0] +" << joint_angle[0] << "\n"
		<< "b1[1] = b1[1] +" << joint_angle[1] << "\n"
		<< "b1[2] = b1[2] +" << joint_angle[2] << "\n"
		<< "b1[3] = b1[3] +" << joint_angle[3] << "\n"
		<< "b1[4] = b1[4] +" << joint_angle[4] << "\n"
		<< "b1[5] = b1[5] +" << joint_angle[5] << "\n"
		<< "movej(b1," << a << "," << v << "," << t << "," << r << ")\n"
		<< "end\n";

	cmd = temp.str();
	sender->SendLine(cmd);
}

void UR_interface::Servoj(const double joint_angle[6], float t, float lookaheadtime, int gain)
{
	stringstream temp;
	string cmd;

	temp << "def f():\n"
		<< " q1 = [" << joint_angle[0] << "," << joint_angle[1] << "," << joint_angle[2] << "," << joint_angle[3] << "," << joint_angle[4] << "," << joint_angle[5] << "]\n"
		<< " q0 = get_actual_joint_positions()\n"
		<< " time =" << time << "\n"
		<< " i = 1\n"
		<< " while i < 6:\n"
		<< "  if norm(q0[i] - q1[i]) > time*20:\n"
		<< "    q1[i] = q0[i]\n"
		<< "    textmsg(\"LargeJoint = \", i)\n"
		<< "  end\n"
		<< " end\n"
		<< " servoj(q1, 0, 0, time, 0)\n"
		<< "end\n";

	//temp << "servoj([" << joint_angle[0] << "," << joint_angle[1] << "," << joint_angle[2] << "," << joint_angle[3] << "," << joint_angle[4] << "," << joint_angle[5] << "],"
	//	<< "0,0," << t << "," << lookaheadtime << "," << gain << ")";
	cmd = temp.str();
	sender->SendLine(cmd);
}

void UR_interface::Servoc(const double tcp_pose[6], float a, float v, float r) {
	stringstream temp;
	string cmd;
	temp << "servoc(p[" << tcp_pose[0] << "," << tcp_pose[1] << "," << tcp_pose[2] << "," << tcp_pose[3] << "," << tcp_pose[4] << "," << tcp_pose[5] << "],"
		<< a << "," << v << "," << r << ")";
	cmd = temp.str();
	sender->SendLine(cmd);
}

void UR_interface::Servoj_TCP(const double relative_pose[6], float t, float lookaheadtime, int gain)
{
	stringstream temp;
	string cmd;
	temp << "def f():\n"
		<< "wp1=get_actual_tcp_pose()\n"
		<< "servoj(pose_trans(wp1,p[" << relative_pose[0] << "," << relative_pose[1] << "," << relative_pose[2] << ","
		<< relative_pose[3] << "," << relative_pose[4] << "," << relative_pose[5] << "])," << "0,0," << t << "," << lookaheadtime << "," << gain << ")\n"
		<< "end\n";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::yb_speedl_relative(double tcp_speed[6], float a, float t)
{
	stringstream temp;
	string cmd;
	temp << "def f():\n"
		<< "wp1 = p[0,0,0,0,0,0]\n"
		<< "speedl(pose_trans(wp1,p[" << tcp_speed[0] << "," << tcp_speed[1] << "," << tcp_speed[2] << ","
		<< tcp_speed[3] << "," << tcp_speed[4] << "," << tcp_speed[5] << "])," << a <<  "," << t << ","<< ")\n"
		<< "end\n";
	cmd = temp.str();
	sender->SendLine(cmd);
}

void UR_interface::GoHome(float a, float v)//定义中不再给出默认值
{
	stringstream temp;   
	string cmd;
	temp<<"movej([0.00,-1.5708,0.00,-1.5708,0.00,0.00]," <<a<<","<<v<<")";  //UR5零位时的joint_angle值
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::Speedl(const double tcp_speed[6],float a, float t)
{
	stringstream temp;   
	string cmd;
	temp<<"speedl(["<<tcp_speed[0]<<","<<tcp_speed[1]<<","<<tcp_speed[2]<<","<<tcp_speed[3]<<","<<tcp_speed[4]<<","<<tcp_speed[5]<<"],"
		<<a<<","<<t<<")";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::Speedj(const double tcp_speed[6], float a, float t)
{
	stringstream temp;   
	string cmd;
	temp<<"speedj(["<<tcp_speed[0]<<","<<tcp_speed[1]<<","<<tcp_speed[2]<<","<<tcp_speed[3]<<","<<tcp_speed[4]<<","<<tcp_speed[5]<<"],"
		<<a<<","<<t<<")";
	cmd = temp.str();
	sender->SendLine(cmd);
}



void UR_interface::Stopl(float a)
{
	stringstream temp;
	string cmd;
	temp << "stopl(" << a << ")";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::Stopj(float a)
{
	stringstream temp;
	string cmd;
	temp << "stopj(" << a << ")";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::PowerOffRobot()
{
	//dashboard_sender->SendLine("shutdown"); //两种方式关机
	sender->SendLine("powerdown()");   //Shutdown the robot, and power off the robot and controller.
}


void UR_interface::SetReal()
{
	sender->SendLine("set real");
}


void UR_interface::SetSim()
{
	sender->SendLine("set sim");
}


void UR_interface::SetSpeed(double ratio)
{
	stringstream temp;
	string cmd;
	temp << "set speed" << " " << ratio;  //ratio为0~1之间的一个小数
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::SetRobotMode(int mode)
{
	switch (mode)
	{
	case ROBOT_RUNNING_MODE:
		sender->SendLine("end_teach_mode()");//UR10
		break;
	case ROBOT_FREEDRIVE_MODE:
		this->StartTeachMode();
		break;
	default:break;
	}
}

void UR_interface::StartTeachMode()
{
	stringstream temp;
	string cmd;
	temp << "def teach():\n"
		<< "	while(True):\n"
		<< "		teach_mode()\n"
		<< "		sleep(0.01)\n"
		<< "	end\n"
		<< "end\n"
		<< "teach()\n";
	cmd = temp.str();
	sender->SendLine(cmd);
}

int UR_interface::GetRobotMode()
{
	uint16_t reg[1];
	modbus_read_registers(mb, 258, 1, reg);

	return reg[0];  //返回值0-9代表不同机器人模式
}


int UR_interface::isSecurityStopped()
{
	uint8_t state[1];
	modbus_read_bits(mb, 261, 1, state); //The function uses the Modbus function code 0x01 (read coil status)

	return state[0];
}


int UR_interface::isEmergencyStopped()
{
	uint8_t state[1];
	modbus_read_bits(mb, 262, 1, state);

	return state[0];
}

void UR_interface::SetTCPPos(const double pos[6])
{
	stringstream temp;   //创建一个流
	string cmd;
	temp << "set_tcp(p[" << pos[0] << "," << pos[1] << "," << pos[2] << "," << pos[3] << "," << pos[4] << "," << pos[5] << "])";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::ServoTest(double delta[], double time) {
	stringstream temp;
	string cmd;
	temp << "def f():\n"
		<< "pose1 = get_actual_tcp_pose()\n"
		<< "q0 = get_actual_joint_positions()\n"
		<< "pose1[0] = pose1[0] +" << delta[0] << "\n"
		<< "pose1[1] = pose1[1] +" << delta[1] << "\n"
		<< "pose1[2] = pose1[2] +" << delta[2] << "\n"
		<< "pose1[3] = pose1[3] +" << delta[3] << "\n"
		<< "pose1[4] = pose1[4] +" << delta[4] << "\n"
		<< "pose1[5] = pose1[5] +" << delta[5] << "\n"
		<< "q1 = get_inverse_kin(pose1,q0)\n"
		<< "servoj(q1, 0, 0, " << time << ",0)\n"
		<< "end\n";
	cmd = temp.str();
	sender->SendLine(cmd);
}

void UR_interface::ControlServoj(double joint_angle[6], double time)
{
	stringstream temp;
	string cmd;
	temp << "def f():\n"        //再处理一下，是否可以直接根据目标位置来写
		<< "textmsg(\"value = \", 3)"
		<< "pose1 = get_actual_tcp_pose()\n"
		<< "q0 = get_actual_joint_positions()\n"
		<< "pose1[0] = pose1[0] +" << joint_angle[0] << "\n"
		<< "pose1[1] = pose1[1] +" << joint_angle[1] << "\n"
		<< "pose1[2] = pose1[2] +" << joint_angle[2] << "\n"
		<< "pose1[3] = pose1[3] +" << joint_angle[3] << "\n"
		<< "pose1[4] = pose1[4] +" << joint_angle[4] << "\n"
		<< "pose1[5] = pose1[5] +" << joint_angle[5] << "\n"
		<< "q1 = get_inverse_kin(pose1,q0)\n"
		<< "delta_q = q1\n"
		<< "delta_q[0] = (q1[0] - q0[0])/5.0\n"
		<< "delta_q[1] = (q1[1] - q0[1])/5.0\n"
		<< "delta_q[2] = (q1[2] - q0[2])/5.0\n"
		<< "delta_q[3] = (q1[3] - q0[3])/5.0\n"
		<< "delta_q[4] = (q1[4] - q0[4])/5.0\n"
		<< "delta_q[5] = (q1[5] - q0[5])/5.0\n"
		<< "new_q = q0\n"
		<< "i = 0\n"
		<< "while i < 5:\n"
		<< " new_q[0] = new_q[0] + delta_q[0]\n"
		<< " new_q[1] = new_q[1] + delta_q[1]\n"
		<< " new_q[2] = new_q[2] + delta_q[2]\n"
		<< " new_q[3] = new_q[3] + delta_q[3]\n"
		<< " new_q[4] = new_q[4] + delta_q[4]\n"
		<< " new_q[5] = new_q[5] + delta_q[5]\n"
		<< " servoj(new_q, 0, 0, 0.008,0)\n"
		<< " i=i+1\n"
		<< " end\n"
		<< "end\n";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::ControlServoj2(double joint_angle[6], double time)
{
	stringstream temp;
	string cmd;
	temp << "def f():\n"        //再处理一下，是否可以直接根据目标位置来写
		<< "pose1 = get_actual_tcp_pose()\n"
		<< "pose0 = pose1\n"
		<< "q0 = get_actual_joint_positions()\n"
		<< "pose1[0] = pose1[0] +" << joint_angle[0] << "\n"
		<< "pose1[1] = pose1[1] +" << joint_angle[1] << "\n"
		<< "pose1[2] = pose1[2] +" << joint_angle[2] << "\n"
		<< "pose1[3] = pose1[3] +" << joint_angle[3] << "\n"
		<< "pose1[4] = pose1[4] +" << joint_angle[4] << "\n"
		<< "pose1[5] = pose1[5] +" << joint_angle[5] << "\n"
		<< "i = 0\n"
		<< "while i < 5:\n"
		<< " interpolate_pose(pose0, pose1, 0.2 * i)\n"
		<< " new_q = get_inverse_kin(pose1,q0)\n"
		<< " servoj(new_q, 0, 0, 0.008,0)\n"
		<< " i=i+1\n"
		<< " end\n"
		<< "end\n";
	cmd = temp.str();
	sender->SendLine(cmd);
}

void UR_interface::SetURasClient() {
	stringstream temp;
	string cmd;
	temp
		<< "def f():\n"
		<< "q0 = get_actual_joint_positions()\n"
		<< "q0[5] = q0[5] + 0.1\n"
		<< "movej(q0, a=1.4, v=1.05, t=0, r=0)\n"
		<< "sleep(3.0)\n"
		<< "var_1 = socket_open(\"169.254.174.117\", 8080, \"socket_1\")\n"
		<< "sleep(1.0)\n"
		//<< "bool_val = read_input_boolean_register(3)\n"
		<< "socket_send_line(\"Input a string\",\"socket_1\")\n"
		<< "sleep(1.0)\n"
		<< "socket_send_line(\"Input a string\",\"socket_1\")\n"
		<< "sleep(1.0)\n"
		<< "socket_send_line(\"Input a string\",\"socket_1\")\n"
		<< "sleep(1.0)\n"
		<< "socket_send_int(2,\"socket_1\")\n"
		<< "sleep(0.01)\n"
		<< "socket_send_int(3,\"socket_1\")\n"
		<< "sleep(0.01)\n"
		<< "socket_send_int(4,\"socket_1\")\n"
		<< "sleep(0.01)\n"
		<< "socket_send_int(5,\"socket_1\")\n"
		<< "sleep(0.01)\n"
		<< "socket_send_int(6,\"socket_1\")\n"
		<< "sleep(0.01)\n"
		<< "socket_send_int(7,\"socket_1\")\n"
		<< "sleep(0.01)\n"
		<< "socket_send_int(8,\"socket_1\")\n"
		<< "sleep(0.01)\n"
		<< "socket_send_int(9,\"socket_1\")\n"
		<< "sleep(0.01)\n"
		<< "socket_send_int(10,\"socket_1\")\n"
		<< "sleep(0.01)\n"
		<< "socket_send_line(\"Input a string\",\"socket_1\")\n"
		<< "sleep(1.0)\n"
		<< "end\n";

	cmd = temp.str();
	sender->SendLine(cmd);
}

void UR_interface::ServoSyncTest(const double relative_pose[6], float t, float lookaheadtime, int gain) {
	stringstream temp;
	string cmd;
	temp << "def f():\n"
		<< "wp1=get_actual_tcp_pose()\n"
		<< "q0 = get_actual_joint_positions()\n"
		<< "transPos = pose_trans(wp1,p[" << relative_pose[0] << "," << relative_pose[1] << "," << relative_pose[2] << ","
		<< relative_pose[3] << "," << relative_pose[4] << "," << relative_pose[5] << "])\n"
		<< "pose = get_inverse_kin(transPos, q0)\n"
		<< "servoj(pose,0,0," << t << "," << lookaheadtime << "," << gain << ")\n"
		<< "sync()\n"
		<< "end\n";
	cmd = temp.str();
	sender->SendLine(cmd);
}


bool UR_interface::GetDigitalInput(int n) {
	uint8_t state[1];
	modbus_read_bits(mb, n, 1, state); //The function uses the Modbus function code 0x01 (read coil status)
	return state[0];
}

bool UR_interface::GetDigitalOutput(int n) {
	uint8_t state[1];
	modbus_read_bits(mb, 16 + n, 1, state); //The function uses the Modbus function code 0x01 (read coil status)
	return state[0];
}

void UR_interface::SetDigitalOutput(int n, const bool b) {
	if (b)
	{
		stringstream temp;
		string cmd;
		temp << "set_standard_digital_out(" << n << ",True)";
		cmd = temp.str();
		sender->SendLine(cmd);
	}
	else {
		stringstream temp;
		string cmd;
		temp << "set_standard_digital_out(" << n << ",False)";
		cmd = temp.str();
		sender->SendLine(cmd);
	}
}

void UR_interface::SetAnalogOutput_Volt(int n, float f) {
	stringstream temp;
	string cmd;
	temp << "def f():\n"
		<< "set_analog_outputdomain(" << n << ",1)\n"
		<< "set_standard_analog_out(" << n << "," << f << ")\n"
		<< "end\n";
	cmd = temp.str();
	sender->SendLine(cmd);
}

void UR_interface::SetAnalogOutput_Current(int n, float f) {
	stringstream temp;
	string cmd;
	temp << "def f():\n"
		<< "set_analog_outputdomain(" << n << ",0)\n"
		<< "set_standard_analog_out(" << n << "," << f << ")\n"
		<< "end\n";
	cmd = temp.str();
	sender->SendLine(cmd);
}


double UR_interface::GetAnalogInput(int n, bool& domain) {
	uint16_t reg[2];

	modbus_read_registers(mb, 4 + 2 * n, 2, reg); //查询地址400~405(10进制)6个寄存器中的数据
	domain = (bool)reg[1];
	if (domain)
	{
		return (double)reg[0] / 65536 * 10;
	}
	else
	{
		return (double)reg[0] / 65536 * 16 + 4;
	}
}

double UR_interface::GetAnalogOutput(int n, bool& domain) {
	uint16_t reg[2];

	modbus_read_registers(mb, 16 + 2 * n, 2, reg); //查询地址400~405(10进制)6个寄存器中的数据
	domain = (bool)reg[1];
	if (domain)
	{
		return (double)reg[0] / 65536 * 10;
	}
	else
	{
		return (double)reg[0] / 65536 * 16 + 4;
	}
}


void UR_interface::GetForwardKinematic(const double* q, double T[4][4]) {
	double s1 = sin(*q), c1 = cos(*q); q++;
	double q234 = *q, s2 = sin(*q), c2 = cos(*q); q++;
	double s3 = sin(*q), c3 = cos(*q); q234 += *q; q++;
	q234 += *q; q++;
	double s5 = sin(*q), c5 = cos(*q); q++;
	double s6 = sin(*q), c6 = cos(*q);
	double s234 = sin(q234), c234 = cos(q234);
	T[0][0] = (c6*(s1*s5 + ((c1*c234 - s1*s234)*c5) / 2.0 + ((c1*c234 + s1*s234)*c5) / 2.0) -
		(s6*((s1*c234 + c1*s234) - (s1*c234 - c1*s234))) / 2.0); 
	T[0][1] = (-(c6*((s1*c234 + c1*s234) - (s1*c234 - c1*s234))) / 2.0 -
		s6*(s1*s5 + ((c1*c234 - s1*s234)*c5) / 2.0 + ((c1*c234 + s1*s234)*c5) / 2.0));
	T[0][2] = -((c1*c234 - s1*s234)*s5) / 2.0 + c5*s1 - ((c1*c234 + s1*s234)*s5) / 2.0; 
	T[0][3] = -((d5*(s1*c234 - c1*s234)) / 2.0 - (d5*(s1*c234 + c1*s234)) / 2.0 -
		d4*s1 + (d6*(c1*c234 - s1*s234)*s5) / 2.0 + (d6*(c1*c234 + s1*s234)*s5) / 2.0 -
		a2*c1*c2 - d6*c5*s1 - a3*c1*c2*c3 + a3*c1*s2*s3); 
	T[1][0] = (c6*(((s1*c234 + c1*s234)*c5) / 2.0 - c1*s5 + ((s1*c234 - c1*s234)*c5) / 2.0) +
		s6*((c1*c234 - s1*s234) / 2.0 - (c1*c234 + s1*s234) / 2.0)); 
	T[1][1] = (c6*((c1*c234 - s1*s234) / 2.0 - (c1*c234 + s1*s234) / 2.0) -
		s6*(((s1*c234 + c1*s234)*c5) / 2.0 - c1*s5 + ((s1*c234 - c1*s234)*c5) / 2.0)); 
	T[1][2] = -c1*c5 - ((s1*c234 + c1*s234)*s5) / 2.0 - ((s1*c234 - c1*s234)*s5) / 2.0; 
	T[1][3] = -((d5*(c1*c234 - s1*s234)) / 2.0 - (d5*(c1*c234 + s1*s234)) / 2.0 + d4*c1 +
		(d6*(s1*c234 + c1*s234)*s5) / 2.0 + (d6*(s1*c234 - c1*s234)*s5) / 2.0 + d6*c1*c5 -
		a2*c2*s1 - a3*c2*c3*s1 + a3*s1*s2*s3); 
	T[2][0] = -((s234*c6 - c234*s6) / 2.0 - (s234*c6 + c234*s6) / 2.0 - s234*c5*c6); 
	T[2][1] = -(s234*c5*s6 - (c234*c6 + s234*s6) / 2.0 - (c234*c6 - s234*s6) / 2.0);
	T[2][2] = ((c234*c5 - s234*s5) / 2.0 - (c234*c5 + s234*s5) / 2.0); 
	T[2][3] = (d1 + (d6*(c234*c5 - s234*s5)) / 2.0 + a3*(s2*c3 + c2*s3) + a2*s2 -
		(d6*(c234*c5 + s234*s5)) / 2.0 - d5*c234); 
	T[3][0] = T[3][1] = T[3][2] = 0; T[3][3] = 1.0;
}

void UR_interface::GetForwardKinematic(const double* q, double* pos) {
	double T[4][4];
	this->GetForwardKinematic(q, T);
	this->matrix_2_UR6params(T, pos);
}

int UR_interface::GetInverseKinematic(const double T[4][4], double* q_sols, double q6_des) {
	int num_sols = 0;
	double T00 = T[0][0]; double T01 = T[0][1]; double T02 = T[0][2]; double T03 = T[0][3];
	double T10 = T[1][0]; double T11 = T[1][1]; double T12 = T[1][2]; double T13 = T[1][3];
	double T20 = T[2][0]; double T21 = T[2][1]; double T22 = T[2][2]; double T23 = T[2][3];

	////////////////////////////// shoulder rotate joint (q1) //////////////////////////////
	////////////////////////////// shoulder rotate joint (q1) //////////////////////////////
	double q1[2];
	{
		double A = d6*T12 - T13;
		double B = d6*T02 - T03;
		double R = A*A + B*B;
		if (fabs(A) < ZERO_THRESH) {
			double div;
			if (fabs(fabs(d4) - fabs(B)) < ZERO_THRESH)
				div = -SIGN(d4)*SIGN(B);
			else
				div = -d4 / B;
			double arcsin = asin(div);
			if (fabs(arcsin) < ZERO_THRESH)
				arcsin = 0.0;
			if (arcsin < 0.0)
				q1[0] = arcsin + 2.0*PI;
			else
				q1[0] = arcsin;
			q1[1] = PI - arcsin;
		}
		else if (fabs(B) < ZERO_THRESH) {
			double div;
			if (fabs(fabs(d4) - fabs(A)) < ZERO_THRESH)
				div = SIGN(d4)*SIGN(A);
			else
				div = d4 / A;
			double arccos = acos(div);
			q1[0] = arccos;
			q1[1] = 2.0*PI - arccos;
		}
		else if (d4*d4 > R) {
			return num_sols;
		}
		else {
			double arccos = acos(d4 / sqrt(R));
			double arctan = atan2(-B, A);
			double pos = arccos + arctan;
			double neg = -arccos + arctan;
			if (fabs(pos) < ZERO_THRESH)
				pos = 0.0;
			if (fabs(neg) < ZERO_THRESH)
				neg = 0.0;
			if (pos >= 0.0)
				q1[0] = pos;
			else
				q1[0] = 2.0*PI + pos;
			if (neg >= 0.0)
				q1[1] = neg;
			else
				q1[1] = 2.0*PI + neg;
		}
	}
	////////////////////////////////////////////////////////////////////////////////

	////////////////////////////// wrist 2 joint (q5) //////////////////////////////
	double q5[2][2];
	{
		for (int i = 0; i<2; i++) {
			double numer = (T03*sin(q1[i]) - T13*cos(q1[i]) - d4);
			double div;
			if (fabs(fabs(numer) - fabs(d6)) < ZERO_THRESH)
				div = SIGN(numer) * SIGN(d6);
			else
				div = numer / d6;
			double arccos = acos(div);
			q5[i][0] = arccos;
			q5[i][1] = 2.0*PI - arccos;
		}
	}
	////////////////////////////////////////////////////////////////////////////////

	{
		for (int i = 0; i<2; i++) {
			for (int j = 0; j<2; j++) {
				double c1 = cos(q1[i]), s1 = sin(q1[i]);
				double c5 = cos(q5[i][j]), s5 = sin(q5[i][j]);
				double q6;
				////////////////////////////// wrist 3 joint (q6) //////////////////////////////
				if (fabs(s5) < ZERO_THRESH)
					q6 = q6_des;
				else {
					q6 = atan2(SIGN(s5)*-(T01*s1 - T11*c1),
						SIGN(s5)*(T00*s1 - T10*c1));
					if (fabs(q6) < ZERO_THRESH)
						q6 = 0.0;
					if (q6 < 0.0)
						q6 += 2.0*PI;
				}
				////////////////////////////////////////////////////////////////////////////////

				double q2[2], q3[2], q4[2];
				///////////////////////////// RRR joints (q2,q3,q4) ////////////////////////////
				double c6 = cos(q6), s6 = sin(q6);
				double x04x = -s5*(T02*c1 + T12*s1) - c5*(s6*(T01*c1 + T11*s1) - c6*(T00*c1 + T10*s1));
				double x04y = c5*(T20*c6 - T21*s6) - T22*s5;
				double p13x = d5*(s6*(T00*c1 + T10*s1) + c6*(T01*c1 + T11*s1)) - d6*(T02*c1 + T12*s1) +
					T03*c1 + T13*s1;
				double p13y = T23 - d1 - d6*T22 + d5*(T21*c6 + T20*s6);

				double c3 = (p13x*p13x + p13y*p13y - a2*a2 - a3*a3) / (2.0*a2*a3);
				if (fabs(fabs(c3) - 1.0) < ZERO_THRESH)
					c3 = SIGN(c3);
				else if (fabs(c3) > 1.0) {
					// TODO NO SOLUTION
					continue;
				}
				double arccos = acos(c3);
				q3[0] = arccos;
				q3[1] = 2.0*PI - arccos;
				double denom = a2*a2 + a3*a3 + 2 * a2*a3*c3;
				double s3 = sin(arccos);
				double A = (a2 + a3*c3), B = a3*s3;
				q2[0] = atan2((A*p13y - B*p13x) / denom, (A*p13x + B*p13y) / denom);
				q2[1] = atan2((A*p13y + B*p13x) / denom, (A*p13x - B*p13y) / denom);
				double c23_0 = cos(q2[0] + q3[0]);
				double s23_0 = sin(q2[0] + q3[0]);
				double c23_1 = cos(q2[1] + q3[1]);
				double s23_1 = sin(q2[1] + q3[1]);
				q4[0] = atan2(c23_0*x04y - s23_0*x04x, x04x*c23_0 + x04y*s23_0);
				q4[1] = atan2(c23_1*x04y - s23_1*x04x, x04x*c23_1 + x04y*s23_1);
				////////////////////////////////////////////////////////////////////////////////
				for (int k = 0; k<2; k++) {
					if (fabs(q2[k]) < ZERO_THRESH)
						q2[k] = 0.0;
					else if (q2[k] < 0.0) q2[k] += 2.0*PI;
					if (fabs(q4[k]) < ZERO_THRESH)
						q4[k] = 0.0;
					else if (q4[k] < 0.0) q4[k] += 2.0*PI;
					q_sols[num_sols * 6 + 0] = q1[i];    q_sols[num_sols * 6 + 1] = q2[k];
					q_sols[num_sols * 6 + 2] = q3[k];    q_sols[num_sols * 6 + 3] = q4[k];
					q_sols[num_sols * 6 + 4] = q5[i][j]; q_sols[num_sols * 6 + 5] = q6;
					num_sols++;
				}

			}
		}
	}
	for (size_t i = 0; i < num_sols; i++)
	{
		if (q_sols[i * 6] > 295 / 57.3)
			q_sols[i * 6] -= 2 * PI;
		if (q_sols[i * 6 + 1] > 5 / 57.3)
			q_sols[i * 6 + 1] -= 2 * PI;
		if (q_sols[i * 6 + 2] > 150 / 57.3)
			q_sols[i * 6 + 2] -= 2 * PI;
	}
	return num_sols;
}

int UR_interface::GetInverseKinematic(const double* pos, double* q_sols, double q6_des) {
	double T[4][4];
	this->UR6params_2_matrix(pos, T);
	return this->GetInverseKinematic(T, q_sols, q6_des);
}

void UR_interface::GetInverseKinematic(const double* pos, const double* q_near, double* q_sol) {
	double q_sols[8 * 6];
	double num = this->GetInverseKinematic(pos, q_sols);
	double minNorm = 10000;
	double tmpNorm;
	
	for (size_t i = 0; i < num; i++)
	{
		if (q_near[3] < 0){
			q_sols[6 * i + 3] -= 2 * PI;
		}
		if (q_near[4] < 0) {
			q_sols[6 * i + 4] -= 2 * PI;
		}
		if (q_near[5] < 0) {
			q_sols[6 * i + 5] -= 2 * PI;
		}
		tmpNorm = (q_near[0] - q_sols[6 * i]) * (q_near[0] - q_sols[6 * i])
			+ (q_near[1] - q_sols[6 * i + 1]) * (q_near[1] - q_sols[6 * i + 1])
			+ (q_near[2] - q_sols[6 * i + 2]) * (q_near[2] - q_sols[6 * i + 2])
			+ (q_near[3] - q_sols[6 * i + 3]) * (q_near[3] - q_sols[6 * i + 3])
			+ (q_near[4] - q_sols[6 * i + 4]) * (q_near[4] - q_sols[6 * i + 4])
			+ (q_near[5] - q_sols[6 * i + 5]) * (q_near[5] - q_sols[6 * i + 5]);
		if (tmpNorm < minNorm){
			minNorm = tmpNorm;
			for (size_t j = 0; j < 6; j++){
				q_sol[j] = q_sols[6 * i + j];
			}
		}
		else continue;
	}
}

void UR_interface::RealTimeControl() {
	server = new SocketServer(8080, SOMAXCONN);
	stringstream temp;
	string cmd;
	temp << "def driverProg():\n"
		<< "  textmsg(\"value=\", 3)\n"
		<< "  MSG_OUT = 1\n"
		<< "  MSG_QUIT = 2\n"
		<< "  MSG_WAYPOINT_FINISHED = 5\n"
		<< "  joint_a = 2\n"  //机器人加速度2rad/s
		<< "  joint_delta_record = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]\n" //记录6个关节上次的速度（用delta表示），保证连续加速减速
		<< "  MSG_FLAG = 0\n"   //控制程序：-1 for QUIT; 0 for WAIT; 1 for RUN; 2 for no Ndi information
		<< "  pi = 3.14159265359\n"
		<< "  q_origin = get_actual_joint_positions()\n"
		<< "  obtain_data_pos = [7, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]\n" //从pc获取的数据
		<< "  joint1_delta = [0.0, 0.0, 0.0, 0.0, 0.0]\n"  //6个关节每次插值，关节角度变化量
		<< "  joint2_delta = [0.0, 0.0, 0.0, 0.0, 0.0]\n"
		<< "  joint3_delta = [0.0, 0.0, 0.0, 0.0, 0.0]\n"
		<< "  joint4_delta = [0.0, 0.0, 0.0, 0.0, 0.0]\n"
		<< "  joint5_delta = [0.0, 0.0, 0.0, 0.0, 0.0]\n"
		<< "  joint6_delta = [0.0, 0.0, 0.0, 0.0, 0.0]\n"
		<< "  servo_q1 = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]\n"  //40ms被切割成5个姿态
		<< "  servo_q2 = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]\n"
		<< "  servo_q3 = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]\n"
		<< "  servo_q4 = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]\n"
		<< "  servo_q5 = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]\n"
		<< "  def send_out(msg):\n"
		<< "    enter_critical\n"
		<< "    socket_send_int(MSG_OUT)\n"
		<< "    socket_send_line(msg)\n"
		<< "    socket_send_line(\"~\")\n"
		<< "    exit_critical\n"
		<< "  end\n"
		<< "  def sign(a):\n"
		<< "    if a >= 0:\n"  //a = 0 时置1是为了速度为0时能运动起来
		<< "      return 1\n"
		<< "    else:\n"
		<< "      return 0\n"
		<< "    end\n"
		<< "  end\n"
		<< "  def safe_range(input):\n"
		<< "    if norm(input) > 0.001:\n"
		<< "      return 0.001*sign(input)\n"
		<< "    else:\n"
		<< "      return input\n"
		<< "    end\n"
		<< "  end\n"
		<< "  def notNeg(rec, del):\n"
		<< "    if rec * del < 0:\n"
		<< "      return 0\n"
		<< "    else:\n"
		<< "      return del\n"
		<< "    end\n"
		<< "  end\n"
		<< "  def compute_delta(delta_q, joints_speed, record):\n"   //返回5个插值大小   //delta为距离目标的角度位移，joints_speed为当前角速度rad/s
		<< "    output = [0,0,0,0,0]\n"   //5个插值均不变
		<< "    if norm(delta_q) < 0.0001:\n "   //误差很小的时候，或速度为0时，Δ置0, 
		<< "      output = [0,0,0,0,0]\n"   //5个插值均不变
		//<< "	  textmsg(\"case =\", 1)\n"////////////
		<< "    elif (delta_q * record <= 0) and (norm(record) > 0.00001):\n"  //反向的话，减速
		<< "      delta = 0.00001*sign(delta_q)\n" //与之前的速度相反，表示减速
		<< "      output = [record+delta, notNeg(record, record+delta*2), notNeg(record, record+delta*3), notNeg(record, record+delta*4), notNeg(record, record+delta*5)]\n"   //减速
		//<< "	  textmsg(\"case =\", 2)\n"////////////
		<< "    elif (norm(delta_q) < 0.05) and (norm(record) > 0.00001):\n"  //当距离小的时候（度），且速度不为0时，开始减速  
		<< "      delta = -0.00001*sign(record)\n" //与之前的速度相反，表示减速
		<< "      output = [record+delta, record+delta*2, record+delta*3, record+delta*4, record+delta*5]\n"   //减速
		//<< "	  textmsg(\"case =\", 3)\n"////////////
		//<< "    elif (norm(delta_q) < 0.1) and (norm(record) > 0.00001):\n"   //当距离中等时（度），且速度不为0时，开始匀速运行
		//<< "      output = [record,record,record,record,record]\n" //0.015
		//<< "	  textmsg(\"case =\", 4)\n"////////////
		<< "    else:\n" //当距离远的时候，或者速度为0时，开始加速运行
		//<< "	  textmsg(\"case =\", 5)\n"////////////
		<< "      delta = sign(delta_q)*0.00001\n"////////////
		<< "      output = [record+delta, record+delta*2, record+delta*3, record+delta*4, record+delta*5]\n"
		<< "    end\n"
		<< "    output[0] = safe_range(output[0])\n"
		<< "    output[1] = safe_range(output[1])\n"
		<< "    output[2] = safe_range(output[2])\n"
		<< "    output[3] = safe_range(output[3])\n"
		<< "    output[4] = safe_range(output[4])\n"
		<< "    return output\n"
		<< "  end\n"
		<< "  def joints_add(q0, q1):\n"
		<< "    q2 = [q0[0] + q1[0], q0[1] + q1[1], q0[2] + q1[2], q0[3] + q1[3], q0[4] + q1[4], q0[5] + q1[5]]\n"
		<< "    return q2\n"
		<< "  end\n"
		<< "  SERVO_IDLE = 0\n"
		<< "  SERVO_RUNNING = 1\n"
		<< "  quitCount = 0\n" //用于判断程序退出，当多次无法读取socket_read_ascii_float时，退出程序
		<< "  cmd_servo_state = SERVO_IDLE\n"
		<< "  cmd_servo_id = 0  # 0 = idle, -1 = stop\n"
		<< "  cmd_servo_q = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]\n"
		<< "  cmd_servo_dt = 0.0\n"
		<< "  def set_servo_setpoint(id, q, dt):\n"
		<< "    enter_critical\n"
		<< "    cmd_servo_state = SERVO_RUNNING\n"
		<< "    cmd_servo_id = id\n"
		<< "    cmd_servo_q = q\n"
		<< "    cmd_servo_dt = dt\n"
		<< "    exit_critical\n"
		<< "  end\n"
		<< "  thread servoThread():\n"
		<< "    state = SERVO_IDLE\n"
		<< "    while True:\n"
		<< "      enter_critical\n"
		<< "      q = cmd_servo_q\n"
		<< "      dt = cmd_servo_dt\n"
		<< "      id = cmd_servo_id\n"
		<< "      do_brake = False\n"
		<< "      if (state == SERVO_RUNNING) and (cmd_servo_state == SERVO_IDLE):\n"
		<< "        do_brake = True\n"
		<< "      end\n"
		<< "      state = cmd_servo_state\n"
		<< "      cmd_servo_state = SERVO_IDLE\n"
		<< "      exit_critical\n"
		<< "      if do_brake:\n"
		<< "        send_out(\"Braking\")\n"
		<< "        sync()\n"
		<< "      elif state == SERVO_RUNNING:\n"
		<< "        servoj(q, 0, 0, dt)\n"
		<< "        #send_out(\"Servoed\")\n"
		<< "      else:\n"
		<< "        sync()\n"
		<< "      end\n"
		<< "    end\n"
		<< "  end\n"
		<< "  thread obtainDataThread():\n"
		<< "	while True:\n"
		<< "      obtain_data_pos = socket_read_ascii_float(7)\n"
		<< "      if obtain_data_pos[0] == 0:\n"
		<< "        quitCount = quitCount + 1\n"
		<< "        continue\n"
		<< "      end\n"
		<< "      quitCount = 0\n"  //obtian_pos为相对TCP的位置
		<< "      MSG_FLAG = obtain_data_pos[1]\n"
		<< "	  textmsg(\"MSG_FLAG =\", MSG_FLAG)\n"////////////
		//<< "      if (MSG_FLAG == 0) or (MSG_FLAG == -1):\n"  //wait和stop状态下不进行插值
		<< "      if MSG_FLAG < 1:\n"
		<< "	    sleep(0.04)\n"
		<< "        continue\n"
		<< "      end\n"
		<< "      enter_critical\n"
		<< "      obtian_pos_TCP = p[obtain_data_pos[2], obtain_data_pos[3], obtain_data_pos[4], obtain_data_pos[5], obtain_data_pos[6], obtain_data_pos[7]]\n"
		<< "	  wp1=get_actual_tcp_pose()\n"
		<< "      obtian_pos = pose_trans(wp1, obtian_pos_TCP)\n"
		<< "      q0 = get_actual_joint_positions()\n"
		<< "      obtain_q = get_inverse_kin(obtian_pos, q0)\n"
		<< "	  delta_q = [(obtain_q[0] - q0[0]), (obtain_q[1] - q0[1]), (obtain_q[2] - q0[2]), (obtain_q[3] - q0[3]), (obtain_q[4] - q0[4]), (obtain_q[5] - q0[5])]\n"
		<< "      speed = get_actual_joint_speeds()\n"
		<< "      if MSG_FLAG == 2:\n"  //当导航信息未返回时（参考架没被看到），发送的pose无用，根据当前速度get_actual_joint_speeds计算delta_q
		<< "        delta_q = [speed[0]/125, speed[1]/125, speed[2]/125, speed[3]/125, speed[4]/125, speed[5]/125]\n"
		<< "      end\n"
		<< "      joint1_delta = compute_delta(delta_q[0], speed[0], joint_delta_record[0])\n"
		<< "      joint2_delta = compute_delta(delta_q[1], speed[1], joint_delta_record[1])\n"
		<< "      joint3_delta = compute_delta(delta_q[2], speed[2], joint_delta_record[2])\n"
		<< "      joint4_delta = compute_delta(delta_q[3], speed[3], joint_delta_record[3])\n"
		<< "      joint5_delta = compute_delta(delta_q[4], speed[4], joint_delta_record[4])\n"
		<< "      joint6_delta = compute_delta(delta_q[5], speed[5], joint_delta_record[5])\n"
		<< "      delta_q = [joint1_delta[0],joint2_delta[0],joint3_delta[0],joint4_delta[0],joint5_delta[0],joint6_delta[0]]\n"
		<< "      servo_q1 = joints_add(q_origin, delta_q)\n"
		<< "      delta_q = [joint1_delta[1],joint2_delta[1],joint3_delta[1],joint4_delta[1],joint5_delta[1],joint6_delta[1]]\n"
		<< "      servo_q2 = joints_add(servo_q1, delta_q)\n"
		<< "      delta_q = [joint1_delta[2],joint2_delta[2],joint3_delta[2],joint4_delta[2],joint5_delta[2],joint6_delta[2]]\n"
		<< "      servo_q3 = joints_add(servo_q2, delta_q)\n"
		<< "      delta_q = [joint1_delta[3],joint2_delta[3],joint3_delta[3],joint4_delta[3],joint5_delta[3],joint6_delta[3]]\n"
		<< "      servo_q4 = joints_add(servo_q3, delta_q)\n"
		<< "      joint_delta_record = [joint1_delta[4],joint2_delta[4],joint3_delta[4],joint4_delta[4],joint5_delta[4],joint6_delta[4]]\n"  //更新记录数据
		<< "      servo_q5 = joints_add(servo_q4, joint_delta_record)\n"
		<< "      q_origin = servo_q5\n"
		<< "      exit_critical\n"
		<< "	  sleep(0.04)\n"
		<< "    end\n"
		<< "  end\n"
		<< "  socket_open(\"169.254.174.117\", 8080)\n"
		<< "  thread_servo = run servoThread()\n"
		<< "  thread_data = run obtainDataThread()\n"
		<< "  sleep(1.0)\n"
		<< "  thread_data = run obtainDataThread()\n"
		<< "  t = 0\n"
		<< "  while True:\n"   //增加一个判断，比如按下按钮
		<< "    if (quitCount >= 4) or (MSG_FLAG == -1):\n"
		<< "      break\n"
		<< "    elif (MSG_FLAG == 1) or (MSG_FLAG == 2):\n"
		<< "      set_servo_setpoint(t, servo_q1, 0.008)\n"
		<< "      t = t + 0.008\n"
		<< "      sleep(0.008)\n"
		<< "      set_servo_setpoint(t, servo_q2, 0.008)\n"
		<< "      t = t + 0.008\n"
		<< "      sleep(0.008)\n"
		<< "      set_servo_setpoint(t, servo_q3, 0.008)\n"
		<< "      t = t + 0.008\n"
		<< "      sleep(0.008)\n"
		<< "      set_servo_setpoint(t, servo_q4, 0.008)\n"
		<< "      t = t + 0.008\n"
		<< "      sleep(0.008)\n"
		<< "      set_servo_setpoint(t, servo_q5, 0.008)\n"
		<< "      t = t + 0.008\n"
		<< "      sleep(0.008)\n"
		<< "    else:\n"
		<< "      sleep(0.04)\n"
		<< "      continue\n"
		<< "    end\n"
		<< "  end\n"
		<< "  socket_send_int(MSG_QUIT)\n"
		<< "end\n";
	cmd = temp.str();
	sender->SendLine(cmd);
	sServer = server->Accept();

	//写发送数据的内容
}

void UR_interface::SendRealTimePose(const int flag, const double tcp_pose[6]) {
	if (sServer == nullptr) return; 
	stringstream temp;
	string cmd;
	temp << "(" << flag << "," << tcp_pose[0] << "," << tcp_pose[1] << "," << tcp_pose[2] << ","
		<< tcp_pose[3] << "," << tcp_pose[4] << "," << tcp_pose[5] << ")";
	cmd = temp.str();
	sServer->SendLine(cmd);
}

double UR_interface::Read() {
	double pos[6] = { 0, 0, 0.1, 0, 0, 0 };
	for (size_t i = 0; i < 100000000; i++)
	{
		pos[2] = 0.05 * cos(i / 200)*cos(i / 200);
		//pos[3] = 0.01 * cos(i/100);
		stringstream temp;
		string cmd;
		temp << "(1," << pos[0] << "," << pos[1] << "," << pos[2] << ","
			<< pos[3] << "," << pos[4] << "," << pos[5] << ")";
		cmd = temp.str();
		sServer->SendLine(cmd);
		Sleep(35);
	}
	return 0;
	
}