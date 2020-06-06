#include <stdio.h>
#include <stdlib.h>
/* You will to add includes here */ 
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


// Included to get the support library
#include <calcLib.h>
#include "protocol.h"

#define PORT 4950
#define MAXSIZE 200

int idCount=0;
map<char*,int> idMap;
time_t timeMap[1000];
double ansMap[1000];

void manageOutdatedClient(int signum){
  time_t now=time(0);
  for(map<char*,int>::iterator it=idMap.begin();it!=idMap.end();it++){
    if(now-timeMap[it->second]>10){
      printf("server: client[%s] does not respond with answer in 10 seconds, so abort its job\n",it->first);
      idMap.erase(it);
    }
  }
}

int main(int argc, char *argv[]){
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

  if(bind(sock_fd, (struct sockaddr *)&addr_serv, sizeof(addr_serv)) < 0)  {  
    perror("socket bind address error");  
    printf("error: server can not bind the socket\n");
    exit(1);  
  }  

  int recv_num;  
  int send_num;  
  char send_buf[MAXSIZE]; 
  char recv_buf[MAXSIZE];  
  struct sockaddr_in sockaddr_client;

  initCalcLib();    //在调用外教提供的指令生成包前必须调用他提供的初始化函数

  struct itimerval alarm;
  alarm.it_interval.tv_sec=0;
  alarm.it_interval.tv_usec=100;
  alarm.it_value.tv_sec=1;
  alarm.it_value.tv_usec=0;
  setitimer(ITIMER_REAL,&alarm,NULL);
  signal(SIGALRM, manageOutdatedClient);

  while(1){
    memset(sockaddr_client,0,sizeof(sockaddr_client));
    recv_num = recvfrom(sock_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&sockaddr_client, (socklen_t *)&len);
    if(recv_num<0){
      perror("function recvfrom error");  
      printf("error: server failed in receiving messages from client\n");
      exit(1); 
    }

    struct calcProtocol * clientData=(struct calcProtocol*)recv_buf;

    char client_addr[20];
    inet_ntop(sockaddr_client.sin_family,&sockaddr_client.sin_addr,client_addr,sizeof(client_addr));
    char ip_and_port[30];
    sprintf(ip_and_port,"%s:%d\0",client_addr,sockaddr_client.sin_port);

    printf("--------------------contacting client[%s:%d]---------------------------\n",client_addr,sockaddr_client.sin_port)

    if(clientData->type==2){
      struct calcMessage * messa=(struct calcMessage*)recv_buf;
      printf("client: query for protocol support\n");
      if(messa->protocol!=17){
        printf("server: do not support the protocol proposed by client\n");
        struct calcMessage serverMessa;
        serverMessa.type=2;
        serverMessa.message=2;
        serverMessa.major_version=1;
        serverMessa.minor_version=0;
        send_num = sendto(sock_fd, &serverMessa, sizeof(serverMessa), 0, (struct sockaddr *)&addr_client, len); 
        if(send_num<0){
          perror("send error");
          printf("error: server can not send messages to client for unknown reason\n");
          continue;
        }
        continue;
      }else{
        printf("server: support the protocol proposed by client\n");

        if(idMap.find(ip_and_port)!=idMap.end()){
          printf("server: has already assign a question to client\n")
          continue;
        }else{
          idMap[ip_and_port]=idCount;
          idCount++;
        }

        struct calcProtocol serverProto;
        int arithType=randomType()+1;
        serverProto.arith=arithType;
        serverProto.id=idMap[ip_and_port];
        if(arithType<=4){
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
        }else{
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
        }
        timeMap[id]=time(0);

        send_num = sendto(sock_fd, &serverProto, sizeof(serverProto), 0, (struct sockaddr *)&addr_client, len); 
        if(send_num<0){
          perror("send error");
          printf("error: server can not send messages to client for unknown reason\n");
          continue;
        }
      }
    }else{
      if(idMap.find(ip_and_port)==idMap.end()){
        printf("server: client does not have a registerd id and there is no job for him to answer\n");
        continue;
      }else if(idMap[ip_and_port]!=clientData->id){
        struct calcMessage errorReply;
        errorReply.type=3;    //3代表服务端发现了客户端用的是假冒的id，并返回错误信息
        errorReply.message=2;
        printf("server: client deceives to use another id\n");

        send_num = sendto(sock_fd, &errorReply, sizeof(errorReply), 0, (struct sockaddr *)&addr_client, len); 
        if(send_num<0){
          perror("send error");
          printf("error: server can not send messages to client for unknown reason\n");
          continue;
        }
        continue;
      }

      double clientAnswer;
      if(clientData->arith<=4){
        clientAnswer=clientData->inResult;
        printf("server: receive answer to question \"%s %d %d\" is %d\n",getRandomTypeName(clientData->arith-1),clientData->inValue1,clientData->inValue2,clientData->inResult);
      }else{
        clientAnswer=clientData->flResult;
        printf("server: receive answer to question \"%s %lf %lf\" is %lf\n",getRandomTypeName(clientData->arith-1),clientData->flValue1,clientData->flValue2,clientData->flResult);
      }

      struct calcMessage ansReply;
      ansReply.type=4;    //4 代表此calcMessage是最后服务端发送给客户端的计算答案正确与否的回复信息，区分于之前的信息类别
      if(clientAnswer==ansMap[idMap[ip_and_port]]){
        ansReply.message=1;
        printf("server: OK, right answer\n");
      }else{
        ansReply.message=2;
        printf("server: NOT OK, wrong answer\n");
      }
      idMap.erase(idMap.find(ip_and_port));
      send_num = sendto(sock_fd, &ansReply, sizeof(ansReply), 0, (struct sockaddr *)&addr_client, len); 
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
