			+--------------------+
			|        CS 4284     |
			| PROJECT 1: THREADS |
			|   DESIGN DOCUMENT  |
			+--------------------+
				   
---- YOU ----

>> Fill in your name and email address

Bug Lee (Chungeun Lee) <bug19@vt.edu>

---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

    L. Chao, "Symmetric MultiProcessing for the Pintos Instructional Operating
    System", M.S. thesis, Dept. Computer Science, Virginia Tech, Blacksburg, VA,
    USA, 2017



			     ALARM CLOCK
			     ===========

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.
   
    struct cpu
        ...
        struct list sleep_queue : per-CPU sleep queue that stores 
                                  blocked threads for timer_sleep().
        ...

    struct thread
        ...
        int64_t wakeup_time : store the time that thread need to wake up 
                              from sleep.
        struct list_elem sleep_queue_elem : list element for the sleep queue.
        struct list_elem ready_queue_elem : list element for the ready queue.
        ...

---- ALGORITHMS ----

>> A2: Briefly describe what happens in a call to timer_sleep(),
>> including the effects of the timer interrupt handler.

    The following describes what happens in a thread calling timer_sleep()
    in order:
    (1) Interrupts are disabled upon entry. Discussed in more detail
        in A4 and A5.
    (2) The calling thread calculates and stores the wakeup_time.
    (3) Transfer the thread to the local sleep queue, and go to sleep (blocked). 
        When it goes to sleep, the CPU context switch to another thread.
    (4) Meanwhile, for each timer interrupt, the timer interrupt handler
        iterate over the sleep queue and check if wakeup_time of any thread
        has been expired. If so, it wakes up (unblock) the corresponding
        thread by transferring the thread back to the ready queue and yielding
        upon exiting interrupt handler.
    (5) When picking the next thread to run, the CPU would see and grab the woken
        up thread inside the ready queue, and then start the timer_sleep() 
        from where it left off.
    (6) Finally, the calling thread wrap-up the timer_sleep() by 
        re-enabling the interrupts.

>> A3: What steps are taken to minimize the amount of time spent in
>> the timer interrupt handler?

    By use of per-CPU sleep queues, we were able to reduce the size of 
    iteration on the sleep queue and remove the contention of going after 
    the same lock while handling a timer interrupt. Comparison with global
    sleep queue is discussed in A6.
    Also, we have implemented our wakeup_sleeper() procedure to end 
    right away once it wakes up one sleeper.
    

---- SYNCHRONIZATION ----

>> A4: How are race conditions avoided when multiple threads call
>> timer_sleep() simultaneously?

    Race conditions were avoided by using per-CPU sleep queues and
    disabling interrupt upon entry. The reason is because
    (1) The running thread and sleeping threads are bound to
        the CPU that called from. Disabling interrupts ensure that
        the thread cannot yield and thus cannot switch its state to 
        ready during the execution of the critical section. Knowing that 
        only ready threads can be migrated, threads in one CPU will
        not be accessible from the other CPUs.
    (2) Execution of timer_sleep() and handling timer interrupt
        cannot happen simultaneously within the CPU. This means that
        pushing and popping the thread from/to the sleep queue is
        guaranteed to be mutually exclusive. 
    (3) Only one thread can be executed at a time for each CPU.

    Provided base code already took care of race condition regarding
    reading from and writing to ready queue by use of spinlock and
    disabling interrupts.

>> A5: How are race conditions avoided when a timer interrupt occurs
>> during a call to timer_sleep()?

    Possible thread migration as a result of handling timer interrupt 
    was the issue that Lance mentioned in his paper. Fortunately, this
    problem was solved conjunctively with A4 (See above).

---- RATIONALE ----

>> A6: Why did you choose this design?  In what ways is it superior to
>> another design you considered?

    In the first implementation of timer_sleep(), we used a global sleep
    queue and lock. The advantage of this design was that it was both simple
    to reason about its correctness and to implement. However, scalability
    suffers from this design as the number of sleeping threads and CPU 
    increases. Iterating through one giant sleep queue and competing for
    one lock, where the loser CPU has to wait, was the problem.
    In contrast, the per-CPU sleep queue spread out the loads to multiple
    sleep queues, reducing the iteration size by factors. It also 
    eliminated the need for lock, removing both overheads associated
    with lock and possible contention.

>> A7: Every CPU has its own timer, and therefore executes the 
>> interrupt handler independently. How has this affected your design?
    
    Although timer interrupt happens independently, CPU 0 is in charge of 
    wall clock time. Due to this difference in timing, a thread that calls 
    timer_sleep() would sleep slightly longer than what it was asked for. 



			  ADVANCED SCHEDULER
			  ==================

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

    struct thread
        ... 
        uint64_t vruntime : Virtual runtime of the thread. 
        uint64_t vruntime_0 : Initial virtual runtime of the thread.
        uint64_t total_runtime : CPU consumption time of the thread throughout
                                 its lifetime.
        uint64_t run_start_time : Initial time when the thread starts 
                                  CPU consumption.
        ...

    struct ready_queue
        ...
        uint64_t min_vruntime : Minimum virtual runtime among all threads
                                inside the ready queue.
        ...

---- ALGORITHMS ----

>> B2: Explain briefly what your scheduler does when each of the following happens:
>> Thread is created
   
    (1) Created thread is initially blocked.
    (2) Assign nice value. 
    (3) Initialize total_runtime = 0. This keep track of CPU consumption
        time of the thread. This is needed for vruntime calculation.
    (4) Initialize vruntime_0 (this effectively become vruntime)
        by calling sched_unblock().

        vruntime_0 = min_vruntime ( = 0 for first thread in ready queue).

>> Current thread blocks

    (1) Update vruntime.
    (2) Update total_runtime.
    (3) Transfer the thread to the sleep/wait queue. 
    (4) Pick a thread with the lowest vruntime from the ready queue 
        to run next. Break ties by choosing thread with tid.

>> Thread is unblocked

    (1) Update vruntime.
    (2) Update min_vruntime.
    (3) Update vruntime_0 with sleeper bonus (this effectively become vruntime)
        
        vruntime_0 = max(vruntime, min_vruntime - sleeper_bonus)

    (4) Transfer the thread to the ready queue.
    (5) Preempt if CPU is idling or vruntime of unblocked thread is
        lower than the running thread.

>> Current thread yields

    (1) Update vruntime.
    (2) Update total_runtime.
    (3) Transfer the thread to the ready queue.
    (4) Pick a thread with the lowest vruntime from the ready queue 
        to run next. Break ties by choosing thread with tid.

>> Timer tick arrives
    
    (1) Update vruntime.
    (2) Preempt if the thread has been running more than its ideal runtime.
        Then, choose a thread with the lowest vruntime from the ready queue 
        to run next. Break ties by choosing thread with tid.

>> B3: What steps did you take to make sure that the idle thread is not 
>> taken into account in any scheduling decisions?

    Idle thread gets scheduled on a CPU only when the ready queue of that
    CPU is empty. Idle thread does not get added the to ready queue, 
    instead, each CPU keeps track of their own idle thread on the side.
    If a thread unblocks (either from thread creation or waking up from 
    sleep) while the CPU is running idle thread, the idle thread gets 
    preempted right away.

---- RATIONALE ----

>> B4: Briefly critique your design, pointing out advantages and
>> disadvantages in your design choices.  If you were to have extra
>> time to work on this part of the project, how might you choose to
>> refine or improve your design?
    
    We have put a big emphasis on creating simple, readable, and 
    maintainable code. We carefully designed our CFS to avoid bloating
    and adding unnecessary modifications outside the scheduler.c source
    file. This way, it was easier to see what was changed and communicate
    with team members. The advantage of this was making the project less
    overwhelming. I am personally satisfied with this approach, but the possible
    disadvantage of this could be being too conservative as we 
    force ourselves to only work inside the set boundary. 
    If we have more time, we would be focusing on enhancing performance
    and keeping the current conservative design approach. With that in mind, 
    we would be working on implementing a priority queue to replace provided 
    doubly linked list. 



			     LOAD BALANCER
			     =============

---- DATA STRUCTURES ----

>> C1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

    struct ready_queue
        ...
        uint64_t load : Sum of the weights of all threads in the ready queue.
        ...

---- ALGORITHMS ----

>> C2: When is load balancing invoked? Is this enough to ensure
>> that no CPUs are idle while there are threads in any ready queue?

    Load balancing gets initiated by the idle CPU. This was enough to
    ensure to make available CPUs busy. 

>> C3: Briefly describe what happens in a call to load_balance()
    
    The following describes what happens in CPU calling sched_load_balance()
    in order. Note that this assumes initiating CPU is always the idle CPU
    and interrupts are disabled before the entry.
    (1) Identify the busiest CPU.
    (2) Acquire two locks to prevent race conditions: 
        one from the initiating CPU and the other from the busiest CPU. 
        Lock with lower corresponding CPU id get acquired first and 
        the reason is discussed in C7.
    (3) Calculate imbalance.
    (4) No load balancing occurs if the imbalance is small.
        Otherwise, pull threads from the busiest CPU to the CPU 
        that initiated the load balancing. Continues until the total 
        weight of migrated threads are equal to or exceeds the imbalance value.
        Virtual runtimes of migrated threads get adjusted accordingly
        to avoid monopolization or starvation of CPU time in the newly
        migrated environment.
    (5) Finally, the initiating CPU wrap-up the load balancing by
        releasing the acquired locks in reverse order.


>> C4: What steps are taken to ensure migrated threads do not
>> monopolize the CPU if they had significantly lower vruntimes
>> than the other threads when they migrated?

    Normalization scheme from Pintos documentation Appendix B.7 was
    adapted to adjust vruntime of the thread at the time of migration.
    
    vruntime_0 = vruntime - busiest_cpu_minvruntime + idle_cpu_minvruntime


>> C5: Let's say hypothetically that CPU0 has 100 threads while 
>> CPU1 has 102 threads, and each thread has the same nice value.
>> CPU0 calls load_balance(). Do any tasks get migrated? If not,
>> what are some possible advantages to refraining from migrating
>> threads in that scenario?

    No tasks should/would be migrated.
    Refraining migration will reduce the ping pong effect (moving threads
    back to back) and preserve processor affinity.

---- SYNCHRONIZATION ----

>> C6: How are race conditions avoided when a CPU looks at another
>> CPU's load, or pulling threads from another CPU?

    We have implemented our load balancing to acquire two spinlocks,
    one from the initiating CPU and the other from the pulling (busiest)
    CPU.  

>> C7: It is possible, although unlikely, that two CPUs may try to load
>> balance from each other at the same time. How do you avoid potential
>> deadlock?

    The potential deadlock described above was avoided by always locking the
    CPU with lower id first. In contrast, unlocking was be done in
    reverse order.

---- RATIONALE ----

>> C8: If you deviated from the specifications, explain why. In what ways was your
>> implementation superior to the one specified?

    Our implementation follows the specifications closely as possible.



               MISC
               ====

>> D1: In Pintos, timer interrupts are issued periodically at a 
>> predetermined frequency. In contrast, advanced systems like Linux have
>> adopted a tickless kernel approach, where the timer interrupt is not
>> issued periodically but rather set to arrive on-demand. Looking back at 
>> at some of the inefficiencies that may have crept into your alarm clock,
>> scheduler, and load balancer, in what ways would on-demand interrupts have
>> been beneficial? Please be specific.

   For the alarm clock, on-demand interrupts would eliminate the need of
   iterating over the sleep queue for each time tick. In addition, since 
   there no longer would be a timing discrepancy between different timer
   interrupt across two CPUs, wake-up time would be more accurate.
   For the scheduler in general, on-demand interrupts would make code
   run faster as it does not have to stop the current execution every
   time tick to handle timer interrupt. Also, the discrepancy between 
   calculated ideal runtime and actual runtime would be minimal, thus,
   making the current CFS fairer.




			   SURVEY QUESTIONS
			   ================

Answering these questions is optional, but it will help us improve the
course in future quarters.  Feel free to tell us anything you
want--these questions are just to spur your thoughts.  You may also
choose to respond anonymously in the course evaluations at the end of
the quarter.

>> In your opinion, was this assignment, or any one of the three problems
>> in it, too easy or too hard?  Did it take too long or too little time?

>> Did you find that working on a particular part of the assignment gave
>> you greater insight into some aspect of OS design?

>> Is there some particular fact or hint we should give students in
>> future quarters to help them solve the problems?  Conversely, did you
>> find any of our guidance to be misleading?

>> Do you have any suggestions for the TAs to more effectively assist
>> students, either for future quarters or the remaining projects?

>> Any other comments?
