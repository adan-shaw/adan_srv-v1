/*
 * pthread_listen.c
 *
 *  Created on: Oct 17, 2016
 *      Author: adan
 */

#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include "static_val.h"
#include "epoll_head.h"
#include "array_stack.h"

//listen pthread 线程函数初始化--线程环境初始化
int fpthread_listen_init(struct static_val *s_val)
{ //创建epoll
  s_val->epoll_fd = epoll_create(MAXEPOLL);//初始化30W 个fd 到epoll 中
  if(s_val->epoll_fd == -1)
    { printf("listen pthread create epoll fail, errno: %d\n",errno);
      s_val->exit_sig++; return -1; }
  //
  //绑定端口,开始监听
  s_val->sfd_li = inet_listen(SRV_IP,SRV_PORT);
  if(s_val->sfd_li == -1)
    { printf("*listen pthread accept client error,program has quit*\n");
      s_val->exit_sig++; return -1; }
  //
  //加入是将listen_fd 和listen_ev 兴趣加入到epoll_fd 中
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLET ;		//!> 读+边缘触发ET
  ev.data.fd = s_val->sfd_li;			//!> 将listen_fd 加入
  if(epoll_ctl(s_val->epoll_fd, EPOLL_CTL_ADD, s_val->sfd_li, &ev) < 0)
    { printf("listen pthread epoll_ctl fail, errno: %d\n", errno);
      s_val->exit_sig++; return -1; }
  printf("~.~.~.~.~.~.epoll init success~.~.~.~.~.~.~.\n");
  return 0;//初始化成功
}
//
//listen pthread 线程函数
void* fpthread_listen(struct static_val *s_val)
{ //启动线程初始化函数
  int tmp = fpthread_listen_init(s_val);
  //无论初始化是否成功,马上激活主线程继续工作,如果错误,就会拉高exit_sig
  pthread_mutex_lock(&s_val->tmp_mutex);
  pthread_cond_signal(&s_val->tmp_cond);
  pthread_mutex_unlock(&s_val->tmp_mutex);
  if(tmp == -1)
     return NULL;//线程init 错误, 退出
  //
  //epoll 事件判断-循环体
  struct as_info as;as_init(&as);	//创造私有临时栈，并初始化
  s_val->cur_fds++;			//因为epoll 已经有listen sfd 了,所以先+1,计数必须准确
  printf("~.~.~.~.~.~.inet socket start listen now~.~.\n\n");
  for(;;)
  { if_event(s_val,&as);	//调用判断事件函数

    if(s_val->exit_sig != 0)	//quit?
      break;

    //检查任务列表--动态分配io 线程接受工作
    if(aq_is_empty(s_val->pm_recv) == 0)
    { //当io 线程有任务时
      for(tmp = 0;tmp < pool_max;tmp++)
      { //每次都放全部线程去捞任务--弥补listen pthread 线程跑得快,IO 线程跑得慢的弊病
	                     //因为此时io 线程在执行任务的时候,listen pthead 已经跑到下一次了
	                     //recv,send,甚至是任务队列加锁等待,都会比listen pthread 慢很多
	                     //任务队列只有一个,呵呵,加锁等待的时候,下一次已经来了--4 个线程等待1个锁
	if(s_val->sig_io[tmp] == 0)//减少冲突--只对空闲线程发信号--6W-8S
				   //不减少冲突--每次都豪放全部--6W-9S
	  pthread_mutex_lock(&s_val->mutex_io[tmp]); pthread_cond_signal(&s_val->cond_io[tmp]); pthread_mutex_unlock(&s_val->mutex_io[tmp]);
      }
    }
  }//listen loop end
  //
  //listen pthread exit_sig 退出
  s_val->sfd_li = 0;
  //关闭监听socket
  shutdown(s_val->sfd_li,2);
  close(s_val->sfd_li);
  s_val->sfd_li = 0;
  //关闭epoll
  close(s_val->epoll_fd);
  s_val->epoll_fd = 0;

  printf("listen pthread has quit, clean model is good.\n");
  s_val->exit_sig++;//拉起退出标志
  return NULL;	//listen pthread 安全退出
}
//监听线程启动函数(主函数调用)
//
int start_plisten(struct static_val *s_val)
{ //创建 listen pthread
  pthread_t pthread;
  if(pthread_create(&pthread,NULL,(void*)fpthread_listen,s_val) != 0)
  { printf("create listen pthread fail, errno: %d",errno);
    return -1; }
  //
  //detach 模式,线程退出内核会回收一切,不保留信息记录,不会成为僵尸线程
  if(pthread_detach(pthread) != 0)
  { s_val->exit_sig++;
    return -1; }
  //
  //等待监听线程创建完毕才返回
  pthread_mutex_lock(&s_val->tmp_mutex);
  pthread_cond_wait(&s_val->tmp_cond,&s_val->tmp_mutex);
  pthread_mutex_unlock(&s_val->tmp_mutex);

  if(s_val->exit_sig != 0)//判断listen 线程是否创建成功
    if(s_val->exit_sig == 1)
      { shutdown(s_val->sfd_li,2);close(s_val->sfd_li);return -1; }//创建失败,回收资源

  s_val->pid_li = pthread;//记录pid
  printf("listen pthread has started\n\n");
  return 0;
}
