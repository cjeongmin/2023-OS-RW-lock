#include "common_threads.h"
#include "zemaphore.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct _rwlock_t {
    sem_t writelock, readlock;
    sem_t mutex;
    int AR; // number of Active Readers
    int AW; // number of Active Writers
    int WR; // number of Waiting Readers
    int WW; // number of Waiting Writers
} rwlock_t;

void rwlock_init(rwlock_t *rw) {
    rw->AR = rw->AW = rw->WR = rw->WW = 0;
    Sem_init(&rw->mutex, 1);
    Sem_init(&rw->writelock, 0);
    Sem_init(&rw->readlock, 0);
}

void rwlock_acquire_readlock(rwlock_t *rw) {
    Sem_wait(&rw->mutex);
    rw->WR++;
    while (rw->AW + rw->WW > 0) {
        Sem_post(&rw->mutex);
        Sem_wait(&rw->readlock);
        Sem_wait(&rw->mutex);
    }
    rw->WR--;
    rw->AR++;
    Sem_post(&rw->mutex);
}

void rwlock_release_readlock(rwlock_t *rw) {
    Sem_wait(&rw->mutex);
    rw->AR--;
    if (rw->AR == 0 && rw->WW > 0)
        Sem_post(&rw->writelock);
    Sem_post(&rw->mutex);
}

void rwlock_acquire_writelock(rwlock_t *rw) {
    Sem_wait(&rw->mutex);
    rw->WW++;
    while (rw->AR + rw->AW > 0) {
        Sem_post(&rw->mutex);
        Sem_wait(&rw->writelock);
        Sem_wait(&rw->mutex);
    }
    rw->WW--;
    rw->AW++;
    Sem_post(&rw->mutex);
}

void rwlock_release_writelock(rwlock_t *rw) {
    Sem_wait(&rw->mutex);
    rw->AW--;
    if (rw->WW > 0)
        Sem_post(&rw->writelock);
    else if (rw->WR > 0) {
        for (int i = rw->WR; i > 0; i--) {
            Sem_post(&rw->readlock);
        }
    }
    Sem_post(&rw->mutex);
}

//
// Don't change the code below (just use it!) But fix it if bugs are found!
//

rwlock_t rwlock;

int loops;
int DB = 0;

// * For count time

int t = 0;
int wating_jobs = 0;
int job_count = 0;
int done_jobs = 0;
sem_t step_lock;
sem_t step_lock_mutex;

char *strings[1000];

void wait_next_step() {
    Sem_wait(&step_lock_mutex);
    Sem_wait(&rwlock.mutex);
    if (wating_jobs == job_count - rwlock.WR - rwlock.WW - 1 - done_jobs) {
        printf("[Time: %3d]: ", t);
        for (int i = 0; i < job_count; i++) {
            printf("%30s", strings[i]);
            sprintf(strings[i], "");
        }
        printf("\n");

        t++;
        for (int i = wating_jobs; i > 0; i--) {
            Sem_post(&step_lock);
        }
    } else {
        wating_jobs++;
        Sem_post(&rwlock.mutex);
        Sem_post(&step_lock_mutex);
        Sem_wait(&step_lock);
        Sem_wait(&step_lock_mutex);
        Sem_wait(&rwlock.mutex);
        wating_jobs--;
    }
    Sem_post(&rwlock.mutex);
    Sem_post(&step_lock_mutex);
}

// * For count time

typedef struct {
    int thread_id;
    int job_type; // 0: reader, 1: writer
    int arrival_delay;
    int running_time;
} arg_t;

sem_t print_lock;

#define TAB 10
void space(int s) {
    Sem_wait(&print_lock);
    printf("[Time: %3d]\t", t);
    s = s * 3;
    int i;
    for (i = 0; i < s * TAB; i++)
        printf(" ");
}

void space_end() { Sem_post(&print_lock); }

#define TICK sleep(1) // 1/100초 단위로 하고 싶으면 usleep(10000)

void *reader(void *arg) {
    arg_t *args = (arg_t *)arg;

    TICK;
    sprintf(strings[args->thread_id], "[%d] acquire readlock", args->thread_id);
    wait_next_step();
    rwlock_acquire_readlock(&rwlock);
    // start reading
    int i;
    for (i = 0; i < args->running_time - 1; i++) {
        TICK;
        sprintf(strings[args->thread_id], "[%d] reading %d of %d",
                args->thread_id, i, args->running_time);
        wait_next_step();
    }
    TICK;
    sprintf(strings[args->thread_id], "[%d] reading %d of %d, DB is %d",
            args->thread_id, i, args->running_time, DB);
    wait_next_step();
    // end reading
    TICK;
    rwlock_release_readlock(&rwlock);
    sprintf(strings[args->thread_id], "[%d] release readlock", args->thread_id);
    wait_next_step();

    Sem_wait(&step_lock_mutex);
    done_jobs++;
    Sem_post(&step_lock_mutex);

    return NULL;
}

void *writer(void *arg) {
    arg_t *args = (arg_t *)arg;

    TICK;
    sprintf(strings[args->thread_id], "[%d] acquire writelock",
            args->thread_id);
    wait_next_step();
    rwlock_acquire_writelock(&rwlock);
    // start writing
    int i;
    for (i = 0; i < args->running_time - 1; i++) {
        TICK;
        sprintf(strings[args->thread_id], "[%d] writing %d of %d",
                args->thread_id, i, args->running_time);
        wait_next_step();
    }
    TICK;
    DB++;
    sprintf(strings[args->thread_id], "[%d] writing %d of %d, DB is %d",
            args->thread_id, i, args->running_time, DB);
    wait_next_step();

    // end writing
    TICK;
    rwlock_release_writelock(&rwlock);
    sprintf(strings[args->thread_id], "[%d] release writelock",
            args->thread_id);
    wait_next_step();

    Sem_wait(&step_lock_mutex);
    done_jobs++;
    Sem_post(&step_lock_mutex);

    return NULL;
}

void *worker(void *arg) {
    arg_t *args = (arg_t *)arg;
    int i;
    for (i = 0; i < args->arrival_delay; i++) {
        TICK;
        sprintf(strings[args->thread_id], "[%d] arrival delay %d of %d",
                args->thread_id, i, args->arrival_delay);
        wait_next_step();
    }
    if (args->job_type == 0)
        reader(arg);
    else if (args->job_type == 1)
        writer(arg);
    else {
        space(args->thread_id);
        printf("Unknown job %d\n", args->thread_id);
        space_end();
    }
    return NULL;
}

#define MAX_WORKERS 10

int main(int argc, char *argv[]) {

    // command line argument로 공급 받거나
    // 예: -n 6 -a 0:0:5,0:1:8,1:3:4,0:5:7,1:6:2,0:7:4    또는   -n 6 -a
    // r:0:5,r:1:8,w:3:4,r:5:7,w:6:2,r:7:4 아래 코드에서 for-loop을 풀고 배열
    // a에 직접 쓰는 방법으로 worker 세트를 구성한다.
    int num_workers = atoi(argv[2]);
    pthread_t p[MAX_WORKERS];
    arg_t a[MAX_WORKERS];

    rwlock_init(&rwlock);

    char *arg = argv[4];

    int i, j = 0;
    for (i = 0; i < num_workers; i++) {
        strings[i] = (char *)malloc(100 * sizeof(char));

        // parse
        int job_type = arg[j] - '0';
        j += 2;

        int arrival_delay = 0;
        while (arg[j] != ':') {
            arrival_delay *= 10;
            arrival_delay += arg[j] - '0';
            j += 1;
        }
        j += 1;

        int running_time = 0;
        while (arg[j] != ',' && arg[j] != '\0') {
            running_time *= 10;
            running_time += arg[j] - '0';
            j += 1;
        }
        j += 1;

        a[i].thread_id = i;
        a[i].job_type = job_type;
        a[i].arrival_delay = arrival_delay;
        a[i].running_time = running_time;
    }

    printf("begin\n");
    printf(" ... heading  ...  \n"); // a[]의 정보를 반영해서 헤딩 라인을 출력

    Sem_init(&print_lock, 1);
    Sem_init(&step_lock, 0);
    Sem_init(&step_lock_mutex, 1);
    job_count = num_workers;

    for (i = 0; i < num_workers; i++)
        Pthread_create(&p[i], NULL, worker, &a[i]);

    for (i = 0; i < num_workers; i++)
        Pthread_join(p[i], NULL);

    printf("end: DB %d\n", DB);

    return 0;
}

/*
// 리포트 기본 매개변수
./reader-writer -n 6 -a 0:0:5,0:1:8,1:3:4,0:5:7,1:6:2,0:7:4

// 예제 시나리오
./reader-writer -n 6 -a 0:0:5,0:1:5,1:3:4,0:5:3,1:6:2,0:7:4

// 만든 시나리오 1

// 만든 시나리오 2

*/