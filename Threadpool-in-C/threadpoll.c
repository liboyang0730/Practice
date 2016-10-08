//gcc Feature Test Macros，为了syscall
//可以参照http://www.gnu.org/software/libc/manual/html_node/Feature-Test-Macros.html
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

//任务回调函数形式
typedef void *(*callback)(void *args);

//任务结构，包含一个回调成员，参数成员，任务指针next组成一个任务队列
typedef struct _task task;
struct _task{
    callback cb;
    void *args;
    struct _task *next;
};

//线程池成员
typedef struct _pool pool;
struct _pool{
    int thread_number;     //工作线程数量限制
    int task_queue_size;   //当前任务队列中的数量
    int max_queue_size;    //最大任务数量
    int running;           
    pthread_t *pt;         //保存工作线程pthread_t for join
    task *task_queue_head; //任务队列
    pthread_mutex_t queue_mutex;  //队列锁
    pthread_cond_t queue_cond;    //条件变量
};

//工作线程执行的函数
void *routine(void *args);

//线程池的初始化
void pool_init(pool *p, int thread_number, int max_queue_size)
{
    p->thread_number = thread_number;
    p->max_queue_size = max_queue_size;
    p->task_queue_size = 0;
    p->task_queue_head = NULL;
    p->pt = (pthread_t *)malloc(sizeof(pthread_t)*p->thread_number);
    if(!p->pt){
        perror("malloc pthread_t array failed");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_init(&p->queue_mutex,NULL);
    pthread_cond_init(&p->queue_cond,NULL);
    for(int i = 0; i < p->thread_number; i++)
    {
        pthread_create (&(p->pt[i]), NULL, routine, (void *)p);
    }
    p->running = 1;
}

//线程池的清理
void pool_clean(pool *p)
{
    if(!p->running)
        return;
    p->running = 0;

    //tell all threads we are exiting
    pthread_cond_broadcast(&p->queue_cond);

    //wait and join all threads
    for (int i = 0; i < p->thread_number; ++i)
    {
        pthread_join(p->pt[i],NULL);
    }
    free(p->pt);

    //free task queue or if needed we can persistent the remaining task
    task *temp;
    while((temp=p->task_queue_head)!=NULL){
        p->task_queue_head = p->task_queue_head->next;
        free(temp);
    }

    pthread_mutex_destroy(&p->queue_mutex);
    pthread_cond_destroy(&p->queue_cond);

    free(p);
    p = NULL;

}


//内部使用
int _pool_add_task(pool *p, task *t)
{
    int ret = 0;
    pthread_mutex_lock(&p->queue_mutex);
    if(p->task_queue_size>=p->max_queue_size){
        pthread_mutex_unlock(&p->queue_mutex);
        //for max queue size error
        ret = 1;
        return ret;
    }
    task *temp = p->task_queue_head;
    if(temp!=NULL){
        while(temp->next!=NULL){
            temp = temp->next;
        }
        temp->next = t;
    }else{
        p->task_queue_head = t;
        pthread_cond_signal(&p->queue_cond);
    }
    p->task_queue_size++;
    pthread_mutex_unlock(&p->queue_mutex);
    return ret;
}

//添加任务接口
int pool_add_task(pool *p, callback cb, void *data)
{
    int ret = 0;
    task *t = (task *)malloc(sizeof(task));
    t->cb = cb;
    t->args = data;
    t->next = NULL;
    if((ret=_pool_add_task(p,t))>0){
        fprintf(stderr,"add wroker failed,reaching max size of task queue\n");
        return ret;
    }
    return ret;
}

//线程routine
void *routine(void *args)
{
    pool *p = (pool *)args;
    task *t;
    fprintf(stdout,"thread_id:%ld\n",syscall(SYS_gettid));
    while(1){

        //将加锁放在条件等待之前可以避免每次添加任务将其他线程白白唤醒
        //而且能保证接受destroy broadcast退出时不会竞争
        pthread_mutex_lock(&p->queue_mutex);
        //wait
        while(p->task_queue_size==0 && p->running){
            pthread_cond_wait(&p->queue_cond,&p->queue_mutex);
        }

        //wake up because pool_destroy
        if(!p->running){
            pthread_mutex_unlock(&p->queue_mutex);
            fprintf(stdout,"thread:%d will exit pool_destroy\n",(int)pthread_self());
            pthread_exit(NULL);
        }

        //wake up to get a task
        t = p->task_queue_head;
        p->task_queue_head = p->task_queue_head->next;
        p->task_queue_size--;
        pthread_mutex_unlock(&p->queue_mutex);

        //when we do the task,release mutex for other threads
        t->cb(t->args);

    }

    pthread_exit(NULL);
}


//测试用的任务回调函数
void *callbacktest(void *args)
{
    fprintf(stdout,"from thread:%d---passed parameter:%d\n",(int)pthread_self(),(int)(*(int *)(args)));
}

int main()
{
	pool *p = (pool *)malloc(sizeof(pool));
	if(p==NULL){
		fprintf(stderr,"malloc pool failed\n");
	}
	pool_init(p,4,10);
    int args[10];
    for (int i=0;i<10;i++){
        args[i] = i;
    }
    for (int i=0;i<10;i++){
        pool_add_task(p,&callbacktest,&args[i]);
    }
    sleep(10);
    pool_clean(p);
	return 0;
}