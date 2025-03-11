#include <evl/thread.h>
#include <evl/clock.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>

#define PERIOD_NS 1000000  // 1.0 ms
#define NUM_SAMPLES 5000   // 5 seconds of data
#define PRIME_LIMIT 10000  // Upper bound for prime search

long execution_times[NUM_SAMPLES]; // Stores execution times
long jitter[NUM_SAMPLES];          // Stores jitter values
struct timespec next_period;       // Stores the next wakeup time

// Prime number computation for CPU load
int is_prime(int n) {
    if (n < 2) return 0;
    for (int i = 2; i <= sqrt(n); i++) {
        if (n % i == 0) return 0;
    }
    return 1;
}

// Real-time periodic task
void *periodic_task(void *arg) {
    int ret;
    struct timespec start_time, end_time, expected_time;
    long expected_ns = 0;
    
    // Set next wake-up time
    evl_read_clock(EVL_CLOCK_MONOTONIC, &next_period);

    for (int i = 0; i < NUM_SAMPLES; i++) {
        evl_read_clock(EVL_CLOCK_MONOTONIC, &start_time);

        // Wait until the next period
        evl_sleep_until(EVL_CLOCK_MONOTONIC, &next_period);
        
        // Compute next period
        next_period.tv_nsec += PERIOD_NS;
        if (next_period.tv_nsec >= 1000000000L) {
            next_period.tv_sec += 1;
            next_period.tv_nsec -= 1000000000L;
        }

        // Perform computational work (prime number search)
        volatile int prime_count = 0;
        for (int num = 2; num <= PRIME_LIMIT; num++) {
            if (is_prime(num)) {
                prime_count++;
            }
        }

        evl_read_clock(EVL_CLOCK_MONOTONIC, &end_time);

        // Calculate execution time in nanoseconds
        execution_times[i] = (end_time.tv_sec - start_time.tv_sec) * 1000000000L + 
                             (end_time.tv_nsec - start_time.tv_nsec);

        // Calculate jitter
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
    
    // Set real-time priority
    param.sched_priority = 80;
    
    // Create real-time thread on core 1
    evl_attach_self("main-thread");
    evl_create_thread(&rt_thread, SCHED_FIFO, param.sched_priority, "rt-thread", periodic_task, NULL);

    // Wait for real-time thread to complete
    pthread_join(rt_thread, NULL);

    // Print execution times and jitter
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