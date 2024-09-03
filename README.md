# comp-304-operating-systems-project-2-sebport


# Air Traffic Control Simulation with Pthreads

<p align="center">
  <img width=70% src="ATC.png" alt="Sebport logo" title="Air Traffic Control Simulation">
</p>

Prepared by:
- Sebnem Demirtas - ID: 76813
- Mete Erdogan - ID: 69666

#### Description: This project implements an Air Traffic Control (ATC) simulator using POSIX threads (pthreads) API.

## Part 1: Air Traffic Control Simulation
We implement a simulation of an airport using a main thread, a thread for the tower(ATC) and distinct threads for each incoming plane.The main aspects that we focused is that our simulation is in discrete timesteps of "t" seconds.  As given in the description, we start by adding one departing and one landing plane to the simulation at time 0. Then, at each time "t", a plane may become ready to land with probability p and another plane may become ready to take-off with probability 1-p. These two events are independent and can occur simultaneously. It is important to note that this does not mean that both a landing and a take-off can happen at the same time because we have only have one runway. So, a plane can become ready for take-off and a new plane can arrive to land at the same time. Therefore, at each timestep, we pick two random values and decide if these events occur or not. After planes come to either land or depart, they are added to the corresponding queue (landingQueue or takeOffQueue). Then, the ATC tower will decide on a plane to use the runway. We implemented all the required parts of the project.

Furthermore, attention is required to syncronize the main, tower and the plane threads to ensure the orderly execution of the events. We explain below the usecases of each mutexes, condition variable and semaphores in detail:

- **Mutexes:** The goal of mutexes is to guarantee mutual exclusion, which makes sure that only one thread at a time can access crucial portions of the code. In this implementation, shared data structures like the takeoff and landing queues and the current time variable are safeguarded using mutexes.
  - **takeoffQueueMutex**:
    - Guards access to the takeoff queue.
    - Ensures only one thread can modify the takeoff queue at a time.
  - **landingQueueMutex**:
    - Guards access to the landing queue.
    - Ensures only one thread can modify the landing queue at a time.
  - **currentTimeMutex**:
    - Ensures consistency of the current_time variable across threads.
  - **towerSyncMutex**:
    - Controls the access to the **towerSyncCond** that is explained below.
  - **towerMutex**:
    - Controls the access to the **towerCond** that is explained below.

- **Condition Variables:** The goal of condition variables is to facilitate thread-to-thread communication. They increase efficiency by preventing busy waiting by allowing threads to block until a specific condition is satisfied.
  - **towerCond**:
    - Signals waiting planes when it's their turn to use the runway.
    - Specific threads with intended plane id's are signaled using this condition variable.
  - **towerSyncCond**:
    - Ensures accurate scheduling decisions in the tower thread.
    - Prevents incorrect prioritization of planes due to timing issues.
    - For instance, consider a scenario where a new landing plane requests permission at second t, the same time the runway becomes available. Normally, the scheduler should prioritize the landing plane. However, without proper synchronization, there's a risk of a departing plane being mistakenly scheduled instead as the new plane is not added to the ready queue yet. Such misalignments could indicate in the logger file that priority was given to the departing plane, which is unacceptable for our simulation.

- **Semaphores:** The semaphores are used to enforce ordering between certain events in our implementation.
  - **syncPlanesSemaphore**:
    - Synchronizes the main thread and plane threads.
    - As the main thread is printing the planes at each second to the terminal, with this semaphore the main thread waits until the plane thread to add the incoming planes to the queues, and prints the queues after planes are properly added at every t. 
  - **availableRunway**:
    - Controls access to the runway, allowing only one plane at a time.
    - Signals the tower thread after a plane completes its purpose with the runway.
  - **syncMainThreadMutex**:
    - Synchronizes the main thread and the tower thread.
    - This semaphore is initialized with a value of 2, which allows the main thread and the tower thread to run in sync. The main thread generates planes and modifies shared resources, such as queues and current time, while the tower thread manages the scheduling of planes for landing and takeoff. After the tower thread completes its scheduling operations, it posts to the semaphore (sem_post(&syncMainThreadMutex);) twice to allow the main thread to proceed. Because, the tower thread waits for 2 seconds after scheduling the runway, the main thread only waits for the tower thread only once in 2 seconds by this mechanism. When the tower thread is signaled by a plane thread, it wakes up ready to make new scheduling decisions, and the main thread is coordinated to wait just before this wake-up to maintain orderly scheduling.


## Part 2: Implementation Enhancement
Prevent starvation of planes waiting on the ground and in the air. We developed a Round-Robin like scheduling algorithm where if the maximum waiting time of the queues are more than a given threshold, the tower gives the control to the landing and departing planes in order.

- Default Scheduler (Part 1): If both landing and takeoff queues are non-empty, the scheduler prioritizes landing planes. This ensures that landing planes are given preference over departing ones, reducing the risk of collisions.

- Enhanced Scheduler (Part 2): In Part 2 of the project, an enhanced scheduler is introduced to prevent starvation of planes waiting on the ground. If there are five or more planes waiting for takeoff, the scheduler prioritizes allowing planes to depart to avoid congestion on the ground. Otherwise, it follows the same priority as the default scheduler, favoring landing planes. However, this causes starvation to the landing planes.

- Starvation Prevention (Part 3): In Part 3, additional conditions are introduced to prevent starvation of waiting planes in the air. Here we tried to build a conditional Round-Robin scheduler.
If a landing plane has been waiting for more than the maximum waiting time (max_waiting_time) and there is a departing plane waiting for less than 1.5 times the maximum waiting time, the scheduler prioritizes landing to avoid starvation. (We decided to make the maximum waiting times for departing planes longer, because we were given the information that landing planes use more energy and we wanted to land them more quickly.) If a departing plane has been waiting for more than the maximum waiting time and there is a landing plane waiting for less than the maximum waiting time, the scheduler prioritizes takeoff to prevent starvation of departing planes. Also, similar to the previous part, if there are ten or more planes waiting for takeoff, the scheduler prioritizes allowing planes to depart to avoid congestion on the ground. However, if both a landing plane and a departing plane has been waiting for more than the maximum waiting time (max_waiting_time), then the scheduler becomes Round-Robin and gives the access to landing and departing planes in changing order. The algorithm for this scheduling is as below:

           if(max_landing_wait > max_waiting_time && max_takeoff_wait < 1.5*max_waiting_time){
               next_plane_lands = true;
           } else if (max_landing_wait > max_waiting_time && max_takeoff_wait > 1.5*max_waiting_time && last_plane==1) {
               next_plane_lands = false;
           } else if (max_landing_wait > max_waiting_time && max_takeoff_wait > 1.5*max_waiting_time && last_plane==0) {
               next_plane_lands = true;
           } else if (max_takeoff_wait > max_waiting_time && max_landing_wait < max_waiting_time) {
               next_plane_lands = false;
           } else if (max_takeoff_wait < max_waiting_time && max_landing_wait > max_waiting_time) {
               next_plane_lands = true;
           } else if (takeoffQueueSize >= 10) {
               next_plane_lands = false;
           } else if (!landQueueEmpty) {
               next_plane_lands = true;
           } else if (!takeoffQueueEmpty) {
               next_plane_lands = false;
           }

## Part 3: Logging
We implemented detailed logging records plane actions and tower decisions. A sample logging can be found as below for the default scheduler:

           PlaneID   Status    Request Time   Runway Time    Turnaround Time     
           2         L         0              2              2                   
           4         L         1              4              3                   
           1         D         0              6              6                   
           3         D         1              8              7                   
           10        L         6              10             4  

A sample printed snapshot of waiting planes, starting from t=0 at each second on the terminal is given as below:

           At 0 sec ground: |1|
           At 0 sec air: |2|
           
           At 1 sec ground: |1|,|3|
           At 1 sec air: |4|
           
           At 2 sec ground: |1|,|3|
           At 2 sec air: |4|
           
           At 3 sec ground: |1|,|3|,|5|
           At 3 sec air: 
           
           At 4 sec ground: |3|,|5|,|7|
           At 4 sec air: 
           
           At 5 sec ground: |3|,|5|,|7|
           At 5 sec air: 
           
           At 6 sec ground: |5|,|7|,|9|
           At 6 sec air: |10|
           
           At 7 sec ground: |5|,|7|,|9|,|11|
           At 7 sec air: |10|
           
           At 8 sec ground: |5|,|7|,|9|,|11|
           At 8 sec air: 

The logger file outputs for multiple runs can be found under the directory "loggers". The logger files have the name format as "ATC_part{k}_{m}" where k is the different scheduler algorithms and m is different trial indexes.


## Implementation Details:
We utilized C++ with pthreads for multi-threading and for the efficient usage of STL queues to handle the landing and departing planes. Each plane is represented as a thread; ATC tower has its own thread. After planes are done utilizing the runway, the corresponding thread of the plane exits.
Mutexes, condition variables, and semaphores are used for synchronization. After the simulation time is exceeded, threads for unscheduled planes and tower thread is cancelled. 

Usage:

Compile the source file: 

    gcc -o air_traffic_control air_traffic_control.cpp -lpthread

Execute with command-line arguments:
- s: Total simulation time. 
- p: Probability of a plane landing.
- n: Number of seconds for logging snapshots.
- w: Maximum waiting time for planes:
  - If zero, the default scheduling algorithm that prioritizes landing planes is used.
  - If negative integer, use the scheduling algorithm of part-2 that schedules departing planes if takeoff queue has +5 planes.
  - If positive integer, we use the round-robin like scheduling to handle starvation.
- seed: Random seed for generating planes.

Example: 

    ./air_traffic_control -s 60 -p 0.5 -n 20 -w 30 -seed 123

for 60 seconds simulation
