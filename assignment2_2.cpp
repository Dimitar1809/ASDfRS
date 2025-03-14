#include <evl/thread.h>
#include <evl/clock.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <string.h>
#include <evl/sched.h>
#include <evl/proxy.h>

#define PERIOD_NS 1000000  // 1.0 ms
#define NUM_SAMPLES 5000   // 5 seconds of data
#define PRIME_LIMIT 10000  // Upper bound for prime search

long execution_times[NUM_SAMPLES]; // Stores execution times
long jitter[NUM_SAMPLES];          // Stores jitter values
struct timespec next_period;       // Stores the next wakeup time

// Prime number computation for CPU load
int is_prime(int n) {
    if (n < 2) return 0;
    for (int i = 2; i * i <= n; i++) {  // Instead of sqrt(n), use i * i <= n
        if (n % i == 0) return 0;   
    }
    return 1;
}

// Real-time periodic task
void *periodic_task(void *arg) {
    struct timespec start_time, end_time;
    long expected_ns = 0;
    
    evl_read_clock(EVL_CLOCK_MONOTONIC, &next_period);

    for (int i = 0; i < NUM_SAMPLES; i++) {
        evl_read_clock(EVL_CLOCK_MONOTONIC, &start_time);
        evl_sleep_until(EVL_CLOCK_MONOTONIC, &next_period);

        next_period.tv_nsec += PERIOD_NS;
        if (next_period.tv_nsec >= 1000000000L) {
            next_period.tv_sec += 1;
            next_period.tv_nsec -= 1000000000L;
        }

        volatile int prime_count = 0;
        for (int num = 2; num <= PRIME_LIMIT; num++) {
            if (is_prime(num)) {
                prime_count++;
            }
        }

        evl_read_clock(EVL_CLOCK_MONOTONIC, &end_time);

        execution_times[i] = (end_time.tv_sec - start_time.tv_sec) * 1000000000L + 
                             (end_time.tv_nsec - start_time.tv_nsec);

        if (i > 0) {
            expected_ns += PERIOD_NS;
            jitter[i] = llabs((end_time.tv_sec - start_time.tv_sec) * 1000000000L +
                              (end_time.tv_nsec - start_time.tv_nsec) - expected_ns);
        }
    }

    return NULL;
}

int main() {
    pthread_t rt_thread;
    struct sched_param param;
    
    param.sched_priority = 80;
    int ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    char thread_name[32];
    snprintf(thread_name, sizeof(thread_name), "app-main-thread-%d", getpid());
    int efd = evl_attach_self(thread_name);

    if (efd < 0) {
        printf("Error attaching thread: %s\n", strerror(-efd));
        return -1;
    }

    ret = pthread_create(&rt_thread, NULL, periodic_task, NULL);
    if (ret != 0) {
        printf("Error creating thread: %s\n", strerror(ret));
        return -1;
    }

    // Set CPU affinity to core 1 (CPU 1)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);  // Clear all CPUs
    CPU_SET(1, &cpuset);  // Set CPU 1 (core 1)

    ret = pthread_setaffinity_np(rt_thread, sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        printf("Error setting CPU affinity: %s\n", strerror(ret));
        return -1;
    }

    pthread_join(rt_thread, NULL);

    printf("Execution times (ns):\n");
    for (int i = 0; i < NUM_SAMPLES; i++) {
        printf("%ld\n", execution_times[i]);
    }

    printf("\nJitter (ns):\n");
    for (int i = 0; i < NUM_SAMPLES; i++) {
        printf("%ld\n", jitter[i]);
    }

    return 0;
}
