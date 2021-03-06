#ifndef _UR_INTERFACE_H
#define _UR_INTERFACE_H

#include <cstring>
#include <sstream>
#include "socket.h"
#include "modbus-tcp.h"
#pragma comment(lib, "libmodbus.lib")  

/* robot modes */
#define ROBOT_RUNNING_MODE 0
#define ROBOT_FREEDRIVE_MODE 1
#define ROBOT_READY_MODE 2
#define ROBOT_INITIALIZING_MODE 3
#define ROBOT_SECURITY_STOPPED_MODE 4
#define ROBOT_EMERGENCY_STOPPED_MODE 5
#define ROBOT_FATAL_ERROR_MODE 6
#define ROBOT_NO_POWER_MODE 7
#define ROBOT_NOT_CONNECTED_MODE 8
#define ROBOT_SHUTDOWN_MODE 9

/****************************************************************************************************
类名：UR_interface
功能：提供控制UR机器人的C++函数接口
时间：20181205
外部依赖：1. libmodbus: modbus-tcp接口,用于获取UR机器人寄存器中的数据
		 2. Socket:提供TCP及UDP通信功能
****************************************************************************************************/
class UR_interface
{
public:
	UR_interface();
	~UR_interface();

	////////////////////////////////////////////////////////////////
	/////////////////////   连接机器人   ////////////////////////////
	////////////////////////////////////////////////////////////////
	bool connect_robot(const std::string ip, int port = 30003); //连接UR机器人,参数为UR机器人IP地址和端口号,默认为30003端口
	void disconnect_robot();
	bool isConnected();

	////////////////////////////////////////////////////////////////
	///////////////////// 机器人运动控制 ////////////////////////////
	////////////////////////////////////////////////////////////////
	void Movej_joint(const double joint_angle[6], float a = 3, float v = 0.1, float t = 0, float r = 0);	//移动到基座标系下指定位置,参数为关节角
	void Movej_pose(const double tcp_pose[6], float a = 3, float v = 0.1, float t = 0, float r = 0);		//移动到基座标系下指定位置,参数为TCP位置
	void Movel_pose(const double tcp_pose[6], float a = 1.2, float v = 0.1, float t = 0, float r = 0);		//移动到基座标系下指定位置,线性插值方式
	void Movej_delta(const double joint_angle[6], float a = 1.2, float v = 0.1, float t = 0, float r = 0);	//旋转相对关节角度，单位rad

	void Speedl(const double tcp_speed[6], float a = 2, float t = 20);			//TCP速度指令
	void Speedj(const double joint_speed[6], float a = 2, float t = 20);		//关节速度指令,单位是rad/s

	void Servoj(const double joint_angle[6], float t, float lookaheadtime = 0.1, int gain = 300);
	void Servoc(const double tcp_pose[6], float a = 1.2, float v = 0.1, float r = 0);

	void GoHome(float a = 1, float v = 0.5);    //UR5回零位
	void Stopl(float a = 2);					//Decellerate tool speed to zero    a: tool acceleration(m/s^2)
	void Stopj(float a = 0.2);					//Decellerate joint speeds to zero  a: tool acceleration(rad/s^2)
	
	////////////////////////////////////////////////////////////////
	///////////////////// 实时控制机器人 ////////////////////////////
	////////////////////////////////////////////////////////////////
	//机器人实时跟踪导航，机器人为客户端，PC为服务端，PC需要不断发送数据
	//需要实时向客户端发送tcp下的坐标
	void RealTimeControl();  
	//PC不断发送机器人的位置，需要以<40ms的频率发送
	// @param flag			-1 for QUIT; 0 for WAIT; 1 for RUN
	void SendRealTimePose(const int flag, const double tcp_pose[6]);  
	//协同操作
	void CollaborativeOperate();


	///////////////////////////////////////////////////////////
	////////////////// 相对于TCP坐标系移动 /////////////////////
	///////////////////////////////////////////////////////////
	void Movej_TCP(const double relative_pose[6], float a = 1.2, float v = 0.1, float t = 0, float r = 0);	//相对于工具坐标系移动
	void Movel_TCP(const double relative_pose[6], float a = 1.2, float v = 0.1, float t = 0, float r = 0);	//相对于工具坐标系移动
	void Servoj_TCP(const double relative_pose[6], float t, float lookaheadtime = 0.1, int gain = 300);			//相对于工具坐标系移动


	////////////////////////////////////////////////////////////////
	/////////////////////获得机器人参数 ////////////////////////////
	////////////////////////////////////////////////////////////////
	void GetJointAngle(double joint_angle[6]);	//获取机器人6个关节角度,单位是弧度
	void GetTCPPos(double pos[6]);				//获取机器人TCP位置和姿态,单位是m
	int GetRobotMode();							//获得机器人当前模式
	int	isSecurityStopped();					//是否安全停机,返回值：0为false,1为true
	int	isEmergencyStopped();					//是否紧急停止,返回值：0为false,1为true


	////////////////////////////////////////////////////////////////
	/////////////////////设置机器人参数 ////////////////////////////
	////////////////////////////////////////////////////////////////
	void SetSpeed(double ratio);				//设置速度比例
	void SetRobotMode(int mode);				//设置机器人模式
	void SetReal();								//设置为真实机器人
	void SetSim();								//设置为模拟机器人
	void PowerOffRobot();						//关闭机器人及控制器
	void StartTeachMode();						//开启机器人示教模式
	void SetTCPPos(const double pos[6]);		//设置机器人TCP姿态



	////////////////////////////////////////////////////////////////
	/////////////////////机器人参数计算 ////////////////////////////
	////////////////////////////////////////////////////////////////
	static void matrix_2_UR6params(const double mat[4][4], double UR_6params[6]);		//4x4齐次矩阵转换为UR机器人内部TCP位置姿态参数
	static void matrix_2_axisangle(const double mat[3][3], double UR_AxisAngle[3]);	//3x3旋转矩阵转换为axis-angle  
	static void UR6params_2_matrix(const double UR_6params[6], double mat[4][4]);		//TCP位置姿态参数转换为4x4齐次矩阵
	static void axisangle_2_matrix(const double UR_AxisAngle[3], double mat[3][3]);	//axis-angle转换为3x3旋转矩阵

	///////////////////////////////////////////////////////////
	////////////////////////    I/O    ////////////////////////
	///////////////////////////////////////////////////////////
	void SetAnalogOutput_Volt(int n, float f);			//n为0~1, f为0~1
	void SetAnalogOutput_Current(int n, float f);		//n为0~1	, f为0~1
	void SetDigitalOutput(int n, const bool b);			//0~8 for BOX, 9~10 for TOOL
	bool GetDigitalInput(int n);						//0~8 for BOX, 9~10 for TOOL
	bool GetDigitalOutput(int n);						//0~8 for BOX, 9~10 for TOOL
	double GetAnalogInput(int n, bool& domain);			//n: 0~4  domain: 0 = current(mA), 1 = voltage(mV)
	double GetAnalogOutput(int n, bool& domain);		//n: 0~4  domain: 0 = current(mA), 1 = voltage(mV)

	///////////////////////////////////////////////////////////
	////////////////////  UR5正/逆运动学  //////////////////////
	///每台机器人参数并不完全一致，自行求逆的误差较大，不推荐使用///
	///////////////////////////////////////////////////////////
	// @param q       The 6 joint values
	// @param T       The 4x4 end effector pose in row-major ordering
	void GetForwardKinematic(const double* q, double T[4][4]);
	// @param q       The 6 joint values
	// @param pos     The end effector pose
	void GetForwardKinematic(const double* q, double* pos);
	// @param T       The 4x4 end effector pose in row-major ordering
	// @param q_sols  An 8x6 array of doubles returned, all angles should be in [0,2*PI)
	// @param q6_des  An optional parameter which designates what the q6 value should take
	//                in case of an infinite solution on that joint.
	// @return        Number of solutions found (maximum of 8)
	int GetInverseKinematic(const double T[4][4], double* q_sols, double q6_des = 0.0);
	// @param pos     The end effector pose
	// @param q_sols  An 8x6 array of doubles returned, all angles should be in [0,2*PI)
	// @param q6_des  An optional parameter which designates what the q6 value should take
	//                in case of an infinite solution on that joint.
	// @return        Number of solutions found (maximum of 8)
	int GetInverseKinematic(const double* pos, double* q_sols, double q6_des = 0.0);

	void GetInverseKinematic(const double* pos, const double* q_near, double* q_sol);



	////////////////////////////////////////////////////////////////
	/////////////////////   测试函数    ////////////////////////////
	////////////////////////////////////////////////////////////////
	void yb_speedl_relative(double tcp_speed[6], float a = 2, float t = 20);
	void ControlServoj(double joint_angle[6], double time = 0.008);
	void ControlServoj2(double joint_angle[6], double time = 0.008);
	void ServoTest(double delta[], double time);//
	void SetURasClient();
	void ServoSyncTest(const double relative_pose[6], float t, float lookaheadtime = 0.1, int gain = 300);
	double Read();

private:
	modbus_t *mb;
	bool connectedFlag;
	SocketClient *sender;          //TCP客户端,一般通过30003端口给UR控制器发送字符串指令
	SocketServer *server;          //TCP服务端, UR机器人访问其8080端口
	Socket* sServer;
	//SocketClient *dashboard_sender;//与DashBoard server通信：通过29999端口给UR面板GUI发送指令,如关机,加载程序等功能


};
#endif        
