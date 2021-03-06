#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include "job.h"
//#define DEBUG
#define RUN
//#define TEXT

int jobid=0;
int siginfo=1;
int fifo;
int globalfd;
int grab=0; /*抢占标志*/
int goon=0;

struct waitqueue *head1=NULL,*head2=NULL,*head3=NULL;//+++++++++++++++++++++
struct waitqueue *next=NULL,*current =NULL;

/* 调度程序 */
void scheduler()
{
	struct jobinfo *newjob=NULL;
	struct jobcmd cmd;
        struct waitqueue *p;
	int  count = 0;
	bzero(&cmd,DATALEN);
	if((count=read(fifo,&cmd,DATALEN))<0)
		error_sys("read fifo failed");
#ifdef DEBUG

	if(count){
		printf("cmd cmdtype\t%d\ncmd defpri\t%d\ncmd data\t%s\n",cmd.type,cmd.defpri,cmd.data);
	}
	else
		printf("no data read\n");
#endif

	/* 更新等待队列中的作业 */
	updateall();
#ifdef TEXT
        printf("queue3:\n");
        for (p=head3;p!=NULL;p=p->next)
             printf("%d\n", p->job->jid);
        printf("queue2:\n");
        for (p=head2;p!=NULL;p=p->next)
             printf("%d\n", p->job->jid);
#endif
	switch(cmd.type){
	case ENQ:
		do_enq(newjob,cmd);

		if(grab==1)
		{
#ifdef TEXT
                        printf("grab!\n");
#endif
			next=jobselect();
#ifdef TEXT
                        printf("%d\n",next->job->jid);
#endif
			jobswitch();
			grab=0;
			return ;
		}
		break;
	case DEQ:
		do_deq(cmd);
		break;
	case STAT:
		do_stat(cmd);
		break;
	default:
		break;
	}

	if(SwitchJobCondition() && ComparePri())
	{
		/* 选择高优先级作业 */

		next=jobselect();
		/* 作业切换 */
		jobswitch();

		return ;
	}
	else
		return ;

	
}
int SwitchJobCondition()  /*切换作业条件*/
{		
	int t=0;	  /*返回1，可以切换；返回0，不能切换*/
	if(current==NULL)
		return 1;
	else
	{
		t=current->job->turn_time;
		switch(current->job->curpri)
		{
			case 1 : 
				if(t>=5)
					return 1;
				else
					return 0;
			case 2 :
				if(t>=2)
					return 1;
				else
					return 0;
			case 3 :
				if(t>=1)
					return 1;
				else
					return 0;
			default:
				return 1;	
		}
	}
}
int ComparePri()//+++++++++++++++++++++++++++
{
	int currentpri=0;    /*返回1，可以切换；返回0，不能切换*/
	struct waitqueue *p;
	if(current==NULL)
		return 1;
	else
	{
		currentpri = current->job->curpri;
		
		for(p=head1;p!=NULL;p=p->next)
		{
			if(p->job->curpri >= currentpri)
				return 1;
		}
		for(p=head2;p!=NULL;p=p->next)
		{
			if(p->job->curpri >= currentpri)
				return 1;
		}
		for(p=head3;p!=NULL;p=p->next)
		{
			if(p->job->curpri >= currentpri)
				return 1;
		}
		return 0;
	}
	
}
int allocjid()
{
	return ++jobid;
}

void updateall()//++++++++++++++++++++++++++++++++
{
	struct waitqueue *p, *q, *prev;
        int flag;   
	/* 更新作业运行时间 */
	if(current){
		current->job->run_time += 1; /* 加1代表1000ms */
		current->job->turn_time += 1;
	}	
	/* 更新作业等待时间及优先级 */

	for(p = head3; p != NULL; p = p->next){
		p->job->wait_time += 1000;
#ifdef RUN
		printf("ID:%d,PID:%d,wait_time:%d\n",p->job->jid,p->job->pid,p->job->wait_time);
#endif
	}
        for(prev = head2, p = head2; p != NULL;p = p->next){
		p->job->wait_time += 1000;
                flag = 0;//
		if(p->job->wait_time >= 10000){//5000  此处应包含降级处理,优先级加减处理
			flag = 1;//
                        p->job->curpri++;
			p->job->wait_time = 0;
                        if(head3)
			{
			    for(q=head3;q->next != NULL; q=q->next);
		            q->next = p;
			}else
			    head3= p;
                        if (prev == p)
                            head2 = p ->next;
                        else 
                            prev->next = p->next;        
		}
                if (!flag) prev = p; //
	}
	
        
        for(prev = head1,p = head1; p != NULL; p = p->next){
		p->job->wait_time += 1000;
                flag = 0;//
		if(p->job->wait_time >= 10000){//5000  此处应包含降级处理,优先级加减处理                       
			p->job->curpri++;
			p->job->wait_time = 0;
                        if (p->job->curpri = 2){
                            flag = 1;//
                            if(head2)
			    {
			        for(q=head2;q->next != NULL; q=q->next);
		                q->next = p;
			    }else
			        head2= p;
                            if (prev == p)
                                head1 = p ->next;
                            else 
                                prev->next = p->next;  
                        }
		}
                if (!flag) prev = p; //
	}

}
struct waitqueue* FindHead1()//+++++++++++++++++++++++++=
{
	struct waitqueue *p,*prev,*select,*selectprev;
	int highest = -1, highesttime = -1;

	select = NULL;
	selectprev = NULL;
	if(head1){
		/* 遍历等待队列中的作业，找到优先级最高的作业 */
		for(prev = head1, p = head1; p != NULL; prev = p,p = p->next)   /*添加对于等待时间的对比*/
			if(p->job->curpri > highest || p->job->curpri == highest && p->job->wait_time > highesttime){
				select = p;
				selectprev = prev;
				highest = p->job->curpri;
                                highesttime = p->job->wait_time;
			}
			selectprev->next = select->next;
			if (select == selectprev)
				head1 = select->next;
	}
	return select;
}
struct waitqueue* FindHead2()//+++++++++++++++++++++++++++
{
	struct waitqueue *p,*prev,*select,*selectprev;
	int highest = -1;

	select = NULL;
	selectprev = NULL;
	if(head2){
		/* 遍历等待队列中的作业，找到优先级最高的作业 */
		for(prev = head2, p = head2; p != NULL; prev = p,p = p->next)   /*添加对于等待时间的对比*/
			if(p->job->wait_time > highest){
				select = p;
				selectprev = prev;
				highest = p->job->wait_time;
			}
			selectprev->next = select->next;
			if (select == selectprev)
				head2 = select->next;
	}
	return select;
}
struct waitqueue* FindHead3()//++++++++++++++++++++++++++++
{
	struct waitqueue *p,*prev,*select,*selectprev;
	int highest = -1;

	select = NULL;
	selectprev = NULL;
	if(head3){
		/* 遍历等待队列中的作业，找到优先级最高的作业 */
		for(prev = head3, p = head3; p != NULL; prev = p,p = p->next){   /*添加对于等待时间的对比*/
			if(p->job->wait_time > highest){
				select = p;
				selectprev = prev;
				highest = p->job->wait_time;
			}
                }
			selectprev->next = select->next;
			if (select == selectprev)
				head3 = select->next;
	}
	return select;
}
struct waitqueue* jobselect()//++++++++++++++++++++++++++++++++
{		
	struct waitqueue *select;
	select = NULL;
        
	if(current == NULL)
	{

		select = FindHead3();
		if(select == NULL)
		{
			select = FindHead2();
			if(select == NULL)
				select = FindHead1();
		}
		return select;
	}
        else {
            if ((select = FindHead3()) == NULL)
                if ((select = FindHead2()) == NULL)
                     select = FindHead1();
            return select;
        }
        /*
	switch(current->job->curpri)
	{
		case 3:
			select = FindHead3();
			if(select == NULL)
			{
				select = FindHead2();
				if(select == NULL)
					select = FindHead1();
			}
			return select;
		case 2:
			select = FindHead2();
			if(select == NULL)
			{
				select = FindHead1();
			}
			return select;
		case 1||0:
			select = FindHead1();
			return select;
			
	}
        */

}

/*struct waitqueue* jobselect()
{
	struct waitqueue *p,*prev,*select,*selectprev;
	int highest = -1;

	select = NULL;
	selectprev = NULL;
	if(head){
		/* 遍历等待队列中的作业，找到优先级最高的作业 */
//		for(prev = head, p = head; p != NULL; prev = p,p = p->next)   /*添加对于等待时间的对比*/
/*			if(p->job->curpri > highest){
				select = p;
				selectprev = prev;
				highest = p->job->curpri;
			}
			selectprev->next = select->next;
			if (select == selectprev)
				head = NULL;
	}
	return select;
}*/

void jobswitch()//+++++++++++++++++++++++++++
{
	struct waitqueue *p;
	int i;


	if(current && current->job->state == DONE){ /* 当前作业完成 */
		/* 作业完成，删除它 */
		for(i = 0;(current->job->cmdarg)[i] != NULL; i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i] = NULL;
		}
		/* 释放空间 */
		free(current->job->cmdarg);
		free(current->job);
		free(current);

		current = NULL;
	}

	if(next == NULL && current == NULL) /* 没有作业要运行 */

		return;
	else if (next != NULL && current == NULL){ /* 开始新的作业 */

		printf("begin start new job\n");
		current = next;
		next = NULL;
		current->job->turn_time = 0;       //开始，轮转时间为0
		current->job->state = RUNNING;  
		kill(current->job->pid,SIGCONT);
		return;
	}
	else if (next != NULL && current != NULL){ /* 切换作业 */

		printf("switch to Pid: %d\n",next->job->pid);
		kill(current->job->pid,SIGSTOP);
		current->job->curpri = current->job->defpri;
		current->job->wait_time = 0;
		current->job->state = READY;
		current->job->turn_time = 0;  //切换，轮转时间为0
		current->next =	NULL;  //使其放回等待队列时有尾
		/* 放回等待队列 */
		switch(current->job->curpri)
		{
			case 1||0 :
				if(head1){
					for(p = head1; p->next != NULL; p = p->next);
				        p->next = current;
				}else
					head1 = current;
				break;
			case 2 :
				if(head2){
					for(p = head2; p->next != NULL; p = p->next);
						p->next = current;
				}else
					head2 = current;
				break;
			case 3 :
				if(head3){
					for(p = head3; p->next != NULL; p = p->next);
						p->next = current;
				}else
					head3 = current;
				break;	
		}
		/*if(head){
			for(p = head; p->next != NULL; p = p->next);
			p->next = current;
		}else{
			head = current;
		}*/
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		current->job->wait_time = 0;
		kill(current->job->pid,SIGCONT);
		return;
	}else{ /* next == NULL且current != NULL，不切换 */
		return;
	}
}

void sig_handler(int sig,siginfo_t *info,void *notused)
{
	int status;
	int ret;

	switch (sig) {
case SIGVTALRM: /* 到达计时器所设置的计时间隔 */
	scheduler();
	return;
case SIGCHLD: /* 子进程结束时传送给父进程的信号 */
	ret = waitpid(-1,&status,WNOHANG);
	if (ret == 0)
		return;
	if(WIFEXITED(status)){  //如果为正常结束子进程返回的状态，则为真
		current->job->state = DONE;
		printf("normal termation, exit status = %d\n",WEXITSTATUS(status));
	}else if (WIFSIGNALED(status)){  //若为异常结束子进程返回的状态，则为真
		printf("abnormal termation, signal number = %d\n",WTERMSIG(status));
	}else if (WIFSTOPPED(status)){  //若为当前暂停子进程的返回状态，则为真
		printf("child stopped, signal number = %d\n",WSTOPSIG(status));
	}
	return;
	default:
		return;
	}
}

void setGoon()
{
	goon = 1;
}

void do_enq(struct jobinfo *newjob,struct jobcmd enqcmd)//++++++++++++++++++++++++
{
	struct waitqueue *newnode,*p;
	int i=0,pid;
	char *offset,*argvec,*q;
	char **arglist;
	sigset_t zeromask;

	sigemptyset(&zeromask);

	/* 封装jobinfo数据结构 */
	newjob = (struct jobinfo *)malloc(sizeof(struct jobinfo));
	newjob->jid = allocjid();
	newjob->defpri = enqcmd.defpri;
	newjob->curpri = enqcmd.defpri;
	newjob->ownerid = enqcmd.owner;
	newjob->state = READY;
	newjob->create_time = time(NULL);  //获得日历时间
	newjob->wait_time = 0;
	newjob->run_time = 0;
	newjob->turn_time = 0;     //初始化
	arglist = (char**)malloc(sizeof(char*)*(enqcmd.argnum+1));
	newjob->cmdarg = arglist;
	offset = enqcmd.data;
	argvec = enqcmd.data;
	while (i < enqcmd.argnum){
		if(*offset == ':'){
			*offset++ = '\0';
			q = (char*)malloc(offset - argvec);
			strcpy(q,argvec);
			arglist[i++] = q;
			argvec = offset;
		}else
			offset++;
	}
	if(current!=NULL&&newjob->defpri > current->job->defpri)/*当前作业的当前优先级已经改变，故比较默认(即初始)优先级，可以体现作业发布者对高优先级的作业的迫切要求，且对于新加入的作业(enq即是添加作业)，当前优先级与默认优先级是相同的*/
		grab = 1;

	arglist[i] = NULL;

#ifdef DEBUG

	printf("enqcmd argnum %d\n",enqcmd.argnum);
	for(i = 0;i < enqcmd.argnum; i++)
		printf("parse enqcmd:%s\n",arglist[i]);

#endif
	/*向等待队列中增加新的作业*/
	newnode = (struct waitqueue*)malloc(sizeof(struct waitqueue));
	newnode->next =NULL;
	newnode->job=newjob;
	switch(newnode->job->defpri)
	{
                case 0 :
		case 1 :
			if(head1)
			{
				for(p=head1;p->next != NULL; p=p->next);
					p->next =newnode;
			}else
				head1=newnode;
			break;
		case 2 :
			if(head2)
			{
				for(p=head2;p->next != NULL; p=p->next);
					p->next =newnode;
			}else
				head2=newnode;
			break;
		case 3 :
			if(head3)
			{
				for(p=head3;p->next != NULL; p=p->next);
					p->next =newnode;
			}else
				head3=newnode;
			break;
	}
#ifdef RUN
	for(p=head1;p!= NULL; p=p->next){
		printf("job ID:%d\n",p->job->jid);
	}
#endif
	/*if(head)
	{
		for(p=head;p->next != NULL; p=p->next);
		p->next =newnode;
	}else
		head=newnode;*/

	/*为作业创建进程*/
	if((pid=fork())<0)
		error_sys("enq fork failed");


	if(pid==0){

		newjob->pid =getpid();
		kill(getppid(),SIGUSR1);
		/*阻塞子进程,等等执行*/
		raise(SIGSTOP);
#ifdef DEBUG

		printf("begin running\n");
		for(i=0;arglist[i]!=NULL;i++)
			printf("arglist %s\n",arglist[i]);
#endif
		/*复制文件描述符到标准输出*/
		dup2(globalfd,1);
		/* 执行命令 */
		if(execv(arglist[0],arglist)<0)
			printf("exec failed\n");
		exit(1);
	}else{
		signal(SIGUSR1,setGoon);
		while(goon==0);
		goon = 0;
		newjob->pid=pid;
#ifdef RUN
#endif
#ifdef DEBUG
		printf("pid is %d\n",pid);
#endif
	}
}

void do_deq(struct jobcmd deqcmd)//++++++++++++++++++=
{
	int deqid,i;
	struct waitqueue *p,*prev,*select,*selectprev;
	deqid=atoi(deqcmd.data);

#ifdef DEBUG
	printf("deq jid %d\n",deqid);
#endif

	/*current jodid==deqid,终止当前作业*/
	if (current && current->job->jid ==deqid){
		printf("teminate current job\n");
		kill(current->job->pid,SIGKILL);
		for(i=0;(current->job->cmdarg)[i]!=NULL;i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i]=NULL;
		}
		free(current->job->cmdarg);
		free(current->job);
		free(current);
		current=NULL;
	}
	else{ /* 或者在等待队列中查找deqid */
		select=NULL;
		selectprev=NULL;
		if(head1){
			for(prev=head1,p=head1;p!=NULL;prev=p,p=p->next)
				if(p->job->jid==deqid){
					select=p;
					selectprev=prev;
					break;
				}
				selectprev->next=select->next;
				if(select==selectprev)
					head1=select->next;
		}
		if(head2){
			for(prev=head2,p=head2;p!=NULL;prev=p,p=p->next)
				if(p->job->jid==deqid){
					select=p;
					selectprev=prev;
					break;
			 	}
				selectprev->next=select->next;
				if(select==selectprev)
					head2=select->next;
		}
		if(head3){
			for(prev=head3,p=head3;p!=NULL;prev=p,p=p->next)
				if(p->job->jid==deqid){
					select=p;
					selectprev=prev;
					break;
				}
				selectprev->next=select->next;
				if(select==selectprev)
					head3=select->next;
		}
		if(select){
			for(i=0;(select->job->cmdarg)[i]!=NULL;i++){
				free((select->job->cmdarg)[i]);
				(select->job->cmdarg)[i]=NULL;
			}
			free(select->job->cmdarg);
			free(select->job);
			free(select);
			select=NULL;
		}
	}
}

void do_stat(struct jobcmd statcmd)
{
	struct waitqueue *p;
	char timebuf[BUFLEN];
	/*
	*打印所有作业的统计信息:
	*1.作业ID
	*2.进程ID
	*3.作业所有者
	*4.作业运行时间
	*5.作业等待时间
	*6.作业创建时间
	*7.作业状态
	*/

	/* 打印信息头部 */
	printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
	if(current){
		strcpy(timebuf,ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			current->job->jid,
			current->job->pid,
			current->job->ownerid,
			current->job->run_time,
			current->job->wait_time,
			timebuf,"RUNNING");
	}

	for(p=head1;p!=NULL;p=p->next){
		strcpy(timebuf,ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			timebuf,
			"READY");
	}
	for(p=head2;p!=NULL;p=p->next){
		strcpy(timebuf,ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			timebuf,
			"READY");
	}
	for(p=head3;p!=NULL;p=p->next){
		strcpy(timebuf,ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			timebuf,
			"READY");
	}	
}

int main()
{
	struct timeval interval;
	struct itimerval new,old;
	struct stat statbuf;
	struct sigaction newact,oldact1,oldact2;

	if(stat("/tmp/server",&statbuf)==0){
		/* 如果FIFO文件存在,删掉 */
		if(remove("/tmp/server")<0)
			error_sys("remove failed");
	}

	if(mkfifo("/tmp/server",0666)<0)
		error_sys("mkfifo failed");
	/* 在非阻塞模式下打开FIFO */
	if((fifo=open("/tmp/server",O_RDONLY|O_NONBLOCK))<0)
		error_sys("open fifo failed");

	/* 建立信号处理函数 */
	newact.sa_sigaction=sig_handler;
	sigemptyset(&newact.sa_mask);
	newact.sa_flags=SA_SIGINFO;
	sigaction(SIGCHLD,&newact,&oldact1);
	sigaction(SIGVTALRM,&newact,&oldact2);

	/* 设置时间间隔为1000毫秒 */
	interval.tv_sec=1;
	interval.tv_usec=0;

	new.it_interval=interval;
	new.it_value=interval;
	setitimer(ITIMER_VIRTUAL,&new,&old);

	while(siginfo==1);

	close(fifo);
	close(globalfd);
	return 0;
}
