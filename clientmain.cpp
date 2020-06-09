#include <stdio.h>
#include <stdlib.h>
#include <string.h>   
#include <errno.h>    
#include <signal.h>
#include <unistd.h>   
#include <sys/types.h>   
#include <sys/socket.h>   
#include <sys/time.h>
#include <netinet/in.h>   
#include <arpa/inet.h> 
#include <ctime> 

#include <calcLib.h>
#include "protocol.h"

using namespace std;

#define PORT 4950
#define MAXSIZE 200		//定义buffer数组长度

int protoCount=0;		//记录重发所支持的传输协议的次数，如果超过三次，则放弃并打印错误信息
int answerCount=0;		//记录重发计算结果的次数，如果超过三次，则放弃并打印错误信息
bool isProto=false;		//标记服务端是否已经返回了对自己所支持的协议的回应，如果没有，则继续重发直到三次用完
bool isAnswer=false;	//标记服务端是否已经返回了对自己计算答案的回应，如果没有，则继续重发直到三次用完

int socket_fd;	//客户端套接字id
struct sockaddr_in server_addr;	//服务端地址信息
int server_len;	//服务端地址结构的长度  
int send_num;	//统计接受来自服务端的字节数
int recv_num;	//统计发送给服务端的字节数
char send_buf[MAXSIZE];		//存储发送信息的数组
char recv_buf[MAXSIZE];		//存储接收到的信息的数组
int id;	//服务端赋予客户端的唯一标识符
struct calcProtocol result;		//存储题目的答案准备发送给服务端
struct calcMessage msg;			//存储客户端支持的协议信息

struct itimerval protoTime;		//用于设置重发协议请求的定时器
struct itimerval answerTime;	//用于设置重发答案的定时器

bool isProtocolLost=false;    /*因为正常情况下报文在本机网络中传输很难丢失，为了能够模拟出丢失的效果，在启动客户端时可以选择故意丢包模式（即在输入服务端地址后再输入参数0），也就是说假装发送欺骗客户端以为
                                *自己发送了所支持的协议信息或者计算答案，然后等待服务端的响应。显而易见，因为根本没发送出去，服务端一定不会响应，所以才能够触发客户端的重发
                                *机制。原理是在每次发送的时候进行一个随机数概率判定（1/2的概率不发送） 
                                */
bool isAnswerLost=false;
bool isDeceiveId=false;

void setDeliberateMissSendMode(int mode){
  initCalcLib();
  if(mode==0){
  	isProtocolLost=true;
  }else if(mode==1){
  	isAnswerLost=true;
  }else if(mode==2){
  	isDeceiveId=true;
  }
}

void delete_alarm(struct itimerval& tmp){
	tmp.it_value.tv_sec=0;
	tmp.it_value.tv_usec=0;
	signal(SIGALRM,SIG_IGN);
}

void send_answer(int sig){
	if(isAnswer){	//如果客户端已经收到来自服务端的答案响应的话，则终止重发
		delete_alarm(answerTime);
		return;
	}
	if(answerCount>3){	//如果重发答案超过三次，则结束
		printf("client: has resent answer for 3 times without any response so terminate the program.\n");
		exit(1);
	}else if(answerCount>0){
		printf("client: message lost, resent the answer for %d time\n",answerCount);
	}

	answerCount++;	//更新重发次数

	if(isAnswerLost&&randomInt()<=50){	//如果设置了故意不传输模式，则按照概率结果决定是否发送
		return;
	}
	//发送计算题答案给服务端
	send_num=sendto(socket_fd,&result,sizeof(result),0,(struct sockaddr*)&server_addr,server_len);
	if(send_num<0){
		perror("function sendto error");  
		printf("error: client failed in sending messages to server\n");
		exit(1); 
	}
}

void send_protocol(int sig){
	if(isProto){	//如果客户端已经收到来自服务端的协议响应的话，则终止重发
		delete_alarm(protoTime);
		return;
	}
	if(protoCount>3){	//如果重发协议超过三次，则结束
		printf("client: has resent protocol requirement for 3 times without any response so terminate the program.\n");
		exit(1);
	}else if(protoCount>0){
		printf("client: message lost, resent the protocol for %d time\n",protoCount);
	}else{
		printf("client: sending message to server asking for protocol support\n");
	}

	protoCount++;	//更新重发次数

	if(isProtocolLost&&randomInt()<=50){	//如果设置了故意不传输模式，则按照概率结果决定是否发送
		return;
	}

	//发送第一次消息给服务端
	send_num=sendto(socket_fd,&msg,sizeof(msg),0,(struct sockaddr*)&server_addr,server_len);
	if(send_num<0){
		perror("function sendto error");  
		printf("error: client failed in sending messages to server\n");
		exit(1); 
	}
}

void init_alarm(__sighandler_t method,struct itimerval& tmpAlarm){
	tmpAlarm.it_interval.tv_sec=2;
	tmpAlarm.it_interval.tv_usec=0;
	tmpAlarm.it_value.tv_sec=2;
	tmpAlarm.it_value.tv_usec=0;
	signal(SIGALRM,SIG_IGN);
	signal(SIGALRM, method);
	setitimer(ITIMER_REAL,&tmpAlarm,NULL);
}

int main(int argc, char *argv[]){
	//判断输入参数数量正确与否
	if (argc>3) {
	  	printf("error: argument number wrong. dir: %s argument num: (%d)\n",argv[0],argc);
	  	exit(1);
	}else if(argc==3){
		printf("client: open deliberate miss sending mode\n");
		if(strcmp(argv[2],"0")==0){
			setDeliberateMissSendMode(0);
		}else if(strcmp(argv[2],"1")==0){
			setDeliberateMissSendMode(1);
		}else if(strcmp(argv[2],"2")==0){
			setDeliberateMissSendMode(2);
		}
	}

	memset(&server_addr, 0, sizeof(server_addr));	//初始化服务地址信息，以免受到之前存过的东西影响	
	server_addr.sin_family = AF_INET;  
	server_addr.sin_addr.s_addr = inet_addr(argv[1]);  
	server_addr.sin_port = htons(PORT);  
	server_len = sizeof(server_addr);

	//创建客户端UDP套接字
	printf("client: creating socket\n");
	if((socket_fd=socket(AF_INET, SOCK_DGRAM, 0))<0){
		perror("socket creating error");  
		printf("error: client can not create a socket\n");
		exit(1); 
	}

	msg.type=22;
	msg.message=0;
	msg.protocol=17;
	msg.major_version=1;
	msg.minor_version=0;

	send_protocol(0);
	init_alarm(send_protocol,protoTime);

	//尝试接收来自服务端的回应
	recv_num = recvfrom(socket_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&server_addr, (socklen_t *)&server_len);
	if(recv_num<0){
		perror("function recvfrom error");  
		printf("error: client failed in receiving messages from server\n");
		exit(1); 
	}
	isProto=true;

	struct calcProtocol *protocolResponse=(struct calcProtocol*)recv_buf;
	id=(*protocolResponse).id;
	if(protocolResponse->type==2){
		printf("server: do not support the protocol proposed by client\n");
		exit(1); 
	}else{
		printf("server: support the protocol proposed by client\n");
	}

	uint32_t arith=protocolResponse->arith;
	result.type=2;
	result.major_version=1;
	result.minor_version=0;
	if(isDeceiveId){
		result.id=0;
	}else{
		result.id=id;
	}
	result.arith=arith;

	char *operation;
	if(arith<=4){
		int value1=protocolResponse->inValue1;	
		int value2=protocolResponse->inValue2;
		result.inValue1=value1,result.inValue2=value2;
		if(arith==1){
			result.inResult=value1+value2;
			operation="add";
		}else if(arith==2){
			result.inResult=value1-value2;
			operation="sub";
		}else if(arith==3){
			result.inResult=value1*value2;
			operation="mul";
		}else{
			result.inResult=value1/value2;
			operation="div";
		}
		printf("server: please answer question \"%s %d %d\"\n",operation,value1,value2);
		printf("client: sending answer \"%d\" to server\n",result.inResult);
	}else{
		double fvalue1=protocolResponse->flValue1;	
		double fvalue2=protocolResponse->flValue2;
		result.flValue1=fvalue1,result.flValue2=fvalue2;
		if(arith==5){
			result.flResult=fvalue1+fvalue2;
			operation="fadd";
		}else if(arith==6){
			result.flResult=fvalue1-fvalue2;
			operation="fsub";
		}else if(arith==7){
			result.flResult=fvalue1*fvalue2;
			operation="fmul";
		}else{
			result.flResult=fvalue1/fvalue2;
			operation="fdiv";
		}
		printf("server: please answer question \"%s %lf %lf\"\n",operation,fvalue1,fvalue2);
		printf("client: sending answer \"%lf\" to server\n",result.flResult);
	}

	send_answer(0);
	init_alarm(send_answer,answerTime);

	//尝试接收来自服务端的对自己计算答案正确与否的回应
	memset(recv_buf,0,sizeof(recv_buf));
	recv_num = recvfrom(socket_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&server_addr, (socklen_t *)&server_len);
	if(recv_num<0){
		perror("function recvfrom error");  
		printf("error: client failed in receiving messages from server\n");
		exit(1); 
	}
	isAnswer=true;

	struct calcMessage *answerResponse=(struct calcMessage *)recv_buf;
	if(answerResponse->message==1){
		printf("server: Answer is OK\n");
	}else if(answerResponse->message==2){
		if(answerResponse->type==3){
			printf("server: wrong ID! You can not deceive with ID!\n");
		}else{
			printf("server: Answer is Not OK\n");
		}
	}

	close(socket_fd);	//任务完成，关闭连接
	printf("client: program shutdown\n");


	return 0;
}
