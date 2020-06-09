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
#include <map>
#include <ctime>

#include <calcLib.h>
#include "protocol.h"

using namespace std;

#define PORT 4950   //服务端端口号
#define MAXSIZE 200

int idCount=0;  //记录有多少个客户端连接过，从而下一次给一个新的客户端分配新id时只需要加个一就可以避免id重复的问题
map<char*,int> idMap;   //记录从ip:port字符串到id的映射, 显然, ip:port对于每个客户端来说一定能够保证是唯一的
time_t timeMap[1000];   //记录每个客户端id第一次与服务端通信的时间, 用来实现客户端超时未响应答案时移除任务
double ansMap[1000];    //记录每个客户端id被分配的任务答案，用于跟客户端返回的结果比较

//被定时器对应的信号处理器每隔1秒调用一次
void manageOutdatedClient(int signum){
  time_t now=time(0);
  for(map<char*,int>::iterator it=idMap.begin();it!=idMap.end();){  //每次调用遍历所有当前被分配任务但还未失效的客户端id
    if(now-timeMap[it->second]>10){   //如果当前时间距离客户端被分配任务的时间已经过去了十秒，即这10秒内服务端没有收到客户端答案，则服务端会抹去分配到任务和客户端id
      printf("server: client[%s] does not respond with answer in 10 seconds, so abort its job\n",it->first);
      idMap.erase(it++);
      continue;
    }
    it++;
  }
}

int main(int argc, char *argv[]){
  printf("server: creating a socket\n");

  //创建服务端UDP套接字
  int sock_fd=socket(AF_INET, SOCK_DGRAM, 0);
  if(sock_fd<0){
    perror("socket creating error");  
    printf("error: server can not create a socket\n");
    exit(1); 
  }

  struct sockaddr_in server_addr;
  int len;
  memset(&server_addr, 0, sizeof(struct sockaddr_in));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(PORT);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  len = sizeof(server_addr); 

  printf("server: binding the socket with local address\n");

  if(bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)  {  
    perror("socket bind address error");  
    printf("error: server can not bind the socket\n");
    exit(1);  
  }  

  int recv_num;   //存储每次接收到的字节数大小，用于判断接收是否异常(小于0)
  int send_num;   //存储每次发出信息的字节数，用于判断发送是否异常(小于0)  
  char send_buf[MAXSIZE]; 
  char recv_buf[MAXSIZE];  
  struct sockaddr_in sockaddr_client;   //每次收到来自客户端的数据包时，存储客户端地址信息

  initCalcLib();    //在调用外教提供的指令生成包前必须调用他提供的初始化函数

  struct itimerval alarm;   //这个定时器是用来每秒比较是否有客户端超过10秒没返回答案
  alarm.it_interval.tv_sec=1;
  alarm.it_interval.tv_usec=0;
  alarm.it_value.tv_sec=1;
  alarm.it_value.tv_usec=0;
  setitimer(ITIMER_REAL,&alarm,NULL);   //设置定时器
  signal(SIGALRM, manageOutdatedClient);  //设置信号处理函数

  printf("server: ready to wait for messages from clients\n");

  while(1){
    memset(&sockaddr_client,0,sizeof(sockaddr_client));   //重置客户端地址信息结构体，重复利用
    recv_num = recvfrom(sock_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&sockaddr_client, (socklen_t *)&len);
    if(recv_num<0){
      perror("function recvfrom error");  
      printf("error: server failed in receiving messages from client\n");
      exit(1); 
    }

    struct calcProtocol *clientData=(struct calcProtocol*)recv_buf;   //将接收到的信息强制转化为calcProtocol类型

    char client_addr[20];   //存储客户端的ip地址字符串
    inet_ntop(sockaddr_client.sin_family,&sockaddr_client.sin_addr,client_addr,sizeof(client_addr));
    char ip_and_port[30];   //存储ip+端口字符串作为唯一标识一个客户端程序的标识符
    sprintf(ip_and_port,"%s:%d\0",client_addr,sockaddr_client.sin_port);

    printf("--------------------from client[%s:%d]---------------------------\n",client_addr,sockaddr_client.sin_port);

    if(clientData->type==22){   //根据外教文档规定，如果type值为22，则是calcMessage类型（之所以能这么判断是因为calcProtocol和calcMessage拥有相同的首数据域）
      struct calcMessage * messa=(struct calcMessage*)recv_buf;
      printf("client: query for protocol support\n");
      if(messa->protocol!=17){  //服务端只支持protocol数据域为17的协议，如果客户端不支持这个协议，则终止
        printf("server: do not support the protocol proposed by client\n");
        struct calcMessage serverMessa;
        serverMessa.type=2;
        serverMessa.message=2;
        serverMessa.major_version=1;
        serverMessa.minor_version=0;
        send_num = sendto(sock_fd, &serverMessa, sizeof(serverMessa), 0, (struct sockaddr *)&sockaddr_client, len); 
        if(send_num<0){
          perror("send error");
          printf("error: server can not send messages to client for unknown reason\n");
        }
        continue;
      }else{
        printf("server: support the protocol proposed by client\n");

        if(idMap.find(ip_and_port)==idMap.end()){   //如果这是该客户端最近第一次访问服务端，则给他分配一个id标识符
          idMap[ip_and_port]=idCount;
          idCount++;
        }

        struct calcProtocol serverProto;  //存储服务端准备返回的计算题目
        int arithType=randomType()+1;     //生成随机运算类型
        serverProto.arith=arithType;

        int id=idMap[ip_and_port];
        serverProto.id=id;

        if(arithType<=4){   //小于5意味着是整数运算
          int value1=randomInt(),value2=randomInt();
          serverProto.inValue1=value1;
          serverProto.inValue2=value2;
          if(arithType==1){
            ansMap[id]=value1+value2;
          }else if(arithType==2){
            ansMap[id]=value1-value2;
          }else if(arithType==3){
            ansMap[id]=value1*value2;
          }else{
            ansMap[id]=value1/value2;
          }
          printf("server: going to send question \"%s %d %d\" to client\n",getRandomTypeName(arithType-1),value1,value2);
        }else{      //大于4意味着浮点数运算
          double fvalue1=randomFloat(),fvalue2=randomFloat();
          serverProto.flValue1=fvalue1;
          serverProto.flValue2=fvalue2;
          if(arithType==5){
            ansMap[id]=fvalue1+fvalue2;
          }else if(arithType==6){
            ansMap[id]=fvalue1-fvalue2;
          }else if(arithType==7){
            ansMap[id]=fvalue1*fvalue2;
          }else{
            ansMap[id]=fvalue1/fvalue2;
          }
          printf("server: going to send question \"%s %f %f\" to client\n",getRandomTypeName(arithType-1),fvalue1,fvalue2);
        }
        timeMap[id]=time(0);    //记录任务开始时间，如果客户端未在10秒内返回结果，则终止任务

        send_num = sendto(sock_fd, &serverProto, sizeof(serverProto), 0, (struct sockaddr *)&sockaddr_client, len); 
        if(send_num<0){
          perror("send error");
          printf("error: server can not send messages to client for unknown reason\n");
          continue;
        }
      }
    }else{  //到这里说明客户端返回的是calcProtocol类型，附带客户端的计算答案
      if(idMap.find(ip_and_port)==idMap.end()){   //如果客户端的计算任务早已被服务端移除，则报错返回（可能因为传输过程中的延时导致到达时超过了10秒）
        printf("server: client does not have a registerd id and there is no job for him to answer\n");
        continue;
      }else if(idMap[ip_and_port]!=clientData->id){   //如果客户端没有使用服务端分配的id，则报错返回
        struct calcMessage errorReply;
        errorReply.type=3;    //3代表服务端发现了客户端用的是假冒的id，并返回错误信息，这个是我自己对外教协议的扩充
        errorReply.message=2;
        printf("server: client deceives to use another id\n");

        send_num = sendto(sock_fd, &errorReply, sizeof(errorReply), 0, (struct sockaddr *)&sockaddr_client, len); 
        if(send_num<0){
          perror("send error");
          printf("error: server can not send messages to client for unknown reason\n");
        }
        continue;
      }

      double clientAnswer;   //之所以可以用double类型来存储浮点或者整形，是因为整形转化为浮点型不会损失精度，所以没关系
      if(clientData->arith<=4){
        clientAnswer=clientData->inResult;
        printf("client: my answer to question \"%s %d %d\" is %d\n",getRandomTypeName(clientData->arith-1),clientData->inValue1,clientData->inValue2,clientData->inResult);
      }else{
        clientAnswer=clientData->flResult;
        printf("client: my answer to question \"%s %lf %lf\" is %lf\n",getRandomTypeName(clientData->arith-1),clientData->flValue1,clientData->flValue2,clientData->flResult);
      }

      struct calcMessage ansReply;
      ansReply.type=4;    //4 代表此calcMessage是最后服务端发送给客户端的计算答案正确与否的回复信息，区分于之前的信息类别
      if(clientAnswer==ansMap[idMap[ip_and_port]]){   //答案正确时
        ansReply.message=1;
        printf("server: OK, right answer\n");
      }else{
        ansReply.message=2;
        printf("server: NOT OK, wrong answer\n");
      }
      idMap.erase(idMap.find(ip_and_port));   //当验证完答案后移除计算任务和对应的客户端id，因为任务完成了，没有继续保留的意义了
      send_num = sendto(sock_fd, &ansReply, sizeof(ansReply), 0, (struct sockaddr *)&sockaddr_client, len); 
      if(send_num<0){
        perror("send error");
        printf("error: server can not send messages to client for unknown reason\n");
        continue;
      }
    }
  }

  close(sock_fd); 
  printf("server: program terminated\n");

  return(0);
}
