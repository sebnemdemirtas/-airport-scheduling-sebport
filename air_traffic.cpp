#include <iostream>
#include <pthread.h>
#include <queue>
#include <unistd.h>
#include <ctime>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <sys/time.h>
#include <iomanip>
#include <cstring>
#include <random>
#include "pthread_sleep.c"
#include <semaphore.h>


using namespace std;
const int runway_usage_time = 2; // 2t seconds
int part = 1; // t seconds


//Mutexes and conditions:

pthread_mutex_t towerMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t towerCond = PTHREAD_COND_INITIALIZER; //Used as a signal to let the plane in the line to utilize the runway.

pthread_mutex_t landingQueueMutex = PTHREAD_MUTEX_INITIALIZER; //Guarding landing queue
pthread_mutex_t takeoffQueueMutex = PTHREAD_MUTEX_INITIALIZER; //Guarding take off queue
pthread_mutex_t currentTimeMutex = PTHREAD_MUTEX_INITIALIZER; //Maintains consistency in the current time variable.

pthread_mutex_t towerSyncMutex = PTHREAD_MUTEX_INITIALIZER; //Syncs tower thread with main thread
pthread_cond_t towerSyncCond = PTHREAD_COND_INITIALIZER; //Syncs tower thread with main thread
 

// Semaphores:
sem_t syncPlanesSemaphore; //Syncs main thread and plane threads
sem_t availableRunway; //Controls access to runway, one at a time.
sem_t syncMainThreadMutex; //To manage the order between main and tower threads. 


//Shared variables:
time_t startTime;
time_t currentTime;
int number_of_sec = 0;
int simulation_time = 60;
int max_waiting_time = 30;
int plane_on_runway = -1; // Initially no plane is on the runway
int last_plane = 0; //remember whether the last plane landed or took off;


unsigned int seed = 0;
bool next_plane_lands = true; //Used in scheduling algorithms.
double p = 0.5;
bool simulationRunning = true;
ofstream planesLog("loggers/ATC_part3_xxx.log");

//Struct plane, contains plane id, the information whether it wants to land or take off and request time.
struct Plane {
	int id;
	bool isLanding;
	time_t requestTime;
	Plane(int PlaneId, bool Landing) : id(PlaneId), isLanding(Landing) {
		requestTime = time(NULL)-startTime;
	}
};

queue<Plane> landingQueue;
queue<Plane> takeoffQueue;

//Used for proper writing to log files:
void print_logger(int id, char status, time_t requestTime, time_t runwayTime) {
	planesLog << left << setw(10) << id << left << setw(10) << status
         	<< left << setw(15) << requestTime << left << setw(15) << runwayTime
         	<< left << setw(20) << (runwayTime - requestTime) << endl;
}

//Used for printing the snapshot to the terminal.
void printAirportSnapshot(int sec) {
	cout << endl << "At " << sec << " sec ground: ";
	pthread_mutex_lock(&takeoffQueueMutex);
	queue<Plane> takeoffQueue_current = takeoffQueue;
	pthread_mutex_unlock(&takeoffQueueMutex);
	while (!takeoffQueue_current.empty()) {
		cout << "|" << takeoffQueue_current.front().id << "|";
		takeoffQueue_current.pop();
		if (!takeoffQueue_current.empty()) cout << ",";
	}
	cout << endl;
 
	cout << "At " << sec << " sec air: ";
	pthread_mutex_lock(&landingQueueMutex);
	queue<Plane> landingQueue_current = landingQueue;
	pthread_mutex_unlock(&landingQueueMutex);
	while (!landingQueue_current.empty()) {
		cout << "|" << landingQueue_current.front().id << "|";
		landingQueue_current.pop();
		if (!landingQueue_current.empty()) cout << ",";
	}
	cout << endl;
}

//For each plane a thread is created.
void* planeThread(void* arg) {
	Plane* plane = (Plane*) arg; //Getting the plane as argument.
	if((*plane).isLanding) {
		pthread_mutex_lock(&landingQueueMutex);
		landingQueue.push(*plane); // Lock mutex to modify the queue
		pthread_mutex_unlock(&landingQueueMutex);
	}else {
		pthread_mutex_lock(&takeoffQueueMutex);
		takeoffQueue.push(*plane); // Lock mutex to modify the queue
		pthread_mutex_unlock(&takeoffQueueMutex);
	}
	sem_post(&syncPlanesSemaphore);
   

	// Now wait until this plane is scheduled for runway
	pthread_mutex_lock(&towerMutex);
	while (plane_on_runway != plane->id && simulationRunning) {
		pthread_cond_wait(&towerCond, &towerMutex);
	}
	pthread_mutex_unlock(&towerMutex);
 
	//Thread sleeps for the time the plane uses the runway, in this case 2t=2 secs.
	if(plane->isLanding) {
		pthread_sleep(runway_usage_time);
		//cout << endl << "My id: " << plane->id << ", I have landed";
	}else {
		pthread_sleep(runway_usage_time);
		//cout << endl << "My id: " << plane->id << ", I have took off";
	}
	sem_post(&availableRunway); //Make runway available for coming planes.
	pthread_exit(NULL);
}

//There is one tower thread making the scheduling decisions.
void* towerThread(void* arg) {
	while (simulationRunning) {
		pthread_mutex_lock(&towerSyncMutex);
		pthread_cond_wait(&towerSyncCond, &towerSyncMutex); //Makes sure for every second decision is made after the newcoming planes are added to the queues.
		pthread_mutex_unlock(&towerSyncMutex);
		 
		if(landingQueue.empty() && takeoffQueue.empty()){
			sem_post(&syncMainThreadMutex);
		}else{
	 	 	pthread_mutex_lock(&currentTimeMutex);
			int currentTimeCopy = currentTime;
			pthread_mutex_unlock(&currentTimeMutex);
			if (currentTimeCopy - startTime >= simulation_time) {
				simulationRunning = false; // End the simulation
				pthread_exit(NULL);
			}
	 	 
			pthread_mutex_lock(&landingQueueMutex);
			int max_landing_wait;
			bool landQueueEmpty = landingQueue.empty();
			int landQueueSize = landingQueue.size();
			Plane landingPlane = landingQueue.front();
			if(landQueueSize==0) max_landing_wait = 0;
			else max_landing_wait = currentTimeCopy-startTime - landingPlane.requestTime; //Will be used for scheduling.
			pthread_mutex_unlock(&landingQueueMutex);

			pthread_mutex_lock(&takeoffQueueMutex);
			int max_takeoff_wait;
			bool takeoffQueueEmpty = takeoffQueue.empty();
			int takeoffQueueSize = takeoffQueue.size();
			Plane takeOffPlane = takeoffQueue.front();
			if(takeoffQueueSize==0) max_takeoff_wait = 0;
			else max_takeoff_wait = currentTimeCopy-startTime - takeOffPlane.requestTime; //Will be used for scheduling.
			pthread_mutex_unlock(&takeoffQueueMutex);
		 	 
			if(part == 1) { // default scheduler for part-1. Prioritize landing planes.
			   	if (!landQueueEmpty) {
				   	next_plane_lands = true;
			   	} else if (!takeoffQueueEmpty) {
				   	next_plane_lands = false;
			   	}
			}else if(part == 2) { // scheduler that choose take-off if waiting more than 5 for part-2
			   	if (takeoffQueueSize >= 5) {
				   	next_plane_lands = false;
			   	} else if (!landQueueEmpty) {
				   	next_plane_lands = true;
			   	} else if (!takeoffQueueEmpty) {
				   	next_plane_lands = false;
			   	}
			}else{ // scheduler that handles starvation for part-2
					 //Still prioritizing landing planes more.
					 //Max waiting time is assigned for both queues. For take_off planes, it is 1.5 times of other's.
					 //If they both exceed these limits, tower gives the runway in changing order. Check README for details.
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
			}
			//Take the next step according to the decision made above.
			if (next_plane_lands) {
			   	pthread_mutex_lock(&landingQueueMutex);
			   	Plane plane = landingQueue.front();
			   	landingQueue.pop(); //Take from the queue.
			   	plane_on_runway = plane.id; // Set the current plane on the runway  
			   	pthread_mutex_unlock(&landingQueueMutex);
			   	pthread_cond_broadcast(&towerCond); // Signal all waiting planes, only the one with the decided id will be able to land.
			   	sem_wait(&availableRunway); //
			   	time_t runwayTime = time(NULL) - startTime; // Calculate runwayTime relative to startTime
			   	print_logger(plane.id, 'L', plane.requestTime, runwayTime);
			   	//cout << endl << "At time " << runwayTime << ", Plane " << plane.id << " has landed.";
			   	cout.flush();
			   	last_plane = 1;
			} else {
			   	pthread_mutex_lock(&takeoffQueueMutex);
			   	Plane plane = takeoffQueue.front();
			   	takeoffQueue.pop(); //Take from the queue.
			   	plane_on_runway = plane.id; // Set the current plane on the runway
			   	pthread_mutex_unlock(&takeoffQueueMutex);
			   	pthread_cond_broadcast(&towerCond); // Signal all waiting planes, only the one with the decided id will be able to take off.
			   	sem_wait(&availableRunway);
			   	time_t runwayTime = time(NULL) - startTime; // Calculate runwayTime relative to startTime
			   	print_logger(plane.id, 'D', plane.requestTime, runwayTime);
			   	//cout << endl << "At time " << runwayTime << ", Plane " << plane.id << " has took off.";
			   	cout.flush();
			   	last_plane = 0;
			}
			sem_post(&syncMainThreadMutex);
			sem_post(&syncMainThreadMutex);
			}
		if (currentTime-startTime >= simulation_time) {
		   	break;
		}
   }
   pthread_exit(NULL);
}


int main(int argc, char* argv[]) {
	if (argc != 11) {
		cerr << "Code usage: " << argv[0] << " -s simulation_time -p probability -n number_of_sec -w max_waiting -seed random_seed" << endl;
		return 1;
	}
	
	//to get the arguments:
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0) {
	   		simulation_time = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-p") == 0) {
	   		p = atof(argv[++i]);
		} else if (strcmp(argv[i], "-n") == 0) {
	   		number_of_sec = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-seed") == 0) {
	   		seed = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-w") == 0) {
	   		max_waiting_time = atoi(argv[++i]);
		}
   	}
   	
	//For different type of scheduling implementations:
	if(max_waiting_time == 0) {
		part = 1;
	}else if(max_waiting_time < 0) {
		part = 2;
	}else {
		part = 3;
	}

	// Initialize start time
	startTime = time(NULL);
	srand(seed);
 
	// Initialize the header of the logging file
	planesLog << left << setw(10) << "PlaneID" << left << setw(10) << "Status"
		 	<< left << setw(15) << "Request Time" << left << setw(15) << "Runway Time"
		 	<< left << setw(20) << "Turnaround Time" << endl;


	int planeIDCount = 0;
	currentTime = time(NULL);

	//initializing semaphores:
	sem_init(&syncPlanesSemaphore, 0, 0);
	sem_init(&availableRunway, 0, 0);
	sem_init(&syncMainThreadMutex, 0, 2);

	//Creating tower thread:
	pthread_t tower;
	pthread_create(&tower, NULL, towerThread, NULL);

	//A vector holding plane threads:
	vector<pthread_t> planeThreads;

    //The while loop that will generate planes during the simulation time:
	while(currentTime < startTime + simulation_time) {
		bool isLanding;
		bool isTakingOff;
		sem_wait(&syncMainThreadMutex); //To make sure main and tower are going with the same pace.
		
		if(currentTime-startTime > 0) {
			isLanding = (rand()%100) < (p*100);
			isTakingOff = (rand()%100) < ((1-p)*100);
		}else { // Ensure there is one plane waiting to take off and one plane waiting to land at time zero
			isLanding = true;
			isTakingOff = true;
		}
   		//cout << endl << "land: " << isLanding << ", takeoff: "<< isTakingOff << "\n";
		 
	   	if (isTakingOff) {
			while (planeIDCount % 2 == 0) {planeIDCount++;} // Ensure odd ID for taking off
		   	Plane* plane = new Plane(planeIDCount++, false);
		   	pthread_t takeoffThreadID;
		   	pthread_create(&takeoffThreadID, NULL, planeThread, (void*) plane); //Creating plane thread
		   	planeThreads.push_back(takeoffThreadID);
	   	}
	 	 
	   	if (isLanding) {
		   	while (planeIDCount % 2 != 0) {planeIDCount++;} // Ensure even ID for landing
		   	Plane* plane = new Plane(planeIDCount++, true);
		   	pthread_t landThreadID;
		   	pthread_create(&landThreadID, NULL, planeThread, (void*) plane); //Creating plane thread
		   	planeThreads.push_back(landThreadID);
	   	}
	   	
	   	if(isTakingOff) {
	   		sem_wait(&syncPlanesSemaphore); //To wait for the planes to be added to the queues properly.
	   	}
	   	if(isLanding) {
	   		sem_wait(&syncPlanesSemaphore); //To wait for the planes to be added to the queues properly.
	   	}
 	
		pthread_mutex_lock(&currentTimeMutex);
		currentTime = time(NULL);
		// Print the snapshot of the waiting planes in the air and ground
		if (currentTime-startTime >= number_of_sec) {printAirportSnapshot(currentTime-startTime);}
		pthread_mutex_unlock(&currentTimeMutex);

		pthread_mutex_lock(&towerSyncMutex);
		pthread_cond_signal(&towerSyncCond);
		pthread_mutex_unlock(&towerSyncMutex);
		pthread_sleep(1); // Simulate time interval t  
	}
   
	// Destroy mutex, semaphores and condition variables
	simulationRunning = false;
	pthread_cancel(tower); // Cancel the tower thread
	for (pthread_t& thread : planeThreads) {
	   pthread_cancel(thread); // Cancel each plane thread
	}
	planesLog.close();
 
	//Destroying mutexes, conditional variables, semaphores.
	pthread_mutex_destroy(&landingQueueMutex);
	pthread_mutex_destroy(&takeoffQueueMutex);
	pthread_mutex_destroy(&towerSyncMutex);
	pthread_cond_destroy(&towerSyncCond);
	pthread_mutex_destroy(&currentTimeMutex);
	sem_destroy(&syncPlanesSemaphore);
	sem_destroy(&availableRunway);
	sem_destroy(&syncMainThreadMutex);
 
	return 0;
}
